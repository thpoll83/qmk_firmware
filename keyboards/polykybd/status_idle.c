// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The status panel's idle screen (split72): a procedural "Poly Kybd" marquee.
//
// The wordmark is set in FreeSansBold24pt7b — the face Eden writes its keycap letters
// in (poly_heavy_font()) — and flows right to left across BOTH panels: it enters on the
// right half, crosses the gap and leaves on the left, as one strip. Each letter is drawn
// as a solid 1 px outline filled with Eden's moving plasma, dithered against Eden's
// noise tile, over a faint plasma haze. Nothing is a stored image: every frame is
// computed from the font's column bytes in flash and the Eden tables.
//
// ⚠️ Why a marquee and not a word drifting inside the panel: the anti-burn-in point is
// that every pixel is worked about equally. A 95..112 px word drifting on a 128 px
// panel keeps the centre lit 35..47 % of the time while the edges never light
// (simulated over 20 min: 137..1148 pixels never lit). The marquee makes every COLUMN
// identical by construction (15.9..16.3 % duty), and a constant-speed vertical bob that
// lets the letters leave the top and bottom by 14 px spreads the ROWS (3.4..24 %;
// the scrolling logos this replaces: 4.7..77 %). No pixel stays lit longer than ~3 s.
//
// ⚠️ Non-blocking: the frame goes through oled_write_raw() (diffed) and QMK's per-pass
// oled_render(), one 64-byte block per main-loop pass. A marquee dirties nearly every
// block every frame, so the frame period (100 ms) is chosen to leave the ~16 passes a
// full flush takes; oled_render_dirty(true) would instead block ~26 ms per frame.

#include "status_idle.h"

#include <stdbool.h>
#include <stdint.h>
#include "quantum.h"
#include "oled_driver.h"
#include "base/disp_array.h"    // get_scratch_buffer, kdisp_set_buffer
#include "base/font_lookup.h"   // kdisp_gfx_glyph_font, pgm_read_bitmap_ptr
#include "base/glyph_meta.h"    // glyph_* accessors, column-native layout
#include "anim/startup_anim.h"  // Eden's plasma and noise tile
#include "poly_util.h"          // poly_heavy_font(): the Eden splash face
#include "side.h"               // is_left_side

#define SI_W 128
#define SI_H 64
#define STATUS_IDLE_FRAME_MS 100

// Marquee motion. SI_GAP_PX is the physical gap between the two panels expressed in
// panel pixels, so the text leaving the right panel reappears on the left one after
// the time it takes to cross that gap. The halves start their idle sessions a few ms apart (the idle
// flag is synced), which is far below one pixel at this speed.
#define SI_SPEED_PX_S 16
#define SI_GAP_PX     40
#define SI_BOB_MS     47000u   // one full up-and-down of the vertical bob
#define SI_BOB_OVER   14       // px the letters may leave the panel at either extreme

static const uint32_t SI_TEXT[] = U"Poly Kybd  ";   // trailing spaces: the gap before it repeats

// Where each glyph of SI_TEXT sits in the strip, computed once per session from the
// font metrics (column-native bitmaps, so a strip column is read straight from flash).
typedef struct {
    int16_t  x;      // first strip column of the ink
    uint16_t bo;     // bitmap offset
    uint8_t  w, h;   // ink size
    uint8_t  top;    // first strip row of the ink (0 = top of the tallest glyph)
} si_gpos_t;

#define SI_MAXG (sizeof(SI_TEXT) / sizeof(SI_TEXT[0]))
static si_gpos_t     s_g[SI_MAXG];
static uint8_t       s_ng;
static int16_t       s_period;    // strip length in columns (the text's total advance)
static uint8_t       s_th;        // strip height in rows (cap top .. descender bottom)
static const uint8_t *s_bitmap;

static uint32_t s_t0;             // session start
static uint32_t s_last_frame;
static uint32_t s_last_call;
static bool     s_started;
static uint8_t  s_worst_ms;
static uint32_t s_next_log;

static void si_layout(void) {
    const GFXfont *const face[] = {poly_heavy_font()};
    int16_t pen = 0, top = 127, bottom = -127;
    s_ng = 0;
    for (const uint32_t *c = SI_TEXT; *c; ++c) {
        const GFXfont  *font = NULL;
        const GFXglyph *g    = kdisp_gfx_glyph_font(face, 1, *c, &font);
        if (g == NULL) continue;
        s_bitmap = pgm_read_bitmap_ptr(font);
        const uint8_t w = glyph_width(g), h = glyph_height(g);
        if (w && h) {
            const int8_t yo = glyph_y_offset(g);
            s_g[s_ng].x   = (int16_t)(pen + glyph_x_offset(g));
            s_g[s_ng].bo  = glyph_bitmap_offset(g);
            s_g[s_ng].w   = w;
            s_g[s_ng].h   = h;
            s_g[s_ng].top = (uint8_t)(int8_t)yo;   // rebased below
            if (yo < top) top = yo;
            if (yo + h > bottom) bottom = (int16_t)(yo + h);
            ++s_ng;
        }
        pen = (int16_t)(pen + glyph_x_advance(g));
    }
    for (uint8_t i = 0; i < s_ng; ++i) s_g[i].top = (uint8_t)((int8_t)s_g[i].top - top);
    s_period = pen;
    s_th     = (uint8_t)(bottom - top);
}

// The text's ink in strip column `s` (0..s_period-1) as a 64-bit column, bit 0 on top.
static uint64_t si_strip_col(int16_t s) {
    uint64_t col = 0;
    for (uint8_t i = 0; i < s_ng; ++i) {
        const int16_t gx = (int16_t)(s - s_g[i].x);
        if (gx < 0 || gx >= s_g[i].w) continue;
        const uint8_t  cb = glyph_col_bytes(s_g[i].h);
        const uint8_t *p  = s_bitmap + s_g[i].bo + (uint16_t)gx * cb;
        uint64_t v = 0;
        for (uint8_t b = 0; b < cb; ++b) v |= (uint64_t)pgm_read_byte(p + b) << (8u * b);
        if (s_g[i].h < 64) v &= ((uint64_t)1 << s_g[i].h) - 1u;
        col |= v << s_g[i].top;
    }
    return col;
}

// Panel column x at marquee offset `off`, bob `y`: the strip column that lands there,
// shifted to the bob. The right panel sees the strip SI_W + SI_GAP_PX further on.
static uint64_t si_panel_col(int16_t x, int32_t base, int8_t y) {
    int32_t s = (base + x) % s_period;
    if (s < 0) s += s_period;
    const uint64_t v = si_strip_col((int16_t)s);
    return y >= 0 ? (v << y) : (v >> (uint8_t)(-y));
}

static uint8_t si_bob(uint32_t t) {   // triangle wave: constant speed between the extremes
    const uint32_t u   = t % SI_BOB_MS;
    const uint32_t tri = u < SI_BOB_MS / 2 ? u : SI_BOB_MS - u;   // 0 .. SI_BOB_MS/2
    const int32_t  lo  = -SI_BOB_OVER, hi = SI_H - s_th + SI_BOB_OVER;
    return (uint8_t)(int8_t)(lo + (int32_t)(((hi - lo) * (int32_t)tri) / (int32_t)(SI_BOB_MS / 2)));
}

void status_idle_screen(void) {
    const uint32_t now = timer_read32();
    if (!s_started || timer_elapsed32(s_last_call) > 500u) {   // a new idle session
        s_started    = true;
        s_t0         = now;
        s_last_frame = now - STATUS_IDLE_FRAME_MS;
        si_layout();
    }
    s_last_call = now;
    if (timer_elapsed32(s_last_frame) < STATUS_IDLE_FRAME_MS) return;
    s_last_frame = now;
    if (s_period <= 0) return;

    const uint32_t t_start = timer_read32();
    const uint32_t t       = timer_elapsed32(s_t0);
    const uint8_t  tp      = (uint8_t)(t >> 4);
    const int32_t  off     = (int32_t)((t * SI_SPEED_PX_S) / 1000u);
    // One strip laid across both panels: the left panel holds strip columns 0..127 of
    // the moment, the right panel continues after the physical gap.
    const int32_t  base    = off + (is_left_side() ? 0 : (SI_W + SI_GAP_PX));
    const int8_t   y       = (int8_t)si_bob(t);

    kdisp_set_buffer(0);
    uint8_t *buf = get_scratch_buffer();

    // Rolling window of three columns for the 4-neighbour erosion that finds the edge.
    uint64_t prev = si_panel_col(-1, base, y), cur = si_panel_col(0, base, y);
    uint8_t  haze_row[SI_H / 2];
    for (int16_t x = 0; x < SI_W; ++x) {
        const uint64_t next    = si_panel_col((int16_t)(x + 1), base, y);
        const uint64_t eroded  = cur & (cur << 1) & (cur >> 1) & prev & next;
        uint64_t       lit     = cur & ~eroded;                  // the 1 px outline, solid
        const uint64_t inside  = eroded;
        if ((x & 1) == 0) {                                      // haze density, 2x2 blocks
            for (uint8_t r = 0; r < SI_H / 2; ++r)
                haze_row[r] = (uint8_t)(startup_anim_plasma((int16_t)(x + 300), (int16_t)(r * 2), tp) / 18u);
        }
        for (uint8_t r = 0; r < SI_H; ++r) {
            const uint64_t bit = (uint64_t)1 << r;
            if (inside & bit) {
                // Letter body: Eden's plasma, dithered, drifting through the letters.
                const uint8_t d = (uint8_t)(startup_anim_plasma(x, r, tp) / 2u + 60u);
                if (d > startup_anim_noise((int16_t)(x + (t >> 6)), (int16_t)(r + (t >> 7)))) lit |= bit;
            } else if (!(cur & bit)) {
                // Faint haze between the letters.
                const uint8_t d = haze_row[r >> 1];
                if (d && d > startup_anim_noise((int16_t)(x * 3 + (t >> 5)), (int16_t)(r * 5))) lit |= bit;
            }
        }
        for (uint8_t p = 0; p < SI_H / 8; ++p) buf[(uint16_t)p * SI_W + (uint16_t)x] = (uint8_t)(lit >> (8u * p));
        prev = cur;
        cur  = next;
    }

    const uint32_t took = timer_elapsed32(t_start);
    if (took > s_worst_ms) s_worst_ms = (uint8_t)(took > 255u ? 255u : took);
    if ((int32_t)(now - s_next_log) >= 0) {
        uprintf("Status idle: worst frame %ums\n", s_worst_ms);
        s_worst_ms = 0;
        s_next_log = now + 5000u;
    }
    oled_write_raw((char *)get_scratch_buffer(), get_scratch_buffer_size());
}
