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
static uint32_t s_render_us     = 0;      // us in update_displays this iter
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
static uint32_t s_max_render_us = 0;
static bool     s_max_overlay   = false;

// Running TOTALS (microseconds) across every OVERLAY iteration. rest = wall - bridge
// - render is what is NOT the two timed costs (HID copy, RLE kick, core1 wait, matrix
// scan, the rest of housekeeping). Split so the summary can say where the overlay-
// iteration wall time actually goes, in aggregate — not just at the single worst
// spike. A u32 us total wraps only after ~71 min of accumulated overlay-iteration
// wall time, far beyond any measurement session, so no ms-rounding (which would lose
// the sub-ms-per-iteration render time) is needed.
static uint32_t s_ovl_wall_us   = 0;
static uint32_t s_ovl_bridge_us = 0;
static uint32_t s_ovl_render_us = 0;

// All-time render sub-phase totals (microseconds), broken out of the render time
// above so a render-bound stall can be pinned to clear / legend / overlay / send.
// These accumulate only during the per-keycap main path in update_displays(), so
// their sum is slightly below s_ovl_render_us (the difference = the per-key loop
// overhead + the rare chrome/lang/doom paths that aren't sub-timed).
static uint32_t s_rphase_us[LP_RP_COUNT];

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

void loop_profile_add_render_us(uint32_t us) {
    s_render_us += us;
}

void loop_profile_add_render_phase(uint8_t phase, uint32_t us) {
    if (phase < LP_RP_COUNT) {
        s_rphase_us[phase] += us;
    }
}

void loop_profile_tick(void) {
    uint32_t now = timer_hw->timerawl;

    if (s_have_start) {
        uint32_t dt = now - s_iter_start_us;   // modular u32 — correct across wrap
        uint8_t  b  = bucket_of(dt);
        if (s_overlay_iter) {
            s_bkt_ovl[b]++;
            s_ovl_iters++;
            // Accumulate this overlay iteration's wall / bridge / render into the
            // running totals. Clamp bridge, then render into the remaining wall, so
            // bridge+render can never exceed the wall and the derived "rest" (wall -
            // bridge - render, unsigned) can never underflow on a measurement artefact.
            uint32_t br  = (s_bridge_us > dt) ? dt : s_bridge_us;
            uint32_t rem = dt - br;
            uint32_t rn  = (s_render_us > rem) ? rem : s_render_us;
            s_ovl_wall_us   += dt;
            s_ovl_bridge_us += br;
            s_ovl_render_us += rn;
        } else {
            s_bkt_norm[b]++;
        }
        s_iters++;

        if (dt > s_max_us) {
            s_max_us        = dt;
            s_max_bridge_us = s_bridge_us;
            s_max_render_us = s_render_us;
            s_max_overlay   = s_overlay_iter;
        }

        if ((s_iters - s_last_log) >= LOOP_PROFILE_LOG_EVERY) {
            s_last_log = s_iters;
            uprintf("LoopProf: iters=%lu ovl=%lu worst=%lums(%s br=%lums rn=%lums)\n",
                    (unsigned long)s_iters, (unsigned long)s_ovl_iters,
                    (unsigned long)(s_max_us / 1000u),
                    s_max_overlay ? "ovl" : "norm",
                    (unsigned long)(s_max_bridge_us / 1000u),
                    (unsigned long)(s_max_render_us / 1000u));
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
            // Where the OVERLAY-iteration wall time goes, in aggregate (ms totals).
            // rest = wall - bridge - render. This is the line that settles bridge-vs-
            // render: a big render share points at update_displays (baked resources
            // would not help); a big bridge share points at the master->slave relay.
            uint32_t rest_us = s_ovl_wall_us - s_ovl_bridge_us - s_ovl_render_us;
            uprintf("  ovltot wall=%lums bridge=%lums render=%lums rest=%lums\n",
                    (unsigned long)(s_ovl_wall_us   / 1000u),
                    (unsigned long)(s_ovl_bridge_us / 1000u),
                    (unsigned long)(s_ovl_render_us / 1000u),
                    (unsigned long)(rest_us         / 1000u));
            // Where the render time itself goes, in aggregate (ms totals). Breaks
            // down the `render=` figure above: clear (scratch wipe) / legend (glyph
            // lookup + raster) / overlay (bitmap blit) / send (SPI window push).
            uprintf("  rphase clear=%lums legend=%lums overlay=%lums send=%lums\n",
                    (unsigned long)(s_rphase_us[LP_RP_CLEAR]   / 1000u),
                    (unsigned long)(s_rphase_us[LP_RP_LEGEND]  / 1000u),
                    (unsigned long)(s_rphase_us[LP_RP_OVERLAY] / 1000u),
                    (unsigned long)(s_rphase_us[LP_RP_SEND]    / 1000u));
        }
    }

    // Arm the next iteration.
    s_iter_start_us = now;
    s_bridge_us     = 0;
    s_render_us     = 0;
    s_overlay_iter  = false;
    s_have_start    = true;
}

#endif // POLYKYBD_LOOP_PROFILE
