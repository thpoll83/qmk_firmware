// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#include "multicore_exec.h"
#include "base/crash_record.h"
#include "polykybd.h"

#include "print.h"
#include "config.h"
#include "base/helpers.h"
#include "base/update.h"
#include "polymod_rle.h"
#include "base/disp_array.h"
#include "polymod_core1.h"
#include "fill_overlay.h"   // for mark_display_has_overlay_post_upload

#ifdef USE_CORE1
static volatile uint16_t core1_bit_index = 0;
static volatile uint32_t core1_decomp_count = 0;
static volatile uint32_t core0_decomp_count = 0;

static volatile uint8_t core1_buffer[HID_DATA_MAX];
// Despite the "bitlen" name this holds a BYTE count, not a bit count: the number
// of writable bytes from the current dest (get_overlay(idx)+core1_bit_index/8) to
// the end of the 360-byte overlay, i.e. 360 - core1_bit_index/8 (set below). It is
// passed straight as rle_decompress's `max` (bytes). Reused across the DECOMPRESS
// continuation chunks rather than declaring a fresh local each time.
static volatile int16_t core1_max_bitlen;
static volatile uint16_t core1_idx;
static volatile roi_update_data_t core1_roi;
// Set by the dispatcher (core0) before pushing each fragment: whether the overlay being
// staged is the modifier variant currently on screen. core1 only requests a display
// refresh on completion when it is — an off-screen variant (e.g. a Shift image while
// Shift is up) is written to memory but needn't re-render (the modifier-press path picks
// it up). Same publish-before-FIFO-push ordering as core1_idx, so core1 reads the value
// that belongs to the fragment it is completing. See fill_overlay.c overlay_variant_visible.
static volatile bool core1_visible = true;

typedef enum {
    CORE1_CMD_DECOMPRESS     = 0xcafe0001,
    CORE1_CMD_ROI_UPDATE     = 0xcafe0002,
    CORE1_CMD_RESET_BIT_IDX  = 0xcafe0003,
} fifo_command_t;

// Overlay buffers for core1 processing
extern uint8_t overlays [NUM_OVERLAY_SLOTS][72*40/8]; // ResX*ResY/PixelPerByte

// Main function for core1, do not use any prtintf or similar, stack is limited!
// Processes decompression and ROI update commands from core0 via FIFO, handles overlay buffer updates.
// Global variables: core1_bit_index, core1_decomp_count, core1_idx, core1_max_bitlen, core1_buffer, core1_roi
void core1_entry(void) {
    // PRIMASK=1 on core1 — without this core1 hangs whenever overlay/ROI data is processed
    // after the upstream-QMK-master merge (May 2026). The trap is the strongly-overridden
    // ChibiOS Vector80 (SIO_IRQ_PROC1) handler whose CH_IRQ_EPILOGUE triggers an NMI via
    // ICSR.NMIPENDSET, which then runs the ChibiOS context-switch NMI handler on a core
    // with no thread state, hanging it. The FIFO IRQ on RP2040 has known quirks
    // (see pico-sdk issue #284) where it appears to fire despite NVIC->ISER bit being clear.
    // core1 in this codebase has no IRQ-driven work — multicore_fifo_pop_blocking polls
    // FIFO_ST and doesn't need an IRQ to wake — so masking all IRQs here is safe.
    // See keyboards/polykybd/CLAUDE.md for the full investigation.
    __asm volatile("cpsid i" ::: "memory");
    multicore_fifo_drain();
    while (true) {
        uint32_t cmd = multicore_fifo_pop_blocking();  // blocks if empty
        switch (cmd) {
            case CORE1_CMD_DECOMPRESS:{
                    uint16_t data_len = core1_bit_index==0?COMPRESSED_START:COMPRESSED_MAX;
                    core1_bit_index += rle_decompress(get_overlay(core1_idx)+core1_bit_index/8, PK_MAX(0,core1_max_bitlen), core1_buffer, data_len, core1_bit_index);

                    if (core1_bit_index >= 360*8 -1) {
                        mark_display_has_overlay_post_upload(core1_idx);
                        // No update_performed() — a host overlay push is not user
                        // activity and must not restart the idle countdown (see
                        // base/update.h). Also keeps core1 out of the idle-timer
                        // state entirely; only core0 writes it now.
                        // Only refresh a variant that is actually on screen (core1_visible).
                        if (core1_visible) {
                            request_disp_refresh();
                        }
                        core1_bit_index = 0;
                    }
                    core1_decomp_count++;
                    if(core1_decomp_count==0) { //handle overflow
                        core1_decomp_count=1;
                    }
                    dmb();
                } break;
            case CORE1_CMD_ROI_UPDATE:{
                    uint8_t data_len = ROI_MAX;
                    if(core1_bit_index==0) {
                        core1_bit_index = core1_roi.y * SCREEN_WIDTH + core1_roi.x;
                        data_len = ROI_START;
                    }
                    core1_bit_index = copy_rectangle_to_overlay(core1_bit_index, get_overlay(core1_idx), core1_buffer, &core1_roi, data_len);
                    if(core1_bit_index >= 2880) {
                        mark_display_has_overlay_post_upload(core1_idx);
                        // No update_performed() — see base/update.h.
                        // Only refresh a variant that is actually on screen (core1_visible).
                        if (core1_visible) {
                            request_disp_refresh();
                        }
                        core1_bit_index = 0;
                    }
                    core1_decomp_count++;
                    if(core1_decomp_count==0) { //handle overflow
                        core1_decomp_count=1;
                    }
                    dmb();
                } break;
            case CORE1_CMD_RESET_BIT_IDX:
                core1_bit_index = 0;
                dmb();
                break;
            default: break;
        }
    }
}

bool core1_is_busy(void) {
    dmb();
    return core0_decomp_count != core1_decomp_count;
}

// Strong override of the weak hook in tmk_core/protocol/chibios/usb_main.c:
// when core1 is still chewing on the previous fragment, refuse to pull the
// next packet off the Raw HID OUT queue this main-loop pass. The packet stays
// queued by the USB driver (RAW_OUT_CAPACITY=4) and matrix_task gets to run.
bool raw_hid_pre_receive_kb(void) {
    return !core1_is_busy();
}

void core1_decompress_fragment(uint8_t keycode, uint8_t mod, uint16_t overlay_idx, const uint8_t* compressed, bool visible) {
    // Defense in depth: callers that respect the raw_hid_pre_receive_kb() gate
    // will never enter the wait. For any caller that didn't gate (e.g. the
    // split-sync bridge path), spin without the uprintf — the previous wait
    // body was burning core0 cycles in a format-and-sink path that starved
    // matrix scan during back-pressure.
    dmb();
    // Tagged so a watchdog timeout in this spin reads as "core0 waiting on core1"
    // (the core1-hang class, see CLAUDE.md) rather than as an anonymous hang.
    uint32_t crash_tag = crash_phase_enter(CRASH_PHASE_CORE1_WAIT, 0);
    while(core0_decomp_count!=core1_decomp_count) {
        dmb();
    }
    crash_phase_leave(crash_tag);
    //copy data to dedicated buffers
    uint8_t data_len = core1_bit_index==0?COMPRESSED_START:COMPRESSED_MAX;
    core1_max_bitlen = 360 - core1_bit_index/8;
    core1_idx = overlay_idx;
    core1_visible = visible;
    for(uint8_t i=0;i<data_len;++i) {
        core1_buffer[i] = compressed[i]; //memcopy not avialable for volatile memory
    }

#ifdef POLY_DEBUG_HID
#    ifdef CORE1_STACK_HWM
    uprintf("CORE1: Key 0x%x (mod 0x%x) fragment decompression: (added %d bytes, bit index: %d, stack HWM: %lu).\n", keycode, mod, core1_bit_index==0?COMPRESSED_START:COMPRESSED_MAX, core1_bit_index, (unsigned long)core1_stack_high_water_mark());
#    else
    uprintf("CORE1: Key 0x%x (mod 0x%x) fragment decompression: (added %d bytes, bit index: %d).\n", keycode, mod, core1_bit_index==0?COMPRESSED_START:COMPRESSED_MAX, core1_bit_index);
#    endif
#else
    (void)keycode;
    (void)mod;
#endif
    core0_decomp_count++;
    if(core0_decomp_count==0) { //handle overflow
        core0_decomp_count=1;
    }
    dmb();
    //allow core1 to start decompressing
    multicore_fifo_push_blocking(CORE1_CMD_DECOMPRESS);
}

void core1_roi_start(void) {
    // No wait or dmb needed: RESET touches only core1_bit_index (not the shared buffers),
    // FIFO ordering guarantees any in-flight DECOMPRESS/ROI_UPDATE completes atomically
    // before this RESET runs, and the immediately-following core1_update_roi() performs
    // its own decomp-count wait before writing buffers.
    multicore_fifo_push_blocking(CORE1_CMD_RESET_BIT_IDX);
}

void core1_update_roi(uint8_t keycode, uint8_t mod, uint16_t overlay_idx, const uint8_t* data, const roi_update_data_t* roi, bool visible) {
    // See core1_decompress_fragment for the backpressure rationale.
    dmb();
    // Tagged so a watchdog timeout in this spin reads as "core0 waiting on core1"
    // (the core1-hang class, see CLAUDE.md) rather than as an anonymous hang.
    uint32_t crash_tag = crash_phase_enter(CRASH_PHASE_CORE1_WAIT, 0);
    while(core0_decomp_count!=core1_decomp_count) {
        dmb();
    }
    crash_phase_leave(crash_tag);
    //copy data to dedicated buffers
    //core1_max_bitlen = 360 - core1_bit_index/8;
    core1_roi = *roi;
    core1_idx = overlay_idx;
    core1_visible = visible;
    const uint8_t data_len = core1_bit_index==0?ROI_START:ROI_MAX;
    for(uint8_t i=0;i<data_len;++i) {
        core1_buffer[i] =  data[i]; //memcopy not available for volatile memory
    }

#ifdef POLY_DEBUG_HID
#    ifdef CORE1_STACK_HWM
    uprintf("CORE1: Key 0x%x (mod 0x%x) roi update: (added %d bytes, bit index: %d, stack HWM: %lu).\n", keycode, mod, data_len, core1_bit_index, (unsigned long)core1_stack_high_water_mark());
#    else
    uprintf("CORE1: Key 0x%x (mod 0x%x) roi update: (added %d bytes, bit index: %d).\n", keycode, mod, data_len, core1_bit_index);
#    endif
#else
    (void)keycode;
    (void)mod;
#endif
    core0_decomp_count++;
    if(core0_decomp_count==0) { //handle overflow
        core0_decomp_count=1;
    }
    dmb();
    //allow core1 to start decompressing
    multicore_fifo_push_blocking(CORE1_CMD_ROI_UPDATE);
}
#endif
