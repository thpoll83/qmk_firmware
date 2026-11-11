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

static void reply(uint8_t *data, uint8_t length, bool ok) {
    memset(data, 0, length);
    data[0] = 'P';
    data[1] = KEY_ATTN_CMD;
    data[2] = ok ? '.' : '!';
    raw_hid_send(data, length);
}

void key_attention_hid(uint8_t *data, uint8_t length) {
    const uint8_t op = length > 2 ? data[2] : 0xFFu;
    poly_sync_t  *ls = access_local_state();
    if (op == KEY_ATTN_OP_CANCEL) {
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
