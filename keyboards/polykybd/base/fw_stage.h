// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdbool.h>
#include <stdint.h>

// The stage of an fw_staging stream (firmware image, font-pack bundle, doom WHX or
// pack), as one explicit state instead of the two flags it used to be
// (s_fw_up_active + s_erase_pending). The fourth combination of those flags,
// erase pending with no stream, was reachable only by finalizing mid-erase, and
// it kept erasing after finalize had released core1. FINALIZED now ends the erase.
// Pure, so the transitions are unit-tested (make test:polykybd_fw_stage).
//
// The FW-2 confirmation prompt is its own small machine inside fw_staging.c, and
// the commit, reboot and font-pack-reload requests are independent armed flags;
// none of them is a stage of the stream.
typedef enum {
    FW_STAGE_IDLE = 0,  // no stream
    FW_STAGE_ERASING,   // a deferred BEGIN is erasing the slot, one sector per pass
    FW_STAGE_RECEIVING, // the slot is erased; chunks are accepted until finalize
} fw_stage_t;

typedef enum {
    FW_EV_BEGIN_SYNC,     // fw_staging_begin_target(): erased synchronously, size valid
    FW_EV_BEGIN_DEFERRED, // fw_staging_begin_deferred_target(): size valid
    FW_EV_BEGIN_REFUSED,  // either BEGIN with a size of 0 or past the slot
    FW_EV_ERASE_DONE,     // fw_staging_process_deferred() erased the last sector
    FW_EV_FINALIZED,      // fw_staging_finalize*(): the stream is over, verdict aside
} fw_stage_event_t;

fw_stage_t fw_stage_next(fw_stage_t stage, fw_stage_event_t ev);

// Whether fw_staging_write_chunk() may write a chunk. Not while the slot is still
// being erased, and not after a finalize stopped that erase (erase_cut_short):
// the stage is IDLE then and s_next_offset is still 0, so chunk 0 would pass the
// offset checks and land on sectors that were never erased. Only the next BEGIN,
// which erases again, clears erase_cut_short.
bool fw_stage_accepts_chunk(fw_stage_t stage, bool erase_cut_short);

// Why fw_staging_write_chunk() refused the most recent chunk. The slave has no
// console, so its uprintf of the reason is lost; this code reaches the master in
// the STATUS reply (fw_staging_status_t.last_chunk_refusal) instead.
typedef enum {
    FW_CHUNK_REFUSAL_NONE = 0,     // written, or never reached the stager (see last_chunk_ack)
    FW_CHUNK_REFUSAL_NOT_INITIALIZED,
    FW_CHUNK_REFUSAL_ERASE_CUT_SHORT,
    FW_CHUNK_REFUSAL_ERASE_PENDING,
    FW_CHUNK_REFUSAL_OFFSET_MISMATCH, // ahead of the cursor: an earlier chunk is missing
    FW_CHUNK_REFUSAL_PAST_END,
} fw_chunk_refusal_t;

// Short name for the console line; "unknown" for a value this build does not know.
const char *fw_chunk_refusal_name(uint8_t refusal);
