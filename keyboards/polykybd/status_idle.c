// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The status panel's idle screen (split72). Replaces the scrolling Poly/Kybd logos.
//
// Three layers, back to front:
//   1. A GLYPH RAIN: columns of half-scale glyphs falling like rain drops, drawn from
//      whatever g_all_fonts can render — the resident Latin/Greek/Cyrillic/currency
//      faces always, and every script of the font pack when one is flashed. Each drop
//      re-rolls its speed, tail length, spacing, column offset and SCRIPT when it
//      restarts, and every cell swaps its glyph on its own clock, so the rain does not
//      repeat itself. The tail fades out with Eden's own noise dither.
//   2. EDEN'S RING RIPPLE: the status panel is a window onto the same board space as
//      the keycaps (its position is taken from the switch plate: the 23.75 x 13 mm
//      window, registered to the key columns), so the idle ring that sweeps across the
//      keycaps also sweeps across this panel. Where a ring crest passes, it relights
//      the rain's faded tails and leaves a faint sparkle between the columns.
//   3. EDEN'S COMETS and a few twinkling STARS (the intro's star shapes) on top.
//
// While the Eden idle style runs, layers 2 and 3 read this half's own loop clock, so
// the panel shows the same instant as the keycaps around it.
//
// ⚠️ Non-blocking by design: the frame is written with oled_write_raw() (which diffs
// and marks only changed blocks dirty) and left to QMK's per-pass oled_render(), which
// flushes one block per main-loop pass. oled_render_dirty(true) would push the whole
// frame in one ~26 ms I2C burst on EVERY frame, during which the matrix is not scanned.

#include "status_idle.h"

#include <stdbool.h>
#include <stdint.h>
#include "quantum.h"
#include "oled_driver.h"
#include "base/disp_array.h"    // get_scratch_buffer, kdisp_set_buffer
#include "base/fontpack.h"      // g_all_fonts / g_all_font_count
#include "base/font_lookup.h"   // kdisp_gfx_glyph_font, pgm_read_bitmap_ptr
#include "base/glyph_meta.h"    // glyph_width/height/bitmap_offset/col_bytes
#include "anim/startup_anim.h"  // the Eden field: ring, comets, stars, noise, loop clock
#include "side.h"               // is_left_side

#define SI_W 128
#define SI_H 64
#define STATUS_IDLE_FRAME_MS 80   // ~12 fps; a frame composes in well under 2 ms

// Where the panel sits in Eden's board space. From the plates
// (poly_kybd_split72_plate_{left,right}.kicad_pcb): the display window is the
// 23.75 x 13.00 mm Eco2.User rectangle, registered to the PCB through the key-column
// "cutout" labels (the plate is drawn mirrored) and then to the board space through
// the key pitch (87 units per 19.05 mm). The 0.96" panel's 21.7 mm active width over
// 128 px is 0.78 board units per pixel. The right half mirrors the left.
#define SI_UPP_Q8    200                                   // board units per pixel, q8
#define SI_CX_LEFT   742
#define SI_CX_RIGHT  931                                   // SA_BOARD_W - SI_CX_LEFT
#define SI_CY        58
#define SI_X0(cx)    ((int16_t)((cx) - ((SI_W * SI_UPP_Q8) >> 9)))
#define SI_Y0        ((int16_t)(SI_CY - ((SI_H * SI_UPP_Q8) >> 9)))

// ---- rain geometry ----
#define SI_CELL_W 12
#define SI_CELL_H 12
#define SI_COLS   (SI_W / SI_CELL_W)   // 10 columns, 8 px of slack for the per-drop x offset

// Scripts the rain draws from, as codepoint ranges of letters (mostly). A range whose
// font is not present (no pack, or a pack without it) is skipped when a drop picks
// its script, so the same table works with and without a font pack.
typedef struct {
    uint32_t first;
    uint16_t count;
} si_script_t;

static const si_script_t SI_SCRIPTS[] = {
    // resident: always present
    {0x0041, 26},  {0x0061, 26},  {0x0030, 10},  {0x00C0, 64},  {0x0100, 128},
    {0x0391, 57},  {0x0400, 96},  {0x20A0, 32},
    // font pack
    {0x0531, 38},  {0x0561, 38},  {0x05D0, 27},  {0x0627, 36},  {0x0905, 53},
    {0x0985, 53},  {0x0B85, 53},  {0x0C05, 53},  {0x0E01, 46},  {0x10D0, 43},
    {0x1200, 347}, {0x13A0, 85},  {0x1401, 620}, {0x3041, 86},  {0x2F00, 214},
    {0x3105, 43},  {0x2190, 112}, {0x2200, 256}, {0x2654, 62},  {0x270E, 178},
    // the fantasy / retro faces (glyph-script blocks in the PUA, font pack)
    {0xE800, 36},  {0xE840, 26},  {0xE880, 26},  {0xE8C0, 36},  {0xE900, 26},
    {0xE940, 36},  {0xE980, 36},  {0xE9C0, 36},  {0xEA00, 36},  {0xEA40, 36},
};
#define SI_NSCRIPTS   ((uint8_t)(sizeof(SI_SCRIPTS) / sizeof(SI_SCRIPTS[0])))
#define SI_MIXED      0xFFu    // a drop whose every cell picks its own script
#define SI_RESIDENT_N 8u       // the first entries above; the fallback when a pick fails

typedef struct {
    uint32_t t0;       // when the head entered the top (may lie in the future: a pause)
    uint16_t gen;      // drop number in this column; every property below re-rolls with it
    uint8_t  speed;    // px per second
    uint8_t  tail;     // cells behind the head
    uint8_t  step;     // 1: every cell, 2: a sparse drop that skips every other cell
    uint8_t  script;   // index into SI_SCRIPTS, or SI_MIXED
    int8_t   xoff;     // -2..+2 px of the column
} si_drop_t;

static si_drop_t s_drop[SI_COLS];
static uint32_t  s_salt;          // per idle session, so no two sessions rain alike
static uint32_t  s_last_frame;
static uint32_t  s_last_call;
static uint32_t  s_eden_t0;       // own clock for the Eden layers when the loop is not running
static bool      s_started;
static uint8_t   s_worst_ms;      // slowest frame since the last console report
static uint32_t  s_next_log;

// Same mixer as the Eden renderer's sa_hash8, widened to 32 bits of output.
static inline uint32_t si_hash(uint32_t v) {
    v ^= v >> 15; v *= 0x2c1b3c6dU;
    v ^= v >> 12; v *= 0x297a2d39U;
    v ^= v >> 15; return v;
}

// ---- plotting into the 128x64 scratch frame (page-major, LSB on top) ----
static uint8_t *s_buf;
static inline void si_set(int16_t x, int16_t y) {
    if (x >= 0 && x < SI_W && y >= 0 && y < SI_H)
        s_buf[(uint16_t)(y >> 3) * SI_W + (uint16_t)x] |= (uint8_t)(1u << (y & 7));
}
static void si_plot(int16_t x, int16_t y) { si_set(x, y); }

static inline int16_t si_bx(int16_t x0, int16_t px) { return (int16_t)(x0 + (((int32_t)px * SI_UPP_Q8) >> 8)); }
static inline int16_t si_by(int16_t py)             { return (int16_t)(SI_Y0 + (((int32_t)py * SI_UPP_Q8) >> 8)); }

// ---- glyphs ----
static bool si_glyph_ok(uint32_t cp) {
    const GFXfont  *font = NULL;
    const GFXglyph *g    = kdisp_gfx_glyph_font(g_all_fonts, g_all_font_count, cp, &font);
    return g != NULL && font != NULL && glyph_width(g) >= 2 && glyph_height(g) >= 4;
}

// A codepoint from `script` that this board can render. A few hashed tries inside the
// range (ranges have holes), then the resident Latin capitals.
static uint32_t si_pick_cp(uint8_t script, uint32_t h) {
    for (uint8_t t = 0; t < 4; ++t) {
        uint8_t sc = script;
        if (sc == SI_MIXED) sc = (uint8_t)(si_hash(h + 0x51u * t) % SI_NSCRIPTS);
        const uint32_t cp = SI_SCRIPTS[sc].first + (si_hash(h + t) % SI_SCRIPTS[sc].count);
        if (si_glyph_ok(cp)) return cp;
    }
    return 0x41u + (h % 26u);
}

// Half-scale glyph (2x2 OR, as kdisp_draw_glyph_half_at), its ink box centred in the
// cell at (cx, cy). `dens` 255 draws it solid; lower values dither it out with Eden's
// noise tile, and a passing ring crest (sampled per pixel) relights it.
static void si_draw_glyph(uint32_t cp, int16_t cx, int16_t cy, uint8_t dens, int16_t x0,
                          uint32_t el, uint16_t nshift) {
    const GFXfont  *font = NULL;
    const GFXglyph *g    = kdisp_gfx_glyph_font(g_all_fonts, g_all_font_count, cp, &font);
    if (g == NULL || font == NULL) return;
    const uint8_t *bitmap = pgm_read_bitmap_ptr(font);
    const uint16_t bo = glyph_bitmap_offset(g);
    const int16_t  w  = glyph_width(g), h = glyph_height(g);
    const int16_t  hw = (int16_t)((w + 1) / 2), hh = (int16_t)((h + 1) / 2);
    const uint8_t  cb = glyph_col_bytes((uint8_t)h);
    const int16_t  gx = (int16_t)(cx - hw / 2), gy = (int16_t)(cy - hh / 2);
    for (int16_t dx = 0; dx < hw; ++dx) {
        const int16_t px = (int16_t)(gx + dx);
        if (px < 0 || px >= SI_W) continue;
        for (int16_t dy = 0; dy < hh; ++dy) {
            const int16_t py = (int16_t)(gy + dy);
            if (py < 0 || py >= SI_H) continue;
            bool lit = false;
            for (int16_t ox = 0; ox < 2 && !lit; ++ox) {
                const int16_t sx = (int16_t)(dx * 2 + ox);
                if (sx >= w) break;
                for (int16_t oy = 0; oy < 2; ++oy) {
                    const int16_t sy = (int16_t)(dy * 2 + oy);
                    if (sy >= h) break;
                    if (pgm_read_byte(&bitmap[bo + (uint16_t)sx * cb + (sy >> 3)]) & (1u << (sy & 7))) {
                        lit = true;
                        break;
                    }
                }
            }
            if (!lit) continue;
            if (dens < 255u) {
                uint16_t d = dens;
                const uint16_t r = (uint16_t)startup_anim_ring_density(si_bx(x0, px), si_by(py), el) * 6u;
                if (r > d) d = r;
                if (startup_anim_noise((int16_t)(px + nshift), (int16_t)(py + (el >> 7))) >= d) continue;
            }
            si_set(px, py);
        }
    }
}

// ---- the rain ----
static void si_roll(uint8_t c, uint32_t now, bool first) {
    si_drop_t *d = &s_drop[c];
    d->gen++;
    const uint32_t h = si_hash(s_salt ^ (c * 0x9E3779B1u) ^ ((uint32_t)d->gen << 8));
    d->speed = (uint8_t)(24u + (h & 0x3Fu) + ((h >> 6) & 0x1Fu));      // 24..118 px/s
    d->tail  = (uint8_t)(2u + ((h >> 11) % 7u));                        // 2..8 cells
    d->step  = ((h >> 14) % 7u) == 0u ? 2u : 1u;                        // 1 in 7 sparse
    d->xoff  = (int8_t)((int8_t)((h >> 17) % 5u) - 2);
    // Pause before the drop starts: the first drops of a session start staggered over
    // ~2.5 s so the rain builds up, later ones leave a gap of 0..1.5 s.
    d->t0    = now + (first ? ((h >> 20) % 2500u) : ((h >> 20) % 1500u));
    // Script: 1 drop in 6 is mixed; the rest pick one script that this board can draw
    // (probing one glyph), falling back to a resident one.
    if (((h >> 3) % 6u) == 0u) {
        d->script = SI_MIXED;
    } else {
        uint8_t sc = (uint8_t)((h >> 24) % SI_NSCRIPTS);
        for (uint8_t t = 0; t < 3 && !si_glyph_ok(SI_SCRIPTS[sc].first + (SI_SCRIPTS[sc].count >> 1)); ++t) {
            sc = (uint8_t)(si_hash(h + t) % SI_NSCRIPTS);
        }
        if (!si_glyph_ok(SI_SCRIPTS[sc].first + (SI_SCRIPTS[sc].count >> 1))) {
            sc = (uint8_t)(si_hash(h) % SI_RESIDENT_N);
        }
        d->script = sc;
    }
}

static void si_rain(uint32_t now, int16_t x0, uint32_t el) {
    for (uint8_t c = 0; c < SI_COLS; ++c) {
        si_drop_t *d = &s_drop[c];
        const int32_t age = (int32_t)(now - d->t0);
        if (age < 0) continue;                                   // still paused
        const int32_t y = (age * d->speed) / 1000;               // head, px from the top
        const int16_t head = (int16_t)(y / SI_CELL_H);
        if ((head - (int16_t)(d->tail * d->step)) * SI_CELL_H > SI_H) {   // tail has left
            si_roll(c, now, false);
            continue;
        }
        const int16_t cx = (int16_t)(c * SI_CELL_W + SI_CELL_W / 2 + 4 + d->xoff);
        const uint32_t hc = s_salt ^ ((uint32_t)c << 20) ^ ((uint32_t)d->gen << 4);
        for (uint8_t k = 0; k <= d->tail; ++k) {
            const int16_t r = (int16_t)(head - (int16_t)(k * d->step));
            if (r < 0) break;
            if (r * SI_CELL_H >= SI_H) continue;
            // Each cell swaps its glyph on its own clock; the head churns fastest.
            const uint32_t hr     = si_hash(hc + (uint32_t)r * 131u);
            const uint16_t period = (k == 0) ? (uint16_t)(70u + (hr & 63u)) : (uint16_t)(220u + (hr & 511u));
            const uint32_t flick  = (now + (hr >> 16)) / period;
            const uint32_t cp     = si_pick_cp(d->script, si_hash(hr ^ (flick * 0x85EBCA6Bu)));
            const uint8_t  dens   = (k == 0) ? 255u : (uint8_t)(150u - (uint8_t)(k * 130u / (d->tail + 1u)));
            si_draw_glyph(cp, cx, (int16_t)(r * SI_CELL_H + SI_CELL_H / 2), dens, x0, el, (uint16_t)(c * 5u));
        }
    }
}

// ---- Eden layers ----
static void si_ring_haze(int16_t x0, uint32_t el) {
    for (int16_t py = 0; py < SI_H; py += 2) {
        for (int16_t px = 0; px < SI_W; px += 2) {
            const uint8_t d = (uint8_t)(startup_anim_ring_density(si_bx(x0, px), si_by(py), el) / 3u);
            if (!d) continue;
            for (int16_t oy = 0; oy < 2; ++oy)
                for (int16_t ox = 0; ox < 2; ++ox)
                    if (d > startup_anim_noise(si_bx(x0, (int16_t)(px + ox)), (int16_t)(si_by((int16_t)(py + oy)))))
                        si_set((int16_t)(px + ox), (int16_t)(py + oy));
        }
    }
}

#define SI_STAR_SLOTS  10
#define SI_STAR_LIFE   3600u   // the intro's SA_STAR_LIFE_MS
#define SI_STAR_PERIOD 9000u
static void si_stars(uint32_t el, uint32_t side) {
    for (uint8_t k = 0; k < SI_STAR_SLOTS; ++k) {
        const uint32_t off  = (si_hash(k * 29u + side) & 0xFFu) * SI_STAR_PERIOD / 255u;
        const uint32_t cyc  = (el + off) / SI_STAR_PERIOD;
        const uint32_t t    = (el + off) % SI_STAR_PERIOD;
        const uint32_t seed = si_hash((cyc * SI_STAR_SLOTS + k + side) * 7u + 1u);
        if ((seed & 0xFFu) >= 150u || t >= SI_STAR_LIFE) continue;
        const int16_t x = (int16_t)(4 + (seed >> 8) % (SI_W - 8));
        const int16_t y = (int16_t)(4 + (seed >> 16) % (SI_H - 8));
        int8_t dx[9], dy[9];
        const uint8_t n = startup_anim_star_pts((uint8_t)((seed >> 24) % 5u), (uint8_t)((t * 5u) / SI_STAR_LIFE), dx, dy);
        for (uint8_t i = 0; i < n; ++i) si_set((int16_t)(x + dx[i]), (int16_t)(y + dy[i]));
    }
}

void status_idle_screen(void) {
    const uint32_t now = timer_read32();
    // A pause in the calls means the panel left the idle screen in between: start a
    // fresh session (new salt, new drops) rather than resuming a stale one.
    if (!s_started || timer_elapsed32(s_last_call) > 500u) {
        s_started    = true;
        s_salt       = si_hash(now ^ (is_left_side() ? 0x1234567u : 0x7654321u));
        s_eden_t0    = now;
        s_last_frame = now - STATUS_IDLE_FRAME_MS;
        for (uint8_t c = 0; c < SI_COLS; ++c) {
            s_drop[c].gen = 0;
            si_roll(c, now, true);
        }
    }
    s_last_call = now;
    if (timer_elapsed32(s_last_frame) < STATUS_IDLE_FRAME_MS) return;
    s_last_frame = now;

    const bool     left = is_left_side();
    const int16_t  x0   = left ? SI_X0(SI_CX_LEFT) : SI_X0(SI_CX_RIGHT);
    // Eden's clock: this half's running loop when there is one (same instant as the
    // keycaps), otherwise our own.
    uint32_t el = startup_anim_loop_ms();
    if (el == 0u) el = timer_elapsed32(s_eden_t0);

    kdisp_set_buffer(0);
    s_buf = get_scratch_buffer();
    const uint32_t t_start = timer_read32();
    si_rain(now, x0, el);
    si_ring_haze(x0, el);
    startup_anim_status_comets(si_plot, x0, SI_Y0, SI_W, SI_H, SI_UPP_Q8, el);
    si_stars(el, left ? 0u : 101u);
    // Compose time, before the blit: the number that says how long this frame held
    // the main loop off the matrix. Reported like Eden's "frame Nms" line.
    const uint32_t took = timer_elapsed32(t_start);
    if (took > s_worst_ms) s_worst_ms = (uint8_t)(took > 255u ? 255u : took);
    if ((int32_t)(now - s_next_log) >= 0) {
        uprintf("Status idle: worst frame %ums (font pack fonts %u)\n", s_worst_ms, g_all_font_count);
        s_worst_ms = 0;
        s_next_log = now + 5000u;
    }
    oled_write_raw((char *)get_scratch_buffer(), get_scratch_buffer_size());
}
