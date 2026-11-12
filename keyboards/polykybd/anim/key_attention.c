// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#include "key_attention.h"

#include <string.h>
#include "quantum.h"
#include "raw_hid.h"
#include "print.h"
#include "state.h"
#include "focus_ring.h"          // poly_focus_start, poly_focus_active, poly_slot_of
#include "tutorial.h"            // tutorial_active
#include "menu_cascade.h"        // poly_render_live
#include "base/tutorial_plan.h"  // TUT_SLOT_NONE
#include "base/update.h"         // request_disp_refresh
#include "base/attn_guide.h"     // op 2: the pure parse and step choice

static void reply(uint8_t *data, uint8_t length, bool ok) {
    memset(data, 0, length);
    data[0] = 'P';
    data[1] = KEY_ATTN_CMD;
    data[2] = ok ? '.' : '!';
    raw_hid_send(data, length);
}

// Op 2 GUIDE, master only: the steps, their pool slots, and which one rings now.
static attn_guide_t s_guide;
static uint8_t      s_guide_slot[ATTN_GUIDE_MAX_STEPS];
static bool         s_guide_on;
static uint8_t      s_guide_step;   // the step last written to attn_*, or ATTN_GUIDE_*
static uint8_t      s_guide_left;   // ripples left of the total (count > 0 only)

static void bump_seq(poly_sync_t *ls) {
    ls->attn_seq = (uint8_t)(ls->attn_seq + 1u) ? (uint8_t)(ls->attn_seq + 1u) : 1u;
}

static void guide_hid(uint8_t *data, uint8_t length) {
    attn_guide_t g;
    bool ok = attn_guide_parse(data, length, MATRIX_ROWS, MATRIX_COLS,
                               DYNAMIC_KEYMAP_LAYER_COUNT, KEY_ATTN_PERIOD_MIN,
                               KEY_ATTN_PERIOD_MAX, &g);
    uint8_t slots[ATTN_GUIDE_MAX_STEPS];
    for (uint8_t i = 0; ok && i < g.nsteps; ++i) {
        slots[i] = poly_slot_of(g.step[i].row, g.step[i].col);
        ok       = slots[i] != TUT_SLOT_NONE;
    }
    if (!ok) {
        uprintf("attn: refused guide\n");
        reply(data, length, false);
        return;
    }
    s_guide = g;
    memcpy(s_guide_slot, slots, sizeof slots);
    s_guide_on   = true;
    s_guide_step = 0xFDu;   // none yet: the next tick writes the step for the active layer
    s_guide_left = g.count;
    uprintf("attn: guide %u step(s) to layer %u\n", (unsigned)g.nsteps, (unsigned)g.target);
    reply(data, length, true);
}

// Master housekeeping: put the ripple on the step whose layer is active. Runs before
// the schedule below reads attn_seq, so a changed step starts on this same tick.
static void guide_tick(uint8_t ripples_left) {
    if (!s_guide_on || !is_keyboard_master()) return;
    poly_sync_t *ls = access_local_state();
    const bool on_step = s_guide_step < ATTN_GUIDE_MAX_STEPS;
    // A limited guide ends when its last ripple has started (that one runs out).
    if (s_guide.count && on_step && ripples_left == 0u) {
        s_guide_on = false;
        uprintf("attn: guide ran out\n");
        return;
    }
    const uint8_t step = attn_guide_select(
        &s_guide, get_highest_layer(layer_state | default_layer_state));
    if (step == s_guide_step) return;
    if (on_step) s_guide_left = ripples_left;   // carry the rest of the total over
    s_guide_step = step;
    if (step == ATTN_GUIDE_DONE) {
        s_guide_on    = false;
        ls->attn_slot = TUT_SLOT_NONE;
        bump_seq(ls);
        uprintf("attn: guide done\n");
        return;
    }
    ls->attn_slot   = step == ATTN_GUIDE_IDLE ? TUT_SLOT_NONE : s_guide_slot[step];
    ls->attn_count  = s_guide.count ? s_guide_left : 0u;
    ls->attn_period = s_guide.period;
    bump_seq(ls);
    if (step != ATTN_GUIDE_IDLE) uprintf("attn: guide step %u\n", (unsigned)step);
}

void key_attention_hid(uint8_t *data, uint8_t length) {
    const uint8_t op = length > 2 ? data[2] : 0xFFu;
    poly_sync_t  *ls = access_local_state();
    if (op == ATTN_GUIDE_OP) {
        guide_hid(data, length);
        return;
    }
    // START and CANCEL end a guide; a refused request leaves it running.
    if (op == KEY_ATTN_OP_CANCEL) {
        s_guide_on    = false;
        ls->attn_slot = TUT_SLOT_NONE;
        ls->attn_seq  = (uint8_t)(ls->attn_seq + 1u) ? (uint8_t)(ls->attn_seq + 1u) : 1u;
        reply(data, length, true);
        return;
    }
    // Parse and validate the whole report before anything changes.
    bool ok = op == KEY_ATTN_OP_START && length > 6;
    uint8_t slot = TUT_SLOT_NONE;
    if (ok) {
        const uint8_t row = data[3], col = data[4], period = data[6];
        ok = row < MATRIX_ROWS && col < MATRIX_COLS &&
             period >= KEY_ATTN_PERIOD_MIN && period <= KEY_ATTN_PERIOD_MAX;
        if (ok) {
            slot = poly_slot_of(row, col);
            ok   = slot != TUT_SLOT_NONE;
        }
    }
    if (!ok) {
        uprintf("attn: refused op %u\n", (unsigned)op);
        reply(data, length, false);
        return;
    }
    s_guide_on      = false;
    ls->attn_slot   = slot;
    ls->attn_count  = data[5];
    ls->attn_period = data[6];
    ls->attn_seq    = (uint8_t)(ls->attn_seq + 1u) ? (uint8_t)(ls->attn_seq + 1u) : 1u;
    reply(data, length, true);
}

static uint8_t  s_seen_seq;
static uint8_t  s_left;       // ripples still to run
static bool     s_endless;
static uint32_t s_next;       // when the next ripple is due
static uint8_t  s_slot = TUT_SLOT_NONE;
static uint16_t s_period_ms;
static bool     s_restart;    // a new START: replace a ripple already running
static bool     s_ours;       // the running ripple is one this file started

bool key_attention_pending(void) { return s_endless || s_left > 0u; }

void key_attention_tick(void) {
    // s_left is this half's count for the request it has loaded; a guide step that is
    // written but not yet loaded has the full count, which guide_tick does not read.
    if (get_local_state()->attn_seq == s_seen_seq) guide_tick(s_left);
    const poly_sync_t *s = get_local_state();
    if (s->attn_seq != s_seen_seq) {
        // A new request (or a cancel) from the master, or this half's own cmd 47.
        // A cancel stops the schedule but lets a running ripple finish: cutting it
        // would leave its arc on the keys it was crossing.
        s_seen_seq  = s->attn_seq;
        s_slot      = s->attn_slot;
        s_endless   = s_slot != TUT_SLOT_NONE && s->attn_count == 0u;
        s_left      = s_slot != TUT_SLOT_NONE ? s->attn_count : 0u;
        s_period_ms = (uint16_t)(s->attn_period * 100u);
        s_next      = timer_read32();
        s_restart   = s_slot != TUT_SLOT_NONE;
        s_ours      = false;   // a running ripple belongs to the old request
    }
    // The firmware prompt owns every panel: stop any ripple still crossing it (this
    // half's own, from cmd 47, an agent or the tutorial) and redraw the prompt whole.
    if (!poly_focus_active()) s_ours = false;
    if (s->fw_confirm && poly_focus_active()) {
        // Our own ripple, cut short: run it again once the prompt closes (an endless
        // request repeats anyway; a CANCEL or a new START already reset the schedule).
        if (s_ours && !s_endless && s_slot != TUT_SLOT_NONE && s_left < UINT8_MAX) ++s_left;
        s_ours = false;
        poly_focus_cancel();
        request_disp_refresh();
    }
    if (!key_attention_pending()) return;
    // Deferred, not dropped, while the firmware prompt owns the displays.
    if (tutorial_active() || !poly_render_live() || s->fw_confirm) return;
    // A START replaces whatever ripple runs now (poly_focus_start keeps the panels the
    // old one still owes a restore); otherwise wait for the running one to finish.
    if (poly_focus_active() && !s_restart) return;
    const uint32_t now = timer_read32();
    if ((int32_t)(now - s_next) < 0) return;
    poly_focus_start(s_slot);
    s_ours    = true;
    s_restart = false;
    s_next = now + s_period_ms;
    if (!s_endless) --s_left;
}
