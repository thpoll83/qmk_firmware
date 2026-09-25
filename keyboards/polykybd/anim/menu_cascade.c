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
#include "tutorial.h"               // tutorial_slot_at()
#include "focus_ring.h"             // poly_focus_draw_legend()
#include "base/update.h"            // request_disp_refresh()

// Round 34: 20% faster than round 33's 1800/360 ("the fade in of the tab item maybe
// 20% faster"). Round 36: 30% faster again ("still too slow").
#define CASC_MS      1008u   // first content key to last
#define CASC_FADE_MS  202u   // each key's own fade-in
#define CASC_ROWS       3u   // a menu: display rows 1..3; row 0 (tabs) and row 4 stay put
#define CASC_BOARD_ROWS 4u   // the Shift reveal: rows 1..4, so the shifts come in too
#define CASC_NAME_ROWS  2u   // a preview name or a "more" screen: rows 1..2, where its letters sit
#define CASC_TICK_MS   30u
#define CASC_KEYS      40u   // display slots per half (8 x 5, some phantom)

static uint32_t s_sig;                 // the menu signature the cascade belongs to
static bool     s_live;
static uint8_t  s_rows;                // display rows 1..s_rows cascade (see poll())
static uint32_t s_start;
static uint32_t s_at;
static uint8_t  s_drawn[5];            // this half's display slots already drawn
static uint8_t  s_full[5];             // …and faded all the way up

static bool bit(const uint8_t *m, uint8_t i) { return (m[i >> 3] & (1u << (i & 7))) != 0; }
static void set_bit(uint8_t *m, uint8_t i)  { m[i >> 3] |= (uint8_t)(1u << (i & 7)); }

// When this half's display slot `idx` appears, in ms from the change, or 0 for a key
// that does not cascade (the tab row, the bottom row, a slot with no panel).
static uint32_t due_ms(bool right, uint8_t idx) {
    const uint8_t dr   = (uint8_t)(idx / 8u);
    const uint8_t rows = s_rows;
    if (dr == 0u || dr > rows) return 0u;
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
}

bool menu_cascade_hidden(uint8_t row, uint8_t col) {
    poll();
    if (!s_live) return false;
    const uint8_t slot = tutorial_slot_at(row, col);
    if (slot == TUT_SLOT_NONE) return false;
    const bool    right = TUT_SLOT_RIGHT(slot);
    if (right == is_left_side()) return false;    // only this half's own keys
    const uint8_t idx = TUT_SLOT_IDX(slot);
    if (bit(s_drawn, idx)) return false;
    const uint32_t due = due_ms(right, idx);
    if (due == 0u) return false;
    if (timer_elapsed32(s_start) < due) return true;
    // Due, and a full render is about to draw it before the tick has: it lands at the
    // normal level, so the tick must not draw it again dim and fade it up a second time.
    set_bit(s_drawn, idx);
    set_bit(s_full, idx);
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
        if (!bit(s_drawn, idx)) {
            // Same draw as the focus ring's repaint: tracked, so the next full render
            // diffs against what is really on the panel. Marked drawn FIRST, because
            // the legend draw asks menu_cascade_hidden() and must get "no".
            set_bit(s_drawn, idx);
            kdisp_set_contrast(lvl);
            kdisp_track_panel(idx);
            kdisp_set_buffer(0x00);
            (void)poly_focus_draw_legend(TUT_SLOT(right ? 1 : 0, idx));
            kdisp_set_gfx_erase(false);
            kdisp_send_window();
        } else {
            kdisp_set_contrast(lvl);
        }
        if (lvl == full) {
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
