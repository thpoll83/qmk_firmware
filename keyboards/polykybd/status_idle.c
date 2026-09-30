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
// first, with an underscore cursor, then deleted from the P onward (see SI_KEY_MS).
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

// The sentence "Poly Kybd" is typed across both panels like one line of text, with an
// underscore cursor, then edited away the way a person would:
//   1. the cursor blinks under the P's place, nothing written yet;
//   2. "Poly Kybd" is typed a key at a time, the cursor under the next character's place
//      (the space carries it across the gap to the right panel); the key that completes
//      the line takes the cursor away;
//   3. the finished text stands on its own;
//   4. the cursor comes back under the d, blinks, and walks back to the P while the
//      text stays;
//   5. Del, a key at a time: the P vanishes, the cursor moves on to the o, and so on
//      across the gap to the d — letters vanish in place, nothing reflows;
//   6. the cursor goes, a pause, and it starts over.
// Both halves run the same timeline from the same idle-session clock. Slots number the
// line: 0..NL-1 the left letters, NL the space, NL+1..NL+NR the right letters.
// ⚠️ The place AFTER the d is never used, on purpose: "Kybd" is 112 px of ink, so
// centred it leaves the underscore 4 px of panel. The cursor leaves as the line
// completes and comes back ON the d, which keeps both words exactly centred.
#define SI_KEY_MS     300u     // one keystroke: letter, space or Del
#define SI_STEP_MS    150u     // one cursor step while walking back
#define SI_WAIT_MS    4000u    // 1: cursor blinking before typing
#define SI_DONE_MS    5000u    // 3: finished text, no cursor
#define SI_APPEAR_MS  1200u    // 4: cursor back at the end, blinking, before it walks
#define SI_PAUSE_MS   800u     // 4: cursor blinking under the P before the first Del
#define SI_GAP_MS     3000u    // 6: nothing, before starting over
#define SI_BLINK_MS   530u     // cursor half-period while it waits
#define SI_CUR_EXTRA  14       // underscore width at the end of a word, px
#define SI_RING       2        // the black ring's radius, px
#define SI_WIN        (2 * SI_RING + 1)
// The plasma bands run on a slowed clock (5/8 of real time).
#define SI_PLASMA_NUM 5u
#define SI_PLASMA_DEN 8u

static const uint32_t SI_WORD_LEFT[]  = U"Poly";
static const uint32_t SI_WORD_RIGHT[] = U"Kybd";
#define SI_NL    ((uint8_t)(sizeof(SI_WORD_LEFT) / sizeof(SI_WORD_LEFT[0]) - 1u))
#define SI_NR    ((uint8_t)(sizeof(SI_WORD_RIGHT) / sizeof(SI_WORD_RIGHT[0]) - 1u))
#define SI_SLOTS ((uint8_t)(SI_NL + 1u + SI_NR))
#define SI_CYCLE_MS (SI_WAIT_MS + SI_SLOTS * SI_KEY_MS + SI_DONE_MS + SI_APPEAR_MS + \
                     (SI_SLOTS - 1u) * SI_STEP_MS + SI_PAUSE_MS + SI_SLOTS * SI_KEY_MS + SI_GAP_MS)

// Each glyph of this half's word, laid out once per session from the font metrics.
typedef struct {
    int16_t  x;      // first word column of the ink
    uint16_t bo;     // bitmap offset
    uint8_t  w, h;   // ink size
    uint8_t  top;    // first word row of the ink (0 = top of the tallest glyph)
} si_gpos_t;

static si_gpos_t      s_g[4];
static int16_t        s_pen0[4];     // pen position before / after each glyph (word columns):
static int16_t        s_pen[4];      // the underscore spans one letter's advance
static uint8_t        s_base;        // baseline row (word rows)
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
            s_pen0[s_ng] = pen;
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
        s_pen0[i]  = (int16_t)(s_pen0[i] - left);
    }
    s_ww   = (int16_t)(right - left);
    s_wh   = (uint8_t)(bottom - top);
    s_base = (uint8_t)(-top);                  // pen y 0 is the baseline
}

// The ink of this half's visible letters (bit i of `mask`), plus the underscore cursor
// in local slot `cur` (-1: none), in word column `wx` as a 64-bit column, bit 0 = the
// word's top row. The underscore sits just below the baseline, under the advance of
// letter `cur`, or after the last letter when cur == s_ng.
static uint64_t si_word_col(int16_t wx, uint8_t mask, int8_t cur) {
    uint64_t col = 0;
    if (cur >= 0) {
        const int16_t x0 = cur < (int8_t)s_ng ? (int16_t)(s_pen0[cur] + 1) : (int16_t)(s_pen[s_ng - 1] + 2);
        const int16_t x1 = cur < (int8_t)s_ng ? (int16_t)(s_pen[cur] - 1) : (int16_t)(x0 + SI_CUR_EXTRA);
        if (wx >= x0 && wx < x1) col = (uint64_t)0x7u << (s_base + 2u);   // 3 px thick
    }
    if (wx < 0 || wx >= s_ww) return col;
    for (uint8_t i = 0; i < s_ng; ++i) {
        if (!(mask & (1u << i))) continue;
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

// What this half shows `u` ms into the cycle: which of its letters (bit mask) and
// where its cursor is (local slot, -1 for none or blinked off).
typedef struct {
    uint8_t mask;
    int8_t  cur;
} si_state_t;

static bool si_blink(uint32_t u) { return ((u / SI_BLINK_MS) & 1u) == 0u; }

// Map the line (slots [from, to) visible, cursor at global slot g, or -1) onto a half.
static si_state_t si_half(bool left, uint8_t from, uint8_t to, int8_t g) {
    const uint8_t first = left ? 0u : (uint8_t)(SI_NL + 1u);
    const uint8_t n     = left ? SI_NL : SI_NR;
    si_state_t    st    = {0, -1};
    for (uint8_t i = 0; i < n; ++i)
        if (first + i >= from && first + i < to) st.mask |= (uint8_t)(1u << i);
    // Left owns slots 0..NL (NL = after the y, where the space goes); right owns the rest.
    if (g >= 0 && (left ? g <= (int8_t)SI_NL : g > (int8_t)SI_NL)) st.cur = (int8_t)(g - (int8_t)first);
    return st;
}

static si_state_t si_state(uint32_t u, bool left) {
    const uint8_t S = SI_SLOTS;
    if (u < SI_WAIT_MS) return si_half(left, 0, 0, si_blink(u) ? 0 : -1);              // 1
    u -= SI_WAIT_MS;
    if (u < S * SI_KEY_MS) {                                                               // 2
        const uint8_t typed = (uint8_t)(u / SI_KEY_MS + 1u);
        return si_half(left, 0, typed, typed < S ? (int8_t)typed : -1);
    }
    u -= S * SI_KEY_MS;
    if (u < SI_DONE_MS) return si_half(left, 0, S, -1);                                  // 3
    u -= SI_DONE_MS;
    if (u < SI_APPEAR_MS) return si_half(left, 0, S, si_blink(u) ? (int8_t)(S - 1u) : -1);   // 4
    u -= SI_APPEAR_MS;
    if (u < (S - 1u) * SI_STEP_MS) return si_half(left, 0, S, (int8_t)(S - 2u - u / SI_STEP_MS));
    u -= (S - 1u) * SI_STEP_MS;
    if (u < SI_PAUSE_MS) return si_half(left, 0, S, si_blink(u) ? 0 : -1);
    u -= SI_PAUSE_MS;
    if (u < S * SI_KEY_MS) {                                                               // 5
        const uint8_t gone = (uint8_t)(u / SI_KEY_MS + 1u);
        return si_half(left, gone, S, gone < S ? (int8_t)gone : -1);
    }
    return si_half(left, 0, 0, -1);                                                        // 6
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
    const uint8_t    msk = st.mask;
    const int8_t     cur = st.cur;
    const bool       any = msk || cur >= 0;
    const uint32_t   tp  = (t * SI_PLASMA_NUM) / SI_PLASMA_DEN;   // the bands' slowed clock
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
        win[k] = any ? (si_word_col((int16_t)(-SI_RING + k - wx0), msk, cur) << wy0) : 0;

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
            const int16_t v = (int16_t)(si_s8(fx * 2 + (int32_t)(tp >> 4)) +
                                        si_s8(y * 3 - (int32_t)(tp / 22u)) +
                                        si_s8(fx + y + (int32_t)(tp / 13u)) +
                                        si_s8(d / 2 + (int32_t)(tp / 9u)));
            if ((((v + 512) >> 4) & 3) == 0) lit |= bit;
        }
        for (uint8_t p = 0; p < SI_H / 8; ++p) buf[(uint16_t)p * SI_W + (uint16_t)x] = (uint8_t)(lit >> (8u * p));
        for (uint8_t k = 0; k < SI_WIN - 1; ++k) win[k] = win[k + 1];
        win[SI_WIN - 1] = any ? (si_word_col((int16_t)(x + 1 + SI_RING - wx0), msk, cur) << wy0) : 0;
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
