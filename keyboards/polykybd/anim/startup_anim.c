// Procedural boot animation — see startup_anim.h. split72 only; stubs elsewhere.
#include "startup_anim.h"

#if defined(KEYBOARD_polykybd_split72)

#include <stdint.h>
#include <stdlib.h>
#include "quantum.h"                       // timer_read32/elapsed32, keymaps
#include "base/disp_array.h"               // scratch buffer, BUFFER_X, kdisp_*
#include "base/shift_reg.h"                // sr_shift_out_buffer_latch
#include "side.h"                           // is_left_side()
#include QMK_KEYBOARD_H                     // get_key_disp_bitmask, NUM_SHIFT_REGISTERS
#include "base/fonts/FreeSansBold24pt7b.h"  // splash glyph font
#include "startup_anim_geom.h"             // SA_GEOM_*, SA_LETTER_*, SA_TARGETS, SA_BOARD_*

// ---- timeline (ms) ----
#define SA_INTRO_MS 2400    // sparks stream + converge, letters form, sparks wink out
#define SA_HOLD_MS  3000    // hold the PolyKybd logo (long)
#define SA_FADE_MS  1300    // dither-dissolve everything to black
#define SA_TOTAL_MS (SA_INTRO_MS + SA_HOLD_MS + SA_FADE_MS)

// ---- effect tuning ----
#define SA_NSPARK       64
#define SA_TRAIL         6
#define SA_TRAILSTEP     6      // per-trail phase step (8-bit units)
#define SA_PGAIN         6      // background plasma density (out of 255) — a very faint haze
#define SA_STRIDE      128      // scratch bytes per page
// ---- ring (expanding ripple) tuning — parsed by the host firmware-port sim ----
// Circular ripple: the sparkle DENSITY peaks at each ring crest and falls off between
// rings, and the whole field dissolves as `ring` fades — so it reads as expanding rings
// with a fading sparkle halo (the "circular effect"), not a thin bare line nor a flat
// 50%-dither cloud. Density = crest(rv) * ring, dithered against the noise tile.
#define SA_RING_FREQ   300      // spatial frequency: higher = MORE concentric rings on the board
#define SA_RING_ANUM     9      // oval aspect numerator   (ax = (gx-cxr)*ANUM/ADEN)
#define SA_RING_ADEN    10      // oval aspect denominator (near-round: 9/10)

static bool     s_active;
static uint32_t s_start;

// --- small integer helpers -------------------------------------------------
static inline uint8_t sa_hash8(uint32_t v) {
    v ^= v >> 15; v *= 0x2c1b3c6dU;
    v ^= v >> 12; v *= 0x297a2d39U;
    v ^= v >> 15; return (uint8_t)v;
}
// White-noise dither threshold via a 32x32 table lookup (was a per-pixel hash —
// this is the hot-loop speedup). Tiling every 32 px is imperceptible for noise.
static inline uint8_t sa_noise(int16_t x, int16_t y) {
    return SA_NOISE[(((uint8_t)y & SA_NOISE_MASK) << 5) | ((uint8_t)x & SA_NOISE_MASK)];
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
    return (uint8_t)(((uint16_t)a + b + c) / 3);
}

static inline void sa_set(uint8_t *buf, int16_t lx, int16_t ly) {
    if (lx >= 0 && lx < SCREEN_WIDTH && ly >= 0 && ly < SCREEN_HEIGHT)
        buf[(size_t)(ly >> 3) * SA_STRIDE + (BUFFER_X + lx)] |= (uint8_t)(1u << (ly & 7));
}

// Spark/trail points are the SAME for every keycap (board-space), so build them ONCE per
// frame here, then each key just filters+rotates the list (sa_plot_sparks). This moves the
// hash/sin/converge work out of the 40× per-key loop — the main spark-phase framerate win.
typedef struct { int16_t sx, sy; uint8_t head; } sa_spark_pt_t;
static sa_spark_pt_t s_spark_pts[SA_NSPARK * SA_TRAIL];
static uint16_t      s_spark_n;

static void sa_build_sparks(uint32_t el, uint8_t cv, uint8_t spark_fade) {
    const int16_t margin = SA_BOARD_W / 8;
    s_spark_n = 0;
    for (uint8_t s = 0; s < SA_NSPARK; ++s) {
        // Staggered death: each spark winks out once the rising `spark_fade` passes its
        // own hash threshold — so the tiny sparks disappear a few at a time, not all at
        // once.
        if (sa_hash8(s * 3u + 7u) < spark_fade) continue;
        uint8_t  p0   = sa_hash8(s * 2u + 1u);
        uint8_t  spd  = 1u + (sa_hash8(s * 7u + 3u) & 3u);
        int16_t  lane = (int16_t)(((uint32_t)sa_hash8(s * 5u + 9u) * SA_BOARD_H) >> 8);
        uint8_t  bw   = 1u + (sa_hash8(s * 11u + 2u) & 3u);
        uint8_t  ph   = sa_hash8(s * 13u + 5u);
        int16_t  bob  = 6 + (int16_t)(sa_hash8(s * 17u) & 31u);
        const sa_target_t *tgt = &SA_TARGETS[s % SA_NUM_TARGETS];
        for (uint8_t j = 0; j < SA_TRAIL; ++j) {
            // stochastic, stable trail fade (head bright, tail thins out)
            if (sa_hash8(s * 31u + j) > (uint8_t)(255u - j * (200u / SA_TRAIL))) continue;
            uint8_t xn = (uint8_t)(p0 + (uint8_t)((el >> 4) * spd) - j * SA_TRAILSTEP);
            int16_t sx = (int16_t)(-margin + (int16_t)(((uint32_t)xn * (SA_BOARD_W + 2 * margin)) >> 8));
            int16_t sy = (int16_t)(lane + (((int16_t)(sa_sin((uint8_t)((el >> 5) * bw + ph)) - 128) * bob) >> 7));
            if (cv) {   // converge toward the letter target
                sx = (int16_t)(sx + (((int32_t)(tgt->cx - sx) * cv) >> 8));
                sy = (int16_t)(sy + (((int32_t)(tgt->cy - sy) * cv) >> 8));
            }
            s_spark_pts[s_spark_n].sx   = sx;
            s_spark_pts[s_spark_n].sy   = sy;
            s_spark_pts[s_spark_n].head = (j == 0);
            s_spark_n++;
        }
    }
}

// Plot the prebuilt spark points that land on this keycap (inverse-rotate into local px).
static void sa_plot_sparks(uint8_t *buf, const sa_key_geom_t *g, bool rot, int16_t cosv, int16_t sinv) {
    for (uint16_t i = 0; i < s_spark_n; ++i) {
        int16_t ddx = (int16_t)(s_spark_pts[i].sx - g->cx);
        int16_t ddy = (int16_t)(s_spark_pts[i].sy - g->cy);
        if (ddx <= -40 || ddx >= 40 || ddy <= -40 || ddy >= 40) continue;
        int16_t lx, ly;
        if (rot) {
            lx = (int16_t)(36 + ((ddx * cosv + ddy * sinv) >> 7));
            ly = (int16_t)(20 + ((-ddx * sinv + ddy * cosv) >> 7));
        } else {
            lx = (int16_t)(36 + ddx);
            ly = (int16_t)(20 + ddy);
        }
        sa_set(buf, lx, ly);
        if (s_spark_pts[i].head) {   // bold 2×2 comet head so the L→R streak reads over the haze
            sa_set(buf, lx + 1, ly);
            sa_set(buf, lx, ly + 1);
            sa_set(buf, lx + 1, ly + 1);
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
    int16_t cvi  = (int16_t)(((int32_t)tt - 110) * 255 / 75);       // converge over tt 110..185
    uint8_t cv   = (uint8_t)(cvi < 0 ? 0 : (cvi > 255 ? 255 : cvi));
    uint8_t ring = (uint8_t)(255 - cv);                            // ripples fade as things converge
    bool letters = tt >= 150;
    bool sparks  = (el < SA_INTRO_MS);
    // Sparks wink out one by one over tt 150..255 (staggered per-spark in sa_build_sparks).
    int16_t sfi  = (int16_t)(((int32_t)tt - 150) * 255 / 105);
    uint8_t spark_fade = (uint8_t)(sfi < 0 ? 0 : (sfi > 255 ? 255 : sfi));
    uint8_t fade = 0;
    if (el >= SA_INTRO_MS + SA_HOLD_MS) {
        uint32_t f = ((el - SA_INTRO_MS - SA_HOLD_MS) * 256) / SA_FADE_MS;
        fade = (uint8_t)(f > 255 ? 255 : f);
    }
    const int16_t cxr = SA_BOARD_W / 2;
    const int16_t cyr = (int16_t)((int32_t)SA_BOARD_H * 42 / 100);

    if (sparks) sa_build_sparks(el, cv, spark_fade);   // once per frame (key-independent)

    for (uint8_t idx = 0; idx < 40; ++idx) {
        const sa_key_geom_t *g = &T[idx];
        if (!g->valid) continue;
        const bool rot = (g->ang != 0);                                // only the 4 thumbs
        int16_t cosv = (int16_t)sa_sin((uint8_t)(g->ang + 64)) - 128;   // cos * ~127
        int16_t sinv = (int16_t)sa_sin(g->ang) - 128;                   // sin * ~127

        sr_shift_out_buffer_latch(get_key_disp_bitmask(idx), get_disp_bitmask_size());
        kdisp_set_buffer(0x00);
        uint8_t *buf = get_scratch_buffer();

        for (int16_t ly = 0; ly < SCREEN_HEIGHT; ++ly) {
            int16_t dy = (int16_t)(ly - 20);
            int16_t gy_flat = (int16_t)(g->cy + dy);   // fast path: no rotation
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
                uint8_t bit = 0;
                uint8_t pv = sa_plasma(gx, gy, tp);
                if ((uint8_t)(((uint16_t)pv * SA_PGAIN) >> 8) > sa_noise(gx, gy)) {
                    bit = 1;
                } else if (ring) {   // faint, diffuse, irregular expanding ripple — a soft
                                     // circular shimmer BEHIND the L→R sparks, dissolving as
                                     // `ring` fades. Kept subtle so the spark motion reads.
                    int16_t ax = (int16_t)(((int32_t)(gx - cxr) * SA_RING_ANUM) / SA_RING_ADEN);
                    int16_t ay = (int16_t)(gy - cyr);
                    uint16_t rr = sa_dist(ax, ay);
                    // irregular: wobble the radius by a slow spatial sine so the rings are
                    // not perfectly concentric and vary across the field (±~16 px).
                    rr = (uint16_t)((int16_t)rr + ((sa_sin((uint8_t)(gx + 2 * gy)) - 128) >> 3));
                    uint8_t rv = sa_sin((uint8_t)(((uint32_t)rr * SA_RING_FREQ >> 8) - tprg));
                    uint8_t crest = rv > 210 ? (uint8_t)(rv - 210) : 0;  // only the peak → THIN rings,
                                                                        // lots of dark space between
                    uint8_t dens  = (uint8_t)(((uint16_t)crest * ring) >> 8);   // ~18% in the thin band
                    if (sa_noise((int16_t)(gx + 50), (int16_t)(gy + 30)) < dens)
                        bit = 1;
                }
                if (bit) buf[(size_t)(ly >> 3) * SA_STRIDE + (BUFFER_X + lx)] |= (uint8_t)(1u << (ly & 7));
            }
        }

        if (sparks) sa_plot_sparks(buf, g, rot, cosv, sinv);

        if (letters && L[idx]) {
            const GFXfont *const lf[1] = { &FreeSansBold24pt7b };
            uint32_t txt[2] = { L[idx], 0 };
            kdisp_write_gfx_text(lf, 1, 49, 38, txt);
        }

        if (fade) {   // dither-dissolve this panel toward black (noise-tile lookup)
            for (int16_t ly = 0; ly < SCREEN_HEIGHT; ++ly)
                for (int16_t lx = 0; lx < SCREEN_WIDTH; ++lx)
                    if (sa_noise((int16_t)(lx + idx * 13), (int16_t)(ly + idx * 7)) < fade)
                        buf[(size_t)(ly >> 3) * SA_STRIDE + (BUFFER_X + lx)] &= (uint8_t)~(1u << (ly & 7));
        }

        kdisp_send_window();   // 360 B (visible cols/pages) not the full 1024 B — faster SPI
    }
}

void startup_anim_start(void) {
    s_start  = timer_read32();
    s_active = true;
    // Eden runs at FULL brightness regardless of the stored brightness. This only
    // touches the OLED contrast register (not local_state->contrast), so the normal
    // brightness is restored after the fade-to-black (see the housekeeping finish
    // edge in poly_keymap.c).
    sr_shift_out_0_latch(NUM_SHIFT_REGISTERS);   // select all panels on this half
    kdisp_enable(true);
    kdisp_set_contrast(255);
}

bool startup_anim_active(void) { return s_active; }

void startup_anim_tick(void) {
    if (!s_active) return;
    uint32_t el = timer_elapsed32(s_start);
    if (el >= SA_TOTAL_MS) { s_active = false; return; }
    sa_render_frame(el);
}

#else  // ---- non-split72: no-op stubs ----
void startup_anim_start(void) {}
void startup_anim_tick(void) {}
bool startup_anim_active(void) { return false; }
#endif
