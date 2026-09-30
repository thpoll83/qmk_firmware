// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The status panel's idle screen (split72): plasma bands with "Poly" / "Kybd".
//
// The background is a classic demoscene plasma — a sum of four sines of Eden's sine
// table, one of them fed by a distance from the centre — drawn as CONTOUR BANDS rather
// than dithered, because on a 1-bit panel a dithered plasma reads as grey noise while
// its contour lines are native. Both panels are one field: the right panel continues
// the left one after the physical gap, so the bands flow across the keyboard.
//
// Over it, "Poly" (left half) and "Kybd" (right half) in FreeSansBold24pt7b — the face
// Eden writes its keycap letters in — are TYPED as one line across both panels, left
// first, with a blinking text cursor, held, and deleted from the end (see SI_HIDE_MS).
// Each word is centred on its panel (the full word's ink box, so the letters are typed
// into their final places). Each letter, and the cursor, is solid, cut out of the bands by a 2 px black ring (the word's shape grown
// by a radius-2 disc) and nothing more: the bands keep flowing between the letters and
// through the counters.
//
// Nothing is stored: every frame is computed from the font's column bytes in flash and
// Eden's tables; RAM is a handful of statics.
//
// ⚠️ Non-blocking: the frame goes through oled_write_raw() (diffed) and QMK's per-pass
// oled_render(), one 64-byte block per main-loop pass. The bands move everywhere, so
// nearly every block is dirty every frame; the 100 ms period leaves room for the ~16
// passes a full flush takes. oled_render_dirty(true) would block ~26 ms per frame.

#include "status_idle.h"

#include <stdbool.h>
#include <stdint.h>
#include "quantum.h"
#include "oled_driver.h"
#include "base/disp_array.h"    // get_scratch_buffer, kdisp_set_buffer
#include "base/font_lookup.h"   // kdisp_gfx_glyph_font, pgm_read_bitmap_ptr
#include "base/glyph_meta.h"    // glyph_* accessors, column-native layout
#include "anim/startup_anim.h"  // Eden's sine table, distance and noise tile
#include "poly_util.h"          // poly_heavy_font(): the Eden splash face
#include "side.h"               // is_left_side

#define SI_W 128
#define SI_H 64
#define STATUS_IDLE_FRAME_MS 100

// The two panels as one field: the right one starts after the physical gap.
#define SI_GAP_PX 40
#define SI_FIELD_CX ((2 * SI_W + SI_GAP_PX) / 2)
#define SI_FIELD_CY (SI_H / 2)

// The sentence "Poly Kybd" is typed across both panels like one line of text, with
// a cursor: the cursor blinks at the start of the left panel, "Poly" is typed there,
// the space moves the cursor to the right panel, "Kybd" is typed, the cursor blinks
// through the hold, and then everything is deleted from the end, back across the gap.
// Both halves run the same timeline from the same idle-session clock.
#define SI_HIDE_MS   5000u     // cursor blinking alone at the start
#define SI_KEY_MS    300u      // one keystroke (letter, space or backspace)
#define SI_HOLD_MS   8000u     // the full sentence, cursor blinking at its end
#define SI_BLINK_MS  530u      // cursor half-period while nothing is typed
#define SI_CURSOR_W  2         // cursor bar width, px
#define SI_CURSOR_GAP 2        // px between the last letter's pen position and the cursor
#define SI_RING      2         // the black ring's radius, px
#define SI_WIN       (2 * SI_RING + 1)

static const uint32_t SI_WORD_LEFT[]  = U"Poly";
static const uint32_t SI_WORD_RIGHT[] = U"Kybd";
#define SI_NL ((uint8_t)(sizeof(SI_WORD_LEFT) / sizeof(SI_WORD_LEFT[0]) - 1u))
#define SI_NR ((uint8_t)(sizeof(SI_WORD_RIGHT) / sizeof(SI_WORD_RIGHT[0]) - 1u))
#define SI_CYCLE_MS (SI_HIDE_MS + 2u * (SI_NL + 1u + SI_NR) * SI_KEY_MS + SI_HOLD_MS)

// Each glyph of this half's word, laid out once per session from the font metrics.
typedef struct {
    int16_t  x;      // first word column of the ink
    uint16_t bo;     // bitmap offset
    uint8_t  w, h;   // ink size
    uint8_t  top;    // first word row of the ink (0 = top of the tallest glyph)
} si_gpos_t;

static si_gpos_t      s_g[4];
static int16_t        s_pen[4];      // pen position after each glyph (word columns): the cursor
static uint8_t        s_ng;
static int16_t        s_ww;          // word width (ink)
static uint8_t        s_wh;          // word height (ink)
static const uint8_t *s_bitmap;

static uint32_t s_t0;
static uint32_t s_last_frame;
static uint32_t s_last_call;
static bool     s_started;
static uint8_t  s_worst_ms;
static uint32_t s_next_log;

static inline int16_t si_s8(int32_t t) { return (int16_t)startup_anim_sin((uint8_t)t) - 128; }

static void si_layout(const uint32_t *text) {
    const GFXfont *const face[] = {poly_heavy_font()};
    int16_t pen = 0, top = 127, bottom = -127, left = 32767, right = -32767;
    s_ng = 0;
    for (const uint32_t *c = text; *c && s_ng < 4; ++c) {
        const GFXfont  *font = NULL;
        const GFXglyph *g    = kdisp_gfx_glyph_font(face, 1, *c, &font);
        if (g == NULL) continue;
        s_bitmap = pgm_read_bitmap_ptr(font);
        const uint8_t w = glyph_width(g), h = glyph_height(g);
        if (w && h) {
            const int8_t  yo = glyph_y_offset(g);
            const int16_t x  = (int16_t)(pen + glyph_x_offset(g));
            s_g[s_ng] = (si_gpos_t){.x = x, .bo = glyph_bitmap_offset(g), .w = w, .h = h, .top = (uint8_t)yo};
            if (yo < top) top = yo;
            if (yo + h > bottom) bottom = (int16_t)(yo + h);
            if (x < left) left = x;
            if (x + w > right) right = (int16_t)(x + w);
            ++s_ng;
        }
        pen = (int16_t)(pen + glyph_x_advance(g));
        if (s_ng) s_pen[s_ng - 1] = pen;
    }
    for (uint8_t i = 0; i < s_ng; ++i) {       // rebase to the ink's top-left
        s_g[i].top = (uint8_t)((int8_t)s_g[i].top - top);
        s_g[i].x   = (int16_t)(s_g[i].x - left);
        s_pen[i]   = (int16_t)(s_pen[i] - left);
    }
    s_ww   = (int16_t)(right - left);
    s_wh   = (uint8_t)(bottom - top);
}

// The ink of the first `n` letters, plus the cursor when `cursor`, in word column `wx`
// as a 64-bit column, bit 0 = the word's top row. The cursor stands where the next
// letter would start (the pen after letter n) and spans the FULL ink height, descender
// included. ⚠️ A cap-height bar right after the last letter read as a letter: "Polyl",
// "Kybdl". Going below the baseline, where no l does, is what makes it a cursor.
static uint64_t si_word_col(int16_t wx, uint8_t n, bool cursor) {
    uint64_t col = 0;
    if (cursor) {
        const int16_t cx = n ? (int16_t)(s_pen[n - 1] + SI_CURSOR_GAP) : 0;
        if (wx >= cx && wx < cx + SI_CURSOR_W) col = ((uint64_t)1 << s_wh) - 1u;
    }
    if (wx < 0 || wx >= s_ww) return col;
    for (uint8_t i = 0; i < n; ++i) {
        const int16_t gx = (int16_t)(wx - s_g[i].x);
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

// What this half shows `u` ms into the cycle: how many of its letters, and whether
// the cursor is on it (and lit, when it blinks).
typedef struct {
    uint8_t n;
    bool    cursor;
} si_state_t;

static bool si_blink(uint32_t u) { return ((u / SI_BLINK_MS) & 1u) == 0u; }

static si_state_t si_state(uint32_t u, bool left) {
    const uint32_t K = SI_KEY_MS;
    // Keystrokes: NL letters, one space, NR letters — then the same, backwards.
    if (u < SI_HIDE_MS) return left ? (si_state_t){0, si_blink(u)} : (si_state_t){0, false};
    u -= SI_HIDE_MS;
    if (u < SI_NL * K) return left ? (si_state_t){(uint8_t)(u / K + 1u), true} : (si_state_t){0, false};
    u -= SI_NL * K;
    if (u < K) return left ? (si_state_t){SI_NL, false} : (si_state_t){0, true};      // the space
    u -= K;
    if (u < SI_NR * K) return left ? (si_state_t){SI_NL, false} : (si_state_t){(uint8_t)(u / K + 1u), true};
    u -= SI_NR * K;
    if (u < SI_HOLD_MS) return left ? (si_state_t){SI_NL, false} : (si_state_t){SI_NR, si_blink(u)};
    u -= SI_HOLD_MS;
    if (u < SI_NR * K) return left ? (si_state_t){SI_NL, false} : (si_state_t){(uint8_t)(SI_NR - 1u - u / K), true};
    u -= SI_NR * K;
    if (u < K) return left ? (si_state_t){SI_NL, true} : (si_state_t){0, false};       // back over the space
    u -= K;
    if (u < SI_NL * K) return left ? (si_state_t){(uint8_t)(SI_NL - 1u - u / K), true} : (si_state_t){0, false};
    return left ? (si_state_t){0, true} : (si_state_t){0, false};
}

void status_idle_screen(void) {
    const uint32_t now = timer_read32();
    if (!s_started || timer_elapsed32(s_last_call) > 500u) {   // a new idle session
        s_started    = true;
        s_t0         = now;
        s_last_frame = now - STATUS_IDLE_FRAME_MS;
        si_layout(is_left_side() ? SI_WORD_LEFT : SI_WORD_RIGHT);
    }
    s_last_call = now;
    if (timer_elapsed32(s_last_frame) < STATUS_IDLE_FRAME_MS) return;
    s_last_frame = now;

    const uint32_t t_start = timer_read32();
    const uint32_t t       = timer_elapsed32(s_t0);
    const bool     left    = is_left_side();
    const int16_t  fx0     = left ? 0 : (SI_W + SI_GAP_PX);   // this panel's field column 0

    // The word: how many letters are typed and where the cursor is, centred on the panel.
    const si_state_t st  = si_state(t % SI_CYCLE_MS, left);
    const uint8_t    n   = st.n;
    const bool       cur = st.cursor;
    const bool       any = n || cur;
    const int16_t  wx0   = (int16_t)((SI_W - s_ww) / 2);
    const uint8_t  wy0   = (uint8_t)((SI_H - s_wh) / 2);

    kdisp_set_buffer(0);
    uint8_t *buf = get_scratch_buffer();

    // A five-column window of the word's columns (x-2 .. x+2) for the black ring: the
    // word grown by a radius-2 disc (offsets with dx*dx + dy*dy <= 4), minus the ink.
    // ⚠️ Deliberately ONLY the ring. Closing the gaps between letters as well (any pixel
    // with ink within N px on both sides) painted solid black wedges between them — a
    // shadow, most visibly between K and y — so the bands show through the gaps and the
    // counters, as they should.
    uint64_t win[SI_WIN];
    for (int8_t k = 0; k < SI_WIN; ++k)
        win[k] = any ? (si_word_col((int16_t)(-SI_RING + k - wx0), n, cur) << wy0) : 0;

    for (int16_t x = 0; x < SI_W; ++x) {
        const uint64_t ink  = win[2];
        const uint64_t ring = (ink << 1) | (ink << 2) | (ink >> 1) | (ink >> 2) |
                              win[1] | (win[1] << 1) | (win[1] >> 1) |
                              win[3] | (win[3] << 1) | (win[3] >> 1) |
                              win[0] | win[4];
        const int16_t fx = (int16_t)(fx0 + x);
        uint64_t      lit = 0;
        for (uint8_t y = 0; y < SI_H; ++y) {
            const uint64_t bit  = (uint64_t)1 << y;
            if (ink & bit) { lit |= bit; continue; }   // the letter: solid
            if (ring & bit) continue;                   // the 2 px black ring
            // Plasma bands: four sines, one of them of the distance from the field centre.
            const int16_t d = (int16_t)startup_anim_dist((int16_t)((fx - SI_FIELD_CX) * 2),
                                                         (int16_t)((y - SI_FIELD_CY) * 4));
            const int16_t v = (int16_t)(si_s8(fx * 2 + (int32_t)(t >> 4)) +
                                        si_s8(y * 3 - (int32_t)(t / 22u)) +
                                        si_s8(fx + y + (int32_t)(t / 13u)) +
                                        si_s8(d / 2 + (int32_t)(t / 9u)));
            if ((((v + 512) >> 4) & 3) == 0) lit |= bit;
        }
        for (uint8_t p = 0; p < SI_H / 8; ++p) buf[(uint16_t)p * SI_W + (uint16_t)x] = (uint8_t)(lit >> (8u * p));
        for (uint8_t k = 0; k < SI_WIN - 1; ++k) win[k] = win[k + 1];
        win[SI_WIN - 1] = any ? (si_word_col((int16_t)(x + 1 + SI_RING - wx0), n, cur) << wy0) : 0;
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
