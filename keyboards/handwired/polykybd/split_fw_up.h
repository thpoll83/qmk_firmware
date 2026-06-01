// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "split_sync.h"
#include "base/fw_staging.h"

#include <stdint.h>

// ---------------------------------------------------------------------------
// HID firmware update — split link transactions (master → slave)
// ---------------------------------------------------------------------------

#define FW_UP_VERSION_LEN 16

// Boot-time version query: master sends its FW_VERSION + fw_size; slave replies with its own.
typedef struct _fw_up_query_sync_t {
    uint32_t crc32;
    char     version[FW_UP_VERSION_LEN];
    uint32_t fw_size;
} fw_up_query_sync_t;

// Announce incoming firmware size + expected CRC32 to slave.
typedef struct _fw_up_begin_sync_t {
    uint32_t crc32;
    uint32_t image_size;
    uint32_t image_crc;
} fw_up_begin_sync_t;

// One chunk of firmware data (FW_UP_CHUNK_SIZE bytes).
// Total struct = 4+4+56 = 64 bytes < RPC_M2S_BUFFER_SIZE (72).
typedef struct _fw_up_chunk_sync_t {
    uint32_t crc32;
    uint32_t offset;
    uint8_t  data[FW_UP_CHUNK_SIZE];
} fw_up_chunk_sync_t;

// Diagnostic status query: master sends an empty/CRC-only request, slave
// fills the reply with fw_staging internal counters (begin/chunk handler
// call counts, current next_offset, erase progress, last chunk ack).  Used
// from the master after a failed FW_UP_CHUNK to see what the slave actually
// observed — distinguishes "slave never received the chunk" from "slave
// received it and rejected".  See FW_UP_DEBUG_NOTES.md.
typedef struct _fw_up_status_request_t {
    uint32_t crc32;       // CRC over the dummy field below
    uint32_t dummy;       // present so the struct has bytes to checksum
} fw_up_status_request_t;

typedef struct _fw_up_status_reply_t {
    uint32_t            crc32;   // CRC over the status fields
    fw_staging_status_t status;
} fw_up_status_reply_t;

// Apply/reboot coordination (master → slave).  A magic guard — on top of the
// CRC32 and QMK's own transport checksum — makes it extremely unlikely that a
// stray transaction triggers an unexpected reboot.  Shared by the FW_UP
// apply-and-reboot and the plain QK_REBOOT paths; the transaction ID
// (USER_SYNC_FW_UP_APPLY vs USER_SYNC_REBOOT) selects the slave's action.
#define FW_UP_SYNC_MAGIC 0xB007B007u

typedef struct _fw_up_apply_sync_t {
    uint32_t crc32;   // [0..3] CRC over the magic, filled in by send_to_bridge()
    uint32_t magic;   // [4..7] must equal FW_UP_SYNC_MAGIC
} fw_up_apply_sync_t;

void user_sync_fw_up_query_handler  (uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data);
void user_sync_fw_up_begin_handler  (uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data);
void user_sync_fw_up_chunk_handler  (uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data);
void user_sync_fw_up_commit_handler (uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data);
void user_sync_fw_up_status_handler (uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data);
void user_sync_fw_up_apply_handler  (uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data);
void user_sync_reboot_handler       (uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data);
