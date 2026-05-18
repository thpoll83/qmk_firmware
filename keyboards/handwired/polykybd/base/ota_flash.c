// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#include "quantum.h"

#include "ota_flash.h"
#include "base/crc32.h"

#include "hardware/flash.h"
#include "hardware/sync.h"

#include <string.h>

// ---------------------------------------------------------------------------
// Linker-exported firmware boundary symbols
// ---------------------------------------------------------------------------
extern uint8_t __flash_binary_start;
extern uint8_t __flash_binary_end;

static bool     s_initialized = false;

// ---------------------------------------------------------------------------
// Internal staging state
// ---------------------------------------------------------------------------
static uint32_t s_image_size;
static uint32_t s_image_crc;

// Page-accumulation buffer (FLASH_PAGE_SIZE = 256 bytes)
static uint8_t  s_page_buf[FLASH_PAGE_SIZE];
static uint32_t s_next_offset;   // total bytes accepted so far
static uint32_t s_buf_fill;      // bytes pending in s_page_buf

static bool     s_commit_pending;

// ---------------------------------------------------------------------------
// Deferred-erase state (used by slave handler to avoid blocking the split link)
// ---------------------------------------------------------------------------
static bool     s_erase_pending;
static uint32_t s_erase_sector_count;  // total sectors to erase (header + data)
static uint32_t s_erase_sector_next;   // next sector index not yet erased

// ---------------------------------------------------------------------------
// Helper: chain CRC32 over a large buffer in 60 000-byte chunks
// (crc32_1byte length parameter is uint16_t, max 65535)
// ---------------------------------------------------------------------------
static uint32_t crc32_large(const uint8_t *data, uint32_t size) {
    uint32_t crc = 0;
    while (size > 0) {
        uint16_t chunk = (size > 60000u) ? 60000u : (uint16_t)size;
        crc  = crc32_1byte(data, chunk, crc);
        data += chunk;
        size -= chunk;
    }
    return crc;
}

// ---------------------------------------------------------------------------
// Flush accumulated page buffer to staging flash
// ---------------------------------------------------------------------------
static void flush_page(void) {
    uint32_t flash_offs = OTA_STAGING_DATA_OFFSET + (s_next_offset - s_buf_fill);
    uint32_t irq = save_and_disable_interrupts();
    flash_range_program(flash_offs, s_page_buf, FLASH_PAGE_SIZE);
    restore_interrupts(irq);
    memset(s_page_buf, 0xFF, FLASH_PAGE_SIZE);
    s_buf_fill = 0;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void ota_flash_init(void) {
    s_initialized    = true;
    s_commit_pending = false;
    s_erase_pending  = false;
}

// Synchronous begin: erases staging sector-by-sector with interrupts briefly
// re-enabled between sectors so USB and watchdog stay alive.  Use this on the
// master (USB side); the host app timeout must cover the full erase duration
// (~50 ms × ceil(image_size/4096) sectors).
void ota_begin(uint32_t image_size, uint32_t image_crc) {
    if (!s_initialized) ota_flash_init();

    s_next_offset    = 0;
    s_buf_fill       = 0;
    s_commit_pending = false;
    s_erase_pending  = false;
    memset(s_page_buf, 0xFF, FLASH_PAGE_SIZE);

    if (image_size == 0 || image_size > OTA_MAX_FW_SIZE) {
        s_image_size = 0;
        s_image_crc  = 0;
        return;
    }
    s_image_size = image_size;
    s_image_crc  = image_crc;

    // Erase header sector then each data sector individually.
    // Re-enabling interrupts between sectors keeps USB/watchdog responsive
    // during the ~50 ms erase window per sector.
    uint32_t data_sectors = (image_size + FLASH_SECTOR_SIZE - 1) / FLASH_SECTOR_SIZE;
    uint32_t irq;

    irq = save_and_disable_interrupts();
    flash_range_erase(OTA_STAGING_OFFSET, FLASH_SECTOR_SIZE);
    restore_interrupts(irq);

    for (uint32_t s = 0; s < data_sectors; s++) {
        irq = save_and_disable_interrupts();
        flash_range_erase(OTA_STAGING_DATA_OFFSET + s * FLASH_SECTOR_SIZE, FLASH_SECTOR_SIZE);
        restore_interrupts(irq);
    }
}

// Deferred begin: stores parameters, schedules sector-by-sector erase via
// ota_process_deferred() called from housekeeping_task_user().
// Returns immediately — safe to call from a split-link transaction handler.
void ota_begin_deferred(uint32_t image_size, uint32_t image_crc) {
    if (!s_initialized) ota_flash_init();

    s_next_offset    = 0;
    s_buf_fill       = 0;
    s_commit_pending = false;
    memset(s_page_buf, 0xFF, FLASH_PAGE_SIZE);

    if (image_size == 0 || image_size > OTA_MAX_FW_SIZE) {
        s_image_size    = 0;
        s_image_crc     = 0;
        s_erase_pending = false;
        return;
    }
    s_image_size = image_size;
    s_image_crc  = image_crc;

    uint32_t data_sectors   = (image_size + FLASH_SECTOR_SIZE - 1) / FLASH_SECTOR_SIZE;
    s_erase_sector_count    = 1 + data_sectors;  // index 0 = header, 1..N = data
    s_erase_sector_next     = 0;
    s_erase_pending         = true;
}

// Erase one staging sector per call.  Must be called repeatedly from
// housekeeping_task_user() until ota_erase_pending() returns false.
void ota_process_deferred(void) {
    if (!s_erase_pending) return;

    if (s_erase_sector_next >= s_erase_sector_count) {
        s_erase_pending = false;
        return;
    }

    // Rate-limit to one sector per 70ms: each sector takes ~50ms (irqs disabled),
    // leaving ~20ms of UART-responsive time between erasures.  Without this gate
    // housekeeping fires every ~1ms and the slave spends >98% of the erase period
    // with interrupts off, making the split link completely unresponsive and
    // causing every OTA_CHUNK transaction from the master to fail.
    static uint32_t s_last_sector_ms = 0;
    if (timer_elapsed32(s_last_sector_ms) < 70) return;

    uint32_t offset;
    if (s_erase_sector_next == 0) {
        offset = OTA_STAGING_OFFSET;   // header sector
    } else {
        offset = OTA_STAGING_DATA_OFFSET + (s_erase_sector_next - 1) * FLASH_SECTOR_SIZE;
    }
    uint32_t irq = save_and_disable_interrupts();
    flash_range_erase(offset, FLASH_SECTOR_SIZE);
    restore_interrupts(irq);
    s_last_sector_ms = timer_read32();

    s_erase_sector_next++;
    if (s_erase_sector_next >= s_erase_sector_count) {
        s_erase_pending = false;
        uprintf("ota_process_deferred: erase complete (%lu sectors)\n", s_erase_sector_count);
    } else {
        uprintf("ota_process_deferred: erased sector %lu/%lu\n", s_erase_sector_next, s_erase_sector_count);
    }
}

bool ota_write_chunk(uint32_t offset, const uint8_t *data, uint8_t len) {
    if (!s_initialized) {
        uprintf("ota_write_chunk: not initialized\n");
        return false;
    }
    if (s_erase_pending) {
        uprintf("ota_write_chunk: erase still pending (sector %lu/%lu)\n", s_erase_sector_next, s_erase_sector_count);
        return false;
    }
    if (offset != s_next_offset) {
        uprintf("ota_write_chunk: offset mismatch got=%lu expected=%lu\n", offset, s_next_offset);
        return false;
    }
    if (offset >= s_image_size) {
        uprintf("ota_write_chunk: offset=%lu >= image_size=%lu\n", offset, s_image_size);
        return false;
    }

    // Clamp trailing bytes for the partial last chunk — callers always pass
    // OTA_CHUNK_SIZE so the pad bytes (0xFF) past s_image_size must be dropped.
    if (offset + len > s_image_size) {
        len = (uint8_t)(s_image_size - offset);
    }

    const uint8_t *src       = data;
    uint8_t        remaining = len;

    while (remaining > 0) {
        uint8_t space = FLASH_PAGE_SIZE - s_buf_fill;
        uint8_t copy  = (remaining < space) ? remaining : space;

        memcpy(s_page_buf + s_buf_fill, src, copy);
        s_buf_fill    += copy;
        s_next_offset += copy;
        src           += copy;
        remaining     -= copy;

        if (s_buf_fill == FLASH_PAGE_SIZE) {
            flush_page();
        }
    }
    return true;
}

bool ota_finalize(void) {
    if (!s_initialized) return false;

    // Flush any partial final page (padded with 0xFF already from ota_begin/flush_page)
    if (s_buf_fill > 0) {
        flush_page();
    }

    // Verify CRC32 of staged data
    const uint8_t *staged = (const uint8_t *)(XIP_BASE + OTA_STAGING_DATA_OFFSET);
    if (crc32_large(staged, s_image_size) != s_image_crc) {
        return false;
    }

    // Write header magic — marks staging as valid for ota_apply_and_reboot()
    static uint8_t hdr_page[FLASH_PAGE_SIZE];
    memset(hdr_page, 0xFF, sizeof(hdr_page));
    uint32_t *w    = (uint32_t *)hdr_page;
    w[0]           = OTA_STAGING_MAGIC;
    w[1]           = s_image_size;
    w[2]           = s_image_crc;
    w[3]           = 0x00000000UL;
    uint32_t irq   = save_and_disable_interrupts();
    flash_range_program(OTA_STAGING_OFFSET, hdr_page, FLASH_PAGE_SIZE);
    restore_interrupts(irq);

    s_commit_pending = true;
    return true;
}

bool ota_erase_pending(void) {
    return s_erase_pending;
}

bool ota_staging_written(void) {
    return s_next_offset > 0;
}

bool ota_commit_pending(void) {
    return s_commit_pending;
}

uint32_t ota_get_own_fw_size(void) {
    return (uint32_t)(&__flash_binary_end - &__flash_binary_start);
}

uint32_t ota_get_own_fw_crc(void) {
    return crc32_large(&__flash_binary_start, ota_get_own_fw_size());
}

const uint8_t *ota_get_fw_base(void) {
    return &__flash_binary_start;
}

// ---------------------------------------------------------------------------
// RAM-resident apply routine: copy staging → active firmware + hard-reset.
// Must run from RAM because it erases the flash region it was loaded from.
// ---------------------------------------------------------------------------
static void __no_inline_not_in_flash_func(ota_do_apply)(uint32_t image_size) {
    uint8_t  page_buf[FLASH_PAGE_SIZE];
    uint32_t irq = save_and_disable_interrupts();

    uint32_t num_sectors = (image_size + FLASH_SECTOR_SIZE - 1) / FLASH_SECTOR_SIZE;

    for (uint32_t sec = 0; sec < num_sectors; sec++) {
        uint32_t dst_offset  = sec * FLASH_SECTOR_SIZE;
        uint32_t src_base    = XIP_BASE + OTA_STAGING_DATA_OFFSET + sec * FLASH_SECTOR_SIZE;

        // After flash_range_erase returns, the SDK re-enables XIP (standard mode).
        // It is therefore safe to read staging data via XIP between operations.
        flash_range_erase(dst_offset, FLASH_SECTOR_SIZE);

        uint32_t pages = FLASH_SECTOR_SIZE / FLASH_PAGE_SIZE;
        for (uint32_t pg = 0; pg < pages; pg++) {
            // Copy staging page to stack buffer (RAM) before XIP is disabled by program().
            memcpy(page_buf, (const void *)(src_base + pg * FLASH_PAGE_SIZE), FLASH_PAGE_SIZE);
            flash_range_program(dst_offset + pg * FLASH_PAGE_SIZE, page_buf, FLASH_PAGE_SIZE);
        }
    }

    restore_interrupts(irq);
    NVIC_SystemReset();
}

void ota_apply_and_reboot(void) {
    const uint32_t *hdr = (const uint32_t *)(XIP_BASE + OTA_STAGING_OFFSET);
    if (hdr[0] != OTA_STAGING_MAGIC) return;   // no valid staged image

    s_commit_pending = false;
    ota_do_apply(hdr[1]);   // never returns
}
