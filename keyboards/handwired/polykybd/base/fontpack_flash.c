// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Master-side flashing of the "PlyF" font pack into the external-flash resource
// region, driven by the HID transport in hid_fontpack.c. Written IN PLACE in a
// single fixed slot — an interrupted flash leaves "no pack" (resident-only),
// which is a safe degraded state, so no separate staging area is needed.
//
// Dual-core flash safety mirrors base/fw_staging.c: core1 (which polls from XIP
// flash) is held in PSM reset for the whole sequence, and every flash_range_*
// call runs with interrupts disabled. An idle-timeout abort in
// fontpack_flash_housekeeping() releases core1 if the host abandons a transfer,
// so the keyboard never gets stuck with overlays disabled.

#include "fontpack.h"
#include <string.h>

#ifdef FONTPACK_FLASH_HOST_TEST
// Host harness supplies a fake flash buffer + stubs (see tests). Lets the
// paging/offset/lazy-erase bookkeeping be validated without hardware.
#  include "fontpack_flash_hosttest.h"
#else
#  include "quantum.h"
#  include "hardware/flash.h"
#  include "hardware/sync.h"
#  include "base/fw_staging.h"   // FW_RESOURCE_OFFSET
#endif

// The pack lives at the start of the resource region (flash offset, XIP-relative
// like the pico flash_range_* API expects — same convention as FW_STAGING_OFFSET).
#define FONTPACK_FLASH_OFFSET FW_RESOURCE_OFFSET

#define FONTPACK_FLASH_IDLE_MS 3000u   // abort a stalled transfer after this

#ifdef USE_CORE1
#    include "base/multicore/core1.h"
// PSM force-off for proc1 (copied from fw_staging.c — keep in sync).
#    define _PSM_FRCE_OFF  (*(volatile uint32_t *)0x40010004u)
#    define _PSM_PROC1_BIT (1u << 16)
static bool s_core1_held = false;
static void hold_core1(void) {
    if (s_core1_held) return;
    _PSM_FRCE_OFF |= _PSM_PROC1_BIT;
    __asm volatile("dsb" ::: "memory");
    s_core1_held = true;
}
static void release_core1(void) {
    if (!s_core1_held) return;
    _PSM_FRCE_OFF &= ~_PSM_PROC1_BIT;
    s_core1_held = false;
    multicore_launch_core1();
}
#else
static void hold_core1(void) {}
static void release_core1(void) {}
#endif

static bool     s_active;
static uint32_t s_pack_size;     // announced total size
static uint32_t s_next_offset;   // bytes accepted so far
static uint32_t s_erased_end;    // next un-erased flash offset (sector-aligned)
static uint8_t  s_buf[FLASH_PAGE_SIZE];
static uint32_t s_buf_fill;
static uint32_t s_last_activity;

// Erase whole sectors until `flash_end` is covered. Sequential writes mean each
// sector is erased exactly once, just before its first page is programmed.
static void erase_through(uint32_t flash_end) {
    while (s_erased_end < flash_end) {
        uint32_t irq = save_and_disable_interrupts();
        flash_range_erase(s_erased_end, FLASH_SECTOR_SIZE);
        restore_interrupts(irq);
        s_erased_end += FLASH_SECTOR_SIZE;
    }
}

static void program_page(uint32_t flash_offs, const uint8_t *page) {
    erase_through(flash_offs + FLASH_PAGE_SIZE);
    uint32_t irq = save_and_disable_interrupts();
    flash_range_program(flash_offs, page, FLASH_PAGE_SIZE);
    restore_interrupts(irq);
}

bool fontpack_flash_begin(uint32_t pack_size) {
    if (pack_size < sizeof(fontpack_header_t) || pack_size > FONTPACK_FLASH_MAX_SIZE) {
        return false;
    }
    release_core1();                 // clean re-arm if a prior transfer was mid-flight
    s_pack_size     = pack_size;
    s_next_offset   = 0;
    s_buf_fill      = 0;
    s_erased_end    = FONTPACK_FLASH_OFFSET;
    s_last_activity = timer_read32();
    s_active        = true;
    hold_core1();                    // held for the whole sequence
    return true;
}

bool fontpack_flash_write(uint32_t offset, const uint8_t *data, uint8_t len) {
    if (!s_active) return false;
    if (offset != s_next_offset) return false;        // out of order — host resyncs to next_offset
    if (s_next_offset + len > s_pack_size) return false;
    s_last_activity = timer_read32();
    for (uint8_t i = 0; i < len; ++i) {
        s_buf[s_buf_fill++] = data[i];
        s_next_offset++;
        if (s_buf_fill == FLASH_PAGE_SIZE) {
            program_page(FONTPACK_FLASH_OFFSET + (s_next_offset - FLASH_PAGE_SIZE), s_buf);
            s_buf_fill = 0;
        }
    }
    return true;
}

bool fontpack_flash_finalize(void) {
    if (!s_active) return false;
    if (s_buf_fill > 0) {                              // flush the partial tail page
        uint32_t flash_offs = FONTPACK_FLASH_OFFSET + (s_next_offset - s_buf_fill);
        memset(&s_buf[s_buf_fill], 0xFF, FLASH_PAGE_SIZE - s_buf_fill);
        program_page(flash_offs, s_buf);
        s_buf_fill = 0;
    }
    s_active = false;
    release_core1();
    fontpack_reload();                                 // CRC-verify from XIP + reassemble g_all_fonts
    return fontpack_present();
}

bool     fontpack_flash_active(void)      { return s_active; }
uint32_t fontpack_flash_next_offset(void) { return s_next_offset; }

// Call from housekeeping_task_user(): abort a stalled transfer so an abandoned
// flash can't leave core1 parked (overlays disabled) indefinitely.
void fontpack_flash_housekeeping(void) {
    if (s_active && timer_elapsed32(s_last_activity) > FONTPACK_FLASH_IDLE_MS) {
        s_active   = false;
        s_buf_fill = 0;
        release_core1();
    }
}
