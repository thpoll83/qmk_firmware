// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#include "fw_stream.h"

// The hold flag is tested and changed under the core1 lock, and the hold is taken
// or released after it. On the slave, take runs on the split thread (a deferred
// BEGIN) and drop on the main thread (the erase completing), and the master
// re-sends BEGIN while it polls, so a BEGIN can preempt a drop. Both orders are
// safe: a BEGIN between the flag clear and the release takes a fresh hold first
// (the count goes 2 -> 1), and a drop is skipped while an erase is pending, so a
// drop that started for the old erase cannot release the new one's hold.
static void hold_take(fw_stream_t *s) {
    bool take = false;
    fw_stream_hw_lock();
    if (!s->hold) {
        s->hold = true;
        take    = true;
    }
    fw_stream_hw_unlock();
    if (take) {
        fw_stream_hw_hold();
    }
}

static void hold_drop(fw_stream_t *s) {
    bool drop = false;
    fw_stream_hw_lock();
    if (s->hold && s->stage != FW_STAGE_ERASING) {
        s->hold = false;
        drop    = true;
    }
    fw_stream_hw_unlock();
    if (drop) {
        fw_stream_hw_release();
    }
}

void fw_stream_init(fw_stream_t *s) {
    s->stage           = FW_STAGE_IDLE;
    s->erase_cut_short = false;
}

void fw_stream_begin_refused(fw_stream_t *s) {
    s->stage = fw_stage_next(s->stage, FW_EV_BEGIN_REFUSED);
}

void fw_stream_begin_sync(fw_stream_t *s) {
    s->stage           = fw_stage_next(s->stage, FW_EV_BEGIN_SYNC);
    s->erase_cut_short = false;
}

void fw_stream_begin_deferred(fw_stream_t *s, uint32_t sector_count) {
    s->sector_count    = sector_count;
    s->sector_next     = 0;
    s->stage           = fw_stage_next(s->stage, FW_EV_BEGIN_DEFERRED);
    s->erase_cut_short = false;
    // Halt core1 now and keep it halted for the whole erase. Restarting it per
    // sector exposes the ChibiOS Vector80/NMI window on every restart.
    hold_take(s);
}

// ERASING -> RECEIVING. Called with interrupts off, so a finalize on the split
// thread cannot land between the read of the stage and the store and have its
// IDLE overwritten. Returns whether this call made the transition; only that call
// drops the hold, after interrupts are back on.
static bool complete_irq_off(fw_stream_t *s) {
    const bool done = s->stage == FW_STAGE_ERASING;
    if (done) {
        s->stage = fw_stage_next(s->stage, FW_EV_ERASE_DONE);
    }
    return done;
}

fw_erase_step_t fw_stream_erase_step(fw_stream_t *s, bool may_erase) {
    if (s->stage != FW_STAGE_ERASING) {
        return FW_ERASE_IDLE;
    }
    if (!may_erase && s->sector_next < s->sector_count) {
        return FW_ERASE_WAIT;
    }
    const uint32_t irq = fw_stream_hw_irq_save();
    // Recheck with interrupts off. A finalize can preempt this pass after the check
    // at the top, end the stream and release core1. While the stage reads ERASING
    // with interrupts off, the hold is in place.
    if (s->stage != FW_STAGE_ERASING) {
        fw_stream_hw_irq_restore(irq);
        return FW_ERASE_STOPPED;
    }
    bool erased = false;
    if (s->sector_next < s->sector_count) {
        fw_stream_hw_erase_sector(s->sector_next);
        s->sector_next++;
        erased = true;
    }
    // Advance and decide completion before interrupts return. A BEGIN that
    // preempts after the restore resets both counters for its new erase; reading
    // them after the restore would skip that erase's sector 0, or end it early.
    const bool done = s->sector_next >= s->sector_count && complete_irq_off(s);
    fw_stream_hw_irq_restore(irq);
    if (done) {
        hold_drop(s);
        return FW_ERASE_DONE;
    }
    return erased ? FW_ERASE_SECTOR : FW_ERASE_STOPPED;
}

void fw_stream_finalize(fw_stream_t *s) {
    if (s->stage == FW_STAGE_ERASING) {
        s->erase_cut_short = true;
    }
    s->stage = fw_stage_next(s->stage, FW_EV_FINALIZED);
    hold_drop(s);
}

bool fw_stream_accepts_chunk(const fw_stream_t *s) {
    return fw_stage_accepts_chunk(s->stage, s->erase_cut_short);
}

bool fw_stream_erase_pending(const fw_stream_t *s) {
    return s->stage == FW_STAGE_ERASING;
}

bool fw_stream_active(const fw_stream_t *s) {
    return s->stage != FW_STAGE_IDLE;
}
