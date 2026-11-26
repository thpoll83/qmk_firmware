// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// QMK's sync timer gives the slave the master's clock: every 100 ms the master sends
// its time, and the slave sets sync_timer_ms = master time - its own time. The slave
// applies that value in its main loop (transactions_slave(), from the matrix scan),
// not when the serial thread receives it. When the loop is held up, the slave's own
// time has moved on by then, so the offset comes out smaller by the delay and the
// slave's clock runs behind the master's. Flash writes while a firmware stages hold
// the slave's loop, and the flash cue on both halves then showed a steady lag of
// about 50 ms on the HIL rig (2026-11-26).
//
// A late apply can only make the offset smaller, never larger, so the largest offset
// seen recently is the one with the least delay. The slave uses the largest offset of
// the last 5 to 10 s (two 5 s buckets), agreed with the maintainer 2026-11-26. The two
// crystals drift apart by well under 1 ms in that time. The master needs no filter.

#include "sync_clock.h"
#include "quantum.h"

#if defined(SPLIT_KEYBOARD) && !defined(DISABLE_SYNC_TIMER)

// quantum/sync_timer.c; written by the slave's main loop, read here on the same loop.
extern volatile int32_t sync_timer_ms;

#    define SYNC_CLOCK_BUCKET_MS 5000u   // two buckets: the best offset of the last 5..10 s
// An offset this much below the best is a new master clock (the master rebooted and
// its timer restarted), not a late update: start over from it. Half the 2048 ms cue
// cycle; a held-up loop is far shorter.
#    define SYNC_CLOCK_JUMP_MS 1024

static int32_t  s_best_cur;
static int32_t  s_best_prev;
static uint32_t s_bucket_at;
static bool     s_have;

static int32_t best_offset(void) {
    return s_best_cur > s_best_prev ? s_best_cur : s_best_prev;
}

void poly_sync_clock_task(void) {
    if (is_keyboard_master()) return;
    const int32_t  off = sync_timer_ms;
    const uint32_t now = timer_read32();
    if (!s_have || off < best_offset() - SYNC_CLOCK_JUMP_MS) {
        s_best_cur = s_best_prev = off;
        s_bucket_at              = now;
        s_have                   = true;
        return;
    }
    if (TIMER_DIFF_32(now, s_bucket_at) >= SYNC_CLOCK_BUCKET_MS) {
        s_best_prev = s_best_cur;
        s_best_cur  = off;
        s_bucket_at = now;
    } else if (off > s_best_cur) {
        s_best_cur = off;
    }
}

uint32_t poly_sync_clock_ms(void) {
    if (is_keyboard_master() || !s_have) return sync_timer_read32();
    return timer_read32() + (uint32_t)best_offset();
}

#else

void poly_sync_clock_task(void) {}

uint32_t poly_sync_clock_ms(void) {
    return timer_read32();
}

#endif
