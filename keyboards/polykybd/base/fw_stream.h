// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "fw_stage.h"

// The bookkeeping of one fw_staging stream: its stage, the deferred erase, and the
// core1 hold it keeps from a deferred BEGIN until the erase completes or the stream
// is finalized. fw_staging.c keeps the image, the CRC, the signature and the flash
// layout; this file decides only WHEN things may happen.
//
// It is pure: the interrupt mask, the sector erase, the core1 lock and the hold go
// through the fw_stream_hw_* seam below, which fw_staging.c implements. On the
// slave the split thread (BEGIN, CHUNK, COMMIT) preempts the main thread
// (housekeeping's erase step), so the unit tests drive that preemption through the
// seam: a fake runs a finalize or a BEGIN at each point where interrupts come back
// on (make test:polykybd_fw_stream).
typedef struct {
    fw_stage_t stage;
    // A finalize that landed mid-erase stopped it, so the slot is partly erased. It
    // counts as dirty, or an identical BEGIN retry would be told the slot is clean
    // and ready, and chunks are refused until the next BEGIN erases again.
    bool erase_cut_short;
    // The stream's core1 hold is taken. Tested and changed under the core1 lock.
    bool     hold;
    uint32_t sector_next;  // next sector index not yet erased
    uint32_t sector_count; // sectors to erase (header + data)
} fw_stream_t;

typedef enum {
    FW_ERASE_IDLE,    // no erase pending
    FW_ERASE_WAIT,    // erase pending, but the caller's rate limit says not yet
    FW_ERASE_SECTOR,  // erased one sector; more remain
    FW_ERASE_DONE,    // this call finished the erase and released the hold
    FW_ERASE_STOPPED, // a finalize ended the stream during this call
} fw_erase_step_t;

void fw_stream_init(fw_stream_t *s);

// BEGIN with a size of 0 or past the slot. Stops a pending erase but does not end a
// stream already in progress (kept from the two-flag original; see fw_stage.c).
void fw_stream_begin_refused(fw_stream_t *s);
// BEGIN that the caller erased synchronously.
void fw_stream_begin_sync(fw_stream_t *s);
// BEGIN whose erase runs one sector per fw_stream_erase_step(). Takes the hold.
void fw_stream_begin_deferred(fw_stream_t *s, uint32_t sector_count);

// One housekeeping pass of a deferred erase. may_erase is the caller's rate limit.
fw_erase_step_t fw_stream_erase_step(fw_stream_t *s, bool may_erase);

// End of the stream (COMMIT, or the host's abort, which is also a COMMIT). Stops a
// pending erase, marks it cut short, and releases the hold.
void fw_stream_finalize(fw_stream_t *s);

bool fw_stream_accepts_chunk(const fw_stream_t *s);
bool fw_stream_erase_pending(const fw_stream_t *s);
bool fw_stream_active(const fw_stream_t *s);

// --- seam (fw_staging.c on the keyboard, a fake in the tests) ---------------
uint32_t fw_stream_hw_irq_save(void);
void     fw_stream_hw_irq_restore(uint32_t saved);
void     fw_stream_hw_erase_sector(uint32_t index);
void     fw_stream_hw_lock(void);
void     fw_stream_hw_unlock(void);
void     fw_stream_hw_hold(void);
void     fw_stream_hw_release(void);
