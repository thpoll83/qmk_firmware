// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
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
    // The page covers firmware bytes [s_next_offset - s_buf_fill .. s_next_offset - 1].
    // Its flash offset within the staging data region is (s_next_offset - s_buf_fill).
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
}

void ota_begin(uint32_t image_size, uint32_t image_crc) {
    if (!s_initialized) ota_flash_init();

    s_next_offset    = 0;
    s_buf_fill       = 0;
    s_commit_pending = false;
    memset(s_page_buf, 0xFF, FLASH_PAGE_SIZE);

    // Reject malformed sizes — protects EEPROM from runaway writes.
    if (image_size == 0 || image_size > OTA_MAX_FW_SIZE) {
        s_image_size = 0;
        s_image_crc  = 0;
        return;
    }
    s_image_size = image_size;
    s_image_crc  = image_crc;

    // Erase header sector + data region in one pass.
    // Aligning up to the next sector boundary covers the full data region.
    uint32_t data_sectors = (OTA_MAX_FW_SIZE + FLASH_SECTOR_SIZE - 1) / FLASH_SECTOR_SIZE;
    uint32_t erase_size   = OTA_STAGING_HEADER_SIZE + data_sectors * FLASH_SECTOR_SIZE;
    uint32_t irq = save_and_disable_interrupts();
    flash_range_erase(OTA_STAGING_OFFSET, erase_size);
    restore_interrupts(irq);
}

bool ota_write_chunk(uint32_t offset, const uint8_t *data, uint8_t len) {
    if (!s_initialized)                    return false;
    if (offset != s_next_offset)           return false;
    if (offset >= s_image_size)            return false;

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
    // Header lives in the first (already erased) sector of the staging region.
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
    // Read staged image header from XIP flash
    const uint32_t *hdr = (const uint32_t *)(XIP_BASE + OTA_STAGING_OFFSET);
    if (hdr[0] != OTA_STAGING_MAGIC) return;   // no valid staged image

    s_commit_pending = false;
    ota_do_apply(hdr[1]);   // never returns
}
