// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#include "host_tutorial.h"

#include <string.h>
#include "quantum.h"
#include "raw_hid.h"
#include "print.h"
#include "transactions.h"          // USER_SYNC_DYNAMIC_KEYMAP_DATA
#include "bridge_helper.h"         // send_to_bridge
#include "split_sync.h"            // dynamic_keymap_sync_t, sync_succeeded
#include "focus_ring.h"            // poly_slot_of, poly_focus_start_sweep
#include "startup_anim.h"          // startup_anim_key_geom
#include "tutorial.h"              // tutorial_active: the first-run lesson owns the board
#include "base/tutorial_plan.h"    // TUT_SLOT_*, TUT_BOARD_REVEAL_MS, tut_sweep_radius
#include "base/update.h"           // request_disp_refresh
#include "base/host_tutorial_plan.h"

// ---- state, both halves (RAM only) -------------------------------------------------

static host_tut_text_t s_text;
static bool            s_text_on;
static uint8_t         s_mask[HOST_TUT_MASK_BYTES];
static bool            s_spot_on;
static bool            s_reveal_on;
static uint8_t         s_reveal_slot = TUT_SLOT_NONE;
static uint32_t        s_reveal_start;

// ---- master only -------------------------------------------------------------------

static host_tut_conds_t s_conds;
static uint32_t         s_last_ms;      // the last cmd 49 report, for the watchdog
// What the slave still has to get. END first, so a TEXT or SPOTLIGHT that follows an
// END is applied after it.
#define DIRTY_END  0x01u
#define DIRTY_TEXT 0x02u
#define DIRTY_SPOT 0x04u
static uint8_t  s_dirty;
static uint32_t s_resend_at;
#define RESEND_MS 250u

// ---- slave only: what the split thread left for housekeeping ------------------------
// The handler runs on the slave's split-protocol thread (SPLIT_SYNC.md). It writes the
// parsed state straight into s_text / s_mask / s_spot_on inside chSysLock() (a copy, no
// bus), and leaves the rest for host_tutorial_tick() on the main loop: the keycap redraw
// and the reveal's focus ring. The status screen copies the text out under the same lock
// (host_tutorial_copy_text), so it never draws a half-written line. No staging copy of
// the text or the mask: that saves 72 B of RAM on each half.
#define PENDING_REDRAW 0x01u
#define PENDING_REVEAL 0x02u
static uint8_t s_pending;
static uint8_t s_pending_reveal_slot = TUT_SLOT_NONE;

static bool session_open(void) {
    return s_text_on || s_spot_on || s_reveal_on || s_conds.count > 0u;
}

static void reply(uint8_t *data, uint8_t length, bool ok) {
    // data[3..] may already hold the op's answer; only the status byte is set here.
    data[0] = 'P';
    data[1] = HOST_TUT_CMD;
    data[2] = ok ? '.' : '!';
    raw_hid_send(data, length);
}

static void clear_reply(uint8_t *data, uint8_t length) { memset(data, 0, length); }

// ---- applying, both halves -----------------------------------------------------------

static void apply_end(void) {
    const bool redraw = s_spot_on || s_reveal_on;
    s_text_on   = false;
    s_spot_on   = false;
    s_reveal_on = false;
    memset(&s_text, 0, sizeof s_text);
    memset(s_mask, 0, sizeof s_mask);
    if (redraw) request_disp_refresh();
}

static void apply_text(const host_tut_text_t *t) {
    s_text    = *t;
    s_text_on = !host_tut_text_empty(t);
}

static void apply_spot(bool on, const uint8_t mask[HOST_TUT_MASK_BYTES]) {
    s_spot_on   = on;
    s_reveal_on = false;   // a new spotlight replaces a reveal still running
    memcpy(s_mask, mask, HOST_TUT_MASK_BYTES);
    request_disp_refresh();
}

static void apply_reveal(uint8_t slot) {
    s_reveal_on    = true;
    s_reveal_slot  = slot;
    s_reveal_start = timer_read32();
    poly_focus_start_sweep(slot, 0u, TUT_BOARD_REVEAL_MS);
}

// ---- the relay to the slave (master) -------------------------------------------------

static bool relay(const uint8_t *report) {
    dynamic_keymap_sync_t msg;
    memset(&msg, 0, sizeof msg);
    memcpy(msg.commands, report, HOST_TUT_REPORT_BYTES);
    return sync_succeeded(send_to_bridge(USER_SYNC_DYNAMIC_KEYMAP_DATA, &msg,
                                         (uint8_t)(sizeof(msg.crc32) + HOST_TUT_REPORT_BYTES), 3));
}

static void build_report(uint8_t *r, uint8_t op) {
    memset(r, 0, HOST_TUT_REPORT_BYTES);
    r[0] = 'P';
    r[1] = HOST_TUT_CMD;
    r[2] = op;
    if (op == HOST_TUT_OP_TEXT) {
        uint8_t pos = HOST_TUT_IDX_ARG;
        for (uint8_t i = 0; i < 2u; ++i) {
            r[pos++] = s_text.len[i];
            memcpy(&r[pos], host_tut_text_line(&s_text, i), s_text.len[i]);
            pos = (uint8_t)(pos + s_text.len[i]);
        }
    } else if (op == HOST_TUT_OP_SPOTLIGHT) {
        r[HOST_TUT_IDX_ARG] = s_spot_on ? HOST_TUT_SPOT_FLAG_ON : 0u;
        memcpy(&r[HOST_TUT_IDX_ARG + 1u], s_mask, HOST_TUT_MASK_BYTES);
    }
}

// Sends what the slave still lacks, END first. Stops at the first failure and tries
// again RESEND_MS later, so an absent slave costs one attempt per interval.
static void push_dirty(void) {
    static const struct { uint8_t bit, op; } order[] = {
        {DIRTY_END, HOST_TUT_OP_END}, {DIRTY_TEXT, HOST_TUT_OP_TEXT}, {DIRTY_SPOT, HOST_TUT_OP_SPOTLIGHT}};
    uint8_t r[HOST_TUT_REPORT_BYTES];
    for (uint8_t i = 0; i < 3u; ++i) {
        if (!(s_dirty & order[i].bit)) continue;
        build_report(r, order[i].op);
        if (!relay(r)) {
            s_resend_at = timer_read32() + RESEND_MS;
            return;
        }
        s_dirty &= (uint8_t)~order[i].bit;
    }
}

static void end_session(void) {
    apply_end();
    memset(&s_conds, 0, sizeof s_conds);
    s_dirty = DIRTY_END;   // a TEXT or SPOTLIGHT not yet sent is moot now
    push_dirty();
}

// ---- cmd 49, master ------------------------------------------------------------------

void host_tutorial_hid(uint8_t *data, uint8_t length) {
    const uint8_t op = length > 2u ? data[2] : 0xFFu;
    s_last_ms = timer_read32();
    // The first-run lesson owns the keycaps and the status panels: refuse anything that
    // would draw over it. INFO, POLL and END still answer.
    const bool draws = op == HOST_TUT_OP_TEXT || op == HOST_TUT_OP_SPOTLIGHT ||
                       op == HOST_TUT_OP_REVEAL || op == HOST_TUT_OP_ARM;
    if (draws && tutorial_active()) {
        uprintf("host tut: op %u refused, first-run tutorial active\n", (unsigned)op);
        clear_reply(data, length);
        reply(data, length, false);
        return;
    }
    switch (op) {
        case HOST_TUT_OP_INFO:
            clear_reply(data, length);
            data[3] = (uint8_t)((1u << HOST_TUT_OP_COUNT) - 1u);
            data[4] = HOST_TUT_MAX_CONDS;
            data[5] = (uint8_t)(HOST_TUT_WATCHDOG_MS / 1000u);
            data[6] = HOST_TUT_TEXT_BYTES;
            reply(data, length, true);
            return;
        case HOST_TUT_OP_TEXT: {
            host_tut_text_t t;
            const bool ok = host_tut_parse_text(data, length, &t);
            if (ok) {
                apply_text(&t);
                s_dirty |= DIRTY_TEXT;
                push_dirty();
            }
            clear_reply(data, length);
            reply(data, length, ok);
            return;
        }
        case HOST_TUT_OP_SPOTLIGHT: {
            bool    on = false;
            uint8_t mask[HOST_TUT_MASK_BYTES];
            const bool ok = host_tut_parse_spotlight(data, length, MATRIX_ROWS, MATRIX_COLS, &on, mask);
            if (ok) {
                apply_spot(on, mask);
                s_dirty |= DIRTY_SPOT;
                push_dirty();
            }
            clear_reply(data, length);
            reply(data, length, ok);
            return;
        }
        case HOST_TUT_OP_REVEAL: {
            const uint8_t row = length > 4u ? data[3] : 0xFFu, col = length > 4u ? data[4] : 0xFFu;
            const uint8_t slot = (row < MATRIX_ROWS && col < MATRIX_COLS) ? poly_slot_of(row, col)
                                                                         : TUT_SLOT_NONE;
            const bool ok = slot != TUT_SLOT_NONE;
            if (ok) {
                // Forwarded once: a reveal that reaches the slave late is worse than one
                // that does not, and the slave lights every key when its spotlight ends
                // with the master's (the master resends that state, below).
                uint8_t r[HOST_TUT_REPORT_BYTES];
                memset(r, 0, sizeof r);
                memcpy(r, data, length < sizeof r ? length : sizeof r);
                apply_reveal(slot);
                if (!relay(r)) uprintf("host tut: reveal did not reach the slave\n");
            }
            clear_reply(data, length);
            reply(data, length, ok);
            return;
        }
        case HOST_TUT_OP_ARM: {
            host_tut_conds_t c;
            const bool ok = host_tut_parse_arm(data, length, MATRIX_ROWS, MATRIX_COLS,
                                               DYNAMIC_KEYMAP_LAYER_COUNT, &c);
            if (ok) s_conds = c;
            clear_reply(data, length);
            reply(data, length, ok);
            return;
        }
        case HOST_TUT_OP_POLL: {
            host_tut_note_layers(&s_conds, (uint32_t)(layer_state | default_layer_state));
            clear_reply(data, length);
            data[3] = (uint8_t)((s_text_on ? HOST_TUT_POLL_TEXT : 0u) |
                                (s_spot_on ? HOST_TUT_POLL_SPOT : 0u) |
                                (s_conds.count ? HOST_TUT_POLL_ARMED : 0u));
            data[4] = s_conds.count;
            data[5] = s_conds.matched;
            data[6] = get_highest_layer(layer_state | default_layer_state);
            reply(data, length, true);
            return;
        }
        case HOST_TUT_OP_END:
            end_session();
            clear_reply(data, length);
            reply(data, length, true);
            return;
        default:
            uprintf("host tut: unknown op %u\n", (unsigned)op);
            clear_reply(data, length);
            reply(data, length, false);
            return;
    }
}

// ---- the slave's side of the relay -----------------------------------------------------

void host_tutorial_slave_receive(const uint8_t *data, uint8_t length) {
    if (data == NULL || length < 3u || data[1] != HOST_TUT_CMD) return;
    const uint8_t op = data[2];
    // Parse outside the lock; only the copy into the live state is inside it.
    host_tut_text_t t;
    bool            spot_on = false;
    uint8_t         mask[HOST_TUT_MASK_BYTES];
    uint8_t         rslot = TUT_SLOT_NONE;
    bool            ok    = false;
    switch (op) {
        case HOST_TUT_OP_END:
            ok = true;
            break;
        case HOST_TUT_OP_TEXT:
            ok = host_tut_parse_text(data, length, &t);
            break;
        case HOST_TUT_OP_SPOTLIGHT:
            ok = host_tut_parse_spotlight(data, length, MATRIX_ROWS, MATRIX_COLS, &spot_on, mask);
            break;
        case HOST_TUT_OP_REVEAL:
            if (length > 4u && data[3] < MATRIX_ROWS && data[4] < MATRIX_COLS) {
                // The slot lookup reads the keymap side tables only, no bus.
                rslot = poly_slot_of(data[3], data[4]);
                ok    = rslot != TUT_SLOT_NONE;
            }
            break;
        default:
            break;
    }
    if (!ok) return;
    chSysLock();
    switch (op) {
        case HOST_TUT_OP_END:
            s_text_on = false;
            memset(&s_text, 0, sizeof s_text);
            s_spot_on = false;
            memset(s_mask, 0, sizeof s_mask);
            s_pending = PENDING_REDRAW;   // drops a reveal not yet started
            break;
        case HOST_TUT_OP_TEXT:
            s_text    = t;
            s_text_on = !host_tut_text_empty(&t);
            break;
        case HOST_TUT_OP_SPOTLIGHT:
            s_spot_on = spot_on;
            memcpy(s_mask, mask, sizeof mask);
            s_pending = (uint8_t)((s_pending & (uint8_t)~PENDING_REVEAL) | PENDING_REDRAW);
            break;
        default:   // REVEAL
            s_pending_reveal_slot = rslot;
            s_pending |= PENDING_REVEAL;
            break;
    }
    chSysUnlock();
}

static void apply_pending(void) {
    if (s_pending == 0u) return;   // a byte read: nothing pending costs no lock
    chSysLock();
    const uint8_t pending = s_pending;
    const uint8_t rslot   = s_pending_reveal_slot;
    s_pending             = 0u;
    chSysUnlock();
    // A spotlight or an END ends a reveal still running (as apply_spot/apply_end do on
    // the master); the split thread leaves that to the main loop, which owns s_reveal_on.
    if (pending & PENDING_REDRAW) {
        s_reveal_on = false;
        request_disp_refresh();
    }
    if (pending & PENDING_REVEAL) apply_reveal(rslot);
}

// ---- housekeeping, both halves -----------------------------------------------------------

void host_tutorial_tick(void) {
    if (!is_keyboard_master()) {
        apply_pending();
    } else {
        if (session_open()) {
            host_tut_note_layers(&s_conds, (uint32_t)(layer_state | default_layer_state));
            if (host_tut_expired(s_last_ms, timer_read32())) {
                uprintf("host tut: no host for %u s, ending\n",
                        (unsigned)(HOST_TUT_WATCHDOG_MS / 1000u));
                end_session();
            }
        }
        if (s_dirty && (int32_t)(timer_read32() - s_resend_at) >= 0) push_dirty();
    }
    // The reveal ends the spotlight once its front has crossed the board.
    if (s_reveal_on && timer_elapsed32(s_reveal_start) >= TUT_BOARD_REVEAL_MS) {
        s_reveal_on = false;
        s_spot_on   = false;
        request_disp_refresh();
    }
}

void host_tutorial_note_key(uint8_t row, uint8_t col, uint16_t keycode, bool pressed) {
    if (s_conds.count) host_tut_note_key(&s_conds, row, col, keycode, pressed);
}

// ---- drawing ---------------------------------------------------------------------------

bool host_tutorial_text_active(void) { return s_text_on; }

bool host_tutorial_copy_text(host_tut_text_t *out) {
    // Under the lock: on the slave the split thread may be writing s_text right now.
    chSysLock();
    const bool on = s_text_on;
    *out          = s_text;
    chSysUnlock();
    return on;
}

// Has the reveal front passed this key's centre? Same geometry and curve as the
// first-run tutorial's board reveal (tut_reveal_reached in tutorial.c).
static bool reveal_reached(uint8_t row, uint8_t col) {
    const uint8_t slot = poly_slot_of(row, col);
    if (slot == TUT_SLOT_NONE || s_reveal_slot == TUT_SLOT_NONE) return false;
    const sa_geom_t k = startup_anim_key_geom(TUT_SLOT_RIGHT(slot), TUT_SLOT_IDX(slot));
    const sa_geom_t o = startup_anim_key_geom(TUT_SLOT_RIGHT(s_reveal_slot), TUT_SLOT_IDX(s_reveal_slot));
    if (!k.valid || !o.valid) return false;
    const int32_t  dx = (int32_t)k.cx - o.cx, dy = (int32_t)k.cy - o.cy;
    const uint32_t r  = tut_sweep_radius(
        host_tut_reveal_progress(timer_elapsed32(s_reveal_start), TUT_BOARD_REVEAL_MS));
    return (uint32_t)(dx * dx + dy * dy) <= r * r;
}

bool host_tutorial_key_hidden(uint8_t row, uint8_t col) {
    if (!s_spot_on) return false;
    if (host_tut_mask_has(s_mask, MATRIX_COLS, row, col)) return false;
    return !(s_reveal_on && reveal_reached(row, col));
}
