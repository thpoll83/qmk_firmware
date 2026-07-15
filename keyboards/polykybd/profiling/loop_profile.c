// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#include "loop_profile.h"

#ifdef POLYKYBD_LOOP_PROFILE

#include <print.h>
#include "hardware/structs/timer.h"   // timer_hw->timerawl — raw 1 MHz us counter
                                      // (lightweight struct header; avoids the pico
                                      // alarm API inlines that trip QMK's -Werror)

// Iterations between emitted summaries. The main loop runs on the order of ~1 kHz,
// so ~8k iterations is roughly one line every several seconds. Emitted with uprintf
// (not debug_enable-gated): a build with this flag deliberately wants the readout,
// matching the split-link health counter in bridge_helper.c.
#ifndef LOOP_PROFILE_LOG_EVERY
#    define LOOP_PROFILE_LOG_EVERY 8192u
#endif

// Per-iteration accumulators, reset at each boundary by loop_profile_tick().
static uint32_t s_iter_start_us = 0;      // time_us at the previous boundary
static uint32_t s_bridge_us     = 0;      // blocking us in send_to_bridge this iter
static bool     s_overlay_iter  = false;  // a bulk overlay/mapping cmd ran this iter
static bool     s_have_start    = false;  // false until the first boundary is seen

// Iteration-time buckets (microseconds), split overlay vs normal:
//   0:<1ms 1:1-2 2:2-5 3:5-10 4:10-20 5:20-50 6:>=50ms
#define NBUCKET 7
static uint32_t s_bkt_norm[NBUCKET];
static uint32_t s_bkt_ovl[NBUCKET];

// Worst iteration seen all-time, with its context.
static uint32_t s_max_us        = 0;
static uint32_t s_max_bridge_us = 0;
static bool     s_max_overlay   = false;

static uint32_t s_iters     = 0;  // total iterations measured
static uint32_t s_ovl_iters = 0;  // of those, iterations that handled an overlay cmd
static uint32_t s_last_log  = 0;  // s_iters at the last emitted summary

static uint8_t bucket_of(uint32_t us) {
    if (us <  1000u) return 0;
    if (us <  2000u) return 1;
    if (us <  5000u) return 2;
    if (us < 10000u) return 3;
    if (us < 20000u) return 4;
    if (us < 50000u) return 5;
    return 6;
}

void loop_profile_note_overlay_cmd(void) {
    s_overlay_iter = true;
}

void loop_profile_add_bridge_us(uint32_t us) {
    s_bridge_us += us;
}

void loop_profile_tick(void) {
    uint32_t now = timer_hw->timerawl;

    if (s_have_start) {
        uint32_t dt = now - s_iter_start_us;   // modular u32 — correct across wrap
        uint8_t  b  = bucket_of(dt);
        if (s_overlay_iter) {
            s_bkt_ovl[b]++;
            s_ovl_iters++;
        } else {
            s_bkt_norm[b]++;
        }
        s_iters++;

        if (dt > s_max_us) {
            s_max_us        = dt;
            s_max_bridge_us = s_bridge_us;
            s_max_overlay   = s_overlay_iter;
        }

        if ((s_iters - s_last_log) >= LOOP_PROFILE_LOG_EVERY) {
            s_last_log = s_iters;
            uprintf("LoopProf: iters=%lu ovl=%lu worst=%lums(%s br=%lums)\n",
                    (unsigned long)s_iters, (unsigned long)s_ovl_iters,
                    (unsigned long)(s_max_us / 1000u),
                    s_max_overlay ? "ovl" : "norm",
                    (unsigned long)(s_max_bridge_us / 1000u));
            uprintf("  norm  <1=%lu 1-2=%lu 2-5=%lu 5-10=%lu 10-20=%lu 20-50=%lu 50+=%lu\n",
                    (unsigned long)s_bkt_norm[0], (unsigned long)s_bkt_norm[1],
                    (unsigned long)s_bkt_norm[2], (unsigned long)s_bkt_norm[3],
                    (unsigned long)s_bkt_norm[4], (unsigned long)s_bkt_norm[5],
                    (unsigned long)s_bkt_norm[6]);
            uprintf("  ovl   <1=%lu 1-2=%lu 2-5=%lu 5-10=%lu 10-20=%lu 20-50=%lu 50+=%lu\n",
                    (unsigned long)s_bkt_ovl[0], (unsigned long)s_bkt_ovl[1],
                    (unsigned long)s_bkt_ovl[2], (unsigned long)s_bkt_ovl[3],
                    (unsigned long)s_bkt_ovl[4], (unsigned long)s_bkt_ovl[5],
                    (unsigned long)s_bkt_ovl[6]);
        }
    }

    // Arm the next iteration.
    s_iter_start_us = now;
    s_bridge_us     = 0;
    s_overlay_iter  = false;
    s_have_start    = true;
}

#endif // POLYKYBD_LOOP_PROFILE
