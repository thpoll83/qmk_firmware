// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
// Procedural boot animation — see startup_anim.h. split72 only; stubs elsewhere.
#include "startup_anim.h"

#if defined(KEYBOARD_polykybd_split72)

#include <stdint.h>
#include <stdlib.h>
#include "quantum.h"                       // timer_read32/elapsed32, keymaps
#include "base/disp_array.h"               // scratch buffer, BUFFER_X, kdisp_*
#include "base/shift_reg.h"                // sr_shift_out_buffer_latch
#include "side.h"                           // is_left_side()
#include "bridge_helper.h"                  // is_usb_host_side() — for the startup trace
#include QMK_KEYBOARD_H                     // get_key_disp_bitmask, NUM_SHIFT_REGISTERS
#include "base/fonts/FreeSansBold24pt7b.h"  // splash glyph font
#include "startup_anim_geom.h"             // SA_GEOM_*, SA_LETTER_*, SA_TARGETS, SA_BOARD_*

// Cut a key's resting legend out of the idle comet field (dark silhouette). Defined
// in poly_keymap.c (it needs the keycode/legend tables); no-op for image legends.
extern bool eden_idle_erase_legend(uint8_t disp_idx);

// ---- timeline (ms) ----
#define SA_INTRO_MS 5000    // sparks stream + converge, letters form, sparks wink out
#define SA_HOLD_MS  5000    // hold the PolyKybd logo (letters up)
#define SA_FADE_MS  3200    // final fade: the letters dissolve to black (slow, gradual)
#define SA_BLACK_MS 1000    // hold on black at the end before the normal display returns
#define SA_TOTAL_MS (SA_INTRO_MS + SA_HOLD_MS + SA_FADE_MS + SA_BLACK_MS)
// The background sparkle haze dissolves EARLY and SLOWLY: it begins the moment the hold
// starts (letters just formed) and clears over SA_BG_FADE_MS, so the dots fade away while
// the clean letters stay up — rather than lingering behind them until the final fade.
#define SA_BG_FADE_START_MS SA_INTRO_MS   // start of the hold (letters are formed)
#define SA_BG_FADE_MS       1800          // quick background dissolve
// Scanline glitch: once the background has FULLY faded (SA_BG_FADE_START_MS + SA_BG_FADE_MS)
// plus a 1 s beat of clean letters, wipe out every second horizontal line of the text ALL
// AT ONCE (a one-frame glitch), then keep that look for the rest of the show. Waiting for a
// clean, noise-free frame first makes the scanline effect read clearly.
#define SA_LINE_CLEAR_DELAY_MS 1000
#define SA_LINE_CLEAR_AT_MS    (SA_BG_FADE_START_MS + SA_BG_FADE_MS + SA_LINE_CLEAR_DELAY_MS)

// ---- effect tuning ----
#define SA_NSPARK      340      // one L→R comet per spark; more of them → denser streaks
#define SA_TRAIL_MAX    24      // longest comet trail (px); each spark rolls its own length
#define SA_PGAIN         6      // background plasma density (out of 255) — a very faint haze
#define SA_STRIDE      128      // scratch bytes per page
// ---- ring (expanding ripple) tuning — parsed by the host firmware-port sim ----
// Circular ripple: the sparkle DENSITY peaks at each ring crest and falls off between
// rings, and the whole field dissolves as `ring` fades — so it reads as expanding rings
// with a fading sparkle halo (the "circular effect"), not a thin bare line nor a flat
// 50%-dither cloud. Density = crest(rv) * ring, dithered against the noise tile.
#define SA_RING_FREQ   300      // spatial frequency: higher = MORE concentric rings on the board
#define SA_RING_ASPECT 230      // oval aspect: ax = (gx-cxr)*SA_RING_ASPECT>>8 (230/256≈0.9, near
                                // round). A shift, NOT a divide — no per-pixel software divide.

// Number of keycap slots per half in the generated geometry tables.
#define SA_NUM_KEYS 40
_Static_assert(sizeof(SA_GEOM_LEFT) / sizeof(SA_GEOM_LEFT[0]) == SA_NUM_KEYS,
               "SA_NUM_KEYS must match the generated SA_GEOM_* table length");

static bool     s_active;
static bool     s_loop;       // true: idle screensaver — restart at the end instead of ending
static uint32_t s_start;
static uint32_t s_next_log;   // next elapsed-ms threshold at which to emit a progress log
static uint32_t s_last_frame; // last idle-loop frame time (frame-rate throttle, loop only)

// ---- idle-loop frame slicing (the responsiveness lever) ----
// A full idle frame renders ~36 keycaps, each a procedural 72x40 background + comet
// trails + the drifting legend + a 360 B SPI push — tens of ms in total. Rendered as
// ONE blocking unit that is a window in which the main loop cannot scan the matrix,
// so a short tap that starts AND ends inside a frame is simply never seen ("the
// keyboard doesn't wake on the first keypress"), and everything else (USB, the split
// transport, and on the slave half its own matrix scan) stalls with it.
//
// So a frame is rendered in TIME-SLICED chunks instead: each tick renders keycaps
// until EDEN_IDLE_SLICE_MS is spent, then returns so QMK gets the loop back, and the
// next tick RESUMES at the same keycap. The whole frame still uses one latched `el`
// and one spark build, so the slices compose into a single coherent frame. The
// worst-case main-loop stall drops from a whole frame to one slice + one keycap.
// The budget is checked AFTER each keycap, so at least one always makes progress.
#define EDEN_IDLE_SLICE_MS 3

static uint32_t s_frame_el;    // `el` latched for the frame currently being sliced
static uint8_t  s_frame_idx;   // next keycap index to render in that frame
static bool     s_frame_busy;  // a frame is partially rendered (slices pending)
// Telemetry for the ~5 s idle log: total ms of render per frame, and the WORST single
// slice — i.e. the longest the main loop was held off. The worst slice is the number
// that matters for key responsiveness (a missed tap needs a stall longer than the tap),
// so it is the one to watch when tuning EDEN_IDLE_SLICE_MS on real hardware.
static uint16_t s_frame_ms;
static uint16_t s_slice_worst_ms;
static bool     s_logged_frame;   // a completed frame has been reported this session

// Minimum GAP (ms) between idle-loop frames, measured from the END of the previous
// frame — NOT a frame period, so the throttle can never collapse to "render every
// pass" when a frame costs more than the period. Together with the slicing above it
// caps the animation's duty cycle (SPI + CPU) while the slicing caps its latency.
// Boot intro is unthrottled and unsliced (brief, swallows keys, owns the CPU).
#define EDEN_IDLE_FRAME_MS 55

// --- small integer helpers -------------------------------------------------
static inline uint8_t sa_hash8(uint32_t v) {
    v ^= v >> 15; v *= 0x2c1b3c6dU;
    v ^= v >> 12; v *= 0x297a2d39U;
    v ^= v >> 15; return (uint8_t)v;
}
// White-noise dither threshold via a 64x64 table lookup (a per-pixel hash in the hot
// loop would be slower). The table is a const in FLASH, not RAM: flash is abundant here
// (~19% of the 2 MB partition used) while SRAM is nearly full (~1.7 KB free), so the 4 KB
// tile belongs in flash. The 64 px tile keeps the dither repetition invisible.
static inline uint8_t sa_noise(int16_t x, int16_t y) {
    return SA_NOISE[(((uint8_t)y & SA_NOISE_MASK) << 6) | ((uint8_t)x & SA_NOISE_MASK)];
}
// Cheap octagonal distance (minimax α·max + β·min) — no FPU/divide/sqrt. This runs for
// ~every background pixel during the ring phase, so keeping it off the sqrt path is the
// main framerate lever. The old "< >" chevron from its 45° corner is a non-issue now the
// rings are faint/diffuse + radius-wobbled (see the ring branch), where a few px of
// irregularity is desirable.
static inline uint16_t sa_dist(int16_t a, int16_t b) {
    a = (int16_t)abs(a); b = (int16_t)abs(b);
    uint16_t mx = a > b ? a : b, mn = a > b ? b : a;
    return (uint16_t)(((uint32_t)mx * 123 + (uint32_t)mn * 51) >> 7);
}
static inline uint8_t sa_sin(uint8_t t) { return SA_SIN[t]; }   // local table, no lib8tion dep
static inline uint8_t sa_plasma(int16_t gx, int16_t gy, uint8_t tp) {
    uint8_t a = sa_sin((uint8_t)(((int32_t)gx * 3) >> 2) + tp);   // gx * 0.75
    uint8_t b = sa_sin((uint8_t)gy - tp);                         // gy * 1.0
    uint8_t c = sa_sin((uint8_t)((gx + gy) >> 1) + tp);           // (gx+gy) * 0.5
    return (uint8_t)((((uint16_t)a + b + c) * 85u) >> 8);         // /3 as *85>>8 (no divide)
}

static inline void sa_set(uint8_t *buf, int16_t lx, int16_t ly) {
    if (lx >= 0 && lx < SCREEN_WIDTH && ly >= 0 && ly < SCREEN_HEIGHT)
        buf[(size_t)(ly >> 3) * SA_STRIDE + (BUFFER_X + lx)] |= (uint8_t)(1u << (ly & 7));
}

// Combined background DENSITY (0..255) at a board point: the faint plasma haze OR'd
// (max) with the dissolved ring ripple. The per-pixel noise compare happens in the
// caller, so this smooth value is what gets 2x2-coarsened for speed (sa_render_frame).
static inline uint8_t sa_bg(int16_t gx, int16_t gy, uint8_t tp, uint8_t tprg,
                            uint8_t ring, uint16_t pgain, int16_t cxr, int16_t cyr) {
    uint8_t pv   = sa_plasma(gx, gy, tp);
    uint8_t dens = (uint8_t)(((uint16_t)pv * pgain) >> 8);      // faint plasma haze
    if (ring) {                                                 // + dissolved ripple
        int16_t  ax = (int16_t)(((int32_t)(gx - cxr) * SA_RING_ASPECT) >> 8);
        int16_t  ay = (int16_t)(gy - cyr);
        uint16_t rr = sa_dist(ax, ay);
        rr = (uint16_t)((int16_t)rr + ((sa_sin((uint8_t)(gx + 2 * gy)) - 128) >> 3));  // wobble
        uint8_t  rv    = sa_sin((uint8_t)(((uint32_t)rr * SA_RING_FREQ >> 8) - tprg));
        // Idle screensaver draws the rings a bit THICKER + DENSER: a lower crest
        // threshold widens the lit band of each ring and raises its peak. Boot intro
        // keeps the fainter 215.
        uint8_t  thr   = s_loop ? 200 : 215;
        uint8_t  crest = rv > thr ? (uint8_t)(rv - thr) : 0;
        uint8_t  rdens = (uint8_t)(((uint16_t)crest * ring) >> 9);
        if (rdens > dens) dens = rdens;
    }
    return dens;
}

// Each spark is ONE L→R comet: a bright head + a continuous horizontal trail drawn behind
// it. (The old discrete phase-offset "trail" spaced its dots ~49 board-px apart, so it read
// as scattered dots, not a streak.) The heads are the same for every keycap (board-space),
// so build them ONCE per frame here, then each key just filters+rotates+draws its comet.
// `thick` = trail is 2 px tall (brighter/bolder) vs 1 px; `tlen` = this comet's trail length.
typedef struct { int16_t sx, sy; uint8_t thick, tlen; } sa_spark_pt_t;
static sa_spark_pt_t s_spark_pts[SA_NSPARK];
static uint16_t      s_spark_n;
// The spark loop counter and s_spark_n are uint16_t. A uint8_t counter silently
// wraps 255->0 when SA_NSPARK > 255, so `s < SA_NSPARK` never ends -> infinite loop
// (QMK builds don't enable -Wtype-limits, so the compiler won't warn). This makes
// the build FAIL instead if SA_NSPARK is ever raised past what the counter holds.
_Static_assert(SA_NSPARK <= UINT16_MAX, "SA_NSPARK exceeds the uint16_t spark loop counter range");
static uint8_t       s_brow[SCREEN_WIDTH];   // one 2x2-block row of background density (sa_bg)

static void sa_build_sparks(uint32_t el, uint8_t cv, uint8_t spark_fade) {
    const int16_t margin = SA_BOARD_W / 8;
    s_spark_n = 0;
    for (uint16_t s = 0; s < SA_NSPARK; ++s) {   // uint16_t: SA_NSPARK may exceed 255 (see _Static_assert)
        // Staggered death: each spark winks out once the rising `spark_fade` passes its
        // own hash threshold — so the sparks disappear a few at a time, not all at once.
        if (sa_hash8(s * 3u + 7u) < spark_fade) continue;
        // Idle screensaver thins the field out for a calmer look + lighter render
        // (fewer comet trails to plot → snappier). ~160/256 skipped ≈ 37% kept.
        if (s_loop && sa_hash8(s * 19u + 11u) < 190u) continue;
        uint8_t  p0   = sa_hash8(s * 2u + 1u);
        // Speed 1..8 in the boot intro; idle uses a WIDER 1..16 spread so the comets
        // clearly move at different speeds (some crawl, some drift), and the extra
        // el-shift below keeps even the fast ones slower than the boot streak.
        uint8_t  spd  = s_loop ? (1u + (sa_hash8(s * 7u + 3u) & 15u))
                               : (1u + (sa_hash8(s * 7u + 3u) & 7u));
        int16_t  lane = (int16_t)(((uint32_t)sa_hash8(s * 5u + 9u) * SA_BOARD_H) >> 8);
        uint8_t  bw   = 1u + (sa_hash8(s * 11u + 2u) & 3u);
        uint8_t  ph   = sa_hash8(s * 13u + 5u);
        int16_t  bob  = 6 + (int16_t)(sa_hash8(s * 17u) & 31u);
        uint8_t  hv   = sa_hash8(s * 23u + 4u);                   // per-spark look variety
        const sa_target_t *tgt = &SA_TARGETS[s % SA_NUM_TARGETS];
        // Idle screensaver drifts much slower than the boot intro: shift `el` two more
        // bits so the L→R comets and their vertical bob crawl (a calm sleeping-keyboard
        // drift). Boot intro keeps the faster streak.
        uint8_t tsh = s_loop ? 7 : 4;
        uint8_t xn = (uint8_t)(p0 + (uint8_t)((el >> tsh) * spd));  // head phase (streams L→R)
        int16_t sx = (int16_t)(-margin + (int16_t)(((uint32_t)xn * (SA_BOARD_W + 2 * margin)) >> 8));
        int16_t sy = (int16_t)(lane + (((int16_t)(sa_sin((uint8_t)((el >> (uint8_t)(tsh + 1)) * bw + ph)) - 128) * bob) >> 7));
        if (cv) {   // converge toward the letter target
            sx = (int16_t)(sx + (((int32_t)(tgt->cx - sx) * cv) >> 8));
            sy = (int16_t)(sy + (((int32_t)(tgt->cy - sy) * cv) >> 8));
        }
        s_spark_pts[s_spark_n].sx    = sx;
        s_spark_pts[s_spark_n].sy    = sy;
        s_spark_pts[s_spark_n].thick = (hv & 1u) ? 2u : 1u;       // ~half are 2 px (brighter)
        // Trail length: the boot intro uses short 8..23 px comets; the idle screensaver
        // (s_loop) uses MUCH longer 36..51 px trails so each comet drags a long, sparse,
        // dither-faded ghost tail across the keys — the "ghosting" persistence look (a
        // true keep-lit-pixels framebuffer won't fit in RAM). The fade formula below
        // (255 - k*230/tlen) stretches with tlen, so the longer tail fades gradually.
        uint8_t base_tlen = (uint8_t)(8u + (hv >> 4));
        s_spark_pts[s_spark_n].tlen  = s_loop ? (uint8_t)(base_tlen + 28u) : base_tlen;
        s_spark_n++;
    }
}

// Draw each comet that touches this keycap: a bright head + a horizontal trail extending
// LEFT (behind the L→R motion), fading toward the tail, with per-spark thickness/length.
// Drawn in local px (correct for the un-rotated keys; the 4 thumbs get a horizontal streak
// on their own panel, which still reads as a comet). sa_set clips, so an over-inclusive cull
// is fine.
static void sa_plot_sparks(uint8_t *buf, const sa_key_geom_t *g, bool rot, int16_t cosv, int16_t sinv) {
    // Idle screensaver uses long ghost trails (see sa_build_sparks); widen the cull
    // margin so a comet whose head has streamed off the right of this key still draws
    // its long tail here instead of being skipped.
    const int16_t cull = s_loop ? (int16_t)(40 + 56) : (int16_t)(40 + SA_TRAIL_MAX);
    for (uint16_t i = 0; i < s_spark_n; ++i) {
        int16_t ddx = (int16_t)(s_spark_pts[i].sx - g->cx);
        int16_t ddy = (int16_t)(s_spark_pts[i].sy - g->cy);
        if (ddx <= -cull || ddx >= cull || ddy <= -40 || ddy >= 40) continue;
        const bool    thick = (s_spark_pts[i].thick == 2u);
        const uint8_t tlen  = s_spark_pts[i].tlen;
        int16_t hx, hy;
        if (rot) {
            hx = (int16_t)(36 + ((ddx * cosv + ddy * sinv) >> 7));
            hy = (int16_t)(20 + ((-ddx * sinv + ddy * cosv) >> 7));
        } else {
            hx = (int16_t)(36 + ddx);
            hy = (int16_t)(20 + ddy);
        }
        sa_set(buf, hx, hy);         // bold 2×2 head
        sa_set(buf, hx + 1, hy);
        sa_set(buf, hx, hy + 1);
        sa_set(buf, hx + 1, hy + 1);
        if (thick) { sa_set(buf, hx, hy - 1); sa_set(buf, hx + 1, hy - 1); }   // taller, brighter head
        const uint16_t fade_step = 230u / tlen;   // invariant across the trail; hoist out of the loop
        for (uint8_t k = 1; k < tlen; ++k) {   // solid neck, then a fading tail
            if (k <= 5 || sa_noise((int16_t)(hx - k + 30), (int16_t)(hy + 12)) <
                          (uint8_t)(255u - (uint16_t)k * fade_step)) {
                sa_set(buf, (int16_t)(hx - k), hy);
                if (thick) sa_set(buf, (int16_t)(hx - k), hy + 1);   // 2 px tall trail
            }
        }
    }
}

static void sa_render_frame(uint32_t el) {
    const bool left = is_left_side();
    const sa_key_geom_t *T = left ? SA_GEOM_LEFT : SA_GEOM_RIGHT;
    const uint16_t      *L = left ? SA_LETTER_LEFT : SA_LETTER_RIGHT;

    // phases / envelopes
    uint8_t tt   = el < SA_INTRO_MS ? (uint8_t)(((uint32_t)el * 256) / SA_INTRO_MS) : 255;
    uint8_t tp   = (uint8_t)(el >> 4);
    uint8_t tprg = (uint8_t)(el >> 5);
    int16_t cvi  = (int16_t)(((int32_t)tt - 55) * 255 / 75);        // converge over tt 55..130 (gather into the letter zones before they appear)
    uint8_t cv   = (uint8_t)(cvi < 0 ? 0 : (cvi > 255 ? 255 : cvi));
    uint8_t ring = (uint8_t)(255 - cv);                            // ripples fade as things converge
    bool letters = tt >= 130;                                     // letters form a bit earlier
    bool sparks  = (el < SA_INTRO_MS);
    // Letters dither IN quickly right as they appear (the reverse of the end dissolve) so
    // they materialise out of the converging sparks instead of popping into existence.
    // Ramps over tt 130..165 (~0.7 s); 255 = fully formed (so it's a no-op during hold/fade).
    int16_t lii = (int16_t)(((int32_t)tt - 130) * 255 / 35);
    uint8_t letter_in = (uint8_t)(lii < 0 ? 0 : (lii > 255 ? 255 : lii));
    // Sparks wink out one by one over tt 130..255 (staggered per-spark in sa_build_sparks).
    int16_t sfi  = (int16_t)(((int32_t)tt - 130) * 255 / 125);
    uint8_t spark_fade = (uint8_t)(sfi < 0 ? 0 : (sfi > 255 ? 255 : sfi));
    // Background dots dissolve EARLY + SLOWLY (over SA_BG_FADE_MS from the top of the
    // hold) so the sparkle haze clears while the letters stay up. The letters only
    // dissolve later, in the final fade phase, over the full (slow) SA_FADE_MS. By then
    // the background is long gone, so the letter dither only hits the letters.
    uint8_t bg_fade = 0, letter_fade = 0;
    if (el >= SA_BG_FADE_START_MS) {
        uint32_t b = ((el - SA_BG_FADE_START_MS) * 256) / SA_BG_FADE_MS;
        bg_fade = (uint8_t)(b > 255 ? 255 : b);
    }
    if (el >= SA_INTRO_MS + SA_HOLD_MS) {
        uint32_t lf = ((el - SA_INTRO_MS - SA_HOLD_MS) * 256) / SA_FADE_MS;
        letter_fade = (uint8_t)(lf > 255 ? 255 : lf);
    }
    // background plasma density scales down as bg_fade rises → the dots dissolve first
    uint16_t pgain = (uint16_t)((uint16_t)SA_PGAIN * (255 - bg_fade)) / 255;
    const int16_t cxr = SA_BOARD_W / 2;
    const int16_t cyr = (int16_t)((int32_t)SA_BOARD_H * 42 / 100);
    // Black tail: the fade is done; hold every keycap on black for SA_BLACK_MS.
    const bool black = el >= SA_INTRO_MS + SA_HOLD_MS + SA_FADE_MS;

    if (sparks) sa_build_sparks(el, cv, spark_fade);   // once per frame (key-independent)

    for (uint8_t idx = 0; idx < SA_NUM_KEYS; ++idx) {
        const sa_key_geom_t *g = &T[idx];
        if (!g->valid) continue;
        const bool rot = (g->ang != 0);                                // only the 4 thumbs
        int16_t cosv = (int16_t)sa_sin((uint8_t)(g->ang + 64)) - 128;   // cos * ~127
        int16_t sinv = (int16_t)sa_sin(g->ang) - 128;                   // sin * ~127

        sr_shift_out_buffer_latch(get_key_disp_bitmask(idx), get_disp_bitmask_size());
        kdisp_set_buffer(0x00);
        uint8_t *buf = get_scratch_buffer();

        if (black) { kdisp_send_window(); continue; }   // just push the cleared (black) buffer

        // Background = plasma haze + dissolved ring, computed on a 2x2 grid (sa_bg is
        // the expensive part: 3 sines + the ring); the per-pixel noise compare keeps the
        // fine dither. s_brow caches one 2x2-block row so odd cols/rows reuse it.
        for (int16_t ly = 0; ly < SCREEN_HEIGHT; ++ly) {
            int16_t dy = (int16_t)(ly - 20);
            int16_t gy_flat = (int16_t)(g->cy + dy);   // fast path: no rotation
            const bool erow = !rot && ((ly & 1) == 0);  // even row of a 2x2 block → recompute
            for (int16_t lx = 0; lx < SCREEN_WIDTH; ++lx) {
                int16_t dx = (int16_t)(lx - 36);
                int16_t gx, gy;
                if (rot) {
                    gx = (int16_t)(g->cx + ((dx * cosv - dy * sinv) >> 7));
                    gy = (int16_t)(g->cy + ((dx * sinv + dy * cosv) >> 7));
                } else {
                    gx = (int16_t)(g->cx + dx);
                    gy = gy_flat;
                }
                uint8_t bgv;
                if (rot) {
                    bgv = sa_bg(gx, gy, tp, tprg, ring, pgain, cxr, cyr);   // full-res thumbs
                } else if (erow) {
                    bgv = (lx & 1) ? s_brow[lx - 1] : sa_bg(gx, gy, tp, tprg, ring, pgain, cxr, cyr);
                    s_brow[lx] = bgv;
                } else {
                    bgv = s_brow[lx];                                       // reuse the even row
                }
                // bgv == 0 can never beat the (unsigned) noise threshold, and at this
                // faint density most pixels are 0 — skip the table lookup for them.
                if (bgv && bgv > sa_noise(gx, gy))
                    buf[(size_t)(ly >> 3) * SA_STRIDE + (BUFFER_X + lx)] |= (uint8_t)(1u << (ly & 7));
            }
        }

        if (sparks) sa_plot_sparks(buf, g, rot, cosv, sinv);

        if (letters && L[idx]) {
            const GFXfont *const lf[1] = { &FreeSansBold24pt7b };
            uint32_t txt[2] = { L[idx], 0 };
            kdisp_write_gfx_text(lf, 1, 49, 38, txt);
        }

        if (letters && L[idx] && letter_in < 255) {   // dither the letters IN (reverse dissolve)
            for (int16_t ly = 0; ly < SCREEN_HEIGHT; ++ly)
                for (int16_t lx = 0; lx < SCREEN_WIDTH; ++lx)
                    if (sa_noise((int16_t)(lx + idx * 13), (int16_t)(ly + idx * 7)) >= letter_in)
                        buf[(size_t)(ly >> 3) * SA_STRIDE + (BUFFER_X + lx)] &= (uint8_t)~(1u << (ly & 7));
        }

        // Scanline glitch: after the background has fully faded + a 1 s beat, wipe out every
        // SECOND horizontal line of the buffer. Re-applied each frame so the look persists
        // (the letters are redrawn every frame); the transition is instant (one frame).
        if (el >= SA_LINE_CLEAR_AT_MS) {
            for (int16_t ly = 1; ly < SCREEN_HEIGHT; ly += 2)
                for (int16_t lx = 0; lx < SCREEN_WIDTH; ++lx)
                    buf[(size_t)(ly >> 3) * SA_STRIDE + (BUFFER_X + lx)] &= (uint8_t)~(1u << (ly & 7));
        }

        if (letter_fade) {   // dither-dissolve the letters (the only lit pixels left by now)
            for (int16_t ly = 0; ly < SCREEN_HEIGHT; ++ly)
                for (int16_t lx = 0; lx < SCREEN_WIDTH; ++lx)
                    if (sa_noise((int16_t)(lx + idx * 13), (int16_t)(ly + idx * 7)) < letter_fade)
                        buf[(size_t)(ly >> 3) * SA_STRIDE + (BUFFER_X + lx)] &= (uint8_t)~(1u << (ly & 7));
        }

        kdisp_send_window();   // 360 B (visible cols/pages) not the full 1024 B — faster SPI
    }
}

// ONE keycap of an idle screensaver frame: the intro's OPENING look — streaming
// comets over the plasma+ripple haze — held open forever. It is the boot animation
// with the letter/converge/fade machinery removed: cv is forced 0 (comets stream
// straight across instead of gathering into the letter zones), letters are never
// drawn, and there is no bg fade / letter fade / scanline / black tail. Time (`el`)
// still advances, so the comets keep coming and going and the ripples keep expanding
// — the lit pixels are always moving, which is the whole point (anti-burn-in).
//
// One keycap at a time (rather than a whole frame) is what lets the tick below hand
// the main loop back mid-frame; every key of a frame reads the same latched
// `s_frame_el` + the spark set built once at frame start, so the slices still
// compose into one coherent frame.
static void sa_render_idle_key(uint8_t idx) {
    const sa_key_geom_t *T = is_left_side() ? SA_GEOM_LEFT : SA_GEOM_RIGHT;
    const sa_key_geom_t *g = &T[idx];
    if (!g->valid) return;

    const uint8_t  tp    = (uint8_t)(s_frame_el >> 4);
    const uint8_t  tprg  = (uint8_t)(s_frame_el >> 5);
    const uint8_t  ring  = 255;          // ripples always present (they expand via tprg)
    const uint16_t pgain = SA_PGAIN;     // constant faint haze — no background fade
    const int16_t  cxr   = SA_BOARD_W / 2;
    const int16_t  cyr   = (int16_t)((int32_t)SA_BOARD_H * 42 / 100);

    const bool rot = (g->ang != 0);
    int16_t cosv = (int16_t)sa_sin((uint8_t)(g->ang + 64)) - 128;
    int16_t sinv = (int16_t)sa_sin(g->ang) - 128;

    sr_shift_out_buffer_latch(get_key_disp_bitmask(idx), get_disp_bitmask_size());
    kdisp_set_buffer(0x00);
    uint8_t *buf = get_scratch_buffer();

    for (int16_t ly = 0; ly < SCREEN_HEIGHT; ++ly) {
        int16_t dy = (int16_t)(ly - 20);
        int16_t gy_flat = (int16_t)(g->cy + dy);
        // 2x2-coarsen the background (sa_bg is the expensive part: 3 sines + the
        // ring) for the ROTATED thumbs too, not just the flat keys — in local space
        // it is the same block approximation, and it cuts a thumb's sa_bg calls 4x.
        // The boot intro deliberately keeps its thumbs full-res (it is unsliced and
        // owns the CPU, so there is nothing to buy there and its look is unchanged).
        const bool erow = ((ly & 1) == 0);
        for (int16_t lx = 0; lx < SCREEN_WIDTH; ++lx) {
            int16_t dx = (int16_t)(lx - 36);
            int16_t gx, gy;
            if (rot) {
                gx = (int16_t)(g->cx + ((dx * cosv - dy * sinv) >> 7));
                gy = (int16_t)(g->cy + ((dx * sinv + dy * cosv) >> 7));
            } else {
                gx = (int16_t)(g->cx + dx);
                gy = gy_flat;
            }
            uint8_t bgv;
            if (erow) {
                bgv = (lx & 1) ? s_brow[lx - 1] : sa_bg(gx, gy, tp, tprg, ring, pgain, cxr, cyr);
                s_brow[lx] = bgv;
            } else {
                bgv = s_brow[lx];
            }
            // bgv == 0 can never beat the (unsigned) noise threshold, and at this
            // faint density most pixels are 0 — skip the table lookup for them.
            if (bgv && bgv > sa_noise(gx, gy))
                buf[(size_t)(ly >> 3) * SA_STRIDE + (BUFFER_X + lx)] |= (uint8_t)(1u << (ly & 7));
        }
    }

    sa_plot_sparks(buf, g, rot, cosv, sinv);
    // Cut this key's resting legend out of the comet field (dark silhouette the
    // comets ghost around). Implemented in poly_keymap.c where the keycode/legend
    // live; idx here is the display index it maps from. No-op for image legends.
    eden_idle_erase_legend(idx);
    kdisp_send_window();
}

// Shared start path for the one-shot (boot/KC_EDEN) and the looping idle screensaver.
// `contrast` is the OLED contrast register value (not local_state->contrast); the
// one-shot runs at full brightness, the loop at the active idle brightness.
static void sa_begin(bool loop, uint8_t contrast) {
    s_start      = timer_read32();
    s_active     = true;
    s_loop       = loop;
    s_next_log   = 0;
    s_last_frame = s_start - EDEN_IDLE_FRAME_MS;   // render the first idle frame at once
    s_frame_busy = false;                          // no half-rendered frame carried over
    s_frame_idx  = 0;
    s_frame_ms       = 0;   // don't report the PREVIOUS idle session's timings in the
    s_slice_worst_ms = 0;   // first log line of this one
    s_logged_frame   = false;
    // Non-blocking progress trace (HID console; dropped when nothing is attached).
    // If a half wedges during the animation, the last line printed shows how far it
    // got. Only the USB (master) half's console is readable — to diagnose the left
    // half, plug USB into it so it is master, then press KC_EDEN and watch the log.
    uprintf("Eden %s (left=%d, master=%d)\n", loop ? "loop start" : "start",
            (int)is_left_side(), (int)is_usb_host_side());
    // This only touches the OLED contrast register (not local_state->contrast), so
    // the one-shot's full brightness is restored to the user value after the fade
    // (see the housekeeping finish edge in poly_keymap.c); the loop is stopped on
    // wake and the normal brightness re-applied there.
    sr_shift_out_0_latch(NUM_SHIFT_REGISTERS);   // select all panels on this half
    kdisp_enable(true);
    kdisp_set_contrast(contrast);
}

void startup_anim_start(void) { sa_begin(false, 255); }

void startup_anim_start_loop(uint8_t contrast) {
    if (s_active && s_loop) return;              // already looping — don't restart mid-cycle
    sa_begin(true, contrast ? contrast : 1);
}

void startup_anim_stop(void) {
    s_active = false;
    s_loop   = false;
    // Drop any partially-rendered frame — the keycaps are handed straight back to
    // update_displays(), so resuming its remaining slices later would paint comets
    // over freshly-woken legends.
    s_frame_busy = false;
    s_frame_idx  = 0;
}

bool startup_anim_is_loop(void) { return s_active && s_loop; }

bool startup_anim_active(void) { return s_active; }

void startup_anim_tick(void) {
    if (!s_active) return;
    uint32_t el = timer_elapsed32(s_start);
    if (s_loop) {
        // Idle screensaver: the perpetual comet field (no letters/converge/fade),
        // rendered a slice of keycaps at a time so the main loop keeps scanning the
        // matrix mid-frame (see EDEN_IDLE_SLICE_MS). Between frames every pass
        // returns immediately until EDEN_IDLE_FRAME_MS has elapsed SINCE THE LAST
        // FRAME ENDED — s_last_frame is stamped when the final slice completes.
        if (!s_frame_busy) {
            if (timer_elapsed32(s_last_frame) < EDEN_IDLE_FRAME_MS) return;
            // Latch the frame's time + build its comet set ONCE, so every slice of
            // this frame draws the same instant (no shear across the keycaps).
            s_frame_el  = el;
            sa_build_sparks(el, 0, 0);   // cv=0: no converge; spark_fade=0: none wink out
            s_frame_idx = 0;
            s_frame_ms  = 0;
            s_frame_busy = true;
        }
        // Render keycaps until the slice budget is spent. Checked AFTER each key, so
        // a slice always makes progress; skipped (invalid) slots cost nothing.
        const uint32_t slice_start = timer_read32();
        do {
            sa_render_idle_key(s_frame_idx++);
        } while (s_frame_idx < SA_NUM_KEYS && timer_elapsed32(slice_start) < EDEN_IDLE_SLICE_MS);
        const uint16_t slice_ms = (uint16_t)timer_elapsed32(slice_start);
        s_frame_ms += slice_ms;
        if (slice_ms > s_slice_worst_ms) s_slice_worst_ms = slice_ms;
        if (s_frame_idx >= SA_NUM_KEYS) {
            s_frame_busy = false;
            s_last_frame = timer_read32();   // gap timed from the END of the frame
            // Report at frame END (so the numbers describe the frame that just
            // finished) and report the FIRST completed frame immediately, then on a
            // quiet ~5 s cadence. A 5 s-only cadence yields NOTHING from a short idle
            // session — a 4.4 s glance at the screensaver printed no timing at all,
            // which makes the instrument useless exactly when you want a quick look.
            if (!s_logged_frame || el >= s_next_log) {
                uprintf("Eden idle %lums (frame %ums, worst slice %ums)\n",
                        (unsigned long)el, s_frame_ms, s_slice_worst_ms);
                s_next_log       = el + 5000;
                s_slice_worst_ms = 0;   // worst-since-the-last-report, not worst-ever
                s_logged_frame   = true;
            }
        }
        return;
    }
    if (el >= SA_TOTAL_MS) {
        s_active = false;
        uprintf("Eden done (%lums)\n", (unsigned long)el);
        return;
    }
    // Emit a progress line ~once/second BEFORE rendering the frame, so if the render
    // hangs the last line shows the elapsed ms it reached. Non-blocking (console).
    if (el >= s_next_log) {
        uprintf("Eden tick %lums\n", (unsigned long)el);
        s_next_log = el + 1000;
    }
    sa_render_frame(el);
}

#else  // ---- non-split72: no-op stubs ----
void startup_anim_start(void) {}
void startup_anim_start_loop(uint8_t contrast) { (void)contrast; }
void startup_anim_stop(void) {}
bool startup_anim_is_loop(void) { return false; }
void startup_anim_tick(void) {}
bool startup_anim_active(void) { return false; }
#endif
