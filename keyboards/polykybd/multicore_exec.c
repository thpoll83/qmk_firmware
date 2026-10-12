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
#include "anim/startup_anim.h"   // startup_anim_core1_job (the Eden idle keycap job)
#include "doom/doom_mode.h"      // doom_mode_active: DOOM owns core1 while it runs
#include "base/fw_staging.h"     // fw_staging_core1_held: a flash erase owns core1
#include "hardware/structs/psm.h"

#ifdef USE_CORE1
static volatile uint16_t core1_bit_index = 0;
static volatile uint32_t core1_decomp_count = 0;
static volatile uint32_t core0_decomp_count = 0;
// When core0 last handed core1 a fragment (DECOMPRESS / ROI_UPDATE). Written by
// core0 only; read by the stall check in raw_hid_pre_receive_kb().
static uint32_t core1_pushed_at = 0;

// The FIFO command core1 last STARTED, and its argument (the Eden job's word; 0 for the
// others). Written by core1 only. When core1 stops answering, this names what it was
// doing: a decode, an Eden keycap, or nothing at all (0 = never got a command).
static volatile uint32_t core1_last_cmd = 0;
static volatile uint32_t core1_last_arg = 0;

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

// Set by core1 once it is inside core1_entry() with IRQs masked; cleared by core0
// before each launch. Read into the boot sub-step paint breadcrumbs (boot_diag.c), so a
// watchdog record from the late-boot window says whether core1 had got that far.
volatile uint32_t g_core1_entered = 0;

typedef enum {
    CORE1_CMD_DECOMPRESS     = 0xcafe0001,
    CORE1_CMD_ROI_UPDATE     = 0xcafe0002,
    CORE1_CMD_RESET_BIT_IDX  = 0xcafe0003,
    CORE1_CMD_EDEN_KEY       = 0xcafe0004,   // followed by ONE argument word
#ifdef POLYKYBD_CRASH_TEST
    CORE1_CMD_CRASH_TEST     = 0xcafe00ff,
#endif
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
    g_core1_entered = 1u;
    dmb();
    multicore_fifo_drain();
    while (true) {
        uint32_t cmd = multicore_fifo_pop_blocking();  // blocks if empty
        core1_last_cmd = cmd;
        core1_last_arg = 0;
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
            case CORE1_CMD_EDEN_KEY:
                // One Eden idle keycap (anim/startup_anim.c). The argument follows the
                // command in the FIFO; the job publishes its own completion. Its stack
                // path is measured: IDLE_STYLES.md, "The idle loop on core1".
                {
                    const uint32_t arg = multicore_fifo_pop_blocking();
                    core1_last_arg = arg;
                    startup_anim_core1_job(arg);
                }
                break;
#ifdef POLYKYBD_CRASH_TEST
            case CORE1_CMD_CRASH_TEST: {
                    // An unaligned word store: ARMv6-M has no unaligned access, so
                    // this HardFaults right here. The vector table is shared with
                    // core0 and PRIMASK does not mask a HardFault, so the naked
                    // HardFault_Handler runs ON CORE1 and the record's `core` field
                    // reads 1. No uprintf -- core1 has no console and a tiny stack.
                    // ⚠️ The address MUST be laundered through a volatile. Written as a
                    // compile-time constant, GCC sees the misalignment and LEGALISES the
                    // store into four strb -- which never fault on ARMv6-M, so core1 wrote
                    // the bytes and carried on and this trigger silently did nothing
                    // (measured 2026-09-04). The volatile hides the alignment, forcing a
                    // real str. Same rule as crash_test.c, different reason from the one
                    // documented there: not deletion, legalisation.
                    static volatile uintptr_t bad_addr;
                    bad_addr = ((uintptr_t)&core1_decomp_count) + 1u;
                    volatile uint32_t *bad = (volatile uint32_t *)bad_addr;
                    // Tag the breadcrumb so the resulting record self-identifies as this
                    // trigger rather than leaving `core=1` as the only evidence.
                    (void)crash_phase_enter(CRASH_PHASE_CORE1_WAIT, 0x00C1);
                    *bad = 0xDEADBEEFu;
                } break;
#endif
            default: break;
        }
    }
}

bool core1_eden_available(void) {
    dmb();
    return g_core1_entered != 0u && !doom_mode_active();
}

void core1_eden_key(uint32_t arg) {
    multicore_fifo_push_blocking(CORE1_CMD_EDEN_KEY);
    multicore_fifo_push_blocking(arg);
}

bool core1_is_busy(void) {
    dmb();
    return core0_decomp_count != core1_decomp_count;
}

// ---- core1 stall recovery ----------------------------------------------------
// A fragment takes core1 well under a millisecond, and at most two Eden keycap jobs
// (~4.3 ms each) can sit in front of it. Half a second means core1 has stopped.
#define CORE1_STALL_MS 500u

// What the last recovery found. Printed from the HID path (core1_stall_report()), not
// at recovery time: QMK drops console output nobody drains, and core1 can stop while
// no host is attached (field 2026-10-09: it was already down before the host
// connected). A host report almost always means somebody is reading the console. The
// exception is a host whose console interface was not up yet when it opened the
// device, which it repairs within a second, so the line is printed CORE1_STALL_REPORTS
// times, CORE1_STALL_REPORT_GAP_MS apart. Every copy carries the same recovery ID,
// "recovery <n> since boot at <uptime> ms", which is how the host's problem scan counts
// one recovery once. The uptime is part of the ID because <n> restarts after a reboot.
#define CORE1_STALL_REPORTS      3u
#define CORE1_STALL_REPORT_GAP_MS 10000u
static struct {
    uint32_t count;      // recoveries since boot
    uint32_t stalled_ms; // how long the oldest fragment had waited
    uint32_t last_cmd;   // core1_last_cmd when it was found stopped
    uint32_t last_arg;
    uint32_t c0, c1;     // core0_decomp_count / core1_decomp_count
    uint32_t entered;    // g_core1_entered
    uint32_t at_ms;      // uptime of the recovery: with `count`, the recovery's ID
    uint32_t printed_at; // uptime of the last copy printed
    uint8_t  printed;    // copies printed of this recovery
    bool     relaunched; // the bounded relaunch answered
} s_c1_stall;

// Hard-reset core1 through the power-on state machine: hold it off until the PSM
// reports it down, then release it into the bootrom's launch wait.
static void core1_psm_reset(void) {
    hw_set_bits(&psm_hw->frce_off, PSM_FRCE_OFF_PROC1_BITS);
    uint32_t spins = 0;
    while ((psm_hw->done & PSM_DONE_PROC1_BITS) && spins < 1000000u) {
        spins++;
    }
    hw_clear_bits(&psm_hw->frce_off, PSM_FRCE_OFF_PROC1_BITS);
}

// core1 owes a fragment and has not answered for CORE1_STALL_MS: reset it, drop the
// work it lost, and launch the service again.
static void core1_recover(uint32_t stalled_ms) {
    s_c1_stall.count++;
    s_c1_stall.stalled_ms = stalled_ms;
    s_c1_stall.last_cmd   = core1_last_cmd;
    s_c1_stall.last_arg   = core1_last_arg;
    s_c1_stall.c0         = core0_decomp_count;
    s_c1_stall.c1         = core1_decomp_count;
    s_c1_stall.entered    = g_core1_entered;
    s_c1_stall.at_ms      = timer_read32();
    s_c1_stall.printed    = 0;

    core1_psm_reset();
    // core1 is held in the bootrom now, so core0 owns every word it shares. The
    // fragment in flight is lost: its overlay keeps whatever was decoded so far.
    core1_bit_index    = 0;
    core1_decomp_count = core0_decomp_count;
    core1_last_cmd     = 0;
    core1_last_arg     = 0;
    g_core1_entered    = 0u;
    // The Eden keycap jobs died with it; without this the idle loop would wait for
    // them and stay on core0 until the next boot.
    startup_anim_core1_lost();
    dmb();
    // BOUNDED, as in fw_staging and doom_mode: a core1 that does not come back must
    // not take core0's main loop with it.
    s_c1_stall.relaunched = multicore_launch_core1_bounded(100u * 1000u);
}

void core1_stall_report(void) {
    if (s_c1_stall.count == 0 || s_c1_stall.printed >= CORE1_STALL_REPORTS) {
        return;
    }
    if (s_c1_stall.printed > 0 && timer_elapsed32(s_c1_stall.printed_at) < CORE1_STALL_REPORT_GAP_MS) {
        return;
    }
    s_c1_stall.printed++;
    s_c1_stall.printed_at = timer_read32();
    uprintf("WARNING core1 stalled: no answer for %lu ms (last cmd 0x%08lx arg 0x%08lx, "
            "counts %lu/%lu, entered %lu) - %s (recovery %lu since boot at %lu ms, report %u/%u)\n",
            (unsigned long)s_c1_stall.stalled_ms, (unsigned long)s_c1_stall.last_cmd,
            (unsigned long)s_c1_stall.last_arg, (unsigned long)s_c1_stall.c0,
            (unsigned long)s_c1_stall.c1, (unsigned long)s_c1_stall.entered,
            s_c1_stall.relaunched ? "core1 relaunched" : "core1 relaunch FAILED, overlays degraded until reboot",
            (unsigned long)s_c1_stall.count, (unsigned long)s_c1_stall.at_ms,
            (unsigned)s_c1_stall.printed, (unsigned)CORE1_STALL_REPORTS);
}

// Strong override of the weak hook in tmk_core/protocol/chibios/usb_main.c:
// when core1 is still chewing on the previous fragment, refuse to pull the
// next packet off the Raw HID OUT queue this main-loop pass. The packet stays
// queued by the USB driver (RAW_OUT_CAPACITY=4) and matrix_task gets to run.
//
// ⚠️ The refusal must END. A core1 that has stopped never catches up, and a gate
// that waits for it closes the raw HID OUT queue for good: the four slots fill and
// the endpoint NAKs every later write, while typing, the console and the idle
// animations go on, and no watchdog fires because nothing spins. Field 2026-10-09:
// the host's writes timed out with 0x3E5 for a minute until a replug. So after
// CORE1_STALL_MS the gate relaunches core1 instead of waiting for it.
//
// Master only: there the gate is the one place a fragment waits, and every core1
// user runs on the main thread. On the slave the split thread hands core1 its
// fragments, and a relaunch from the main thread could interleave with that push.
bool raw_hid_pre_receive_kb(void) {
    if (!core1_is_busy()) {
        return true;
    }
    if (!is_keyboard_master() || doom_mode_active() || fw_staging_core1_held()) {
        return false;   // core1 is busy on purpose, or not ours to reset from here
    }
    const uint32_t stalled_ms = timer_elapsed32(core1_pushed_at);
    if (stalled_ms < CORE1_STALL_MS) {
        return false;
    }
    core1_recover(stalled_ms);
    return true;
}

void core1_decompress_fragment(uint8_t keycode, uint8_t mod, uint16_t overlay_idx, const uint8_t* compressed, bool visible, bool first) {
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
    // A new image starts at pixel 0. core1 only rewinds its cursor when it has decoded
    // a whole image, so an image left incomplete (the fragment a stall recovery
    // dropped) would otherwise start the NEXT key's image part-way through its buffer.
    // core1 is idle on decode work here (the counts match), so core0 may write it.
    if (first) {
        core1_bit_index = 0;
    }
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
    core1_pushed_at = timer_read32();
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
    core1_pushed_at = timer_read32();
    core0_decomp_count++;
    if(core0_decomp_count==0) { //handle overflow
        core0_decomp_count=1;
    }
    dmb();
    //allow core1 to start decompressing
    multicore_fifo_push_blocking(CORE1_CMD_ROI_UPDATE);
}
#ifdef POLYKYBD_CRASH_TEST
void core1_crash_test(void) {
    multicore_fifo_push_blocking(CORE1_CMD_CRASH_TEST);
}
#endif


#else  // !USE_CORE1: no core1 service, so the Eden idle loop renders on core0
bool core1_eden_available(void) { return false; }
void core1_eden_key(uint32_t arg) { (void)arg; }
#endif
