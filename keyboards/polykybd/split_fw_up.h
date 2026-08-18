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

// The flash-staging stream (firmware AND font-pack alike) is carried by ONE split
// transaction, USER_SYNC_FLASH_STAGE, whose sub-operation is selected by an `op`
// word right after the CRC32 in every request.  Keeping the four begin/chunk/
// commit/status transactions folded into one reclaims scarce transaction slots
// (QMK caps the table at 32).  `op` is a uint32_t, not a byte, so the u32 fields
// that follow stay 4-byte aligned — the RP2040 (Cortex-M0+) cannot do unaligned
// word access.  The single slave-side dispatcher is user_sync_flash_stage_handler.
enum flash_stage_op {
    FLASH_STAGE_BEGIN  = 0,  // announce size/crc + kick the deferred erase
    FLASH_STAGE_CHUNK  = 1,  // one FW_UP_CHUNK_SIZE-byte fragment
    FLASH_STAGE_COMMIT = 2,  // verify staged CRC + finalize (no reboot)
    FLASH_STAGE_STATUS = 3,  // diagnostic: return fw_staging internal counters
};

// Announce incoming firmware/font-pack size + expected CRC32 to slave.
// `target` (fw_target_t) selects which flash region the slave stages into and how
// it finalizes — 0 = FIRMWARE (also what older senders leave it), 1 = FONTPACK.
// Chunk/commit transactions carry no target; the slave's fw_staging remembers it
// from this begin.
typedef struct _fw_up_begin_sync_t {
    uint32_t crc32;
    uint32_t op;         // = FLASH_STAGE_BEGIN
    uint32_t image_size;
    uint32_t image_crc;
    uint8_t  target;
    uint8_t  bundle;   // FONTPACK target: which bundle slot (fontpack_slot()); 0 for FIRMWARE
} fw_up_begin_sync_t;

// One chunk of firmware data (FW_UP_CHUNK_SIZE bytes).
// Total struct = 4+4+4+56 = 68 bytes < RPC_M2S_BUFFER_SIZE (72).
typedef struct _fw_up_chunk_sync_t {
    uint32_t crc32;
    uint32_t op;         // = FLASH_STAGE_CHUNK
    uint32_t offset;
    uint8_t  data[FW_UP_CHUNK_SIZE];
} fw_up_chunk_sync_t;

// COMMIT carries no payload beyond the op selector (the slave finalizes whatever
// it has staged for the target it remembered from BEGIN).
typedef struct _fw_up_commit_sync_t {
    uint32_t crc32;
    uint32_t op;         // = FLASH_STAGE_COMMIT
} fw_up_commit_sync_t;

// Chunk reply — identity-bound ACK.  Every chunk used to be answered with the
// same bare 1-byte ACK (poly_sync_reply_t), so a stale reply left over from
// the PREVIOUS chunk was indistinguishable from the current one: the master
// counted the chunk as delivered while the slave never staged it, and the two
// write cursors drifted apart (observed 2026-06-10: slave next_off one chunk
// behind the stream; every later offset rejected; update dead at 6% / 83%).
// next_offset is the slave's write cursor AFTER processing this RPC; the
// master only accepts the chunk when ack == SYNC_ACK *and* next_offset has
// moved PAST the chunk's offset.  A stale previous-chunk reply has
// next_offset == offset and is rejected; a duplicate re-send of an
// already-staged chunk has next_offset > offset and passes (idempotent).
typedef struct _fw_up_chunk_reply_t {
    uint8_t  ack;
    uint8_t  _pad[3];
    uint32_t next_offset;
} fw_up_chunk_reply_t;

// Diagnostic status query: master sends an empty/CRC-only request, slave
// fills the reply with fw_staging internal counters (begin/chunk handler
// call counts, current next_offset, erase progress, last chunk ack).  Used
// from the master after a failed FW_UP_CHUNK to see what the slave actually
// observed — distinguishes "slave never received the chunk" from "slave
// received it and rejected".  See FW_UP_DEBUG_NOTES.md.
typedef struct _fw_up_status_request_t {
    uint32_t crc32;       // CRC over the fields below
    uint32_t op;          // = FLASH_STAGE_STATUS
    uint32_t dummy;       // present so the struct has bytes to checksum
} fw_up_status_request_t;

typedef struct _fw_up_status_reply_t {
    uint32_t            crc32;   // CRC over the status fields
    fw_staging_status_t status;
} fw_up_status_reply_t;

// The slave→master RPC buffer is a SILENT ceiling: transaction_rpc_exec refuses a
// transfer bigger than it and returns false BEFORE sending anything, so outgrowing
// it would make the status probe simply stop answering with nothing in the log.
// (last_commit_ack was added inside the existing pad, so this is unchanged — the
// assert is here to keep it that way.)
static_assert(sizeof(fw_up_status_reply_t) <= RPC_S2M_BUFFER_SIZE,
              "fw_up_status_reply_t exceeds RPC_S2M_BUFFER_SIZE — the STATUS probe would silently stop answering");

// Reset/apply coordination (master → slave).  ONE transaction (USER_SYNC_RESET)
// carries every "make the other half restart" action; the `action` byte selects
// which.  A magic guard — on top of the CRC32 and QMK's own transport checksum —
// makes it extremely unlikely that a stray transaction triggers an unexpected
// reset.  Actions:
//   RESET_ACTION_APPLY  — install the staged firmware image, then reboot (the
//                         second phase of a firmware update; the slave NACKs
//                         unless it holds a valid staged image).
//   RESET_ACTION_REBOOT — reboot only, no apply (the QK_REBOOT path).  Doubles as
//                         the handedness-change carrier: when set_handedness != 0
//                         the slave persists `is_left` to its EE_HANDS marker
//                         before rebooting, so both halves come up on the
//                         corrected left/right assignment (see hid_com.c case 25).
// The plain reboot/apply senders leave set_handedness zero via designated
// initialisers, so their behaviour is unchanged.
#define POLY_RESET_MAGIC 0xB007B007u

enum reset_action {
    RESET_ACTION_APPLY  = 0,  // install the staged image, then reboot
    RESET_ACTION_REBOOT = 1,  // reboot only (optionally persisting handedness first)
};

typedef struct _poly_reset_sync_t {
    uint32_t crc32;          // [0..3] CRC over the bytes below, filled in by send_to_bridge()
    uint32_t magic;          // [4..7] must equal POLY_RESET_MAGIC
    uint8_t  action;         // [8]    enum reset_action
    uint8_t  set_handedness; // [9]    reboot action only: 1 = persist `is_left` before reboot
    uint8_t  is_left;        // [10]   slave's new handedness when set_handedness != 0 (1 = left)
} poly_reset_sync_t;

// Master-side: relay one upload chunk (offset + FW_UP_CHUNK_SIZE bytes) to the
// slave with retries. `log_tag` non-NULL enables per-retry debug logging (pass
// NULL for a quiet relay). Returns true on slave ACK. See split_fw_up.c.
bool fw_up_relay_chunk_to_slave(uint32_t offset, const uint8_t *chunk_data, const char *log_tag);

// One slave-side dispatcher for the whole flash-staging stream (BEGIN / CHUNK /
// COMMIT / STATUS); it reads the `op` word and routes to the per-op logic.
void user_sync_flash_stage_handler  (uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data);

// Master-side helpers over the read-only FLASH_STAGE_STATUS op.
bool fw_up_query_slave_status(fw_staging_status_t *out);
void fw_up_log_slave_status(const char *tag);
// True when a non-ACK COMMIT means the slave REFUSED (re-flash) rather than the
// link dropping the answer (retry). `tag` prefixes the diagnostic line.
bool fw_up_slave_refused_commit(uint8_t slave_ack, const char *tag);
// One transaction (USER_SYNC_RESET) for apply-and-reboot, plain reboot, and the
// handedness-change reboot; the poly_reset_sync_t `action` byte selects which.
void user_sync_reset_handler        (uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data);
