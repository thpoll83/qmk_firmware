// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The status panel's idle screen (split72): plasma bands with "Poly Kybd" being typed,
// then a short poem typed across both panels.
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
// rows are never computed, which halves the plasma's cost per frame. ⚠️ Which rows are
// lit swaps every cycle (si_band_parity()): with the even rows always lit, they would
// age faster than the odd ones and leave faint stripes on a panel that idles for hours. Hollow letters and
// scanline bands take the light down. Letters in scanlines were tried first and looked
// too sparse at one row lit in three; a 2 px outline was brighter than it needed to be.
// The idle panel is already at contrast register 0, the SSD1306 floor. Dimming it
// further through the panel's VCOMH or pre-charge registers flickered on hardware, with
// brighter strips, so the light comes off the content.
//
// After "Poly Kybd" is deleted, a short poem (one of SI_POEMS, picked at random each cycle)
// is typed in a smaller face
// (NotoSans_Regular_Base_14pt7b, the resident keycap face, ~10 letters a panel) as one
// line across both panels. Once the line reaches the right edge, every keystroke pushes
// the whole line left by that character's advance, so the text leaves on the left as a
// typewriter's would; letters crossing the physical gap between the panels are simply
// not shown. It is then deleted again with Backspace, from the end, and the start of
// the line slides back in from the left as the line shortens. Its letters are drawn
// like the big ones: a 1 px outline, dark inside, in a 2 px black ring. At this size
// many strokes are only 2 px wide, and their outline is the whole stroke, so only the
// wider parts read as hollow. ⚠️ So the poem's outline is drawn 1 px OUTSIDE the glyph
// instead (the glyph grown by one px in all eight directions, minus the glyph): the
// whole glyph is the dark inside, which keeps even a 2 px stroke hollow, and the ring
// goes around the outline. "Poly Kybd" keeps the outline inside its 24 pt strokes.
//
// Nothing is stored: every frame is computed from the fonts' column bytes in flash and
// Eden's tables; RAM is a handful of statics.
//
// ⚠️ Frame pacing: the bands move everywhere, so nearly all 16 blocks are dirty every
// frame, and QMK sends them over I2C a few per main-loop pass (OLED_UPDATE_PROCESS_LIMIT
// in config.h), blocking the loop ~3 ms per pass and ~23 ms per frame. Three rules:
//   - a new frame is composed only once the previous one has been sent completely
//     (oled_dirty == 0): writing over a half-sent frame showed the top of one frame
//     over the bottom of the other;
//   - at most one frame per SI_FRAME_MS, because every frame costs the main loop its
//     flush and the Eden idle loop on the keycaps runs in that same loop
//     (SI_FRAME_FAST_MS while Eden computes on core1);
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
//   then the poem (phases 6..8, below), and it starts over.
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
#define SI_GAP_MS     3000u    // 8: nothing, before starting over
#define SI_BLINK_MS   530u     // cursor half-period while it waits
#define SI_RING       2        // the black ring's radius, px
// How far a pixel's look-up reaches sideways: the ring, plus the poem's outline, which
// sits 1 px OUTSIDE its glyphs. The window holds the line's columns x-REACH .. x+REACH.
#define SI_REACH      (SI_RING + 1)
#define SI_WIN        (2 * SI_REACH + 1)
// The panel's frame period. 150 ms divides SI_STEP_MS and SI_KEY_MS, so every cursor
// step and keystroke lasts a whole number of frames and the typing stays even.
#define SI_FRAME_MS   150u
// With Eden's keycaps computed on core1 there is no turn-taking, and core0 spends
// ~40 ms of each ~190 ms Eden frame, so the panel can go faster. 75 ms also divides
// SI_STEP_MS and SI_KEY_MS. 50 ms looked smooth here but visibly slowed Eden on
// hardware: each frame still holds core0 ~31 ms (compose + I2C), and Eden's legend
// cut and SPI push wait behind it. Every other idle style keeps SI_FRAME_MS.
#define SI_FRAME_FAST_MS 75u
// Eden waits for our flush at most this long, so a stuck bus cannot freeze the keycaps.
#define SI_HOLD_MAX_MS 100u
// The poem (phases 6 and 7), typed after "Poly Kybd" has been deleted:
//   6. the cursor blinks at the start of the left panel, then the poem is typed a key at
//      a time; after each line break it waits, blinking, as a typist would;
//   7. the last line stands with the cursor blinking after it, then Backspace deletes
//      it a character at a time from the end, at the pace of a held key, and the empty
//      cursor blinks a moment;
//   8. then the cursor goes, a pause, and it starts over.
// 150 ms per key is ~7 keys/s; it and SI_POEM_LINE_MS divide both frame periods.
#define SI_POEM_LEAD_MS 1200u  // 6: cursor blinking before the first key
#define SI_POEM_KEY_MS  150u   // 6: one keystroke
#define SI_POEM_LINE_MS 1200u  // 6: extra wait after a line break
#define SI_POEM_HOLD_MS 4000u  // 7: the end of the poem standing
#define SI_POEM_BS_MS   75u    // 7: one Backspace (a held key's repeat); divides both frame periods
#define SI_POEM_EMPTY_MS 1200u // 7: the cursor blinking on the emptied line
#define SI_POEM_X0      4      // field column the poem starts at
#define SI_POEM_MARGIN  4      // px kept free at the right panel's right edge
#define SI_POEM_NL_SP   3u     // a line break takes the room of this many spaces
// Glyphs one panel's columns (plus the ring's window) can show at once. The narrowest
// glyph in the poem advances 4 px, so 132 columns hold at most 33; a longer run is
// cut at the right edge rather than overflowing (see si_poem_visible()).
#define SI_POEM_VIS_MAX 34u
// The plasma bands run on a slowed clock (5/32 of real time).
#define SI_PLASMA_NUM 5u
#define SI_PLASMA_DEN 32u

static const uint32_t SI_WORD_LEFT[]  = U"Poly";
static const uint32_t SI_WORD_RIGHT[] = U"Kybd";
#define SI_NL    ((uint8_t)(sizeof(SI_WORD_LEFT) / sizeof(SI_WORD_LEFT[0]) - 1u))
#define SI_NR    ((uint8_t)(sizeof(SI_WORD_RIGHT) / sizeof(SI_WORD_RIGHT[0]) - 1u))
#define SI_SP    2u            // cursor stops between the words
#define SI_SLOTS ((uint8_t)(SI_NL + SI_SP + SI_NR))
// Phases 1..5: "Poly Kybd" typed and deleted.
#define SI_NAME_MS (SI_WAIT_MS + SI_SLOTS * SI_KEY_MS + SI_DONE_MS + SI_APPEAR_MS + \
                    (SI_SLOTS - 1u) * SI_STEP_MS + SI_PAUSE_MS + SI_SLOTS * SI_KEY_MS)

// The poems, one picked at random each cycle (si_poem_select()): printable ASCII (the
// face covers 0x20..0x7E) and '\n' for a line break. They are const, so they live in
// flash and cost no RAM; each one adds only its entry in s_ptype_ms.
static const char SI_POEM_0[] =
    "Every key knows its letter,\n"
    "every letter finds its key.\n"
    "Type a word, then type a better -\n"
    "the keyboard waits for me.";
static const char SI_POEM_1[] =
    "Seventy-two small screens,\n"
    "each one a tiny page.\n"
    "Press one, and watch it change -\n"
    "a stage upon a stage.";
static const char SI_POEM_2[] =
    "Soft clicks in the evening,\n"
    "letters falling into line.\n"
    "One key, then another,\n"
    "until the words are mine.";
_Static_assert(sizeof(SI_POEM_0) - 1u <= 255u, "a poem is indexed by a uint8_t");
_Static_assert(sizeof(SI_POEM_1) - 1u <= 255u, "a poem is indexed by a uint8_t");
_Static_assert(sizeof(SI_POEM_2) - 1u <= 255u, "a poem is indexed by a uint8_t");
static const char SI_POEM_3[] =
    "A thousand words a day,\n"
    "and not one of them mine.\n"
    "I only hold the letters\n"
    "until you make them shine.";
static const char SI_POEM_4[] =
    "When the room is quiet\n"
    "and the cursor stops to rest,\n"
    "I dream in little letters -\n"
    "the short words are the best.";
_Static_assert(sizeof(SI_POEM_3) - 1u <= 255u, "a poem is indexed by a uint8_t");
_Static_assert(sizeof(SI_POEM_4) - 1u <= 255u, "a poem is indexed by a uint8_t");
static const char *const SI_POEMS[] = {SI_POEM_0, SI_POEM_1, SI_POEM_2, SI_POEM_3, SI_POEM_4};
_Static_assert(sizeof(SI_POEMS) / sizeof(SI_POEMS[0]) >= 2u, "si_poem_select() needs two poems");
#define SI_POEM_N ((uint8_t)(sizeof(SI_POEMS) / sizeof(SI_POEMS[0])))
// Defined in poly_keymap.c's translation unit (gfx_used_fonts.h, RESIDENT_FONTS);
// extern here so its PROGMEM tables are not linked twice (see oled_helper.c).
extern const GFXfont NotoSans_Regular_Base_14pt7b;

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

// The poem's layout, measured once per session: its baseline below the tallest
// letter's top, the cursor's width, how long typing it takes, and the whole cycle.
static const uint8_t *s_pbitmap;
static uint8_t        s_pbase;
static uint8_t        s_pcur_w;
static uint32_t       s_ptype_ms[SI_POEM_N];   // how long typing each poem takes
static uint32_t       s_cycle_ms;              // fixed: set by the longest poem
// The poem this cycle (si_poem_select()).
static const char    *s_poem;
static uint8_t        s_plen;
static uint32_t       s_ptype;

// One poem glyph in view this frame, in field columns before the scroll is applied.
typedef struct {
    int16_t  x;
    uint16_t bo;
    uint8_t  w, h, top;
} si_pglyph_t;
static si_pglyph_t s_pvis[SI_POEM_VIS_MAX];
static uint8_t     s_pnvis;
static int16_t     s_pcur_x;       // the cursor's first column, -1 for none

static uint32_t s_t0;
static uint16_t s_frames;         // frames composed since the last console report
static uint32_t s_last_call;
static uint32_t s_last_frame;     // when the last frame was composed
static bool     s_started;
static uint16_t s_session;        // idle sessions since boot: seeds the poem pick
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

// Column `gx` of a glyph as a 64-bit column, bit 0 = the glyph's top row.
static uint64_t si_glyph_col(const uint8_t *bitmap, uint16_t bo, uint8_t h, uint8_t gx) {
    const uint8_t  cb = glyph_col_bytes(h);
    const uint8_t *p  = bitmap + bo + (uint16_t)gx * cb;
    uint64_t v = 0;
    for (uint8_t b = 0; b < cb; ++b) v |= (uint64_t)pgm_read_byte(p + b) << (8u * b);
    if (h < 64) v &= ((uint64_t)1 << h) - 1u;
    return v;
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
        col |= si_glyph_col(s_bitmap, g->bo, g->h, (uint8_t)gx) << g->top;
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
    return (si_state_t){0, 0, 0, -1};                                                       // 5 done
}

// --- the poem --------------------------------------------------------------

static const GFXglyph *si_poem_glyph(char c) {
    const GFXfont *const face[] = {&NotoSans_Regular_Base_14pt7b};
    const GFXfont        *font  = NULL;
    const GFXglyph       *g     = kdisp_gfx_glyph_font(face, 1, (uint32_t)(uint8_t)c, &font);
    if (g != NULL) s_pbitmap = pgm_read_bitmap_ptr(font);
    return g;
}

// How far the pen moves for one character; a line break is SI_POEM_NL_SP spaces.
static uint8_t si_poem_advance(char c) {
    const GFXglyph *g = si_poem_glyph(c == '\n' ? ' ' : c);
    const uint8_t   a = g ? glyph_x_advance(g) : 0u;
    return c == '\n' ? (uint8_t)(a * SI_POEM_NL_SP) : a;
}

// The time from one keystroke to the next, after typing `c`.
static uint32_t si_poem_key_ms(char c) { return SI_POEM_KEY_MS + (c == '\n' ? SI_POEM_LINE_MS : 0u); }

// Measure every poem once per session. They share one baseline (the tallest letter of
// any poem) so the line sits at the same height whichever is typed, and one cycle
// length (the longest poem's), so the cycle arithmetic stays a plain modulo; a shorter
// poem just leaves a longer blank pause before "Poly Kybd" starts again.
static void si_poem_layout(void) {
    int8_t   top     = 0;
    uint32_t longest = 0;
    for (uint8_t p = 0; p < SI_POEM_N; ++p) {
        uint32_t type = 0;
        uint8_t  len  = 0;
        for (const char *c = SI_POEMS[p]; *c; ++c, ++len) {
            type += si_poem_key_ms(*c);
            if (*c == ' ' || *c == '\n') continue;
            const GFXglyph *g = si_poem_glyph(*c);
            if (g != NULL && glyph_y_offset(g) < top) top = glyph_y_offset(g);
        }
        s_ptype_ms[p] = type;
        const uint32_t all = type + (uint32_t)len * SI_POEM_BS_MS;   // typed, then deleted
        if (all > longest) longest = all;
    }
    s_pbase  = (uint8_t)(-top);
    const uint8_t n = si_poem_advance('n');
    s_pcur_w = (uint8_t)(n > 2u ? n - 2u : n);
    s_cycle_ms = SI_NAME_MS + SI_POEM_LEAD_MS + longest + SI_POEM_HOLD_MS + SI_POEM_EMPTY_MS + SI_GAP_MS;
}

// A 32-bit mix of two numbers (a multiply-xorshift hash), for the poem pick.
static uint32_t si_hash(uint32_t a, uint32_t b) {
    uint32_t x = a * 0x9E3779B1u ^ b * 0x85EBCA6Bu;
    x ^= x >> 15;
    x *= 0x2C1B3C6Du;
    x ^= x >> 12;
    return x;
}

// Pick the poem for the cycle `t` falls in: at random, and never the one just typed.
// Each cycle steps 1..N-1 poems on from the last, the step drawn from a hash of the
// session and the cycle, so the sequence is a pure function of those two numbers.
// ⚠️ That is what keeps the halves in step without a sync field: both run the same
// cycle clock, and both count idle sessions, because both enter idle together through
// the synced DISP_IDLE flag. A half rebooted on its own would count differently and
// type a different poem than its partner until the next boot; nothing worse.
// The walk from the session's first cycle is a few dozen steps at most, since the
// panels turn off long before then.
static void si_poem_select(uint32_t t) {
    const uint32_t cycle = t / s_cycle_ms;
    uint8_t        p     = (uint8_t)(si_hash(s_session, 0xFFFFFFFFu) % SI_POEM_N);
    for (uint32_t c = 0; c <= cycle; ++c)
        p = (uint8_t)((p + 1u + si_hash(s_session, c) % (SI_POEM_N - 1u)) % SI_POEM_N);
    s_poem  = SI_POEMS[p];
    s_ptype = s_ptype_ms[p];
    s_plen  = 0;
    while (s_poem[s_plen] != '\0') ++s_plen;
}

// Work out what of the poem shows `u` ms into phase 6, for the panel whose field column
// 0 is `fx0`: the glyphs in view go to s_pvis, the cursor's first column to s_pcur_x.
// Returns false when nothing of it shows anywhere (phase 8).
static bool si_poem_visible(uint32_t u, int16_t fx0) {
    s_pnvis  = 0;
    s_pcur_x = -1;
    uint8_t typed;
    bool    cursor;
    if (u < SI_POEM_LEAD_MS) {
        typed  = 0;
        cursor = si_blink(u);
    } else if ((u -= SI_POEM_LEAD_MS) < s_ptype) {
        // Character i appears at the sum of the keystroke times before it.
        uint32_t at = 0, last = 0;
        typed = 0;
        while (typed < s_plen && at <= u) {
            last = at;
            at += si_poem_key_ms(s_poem[typed]);
            ++typed;
        }
        // Steady while keys are coming, blinking while the typist waits at a line break.
        const uint32_t since = u - last;
        cursor = since < SI_POEM_KEY_MS || si_blink(since - SI_POEM_KEY_MS);
    } else if ((u -= s_ptype) < SI_POEM_HOLD_MS) {
        typed  = s_plen;
        cursor = si_blink(u);
    } else if ((u -= SI_POEM_HOLD_MS) < (uint32_t)s_plen * SI_POEM_BS_MS) {
        // Backspace: the first press takes the last character at once.
        typed  = (uint8_t)(s_plen - 1u - u / SI_POEM_BS_MS);
        cursor = true;
    } else if ((u -= (uint32_t)s_plen * SI_POEM_BS_MS) < SI_POEM_EMPTY_MS) {
        typed  = 0;
        cursor = si_blink(u);
    } else {
        return false;
    }

    int16_t pen = SI_POEM_X0;
    for (uint8_t i = 0; i < typed; ++i) pen = (int16_t)(pen + si_poem_advance(s_poem[i]));
    // Keep the cursor inside the right panel: once the line reaches it, the whole line
    // moves left by what each new key adds, and the start leaves on the left.
    const int16_t right = (int16_t)(SI_RIGHT_X0 + SI_W - SI_POEM_MARGIN - s_pcur_w - 1);
    const int16_t shift = pen > right ? (int16_t)(pen - right) : 0;

    pen = (int16_t)(SI_POEM_X0 - shift);
    for (uint8_t i = 0; i < typed; ++i) {
        const char c = s_poem[i];
        if (c != ' ' && c != '\n') {
            const GFXglyph *g = si_poem_glyph(c);
            if (g != NULL) {
                const int16_t x = (int16_t)(pen + glyph_x_offset(g));
                const uint8_t w = glyph_width(g);
                if (x + w > fx0 - SI_REACH && x < fx0 + SI_W + SI_REACH && s_pnvis < SI_POEM_VIS_MAX)
                    s_pvis[s_pnvis++] = (si_pglyph_t){.x = x, .bo = glyph_bitmap_offset(g), .w = w,
                                                      .h = glyph_height(g),
                                                      .top = (uint8_t)(glyph_y_offset(g) + s_pbase)};
            }
        }
        pen = (int16_t)(pen + si_poem_advance(c));
    }
    if (cursor) s_pcur_x = (int16_t)(pen + 1);
    return true;
}

// The poem in field column `fx`, bit 0 = the tallest letter's top, with the cursor
// 3 px thick just below the baseline.
static uint64_t si_poem_col(int16_t fx) {
    uint64_t col = 0;
    if (s_pcur_x >= 0 && fx >= s_pcur_x && fx < s_pcur_x + s_pcur_w) col = (uint64_t)0x7u << (s_pbase + 2u);
    for (uint8_t k = 0; k < s_pnvis; ++k) {
        const si_pglyph_t *g  = &s_pvis[k];
        const int16_t      gx = (int16_t)(fx - g->x);
        if (gx < 0 || gx >= g->w) continue;
        col |= si_glyph_col(s_pbitmap, g->bo, g->h, (uint8_t)gx) << g->top;
    }
    return col;
}

// Which scanlines the bands use this cycle: even rows on one cycle, odd on the next, so
// both age alike. The swap lands at the cycle's start, where only the bands show.
static uint8_t si_band_parity(uint32_t t) { return (uint8_t)((t / s_cycle_ms) & 1u); }

// A column grown by `k` px up and down.
static uint64_t si_spread(uint64_t v, uint8_t k) {
    uint64_t r = v;
    for (uint8_t i = 1; i <= k; ++i) r |= (v << i) | (v >> i);
    return r;
}

// Column `fx` of whichever line the cycle is on.
static uint64_t si_col(int16_t fx, const si_state_t *st, bool poem) {
    return poem ? si_poem_col(fx) : si_line_col(fx, st->from, st->to, st->shift, st->cur);
}

static uint32_t si_frame_ms(void) {
    return startup_anim_idle_on_core1() ? SI_FRAME_FAST_MS : SI_FRAME_MS;
}

void status_idle_screen(void) {
    const uint32_t now = timer_read32();
    if (!s_started || timer_elapsed32(s_last_call) > 500u) {   // a new idle session
        s_started    = true;
        ++s_session;
        s_t0         = now;
        s_last_frame = now - SI_FRAME_MS;   // the first frame is due at once
        si_layout();
        si_poem_layout();
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
    if (timer_elapsed32(s_last_frame) < si_frame_ms()) return;
    if (startup_anim_frame_busy()) return;   // Eden is mid-frame: let it finish first
    const uint32_t now = timer_read32();
    s_last_frame = now;
    ++s_frames;

    const uint32_t t_start = timer_read32();
    const uint32_t t       = timer_elapsed32(s_t0);
    const int16_t  fx0     = is_left_side() ? 0 : SI_RIGHT_X0;   // this panel's field column 0

    const uint32_t   u      = t % s_cycle_ms;
    const bool       poem   = u >= SI_NAME_MS;   // phases 6..8
    const si_state_t st     = poem ? (si_state_t){0, 0, 0, -1} : si_state(u);
    if (poem) si_poem_select(t);
    const bool       any    = poem ? si_poem_visible(u - SI_NAME_MS, fx0) : (st.to > st.from || st.cur >= 0);
    const uint8_t    parity = si_band_parity(t);
    const uint32_t   tp     = (t * SI_PLASMA_NUM) / SI_PLASMA_DEN;   // the bands' slowed clock
    // Vertically centred on the letter BODY — the tallest letter's top to the baseline —
    // not the ink box: the y's descender (and the underscore) hang below, as text does.
    // Centring the whole ink box put the letters visibly high.
    const uint8_t    wy0 = (uint8_t)((SI_H - (poem ? s_pbase : s_base)) / 2);

    kdisp_set_buffer(0);
    uint8_t *buf = get_scratch_buffer();

    // A seven-column window of the line's columns (x-3 .. x+3, centre win[3]) for the
    // outline and the black ring. The ring is the shape grown by a radius-2 disc
    // (offsets with dx*dx + dy*dy <= 4), minus the shape.
    // ⚠️ Deliberately ONLY the ring. Closing the gaps between letters as well (any pixel
    // with ink within N px on both sides) painted solid black wedges between them — a
    // shadow, most visibly between K and y — so the bands show through the gaps and the
    // counters, as they should.
    uint64_t win[SI_WIN];
    for (int8_t k = 0; k < SI_WIN; ++k)
        win[k] = any ? (si_col((int16_t)(fx0 - SI_REACH + k), &st, poem) << wy0) : 0;

    for (int16_t x = 0; x < SI_W; ++x) {
        const uint64_t ink = win[3];
        // `shape` is what the letter covers (outline and dark inside), `edge` the lit
        // outline in it, `ring` the black ring around it.
        uint64_t shape, edge, ring;
        if (poem) {
            // The glyph grown by 1 px in all eight directions; the outline is the growth.
            shape = si_spread(win[2], 1) | si_spread(ink, 1) | si_spread(win[4], 1);
            edge  = shape & ~ink;
            // A radius-2 disc around that square growth: 3 px up and down within one
            // column of x, 2 px at two columns, 1 px at three.
            ring = (si_spread(win[2], 3) | si_spread(ink, 3) | si_spread(win[4], 3) |
                    si_spread(win[1], 2) | si_spread(win[5], 2) |
                    si_spread(win[0], 1) | si_spread(win[6], 1)) & ~shape;
        } else {
            // The letter shrunk by 1 px: ink whose four neighbours are all ink. What is
            // left of the letter after taking that away is its 1 px outline.
            const uint64_t core = ink & (ink << 1) & (ink >> 1) & win[2] & win[4];
            shape = ink;
            edge  = ink & ~core;
            ring  = si_spread(ink, 2) | si_spread(win[2], 1) | si_spread(win[4], 1) | win[1] | win[5];
        }
        const int16_t fx = (int16_t)(fx0 + x);
        uint64_t      lit = 0;
        for (uint8_t y = 0; y < SI_H; ++y) {
            const uint64_t bit  = (uint64_t)1 << y;
            if (shape & bit) {   // the letter: a 1 px outline, dark inside
                if (edge & bit) lit |= bit;
                continue;
            }
            if (ring & bit) continue;                   // the 2 px black ring
            if ((y & 1u) != parity) continue;           // the bands: one row in two
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
        win[SI_WIN - 1] = any ? (si_col((int16_t)(fx + 1 + SI_REACH), &st, poem) << wy0) : 0;
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
