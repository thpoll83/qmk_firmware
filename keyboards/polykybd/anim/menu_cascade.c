// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
// The menu cascade — see anim/menu_cascade.h.
#include "menu_cascade.h"

#if defined(KEYBOARD_polykybd_split72)

#include "quantum.h"
#include "base/disp_array.h"
#include "base/shift_reg.h"
#include "base/tutorial_plan.h"     // TUT_SLOT_*, tut_fade_contrast()
#include "side.h"
#include QMK_KEYBOARD_H             // get_key_disp_bitmask
#include "startup_anim.h"           // startup_anim_key_geom / startup_anim_board_w
#include "menu_cascade_rows.h"      // CASC_ROW_* (tools/gen_cascade_rows.py)
#include "tutorial.h"               // tutorial_slot_at()
#include "focus_ring.h"             // poly_focus_draw_legend()
#include "base/update.h"            // request_disp_refresh()

// Round 34: 20% faster than round 33's 1800/360 ("the fade in of the tab item maybe
// 20% faster"). Round 36: 30% faster again ("still too slow"). Round 39: 25% faster
// again, since the two zoom frames make each key recognisable sooner.
#define CASC_MS       756u   // first content key to last
#define CASC_FADE_MS  152u   // each key's own fade-in
// Rows are PHYSICAL rows (menu_cascade_rows.h), counted from 0 at the top.
#define CASC_ROWS       3u   // a menu: rows 1..3; row 0 (tabs) and row 4 stay put
#define CASC_BOARD_ROWS 3u   // the Shift reveal: rows 1..3, the letters and both shifts
#define CASC_NAME_ROWS  2u   // a preview name or a "more" screen: rows 1..2, where its letters sit
#define CASC_TICK_MS   30u
// Round 38: each key ZOOMS in over its fade — a 2x2 dot at the centre, then the real
// legend at half size, then full size. Same timing as before; two preview frames.
#define CASC_ZOOM_DOT_MS  (CASC_FADE_MS / 3u)        // 0..50 ms: the dot
#define CASC_ZOOM_HALF_MS ((CASC_FADE_MS * 2u) / 3u) // ..101 ms: half size, then full
enum { ZOOM_NONE = 0, ZOOM_DOT, ZOOM_HALF, ZOOM_FULL };
#define CASC_KEYS      40u   // display slots per half (8 x 5, some phantom)

static uint32_t s_sig;                 // the menu signature the cascade belongs to
static bool     s_live;
static uint8_t  s_rows;                // physical rows 1..s_rows cascade (see poll())
static uint32_t s_start;
static uint32_t s_at;
static uint8_t  s_drawn[5];            // this half's display slots the cascade has started
static uint8_t  s_full[5];             // …and faded all the way up
static uint8_t  s_stage[CASC_KEYS];    // the zoom frame on each panel now (ZOOM_*)
static bool     s_in_draw;             // the tick is drawing: the legend draw must not be hidden

static bool bit(const uint8_t *m, uint8_t i) { return (m[i >> 3] & (1u << (i & 7))) != 0; }
static void set_bit(uint8_t *m, uint8_t i)  { m[i >> 3] |= (uint8_t)(1u << (i & 7)); }

// When this half's display slot `idx` appears, in ms from the change, or 0 for a key
// that does not cascade (the tab row, the bottom row, a slot with no panel).
static uint32_t due_ms(bool right, uint8_t idx) {
    // ⚠️ The PHYSICAL row, not idx / 8. The thumb cluster's "Lang" and "PgDn" keys sit
    // on the keyboard's fourth row but on the fifth DISPLAY row, so the display row
    // counted them as the bottom row and drew them at once (hardware).
    if (idx >= CASC_KEYS) return 0u;
    const uint8_t dr   = right ? CASC_ROW_RIGHT[idx] : CASC_ROW_LEFT[idx];
    const uint8_t rows = s_rows;
    if (dr == 0u || dr == 0xFFu || dr > rows) return 0u;
    const sa_geom_t g  = startup_anim_key_geom(right, idx);
    const uint32_t  bw = startup_anim_board_w();
    if (!g.valid || bw == 0u) return 0u;
    const uint32_t x = (uint32_t)(g.cx < 0 ? 0 : g.cx);
    // +1 so the first key's moment is never 0 (0 means "does not cascade").
    return 1u + (CASC_MS * ((uint32_t)(dr - 1u) * bw + (x > bw ? bw : x))) / ((uint32_t)rows * bw);
}

static void poll(void) {
    // The tutorial's Shift reveal first: while it runs, the base layer is up and the
    // menu signature is 0 anyway, so the two never compete.
    uint32_t sig = tutorial_cascade_signature();
    if (sig == 0u) sig = poly_menu_signature();
    if (sig == s_sig) return;
    // ⚠️ Cut short with keys still waiting (the reveal ends, or the slave sees the next
    // phase a sync early): those keys were never drawn, and the diffing renderer has
    // no reason to draw them now, so they would stay dark. Ask for a full repaint.
    if (s_live && sig == 0u) request_disp_refresh();
    s_sig   = sig;
    s_live  = sig != 0u;
    // The signature's top byte says what is cascading, and so which rows: 0x01/0x02 a
    // menu, 0x03 the tutorial's Shift reveal, 0x04 a preview name or a "more" screen.
    switch (sig >> 24) {
        case 0x03u: s_rows = CASC_BOARD_ROWS; break;
        case 0x04u: s_rows = CASC_NAME_ROWS;  break;
        default:    s_rows = CASC_ROWS;       break;
    }
    if (!s_live) return;
    s_start = timer_read32();
    s_at    = s_start - CASC_TICK_MS;
    for (uint8_t i = 0; i < sizeof(s_drawn); ++i) s_drawn[i] = s_full[i] = 0u;
    for (uint8_t i = 0; i < CASC_KEYS; ++i) s_stage[i] = ZOOM_NONE;
}

bool menu_cascade_hidden(uint8_t row, uint8_t col) {
    if (s_in_draw) return false;   // the tick's own legend draw, for a zoom frame
    poll();
    if (!s_live) return false;
    const uint8_t slot = tutorial_slot_at(row, col);
    if (slot == TUT_SLOT_NONE) return false;
    const bool    right = TUT_SLOT_RIGHT(slot);
    if (right == is_left_side()) return false;    // only this half's own keys
    const uint8_t idx = TUT_SLOT_IDX(slot);
    if (bit(s_drawn, idx)) {
        if (s_stage[idx] >= ZOOM_FULL) return false;
        // Mid-zoom: a full render would paint the full legend over a preview frame.
        // Keep it dark and let the tick repaint the frame it is on.
        s_stage[idx] = ZOOM_NONE;
        return true;
    }
    const uint32_t due = due_ms(right, idx);
    if (due == 0u) return false;
    if (timer_elapsed32(s_start) < due) return true;
    // Due, and a full render is about to draw it before the tick has: it lands at the
    // normal level, so the tick must not draw it again dim and fade it up a second time.
    set_bit(s_drawn, idx);
    set_bit(s_full, idx);
    s_stage[idx] = ZOOM_FULL;
    return false;
}

void menu_cascade_tick(void) {
    poll();
    if (!s_live) return;
    // Idle, Eden, DOOM, a flash: the keycaps belong to someone else. The cascade is a
    // moment, not a state, so it ends rather than resuming later over a stale screen.
    if (!poly_render_live()) {
        s_live = false;
        return;
    }
    const uint32_t now = timer_read32();
    if ((uint32_t)(now - s_at) < CASC_TICK_MS) return;
    s_at = now;
    const bool     right = !is_left_side();
    const uint32_t el    = (uint32_t)(now - s_start);
    const uint8_t  full  = poly_panel_full_contrast();
    bool           done  = true;
    for (uint8_t idx = 0; idx < CASC_KEYS; ++idx) {
        if (bit(s_full, idx)) continue;
        const uint32_t due = due_ms(right, idx);
        if (due == 0u) continue;
        if (el < due) {
            done = false;
            continue;
        }
        // A key the tutorial keeps dark (the reveal covers every key in its rows, the
        // lit set is a few of them) is left alone: no latch, no send.
        if (!poly_slot_visible(TUT_SLOT(right ? 1 : 0, idx))) {
            set_bit(s_drawn, idx);
            set_bit(s_full, idx);
            continue;
        }
        sr_shift_out_buffer_latch(get_key_disp_bitmask(idx), get_disp_bitmask_size());
        const uint32_t into = el - due;
        const uint8_t  lvl  = into >= CASC_FADE_MS
                                  ? full
                                  : (uint8_t)(((uint32_t)full *
                                               tut_fade_contrast((uint8_t)((into * 255u) / CASC_FADE_MS))) /
                                              255u);
        const uint8_t want = into < CASC_ZOOM_DOT_MS    ? (uint8_t)ZOOM_DOT
                             : into < CASC_ZOOM_HALF_MS ? (uint8_t)ZOOM_HALF
                                                        : (uint8_t)ZOOM_FULL;
        if (!bit(s_drawn, idx) || s_stage[idx] != want) {
            // Same draw as the focus ring's repaint: tracked, so the next full render
            // diffs against what is really on the panel. s_in_draw because the legend
            // draw asks menu_cascade_hidden(), which must answer "no" to its own frame.
            set_bit(s_drawn, idx);
            kdisp_set_contrast(lvl);
            kdisp_track_panel(idx);
            kdisp_set_buffer(0x00);
            s_in_draw = true;
            (void)poly_focus_draw_legend(TUT_SLOT(right ? 1 : 0, idx));
            s_in_draw = false;
            kdisp_set_gfx_erase(false);
            // Round 39: a key with nothing on it gets no dot either ("we should not show
            // the 2x2 dot if there is nothing displayed", hardware). It is finished as
            // it stands, blank.
            const bool blank = kdisp_window_is_blank();
            if (blank) {
                s_stage[idx] = ZOOM_FULL;
                kdisp_set_contrast(full);
                kdisp_send_window();
                set_bit(s_full, idx);
                continue;
            }
            if (want == ZOOM_DOT) {
                kdisp_set_buffer(0x00);
                kdisp_fill_rect((int8_t)(BUFFER_X + SCREEN_WIDTH / 2 - 1), (int8_t)(SCREEN_HEIGHT / 2 - 1), 2, 2);
            } else if (want == ZOOM_HALF) {
                kdisp_zoom_half_window();
            }
            kdisp_send_window();
            s_stage[idx] = want;
        } else {
            kdisp_set_contrast(lvl);
        }
        if (lvl == full && s_stage[idx] == ZOOM_FULL) {
            set_bit(s_full, idx);
        } else {
            done = false;
        }
    }
    if (done) s_live = false;
}

#else   // split42: no geometry table, so no cascade (as with the tutorial)

bool menu_cascade_hidden(uint8_t row, uint8_t col) {
    (void)row;
    (void)col;
    return false;
}
void menu_cascade_tick(void) {}

#endif
