// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#include "focus_ring.h"

#ifdef KEYBOARD_polykybd_split72
#    include <stdint.h>
#    include "quantum.h"
#    include "base/disp_array.h"
#    include "base/shift_reg.h"
#    include "base/tutorial_plan.h"   // the ring geometry + the dither field
#    include "side.h"
#    include QMK_KEYBOARD_H           // get_key_disp_bitmask, get_disp_bitmask_size

#    define POLY_FOCUS_KEYS      40   // display slots per half
#    define POLY_FOCUS_STRIDE   128   // scratch bytes per page row
#    define POLY_FOCUS_KEY_REACH 41   // half-diagonal of a 72x40 keycap, board units
#    define POLY_FOCUS_SLICE_MS   3   // the hard per-pass budget (see the header)
#    define POLY_FOCUS_FRAME_MS  16   // one latched instant per frame

static bool       s_active;
// ⚠️ THE FRAME LATCH, and deleting it is what turned the ripple into a scatter. Every
// slice of one frame must draw the SAME instant, or different keys show the arc at
// different radii and the ring shears apart. The tutorial's old renderer had this and I
// dropped it as incidental; it is not.
static bool       s_frame_busy;
static uint32_t   s_last_frame;
static bool       s_live;
static int16_t    s_cx, s_cy;         // the ripple's origin, board units
static uint32_t   s_start;
static bool       s_sweep;            // board-reveal profile (see poly_focus_start_sweep)
static uint8_t    s_scan;             // round-robin cursor over this half's slots
static uint8_t    s_marked[(POLY_FOCUS_KEYS + 7) / 8];   // keys currently carrying ink

// Latched once per tick so every key of one pass draws the SAME instant — the same
// reason the startup animation latches its comet set.
static tut_ring_t s_band;             // the drawn band
static tut_ring_t s_cull;             // the band grown by a keycap half-diagonal
static uint8_t    s_dens;
// The reveal's SPARK TRAIL (sweep profile only): the annulus just behind the front,
// from s_band.inner2 in to s_trail_in2, where sparse 2x2 sparks twinkle and thin out.
static bool       s_trail;            // false for the letter ring
static uint32_t   s_trail_in2;
static uint8_t    s_spark_tick;       // time bucket: the sparks re-roll per bucket

static inline bool bit_get(const uint8_t *m, uint8_t i) { return (m[i >> 3] >> (i & 7)) & 1u; }
static inline void bit_set(uint8_t *m, uint8_t i, bool v) {
    if (v) m[i >> 3] |= (uint8_t)(1u << (i & 7));
    else   m[i >> 3] &= (uint8_t)~(1u << (i & 7));
}

bool poly_focus_active(void) { return s_active; }

void poly_focus_cancel(void) {
    s_active = false;
    for (uint8_t i = 0; i < sizeof(s_marked); ++i) s_marked[i] = 0;
}

// Shared by both profiles. `already_ms` back-dates the start.
static void focus_start(uint8_t slot, bool sweep, uint32_t already_ms) {
    if (slot == TUT_SLOT_NONE) { poly_focus_cancel(); return; }
    const sa_geom_t g = startup_anim_key_geom(TUT_SLOT_RIGHT(slot), TUT_SLOT_IDX(slot));
    if (!g.valid) { poly_focus_cancel(); return; }
    s_cx         = g.cx;
    s_cy         = g.cy;
    s_start      = timer_read32() - already_ms;
    s_sweep      = sweep;
    s_scan       = 0;
    s_frame_busy = false;
    s_last_frame = timer_read32() - POLY_FOCUS_FRAME_MS;
    s_active     = true;
    // NOT clearing s_marked: a ripple restarted while another is fading still owes those
    // keys a restore, and the membership diff below is what pays it.
}

void poly_focus_start(uint8_t slot) { focus_start(slot, false, 0); }
void poly_focus_start_sweep(uint8_t slot, uint32_t already_ms) {
    focus_start(slot, true, already_ms);
}

// The reveal front: thicker than the letter ring so it reads as a wave rather than a
// line at board scale, solid for most of its run, and eroding over the last quarter so
// it dies at the board's far edge instead of vanishing in one frame.
// 20 read as too thin for the board-wide wave ("could be a bit wider", hardware); 44 is
// about 60 % of a keycap's width.
#define POLY_FOCUS_SWEEP_W 44u
// Behind the front: a trail of sparks, densest at the front and gone by TRAIL units
// back, re-rolled every SPARK_TICK_MS so they twinkle rather than sit still. Sparks are
// 2x2 px (a 1 px dot barely reads on a keycap) and at most SPARK_MAX/255 (~5 %) of those cells
// light, so they stay "here and there". ⚠️ Every key the trail covers is repainted each
// frame, so TRAIL is a cost dial as much as a look: ~3 key widths.
#define POLY_FOCUS_TRAIL         220u
#define POLY_FOCUS_SPARK_MAX      12u
#define POLY_FOCUS_SPARK_TICK_MS  90u
static uint8_t focus_sweep_density(uint8_t p) {
    if (p < 192u) return 255u;
    const uint16_t left = (uint16_t)(255u - p);          // 63 .. 0
    return (uint8_t)((left * left * 255u) / (63u * 63u));  // quadratic, like the ripple
}

// Latch the wavefront for this pass. Returns false once the ripple is over.
static bool focus_latch(void) {
    const uint32_t el = timer_elapsed32(s_start);
    if (s_sweep) {
        if (el >= TUT_BOARD_REVEAL_MS) return false;
        const uint8_t  p = (uint8_t)((el * 255u) / TUT_BOARD_REVEAL_MS);
        const uint16_t r = tut_sweep_radius(p);
        s_dens = focus_sweep_density(p);
        s_band = tut_ring_bounds(r, POLY_FOCUS_SWEEP_W);
        const uint16_t in  = (r > POLY_FOCUS_SWEEP_W) ? (uint16_t)(r - POLY_FOCUS_SWEEP_W) : 0u;
        const uint16_t tin = (in > POLY_FOCUS_TRAIL) ? (uint16_t)(in - POLY_FOCUS_TRAIL) : 0u;
        s_trail      = true;
        s_trail_in2  = (uint32_t)tin * tin;
        s_spark_tick = (uint8_t)(el / POLY_FOCUS_SPARK_TICK_MS);
        // The cull covers the band AND the trail behind it, or trail keys are never
        // repainted and their sparks neither appear nor clear.
        s_cull = tut_ring_bounds((uint16_t)(r + POLY_FOCUS_KEY_REACH),
                                 (uint16_t)(POLY_FOCUS_SWEEP_W + POLY_FOCUS_TRAIL +
                                            2 * POLY_FOCUS_KEY_REACH));
        return true;
    }
    s_trail = false;
    if (el >= TUT_RIPPLE_MS) return false;
    const uint8_t  p = (uint8_t)((el * 255u) / TUT_RIPPLE_MS);
    const uint16_t r = tut_ripple_radius(p);
    const uint8_t  w = tut_ring_width(p);
    s_dens = tut_ripple_density(p);
    s_band = tut_ring_bounds(r, w);
    s_cull = tut_ring_bounds((uint16_t)(r + POLY_FOCUS_KEY_REACH),
                             (uint16_t)(w + 2 * POLY_FOCUS_KEY_REACH));
    return true;
}

// A spark in the trail behind the reveal front? Decided per 2x2 BOARD cell, so a spark
// straddling a keycap edge stays one spark, and re-rolled per time bucket by offsetting
// the hash, so the trail twinkles. Density falls linearly (in squared distance, close
// enough for a sprinkle) from SPARK_MAX at the front to 0 at the trail's end.
static bool focus_spark(int16_t gx, int16_t gy) {
    const int32_t  dx = gx - s_cx, dy = gy - s_cy;
    const uint32_t d2 = (uint32_t)(dx * dx) + (uint32_t)(dy * dy);
    if (d2 >= s_band.inner2 || d2 < s_trail_in2) return false;
    const uint32_t span = s_band.inner2 - s_trail_in2;
    if (span == 0) return false;
    const uint32_t dens = (POLY_FOCUS_SPARK_MAX * (d2 - s_trail_in2)) / span;
    const int16_t  hx   = (int16_t)((gx >> 1) + s_spark_tick * 37);
    const int16_t  hy   = (int16_t)((gy >> 1) + s_spark_tick * 91);
    return tut_dither(hx, hy) < dens;
}

void poly_focus_overlay(uint8_t disp_idx, const sa_geom_t *g) {
    if (!s_active || !g->valid) return;
    // ⚠️ MARK the key. This hook is also called from update_displays()' own pass, so a
    // full refresh mid-ripple paints arcs on keys the DRIVER is not tracking — and the
    // driver only restores keys it has marked, so those arcs were never cleaned up.
    // That is the "parts of the ring not being removed on the slave" report: the slave
    // takes more full refreshes (every layer/state sync), so it collected more of them.
    // Whoever draws the ink owns saying so.
    // ⚠️ …but mark it on the INK, not on the call. The band is culled by a keycap
    // half-diagonal, and update_displays() calls this for every key it repaints, so
    // most calls write no pixel at all — marking those owed each of them a restore
    // repaint the next frame, i.e. a whole-half repaint pass for nothing.
    bool inked = false;
    uint8_t *buf = get_scratch_buffer();
    for (int16_t ly = 0; ly < SCREEN_HEIGHT; ++ly) {
        const int16_t dy = (int16_t)(ly - 20);
        for (int16_t lx = 0; lx < SCREEN_WIDTH; ++lx) {
            const int16_t dx = (int16_t)(lx - 36);
            int16_t gx, gy;
            if (g->rot) {
                gx = (int16_t)(g->cx + ((dx * g->cosv - dy * g->sinv) >> 7));
                gy = (int16_t)(g->cy + ((dx * g->sinv + dy * g->cosv) >> 7));
            } else {
                gx = (int16_t)(g->cx + dx);
                gy = (int16_t)(g->cy + dy);
            }
            if (!tut_ring_hit(&s_band, gx - s_cx, gy - s_cy)) {
                if (!s_trail || !focus_spark(gx, gy)) continue;
                buf[(size_t)(ly >> 3) * POLY_FOCUS_STRIDE + (BUFFER_X + lx)] |=
                    (uint8_t)(1u << (ly & 7));
                inked = true;
                continue;
            }
            // ⚠️ Dithered on the BOARD position, not the local pixel: on the local pixel
            // the identical pattern repeats on every keycap and the fade reads as a
            // screen door closing rather than ink eroding.
            if (s_dens <= tut_dither(gx, gy)) continue;
            buf[(size_t)(ly >> 3) * POLY_FOCUS_STRIDE + (BUFFER_X + lx)] |=
                (uint8_t)(1u << (ly & 7));
            inked = true;
        }
    }
    if (disp_idx < POLY_FOCUS_KEYS) bit_set(s_marked, disp_idx, inked);
}

// Repaint one key: its ordinary legend, plus the arc when the band is over it.
static void focus_repaint(uint8_t idx, bool with_arc, const sa_geom_t *g) {
    const uint8_t slot = TUT_SLOT(is_left_side() ? 0 : 1, idx);
    sr_shift_out_buffer_latch(get_key_disp_bitmask(idx), get_disp_bitmask_size());
    // ⚠️ TRACK the panel. These writes happen outside update_displays()' own pass, and
    // an untracked write leaves that panel's dirty-window box describing whatever was
    // there before — which the first repaint afterwards then pushes as a delta, giving
    // black and half-erased keycaps. The tutorial's own renderer was caught by exactly
    // this and has to invalidate everything on teardown; tracking avoids the need.
    kdisp_track_panel(idx);
    kdisp_set_buffer(0x00);
    // ⚠️ The LEGEND is what a hidden key must not show. The ARC still must — it is the
    // whole point of the ring that it crosses the board, and chapter 1's lit set is
    // exactly ONE key, so gating the arc on visibility too left "only a few ring
    // artifacts on the actual key" and no expanding ring at all (hardware).
    // poly_focus_draw_legend() draws nothing for a hidden key and says so; the arc is
    // drawn regardless.
    (void)poly_focus_draw_legend(slot);
    if (with_arc) poly_focus_overlay(idx, g);
    // ⚠️ The gfx plotter flags are STATIC. A legend that set erase (an inverted keycap)
    // would otherwise leave it set and blank every keycap drawn after this one, here and
    // in the next update_displays() pass. Clearing it is the caller's job at every draw
    // site, and this is one.
    kdisp_set_gfx_erase(false);
    kdisp_send_window();
}

void poly_focus_tick(void) {
    if (!s_active) return;

    // One LATCH per frame, then slice the key walk across as many passes as it takes.
    if (!s_frame_busy) {
        if (timer_elapsed32(s_last_frame) < POLY_FOCUS_FRAME_MS) return;
        s_live       = focus_latch();
        s_scan       = 0;
        s_frame_busy = true;
    }

    const uint32_t slice = timer_read32();
    while (s_scan < POLY_FOCUS_KEYS) {
        const uint8_t idx = s_scan++;
        const sa_geom_t g = startup_anim_key_geom(!is_left_side(), idx);
        if (!g.valid) continue;

        const bool want = s_live && tut_ring_hit(&s_cull, g.cx - s_cx, g.cy - s_cy);
        const bool have = bit_get(s_marked, idx);
        // ⚠️ REPAINT EVERY FRAME THE KEY IS IN THE BAND — not only when its membership
        // changes. The arc MOVES THROUGH the key: painting it once on entry freezes it
        // at the radius it had then, so each key lights up and sits there and the whole
        // thing reads as "a scatter light-up of surrounding keys" rather than an
        // expanding ring (hardware). `have && !want` is the one-off restore as the band
        // leaves. This is also what the design note's cost measured — 6 keys per FRAME,
        // not 6 keys per ripple.
        if (!want && !have) continue;
        focus_repaint(idx, want, &g);
        bit_set(s_marked, idx, want);

        if (timer_elapsed32(slice) >= POLY_FOCUS_SLICE_MS) return;   // resume next pass
    }

    // Frame complete. Timed from the END of the frame, like the animation loop, so a
    // slow frame does not immediately owe another one.
    s_frame_busy = false;
    s_last_frame = timer_read32();
    if (!s_live) {
        bool any = false;
        for (uint8_t i = 0; i < sizeof(s_marked); ++i) any |= (s_marked[i] != 0);
        if (!any) s_active = false;
    }
}

#else   // split42: no per-keycap ripple
void poly_focus_start(uint8_t slot) { (void)slot; }
void poly_focus_start_sweep(uint8_t slot, uint32_t already_ms) { (void)slot; (void)already_ms; }
void poly_focus_cancel(void) {}
bool poly_focus_active(void) { return false; }
void poly_focus_tick(void) {}
void poly_focus_overlay(uint8_t disp_idx, const sa_geom_t *g) { (void)disp_idx; (void)g; }
#endif
