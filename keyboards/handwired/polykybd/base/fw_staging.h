// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdint.h>
#include <stdbool.h>

// Flash offsets (relative to start of physical flash, i.e. XIP_BASE + offset = XIP address)
#define FW_STAGING_OFFSET      0x100000UL      // staging region starts at 1 MB
#define FW_STAGING_HEADER_SIZE 0x001000UL      // first 4 KB of staging = header sector
#define FW_STAGING_DATA_OFFSET (FW_STAGING_OFFSET + FW_STAGING_HEADER_SIZE)
#define FW_UP_MAX_SIZE         0x100000UL      // max 1 MB firmware image
#define FW_STAGING_MAGIC       0xD1F1A51BUL

// Bytes per firmware-update chunk (HID and split-RPC payload).
// 56 bytes fits in the 64-byte RAW_EPSIZE HID packet (2 bytes framing + 4 bytes offset + 56 data + 2 spare).
// The split RPC struct is 4 CRC + 4 offset + 56 data = 64 bytes < RPC_M2S_BUFFER_SIZE 72 bytes.
#define FW_UP_CHUNK_SIZE 56

// Must be called once before any other fw_staging_* function.
void fw_staging_init(void);

// Synchronous begin (master / USB side): erases staging flash sector-by-sector
// with interrupts briefly re-enabled between sectors.  Blocks for ~50 ms per
// sector (ceil(image_size/4096) + 1 sectors total).
void fw_staging_begin(uint32_t image_size, uint32_t image_crc);

// Deferred begin (slave side): stores parameters and schedules the erase.
// Returns immediately — safe inside a split-link transaction handler.
// Call fw_staging_process_deferred() from housekeeping_task_user() to drive the erase.
void fw_staging_begin_deferred(uint32_t image_size, uint32_t image_crc);

// Erases one staging sector per call.  Call from housekeeping_task_user()
// until fw_staging_write_chunk() no longer returns false for offset==0.
void fw_staging_process_deferred(void);

// Write one chunk of sequential firmware data to staging.
// `offset` must equal the cumulative bytes already written; returns false on error.
bool fw_staging_write_chunk(uint32_t offset, const uint8_t *data, uint8_t len);

// Flush any partial last page, verify full CRC32 of staged data, write header magic.
// Returns true on CRC match; sets commit-pending flag on success.
bool fw_staging_finalize(void);

// True while the deferred sector-by-sector erase (fw_staging_begin_deferred) is still running.
bool fw_staging_erase_pending(void);

// True from fw_staging_begin/fw_staging_begin_deferred until fw_staging_finalize() returns
// (success or failure).  Use this to suppress EEPROM saves during a fw_up to avoid a
// wear-leveling compact operation (backing_store_erase = 100 ms IRQ-disabled) racing
// with incoming FW_UP_CHUNK transactions and exhausting all UART retries.
bool fw_staging_fw_up_active(void);

// True if any chunk data has been written to staging since the last fw_staging_begin/fw_staging_begin_deferred.
// Used by the slave handler to detect partial writes from a previous failed attempt
// that require re-erasing staging before accepting new chunks.
bool fw_staging_written(void);

// True after a successful fw_staging_finalize(); cleared after fw_staging_apply_and_reboot() is called.
bool fw_staging_commit_pending(void);

// Return firmware image size (from linker symbols).
uint32_t fw_staging_get_own_fw_size(void);

// Compute CRC32 of the running firmware image (slow; avoid calling on every boot).
uint32_t fw_staging_get_own_fw_crc(void);

// Return pointer to start of active firmware in flash (XIP address of __flash_binary_start).
const uint8_t *fw_staging_get_fw_base(void);

// Copy staging area → active firmware region, then hard-reset.  NEVER RETURNS.
// No-op (returns normally) if no valid staged image is found.
void fw_staging_apply_and_reboot(void);

// True if the staging header sector holds a valid, applyable image
// (magic present, size in range).  Written by fw_staging_finalize() on CRC match.
bool fw_staging_has_valid_staged_image(void);

// Arm the deferred apply: housekeeping_task_user() will then run
// fw_staging_apply_and_reboot() once.  Use only after a successful COMMIT
// (fw_staging_has_valid_staged_image() == true).  PHASE 2: master only.
void fw_staging_arm_apply(void);

// ---------------------------------------------------------------------------
// Diagnostic snapshot — populated by the slave's handlers so the master can
// query "what does the slave think happened" after a failed FW_UP_CHUNK.
// ---------------------------------------------------------------------------
typedef struct _fw_staging_status_t {
    uint8_t  initialized;
    uint8_t  fw_up_active;
    uint8_t  erase_pending;
    uint8_t  last_chunk_ack;        // last value returned from chunk handler (SYNC_ACK / SYNC_CRC32_ERR)
    uint16_t erase_sector_next;
    uint16_t erase_sector_count;
    uint32_t next_offset;            // s_next_offset (bytes accepted)
    uint32_t last_chunk_offset;      // offset arg of most recent chunk
    uint16_t begin_handler_calls;
    uint16_t chunk_handler_calls;
    uint16_t chunk_handler_errors;   // chunks that hit CRC mismatch / write fail
    uint16_t process_deferred_calls; // times fw_staging_process_deferred() entered
    uint16_t process_deferred_advances; // times a sector was actually erased
    uint16_t pad;
} fw_staging_status_t;

// Fill `out` with the current internal state.  Safe to call from anywhere.
void fw_staging_get_status(fw_staging_status_t *out);

// Counter / state mutators used by the split handlers in split_fw_up.c.
void fw_staging_note_begin_call(void);
void fw_staging_note_chunk_call(uint32_t offset, uint8_t ack);

// Diagnostic helper used while bisecting the fw_up slave-hang bug
// (see FW_UP_DEBUG_NOTES.md): set the s_fw_up_active flag without
// performing the staging erase / core1 halt.  Lets the master act as a
// pure relay (master never touches its own staging) so we can isolate
// whether the failure is in the staging code or in the split transport.
// Must be paired with a `false` call to clear when the relay-only flow
// ends.
void fw_staging_set_fw_up_active(bool active);
