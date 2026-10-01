// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The status panel's idle screen (split72): plasma bands with "Poly Kybd" being typed.
//
// The background is a classic demoscene plasma — a sum of four sines of Eden's sine
// table, one of them fed by a distance from the centre — drawn as CONTOUR BANDS rather
// than dithered, because on a 1-bit panel a dithered plasma reads as grey noise while
// its contour lines are native. Both panels are one field: the right panel continues
// the left one after the physical gap, so the bands flow across the keyboard.
//
// Over it, "Poly Kybd" in FreeSansBold24pt7b — the face Eden writes its keycap letters
// in — is ONE LINE of text across both panels ("Poly" centred on the left, "Kybd" on
// the right), typed and then edited away with an underscore cursor (see SI_KEY_MS).
// Every letter, and the cursor, is drawn as a 1 px outline (the ink minus the ink shrunk
// by 1 px), dark inside, and cut out of the bands by a 2 px black ring (the shape grown
// by a radius-2 disc) and nothing more: the bands keep flowing between the letters and
// through the counters. The bands are scanlines (one row lit, one dark), and the dark
// rows are never computed, which halves the plasma's cost per frame. Hollow letters and
// scanline bands take the light down. Letters in scanlines were tried first and looked
// too sparse at one row lit in three; a 2 px outline was brighter than it needed to be.
// The idle panel is already at contrast register 0, the SSD1306 floor. Dimming it
// further through the panel's VCOMH or pre-charge registers flickered on hardware, with
// brighter strips, so the light comes off the content.
//
// Nothing is stored: every frame is computed from the font's column bytes in flash and
// Eden's tables; RAM is a handful of statics.
//
// ⚠️ Frame pacing: the bands move everywhere, so nearly all 16 blocks are dirty every
// frame, and QMK sends them over I2C a few per main-loop pass (OLED_UPDATE_PROCESS_LIMIT
// in config.h), blocking the loop ~6 ms per pass and ~23 ms per frame. Three rules:
//   - a new frame is composed only once the previous one has been sent completely
//     (oled_dirty == 0): writing over a half-sent frame showed the top of one frame
//     over the bottom of the other;
//   - at most one frame per SI_FRAME_MS, because every frame costs the main loop its
//     flush and the Eden idle loop on the keycaps runs in that same loop;
//   - the panel and Eden TAKE TURNS: no frame is composed while Eden is part-way
//     through a keycap frame, and Eden starts no frame while ours is still being sent
//     (status_idle_holds_bus()). Interleaved, each 3 ms Eden slice waited behind ~6 ms
//     of I2C, stretching its frames two- to threefold.
// So the frames are composed from status_idle_task(), every main-loop pass, rather than
// from oled_task_user(), which runs only every OLED_UPDATE_INTERVAL (66 ms) and would
// rarely land in Eden's short gap between frames. oled_task_user() only says the panel
// is ours (status_idle_screen()) or not (status_idle_release()).
// All motion is a function of time, so pacing lowers the frame rate, never the speed.
// oled_render_dirty(true) would instead block the matrix scan ~26 ms per frame.

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
#include "state.h"              // get_local_state
#include "base/com.h"           // DISP_IDLE

#define SI_W 128
#define SI_H 64
extern OLED_BLOCK_TYPE oled_dirty;   // drivers/oled/oled_driver.c: blocks not yet sent

// The two panels as one field: the right one starts after the physical gap.
#define SI_GAP_PX 40
#define SI_RIGHT_X0 (SI_W + SI_GAP_PX)
#define SI_FIELD_CX ((2 * SI_W + SI_GAP_PX) / 2)
#define SI_FIELD_CY (SI_H / 2)

// The line is typed and edited away the way a person would:
//   1. the cursor blinks under the P's place, nothing written yet;
//   2. "Poly Kybd" is typed a key at a time, the cursor under the next character's place;
//      the gap between the words is TWO spaces for the cursor — one stop after the y,
//      one at the start of the right panel — so it does not leap the whole gap in one
//      keystroke; the key that completes the line takes the cursor away;
//   3. the finished text stands on its own;
//   4. the cursor comes back under the d, blinks, and walks back to the P while the
//      text stays;
//   5. Del, a key at a time: the character under the cursor goes and the rest of the
//      line moves LEFT to close the gap, as in an editor — so "Kybd" slides across the
//      physical gap into the left panel as the line empties;
//   6. the cursor goes, a pause, and it starts over.
// Both halves run the same timeline from the same idle-session clock, and each lays
// out the WHOLE line in field columns, since letters cross from one panel to the other.
// Slots number the line: 0..NL-1 the left letters, NL..NL+SP-1 the spaces, the right
// letters after them.
// ⚠️ The place AFTER the d is never used, on purpose: "Kybd" is 112 px of ink, so
// centred it leaves the underscore 4 px of panel. The cursor leaves as the line
// completes and comes back ON the d, which keeps both words exactly centred.
#define SI_KEY_MS     300u     // one keystroke: letter, space or Del
#define SI_STEP_MS    150u     // one cursor step while walking back
#define SI_WAIT_MS    4000u    // 1: cursor blinking before typing
#define SI_DONE_MS    5000u    // 3: finished text, no cursor
#define SI_APPEAR_MS  1200u    // 4: cursor back on the d, blinking, before it walks
#define SI_PAUSE_MS   800u     // 4: cursor blinking under the P before the first Del
#define SI_GAP_MS     3000u    // 6: nothing, before starting over
#define SI_BLINK_MS   530u     // cursor half-period while it waits
#define SI_RING       2        // the black ring's radius, px
#define SI_WIN        (2 * SI_RING + 1)
// The panel's frame period. 150 ms divides SI_STEP_MS and SI_KEY_MS, so every cursor
// step and keystroke lasts a whole number of frames and the typing stays even.
#define SI_FRAME_MS   150u
// Eden waits for our flush at most this long, so a stuck bus cannot freeze the keycaps.
#define SI_HOLD_MAX_MS 100u
// The plasma bands run on a slowed clock (5/32 of real time).
#define SI_PLASMA_NUM 5u
#define SI_PLASMA_DEN 32u

static const uint32_t SI_WORD_LEFT[]  = U"Poly";
static const uint32_t SI_WORD_RIGHT[] = U"Kybd";
#define SI_NL    ((uint8_t)(sizeof(SI_WORD_LEFT) / sizeof(SI_WORD_LEFT[0]) - 1u))
#define SI_NR    ((uint8_t)(sizeof(SI_WORD_RIGHT) / sizeof(SI_WORD_RIGHT[0]) - 1u))
#define SI_SP    2u            // cursor stops between the words
#define SI_SLOTS ((uint8_t)(SI_NL + SI_SP + SI_NR))
#define SI_CYCLE_MS (SI_WAIT_MS + SI_SLOTS * SI_KEY_MS + SI_DONE_MS + SI_APPEAR_MS + \
                     (SI_SLOTS - 1u) * SI_STEP_MS + SI_PAUSE_MS + SI_SLOTS * SI_KEY_MS + SI_GAP_MS)

// One slot of the line, laid out once per session, in FIELD columns (x) and in rows
// relative to the top of the tallest letter (top).
typedef struct {
    int16_t  x;          // first field column of the ink
    int16_t  pen0, pen1; // pen before / after: the underscore spans this advance
    uint16_t bo;         // bitmap offset (0 with w == 0: a space)
    uint8_t  w, h;       // ink size
    uint8_t  top;        // first row of the ink below the tallest letter's top
} si_slot_t;

static si_slot_t      s_slot[SI_SLOTS];
static uint8_t        s_base;        // baseline row, below the tallest letter's top
static const uint8_t *s_bitmap;

static uint32_t s_t0;
static uint16_t s_frames;         // frames composed since the last console report
static uint32_t s_last_call;
static uint32_t s_last_frame;     // when the last frame was composed
static bool     s_started;
static bool     s_owned;          // oled_task_user() gave the panel to the idle screen
static uint8_t  s_worst_ms;
static uint32_t s_next_log;

static inline int16_t si_s8(int32_t t) { return (int16_t)startup_anim_sin((uint8_t)t) - 128; }

// Lay one word out into slots [first, first + n), centred on the panel whose field
// column 0 is `panel_x0`. Returns the lowest yOffset (the tallest letter's top).
static int8_t si_layout_word(const uint32_t *text, uint8_t first, int16_t panel_x0) {
    const GFXfont *const face[] = {poly_heavy_font()};
    int16_t pen = 0, left = 32767, right = -32767;
    int8_t  top = 127;
    uint8_t i   = first;
    for (const uint32_t *c = text; *c; ++c, ++i) {
        const GFXfont  *font = NULL;
        const GFXglyph *g    = kdisp_gfx_glyph_font(face, 1, *c, &font);
        if (g == NULL) continue;
        s_bitmap = pgm_read_bitmap_ptr(font);
        const int8_t yo = glyph_y_offset(g);
        s_slot[i] = (si_slot_t){.x = (int16_t)(pen + glyph_x_offset(g)), .pen0 = pen,
                                .pen1 = (int16_t)(pen + glyph_x_advance(g)),
                                .bo = glyph_bitmap_offset(g), .w = glyph_width(g),
                                .h = glyph_height(g), .top = (uint8_t)yo};
        if (s_slot[i].x < left) left = s_slot[i].x;
        if (s_slot[i].x + s_slot[i].w > right) right = (int16_t)(s_slot[i].x + s_slot[i].w);
        if (yo < top) top = yo;
        pen = s_slot[i].pen1;
    }
    const int16_t dx = (int16_t)(panel_x0 + (SI_W - (right - left)) / 2 - left);   // centre the ink
    for (uint8_t k = first; k < i; ++k) {
        s_slot[k].x    = (int16_t)(s_slot[k].x + dx);
        s_slot[k].pen0 = (int16_t)(s_slot[k].pen0 + dx);
        s_slot[k].pen1 = (int16_t)(s_slot[k].pen1 + dx);
    }
    return top;
}

static void si_layout(void) {
    const int8_t tl  = si_layout_word(SI_WORD_LEFT, 0, 0);
    const int8_t tr  = si_layout_word(SI_WORD_RIGHT, (uint8_t)(SI_NL + SI_SP), SI_RIGHT_X0);
    const int8_t top = tl < tr ? tl : tr;
    for (uint8_t k = 0; k < SI_SLOTS; ++k)
        if (k < SI_NL || k >= SI_NL + SI_SP) s_slot[k].top = (uint8_t)((int8_t)s_slot[k].top - top);
    // The spaces share the run from the pen after the y to the pen before the K equally.
    const int16_t a = s_slot[SI_NL - 1].pen1, b = s_slot[SI_NL + SI_SP].pen0;
    for (uint8_t k = 0; k < SI_SP; ++k) {
        const int16_t p0 = (int16_t)(a + (b - a) * k / SI_SP), p1 = (int16_t)(a + (b - a) * (k + 1) / SI_SP);
        s_slot[SI_NL + k] = (si_slot_t){.x = p0, .pen0 = p0, .pen1 = p1, .bo = 0, .w = 0, .h = 0, .top = 0};
    }
    s_base = (uint8_t)(-top);                  // pen y 0 is the baseline
}

// The line in field column `fx` as a 64-bit column, bit 0 = the tallest letter's top:
// slots [from, to) drawn `shift` px to the left, plus the underscore under slot `cur`
// (-1: none), 5 px thick just below the baseline.
static uint64_t si_line_col(int16_t fx, uint8_t from, uint8_t to, int16_t shift, int8_t cur) {
    uint64_t col = 0;
    fx = (int16_t)(fx + shift);
    if (cur >= 0) {
        // Under a letter: its advance. Under a space: the space's run, which lies partly
        // in the gap between the panels, so the second stop shows at the right panel's
        // left edge.
        const si_slot_t *c  = &s_slot[cur];
        const int16_t    x0 = (int16_t)(c->pen0 + (c->w ? 1 : 2));
        const int16_t    x1 = (int16_t)(c->pen1 - (c->w ? 1 : 2));
        if (fx >= x0 && fx < x1) col = (uint64_t)0x1Fu << (s_base + 2u);   // 5 px thick
    }
    for (uint8_t i = from; i < to; ++i) {
        const si_slot_t *g  = &s_slot[i];
        const int16_t    gx = (int16_t)(fx - g->x);
        if (gx < 0 || gx >= g->w) continue;
        const uint8_t  cb = glyph_col_bytes(g->h);
        const uint8_t *p  = s_bitmap + g->bo + (uint16_t)gx * cb;
        uint64_t v = 0;
        for (uint8_t b = 0; b < cb; ++b) v |= (uint64_t)pgm_read_byte(p + b) << (8u * b);
        if (g->h < 64) v &= ((uint64_t)1 << g->h) - 1u;
        col |= v << g->top;
    }
    return col;
}

// The line `u` ms into the cycle: the slots showing, how far they are pushed left (Del
// closes the gap), and the cursor's slot (-1 for none or blinked off).
typedef struct {
    uint8_t from, to;
    int16_t shift;
    int8_t  cur;
} si_state_t;

static bool si_blink(uint32_t u) { return ((u / SI_BLINK_MS) & 1u) == 0u; }

static si_state_t si_state(uint32_t u) {
    const uint8_t S = SI_SLOTS;
    if (u < SI_WAIT_MS) return (si_state_t){0, 0, 0, si_blink(u) ? 0 : -1};              // 1
    u -= SI_WAIT_MS;
    if (u < S * SI_KEY_MS) {                                                                // 2
        const uint8_t typed = (uint8_t)(u / SI_KEY_MS + 1u);
        return (si_state_t){0, typed, 0, typed < S ? (int8_t)typed : -1};
    }
    u -= S * SI_KEY_MS;
    if (u < SI_DONE_MS) return (si_state_t){0, S, 0, -1};                                 // 3
    u -= SI_DONE_MS;
    if (u < SI_APPEAR_MS) return (si_state_t){0, S, 0, si_blink(u) ? (int8_t)(S - 1u) : -1};   // 4
    u -= SI_APPEAR_MS;
    if (u < (S - 1u) * SI_STEP_MS) return (si_state_t){0, S, 0, (int8_t)(S - 2u - u / SI_STEP_MS)};
    u -= (S - 1u) * SI_STEP_MS;
    if (u < SI_PAUSE_MS) return (si_state_t){0, S, 0, si_blink(u) ? 0 : -1};
    u -= SI_PAUSE_MS;
    if (u < S * SI_KEY_MS) {                                                                // 5
        // Del: the first `gone` characters are removed and the rest closes up, so slot
        // `gone` now stands where the P stood — and so does the cursor.
        const uint8_t gone = (uint8_t)(u / SI_KEY_MS + 1u);
        if (gone >= S) return (si_state_t){0, 0, 0, -1};
        return (si_state_t){gone, S, (int16_t)(s_slot[gone].pen0 - s_slot[0].pen0), (int8_t)gone};
    }
    return (si_state_t){0, 0, 0, -1};                                                       // 6
}

void status_idle_screen(void) {
    const uint32_t now = timer_read32();
    if (!s_started || timer_elapsed32(s_last_call) > 500u) {   // a new idle session
        s_started    = true;
        s_t0         = now;
        s_last_frame = now - SI_FRAME_MS;   // the first frame is due at once
        si_layout();
    }
    s_last_call = now;
    s_owned     = true;
}

void status_idle_release(void) { s_owned = false; }

bool status_idle_holds_bus(void) {
    return s_owned && oled_dirty && timer_elapsed32(s_last_frame) < SI_HOLD_MAX_MS;
}

void status_idle_task(void) {
    // Also re-check the idle flag: a wake between two oled_task_user() calls must not
    // compose one more frame over the screen that is about to replace this one.
    if (!s_owned || (get_local_state()->flags & DISP_IDLE) == 0) return;
    if (oled_dirty) return;   // the previous frame is still going out over I2C
    if (timer_elapsed32(s_last_frame) < SI_FRAME_MS) return;
    if (startup_anim_frame_busy()) return;   // Eden is mid-frame: let it finish first
    const uint32_t now = timer_read32();
    s_last_frame = now;
    ++s_frames;

    const uint32_t t_start = timer_read32();
    const uint32_t t       = timer_elapsed32(s_t0);
    const int16_t  fx0     = is_left_side() ? 0 : SI_RIGHT_X0;   // this panel's field column 0

    const si_state_t st  = si_state(t % SI_CYCLE_MS);
    const bool       any = st.to > st.from || st.cur >= 0;
    const uint32_t   tp  = (t * SI_PLASMA_NUM) / SI_PLASMA_DEN;   // the bands' slowed clock
    // Vertically centred on the letter BODY — the tallest letter's top to the baseline —
    // not the ink box: the y's descender (and the underscore) hang below, as text does.
    // Centring the whole ink box put the letters visibly high.
    const uint8_t    wy0 = (uint8_t)((SI_H - s_base) / 2);

    kdisp_set_buffer(0);
    uint8_t *buf = get_scratch_buffer();

    // A five-column window of the line's columns (x-2 .. x+2) for the black ring: the
    // shape grown by a radius-2 disc (offsets with dx*dx + dy*dy <= 4), minus the ink.
    // ⚠️ Deliberately ONLY the ring. Closing the gaps between letters as well (any pixel
    // with ink within N px on both sides) painted solid black wedges between them — a
    // shadow, most visibly between K and y — so the bands show through the gaps and the
    // counters, as they should.
    uint64_t win[SI_WIN];
    for (int8_t k = 0; k < SI_WIN; ++k)
        win[k] = any ? (si_line_col((int16_t)(fx0 - SI_RING + k), st.from, st.to, st.shift, st.cur) << wy0) : 0;

    for (int16_t x = 0; x < SI_W; ++x) {
        const uint64_t ink  = win[2];
        const uint64_t ring = (ink << 1) | (ink << 2) | (ink >> 1) | (ink >> 2) |
                              win[1] | (win[1] << 1) | (win[1] >> 1) |
                              win[3] | (win[3] << 1) | (win[3] >> 1) |
                              win[0] | win[4];
        // The letter shrunk by 1 px: ink whose four neighbours are all ink. What is left
        // of the letter after taking that away is its 1 px outline.
        const uint64_t core = ink & (ink << 1) & (ink >> 1) & win[1] & win[3];
        const uint64_t edge = ink & ~core;
        const int16_t fx = (int16_t)(fx0 + x);
        uint64_t      lit = 0;
        for (uint8_t y = 0; y < SI_H; ++y) {
            const uint64_t bit  = (uint64_t)1 << y;
            if (ink & bit) {   // the letter: a 1 px outline, dark inside
                if (edge & bit) lit |= bit;
                continue;
            }
            if (ring & bit) continue;                   // the 2 px black ring
            if (y & 1u) continue;                       // the bands: even panel rows only
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
        win[SI_WIN - 1] = any ? (si_line_col((int16_t)(fx + 1 + SI_RING), st.from, st.to, st.shift, st.cur) << wy0) : 0;
    }

    const uint32_t took = timer_elapsed32(t_start);
    if (took > s_worst_ms) s_worst_ms = (uint8_t)(took > 255u ? 255u : took);
    if ((int32_t)(now - s_next_log) >= 0) {
        uprintf("Status idle: %u frames/5s, worst compose %ums\n", s_frames, s_worst_ms);
        s_frames   = 0;
        s_worst_ms = 0;
        s_next_log = now + 5000u;
    }
    oled_write_raw((char *)get_scratch_buffer(), get_scratch_buffer_size());
}
