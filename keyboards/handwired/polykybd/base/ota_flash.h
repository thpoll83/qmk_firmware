#pragma once

#include <stdint.h>
#include <stdbool.h>

// Flash offsets (relative to start of physical flash, i.e. XIP_BASE + offset = XIP address)
#define OTA_STAGING_OFFSET      0x100000UL      // staging region starts at 1 MB
#define OTA_STAGING_HEADER_SIZE 0x001000UL      // first 4 KB of staging = header sector
#define OTA_STAGING_DATA_OFFSET (OTA_STAGING_OFFSET + OTA_STAGING_HEADER_SIZE)
#define OTA_MAX_FW_SIZE         0x100000UL      // max 1 MB firmware image
#define OTA_STAGING_MAGIC       0xD1F1A51BUL

// Bytes per OTA chunk (HID and split-RPC payload).
// 56 bytes fits in the 64-byte RAW_EPSIZE HID packet (2 bytes framing + 4 bytes offset + 56 data + 2 spare).
// The split RPC struct is 4 CRC + 4 offset + 56 data = 64 bytes < RPC_M2S_BUFFER_SIZE 72 bytes.
#define OTA_CHUNK_SIZE 56

// Must be called once before any other ota_* function.
// Copies boot2 to RAM (required by the RAM-resident apply routine).
void ota_flash_init(void);

// Erase staging area and prepare for incoming firmware chunks.
void ota_begin(uint32_t image_size, uint32_t image_crc);

// Write one chunk of sequential firmware data to staging.
// `offset` must equal the cumulative bytes already written; returns false on error.
bool ota_write_chunk(uint32_t offset, const uint8_t *data, uint8_t len);

// Flush any partial last page, verify full CRC32 of staged data, write header magic.
// Returns true on CRC match; sets commit-pending flag on success.
bool ota_finalize(void);

// True after a successful ota_finalize(); cleared after ota_apply_and_reboot() is called.
bool ota_commit_pending(void);

// Return firmware image size (from linker symbols).
uint32_t ota_get_own_fw_size(void);

// Compute CRC32 of the running firmware image (slow; avoid calling on every boot).
uint32_t ota_get_own_fw_crc(void);

// Return pointer to start of active firmware in flash (XIP address of __flash_binary_start).
const uint8_t *ota_get_fw_base(void);

// Copy staging area → active firmware region, then hard-reset.  NEVER RETURNS.
// No-op (returns normally) if no valid staged image is found.
void ota_apply_and_reboot(void);
