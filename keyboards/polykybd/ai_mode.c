// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Firmware binding for AI mode (ai_mode.h): the AI key, the key swallowing, the event
// ring, cmd 45 / 46, the keycap drawing and the LEDs. The pure part (the ring, the
// key index, the checks on what the host sends) is base/ai_events.c. PROTOTYPE.
#include "ai_mode.h"

#include "quantum.h"
#include QMK_KEYBOARD_H
#include "print.h"
#include "raw_hid.h"
#include <transactions.h>   // USER_SYNC_OVERLAY_DATA
#include <string.h>

#include "base/ai_events.h"
#include "base/update.h"        // request_disp_refresh / update_performed
#include "base/overlay.h"       // get_overlay
#include "base/disp_array.h"
#include "base/font_lookup.h"   // kdisp_gfx_text_bbox
#include "state.h"
#include "keycode_helper.h"     // KC_AI
#include "bridge_helper.h"      // send_to_bridge
#include "split_sync.h"         // overlay_sync_t, OVERLAY_SEG_RAW_SLOT
#include "anim/focus_ring.h"    // poly_focus_start, poly_slot_of (attention ripple)
#include "anim/key_attention.h" // key_attention_pending
#include "anim/tutorial.h"      // tutorial_active
#include "anim/menu_cascade.h"  // poly_render_live
#include "base/tutorial_plan.h" // TUT_SLOT_NONE
#ifdef RGB_MATRIX_ENABLE
#    include <lib/lib8tion/lib8tion.h>   // sin8
#endif

// Defined in base/fonts/util_font.h, which carries the data and is included once
// (poly_keymap.c). ASCII, resident.
extern const GFXfont NotoSans_Regular_Mid_19px7b;

bool display_wakeup(keyrecord_t *record);   // poly_keymap.c

_Static_assert(KC_1 == AI_HID_KC_1 && KC_0 == AI_HID_KC_0 && KC_ESC == AI_HID_KC_ESC &&
                   KC_MINUS == AI_HID_KC_MINUS && KC_EQUAL == AI_HID_KC_EQUAL,
               "base/ai_events.h key codes differ from QMK's");
// Op 2's reply: data[5 + 2i], data[6 + 2i] for i < AI_RING_SIZE must fit the report.
_Static_assert(6 + 2 * (AI_RING_SIZE - 1) < 64, "op 2 reply does not fit a report");
_Static_assert(3 + AI_SET_KEYS_LEN <= 64, "op 1 request does not fit a report");
_Static_assert(sizeof(((poly_sync_t *)0)->ai_state) == AI_NUM_KEYS, "poly_sync_t.ai_state size");
_Static_assert(sizeof(((poly_sync_t *)0)->ai_slot) == AI_NUM_CARDS * sizeof(uint16_t), "poly_sync_t.ai_slot size");
_Static_assert(NUM_OVERLAY_SLOTS <= 0x400, "cmd 46 carries the slot in 10 bits");

#define AI_CMD      0x2Du   // 45
#define AI_CARD_CMD 0x2Eu   // 46
#define NO_KEY      0xFFu

static ai_ring_t s_ring;
// The AI key press being tracked (its release decides hold or tap).
static bool     s_ai_down;
static uint8_t  s_ai_row;
static uint8_t  s_ai_col;
static uint16_t s_ai_t;
// A number key pressed outside the mode while its agent WAITS: held past the tapping
// term it raises that agent (as AI key + number); a tap types the digit. The press
// is held back until that is decided, then replayed through process_record().
static bool        s_num_pending;
static uint8_t     s_num_key;
static keyrecord_t s_num_rec;
static bool        s_replaying;
static uint16_t    s_num_t;
static bool     s_ai_entered;   // this press turned the mode on (a toggle-off press did not)
static uint32_t s_keys_set_at;  // the last SET_KEYS (AI_KEYS_HOST_TIMEOUT_MS)
static bool     s_ai_pending;   // down, not yet a hold: a release now is a tap (a Hyper tap)
static bool     s_ai_used;      // a key was pressed in the mode while the AI key was held
static uint32_t s_last_input;   // for AI_MODE_LATCH_TIMEOUT_MS
// Keys already down when the mode started: their releases are not ours.
static matrix_row_t s_held_at_start[MATRIX_ROWS];
// Presses the mode swallowed: their releases are swallowed too, also after the mode
// ended (a number key ends it on the press).
static matrix_row_t s_swallowed[MATRIX_ROWS];

// The ripples of a finished agent (ai_attention_tick): still to run per key, and the
// key and start time of the last one that ran.
#define AI_ATTENTION_PERIOD_MS 4000u
static uint8_t  s_done_left[AI_NUM_KEYS];
static uint8_t  s_done_last_key = AI_KEY_NONE;
static uint32_t s_done_last_t;

// True while a finished agent's key rings: ripples are still to run, or the last
// one started less than a period ago. Its number key is then a hold key, as a
// waiting agent's is.
static bool ai_done_ringing(uint8_t key) {
    if (key >= AI_NUM_KEYS) return false;
    return s_done_left[key] > 0
        || (s_done_last_key == key && timer_elapsed32(s_done_last_t) < AI_ATTENTION_PERIOD_MS);
}

static bool rc_test(const matrix_row_t *m, uint8_t row, uint8_t col) {
    return (m[row] & ((matrix_row_t)1 << col)) != 0;
}
static void rc_set(matrix_row_t *m, uint8_t row, uint8_t col) {
    m[row] |= (matrix_row_t)1 << col;
}
static void rc_clear(matrix_row_t *m, uint8_t row, uint8_t col) {
    m[row] &= (matrix_row_t)~((matrix_row_t)1 << col);
}

bool ai_mode_active(void) {
    return get_local_state()->ai_mode != 0;
}

// The AI key index (0..9 number keys, AI_KEY_ESC) of the key at (row, col), from its
// keycode on the ACTIVE DEFAULT layer (def_layer, a layer index): the same lookup the
// renderer and the key events use (poly_keycode_at() behind keymap_key_to_keycode()),
// so a remap or another base layout moves the key. Layer 0 is not enough: the shipped
// layer 1 has 6 on the left half, so its number keys are not where layer 0's are.
// The synced snapshot is read on both halves; it lags a default-layer change by one
// housekeeping pass, which only delays where the cards appear.
static uint8_t ai_key_at(uint8_t row, uint8_t col) {
    if (row >= MATRIX_ROWS || col >= MATRIX_COLS) return AI_KEY_NONE;
    const keypos_t pos = {.col = col, .row = row};
    return ai_key_index(keymap_key_to_keycode((uint8_t)get_local_layer()->def_layer, pos));
}

// The position of every AI key, (row << 4) | col, NO_KEY if absent. The first match
// wins when a keycode is on the default layer twice.
static void ai_find_keys(uint8_t *rc) {
    memset(rc, NO_KEY, AI_KEY_COUNT);
    for (uint8_t r = 0; r < MATRIX_ROWS; ++r) {
        for (uint8_t c = 0; c < MATRIX_COLS; ++c) {
            const uint8_t k = ai_key_at(r, c);
            if (k != AI_KEY_NONE && rc[k] == NO_KEY) rc[k] = (uint8_t)((r << 4) | c);
        }
    }
}

// ---- the mode ---------------------------------------------------------------------

static void ai_enter(void) {
    // Release anything held before swallowing begins.
    clear_keyboard();
    for (uint8_t r = 0; r < MATRIX_ROWS; ++r) {
        s_held_at_start[r] = matrix_get_row(r);
        s_swallowed[r]     = 0;
    }
    access_local_state()->ai_mode = 1;   // reaches the slave with the next state sync
    s_last_input = timer_read32();
    update_performed();
    request_disp_refresh();
    uprint("ai: on\n");
}

static void ai_leave(const char *why) {
    access_local_state()->ai_mode = 0;
    request_disp_refresh();
    uprintf("ai: off (%s)\n", why);
}

void ai_reset_keys(const char *why) {
    if (!is_keyboard_master()) return;   // the slave takes the master's state
    poly_sync_t *ls = access_local_state();
    bool any = ls->ai_flags != 0;
    for (uint8_t i = 0; i < AI_NUM_KEYS; ++i) any |= ls->ai_state[i] != 0;
    if (!any && !ls->ai_mode) return;    // suspend calls this every ~17 ms
    memset(ls->ai_state, 0, sizeof(ls->ai_state));
    for (uint8_t i = 0; i < AI_NUM_CARDS; ++i) ls->ai_slot[i] = AI_SLOT_NONE;
    ls->ai_flags = 0;
    if (ls->ai_mode) ai_leave(why);
    s_ai_down = s_ai_pending = false;
    s_num_pending = false;
    request_disp_refresh();
    uprintf("ai: keys cleared (%s)\n", why);
}

static void ai_record(uint8_t key) {
    const uint8_t seq = ai_ring_push(&s_ring, key);
    poly_state_touch();   // the GET_ID 'G' counter: the host reads op 2 when it moves
    uprintf("ai ev %u %u\n", (unsigned)seq, (unsigned)key);
}

static bool is_layer_key(uint16_t kc) {
    return IS_QK_MOMENTARY(kc) || IS_QK_LAYER_TAP(kc) || IS_QK_LAYER_MOD(kc) || IS_QK_ONE_SHOT_LAYER(kc) ||
           IS_QK_LAYER_TAP_TOGGLE(kc) || IS_QK_TO(kc) || IS_QK_TOGGLE_LAYER(kc);
}

// The held-back number press goes through the whole pipeline now (process_record_user
// included, so a language layer's digit keycode works), and its real release later.
static void num_replay_press(void) {
    s_num_pending = false;
    keyrecord_t rec = s_num_rec;
    s_replaying = true;
    process_record(&rec);
    s_replaying = false;
}

bool ai_process_record(uint16_t keycode, keyrecord_t *record) {
    const uint8_t row     = record->event.key.row;
    const uint8_t col     = record->event.key.col;
    const bool    pressed = record->event.pressed;
    if (s_replaying) return false;
    if (s_num_pending) {
        if (!pressed && row == s_num_rec.event.key.row && col == s_num_rec.event.key.col) {
            num_replay_press();   // released before the tapping term: a tap
            return false;         // and this release follows it normally
        }
        if (pressed) num_replay_press();   // another key: the digit first, in order
    }
    if (row >= MATRIX_ROWS || col >= MATRIX_COLS) {
        // Not a matrix key (an encoder step): swallowed while the mode is on.
        return ai_mode_active();
    }

    // The AI key's own release: a tap sends a Hyper tap (Ctrl+Shift+Alt+GUI
    // pressed and released), as the plain KC_HYPR key did; on Windows that opens
    // Copilot. The end of a hold ends the mode.
    if (!pressed && s_ai_down && row == s_ai_row && col == s_ai_col) {
        s_ai_down = false;
        rc_clear(s_swallowed, row, col);
        if (s_ai_pending) {
            s_ai_pending = false;
            // Only the Hyper bits not already held: releasing a held Ctrl would end it.
            const uint8_t hyper = (uint8_t)((MOD_BIT(KC_LCTL) | MOD_BIT(KC_LSFT) | MOD_BIT(KC_LALT) |
                                             MOD_BIT(KC_LGUI)) & (uint8_t)~get_mods());
            register_mods(hyper);      // sends the report: the PC sees Hyper down
            wait_ms(TAP_CODE_DELAY);
            unregister_mods(hyper);    // and up, with no key between
        } else if (ai_mode_active() && s_ai_entered) {
            if (s_ai_used) {
                ai_leave("hold");
            } else {
                // A long press released with nothing pressed: the mode stays on, so
                // the same hand can reach 6..0 (the AI key is on the right half).
                s_last_input = timer_read32();
            }
        }
        return true;
    }
    // Another key while the AI key is down but not yet a hold: that makes it a hold
    // now, and this press is handled in the mode.
    if (pressed && s_ai_down && s_ai_pending) {
        s_ai_pending = false;
        s_ai_entered = true;
        ai_enter();
    }
    if (pressed && s_ai_down && !(row == s_ai_row && col == s_ai_col)) s_ai_used = true;
    // A release whose press the mode swallowed.
    if (!pressed && rc_test(s_swallowed, row, col)) {
        rc_clear(s_swallowed, row, col);
        return true;
    }

    if (!ai_mode_active()) {
        if (pressed && is_keyboard_master()) {
            const uint8_t key = ai_key_at(row, col);
            if (key < AI_NUM_KEYS
                && (get_local_state()->ai_state[key] == AI_STATE_WAITING || ai_done_ringing(key))) {
                s_num_pending       = true;
                s_num_key           = key;
                s_num_rec           = *record;
                s_num_t             = timer_read();
                return true;      // decided in ai_mode_tick or by the next event
            }
        }
        if (pressed && keycode == KC_AI) {
            // Wake the board as any key does. A dead wake press (displays off,
            // DEAD_KEY_ON_WAKEUP) is only a wake.
            if (!display_wakeup(record)) return true;
            // Not in the mode yet: held past the tapping term (ai_mode_tick) or with
            // another key pressed it is a hold; released before, a tap (Hyper). So a
            // tap never flashes the cards.
            s_ai_down    = true;
            s_ai_row     = row;
            s_ai_col     = col;
            s_ai_t       = timer_read();
            s_ai_entered = false;
            s_ai_pending = true;
            s_ai_used    = false;
            return true;
        }
        return false;
    }

    s_last_input = timer_read32();
    update_performed();   // the swallowed events skip the normal wake / idle path
    if (!pressed) {
        // A key that was down when the mode started keeps its normal release, so a
        // layer key does not stay on (clear_keyboard() does not release layers).
        if (rc_test(s_held_at_start, row, col)) {
            rc_clear(s_held_at_start, row, col);
            return false;
        }
        // Modifier and layer releases fall through (the Intl-layer rule): a release
        // with no press behind it does nothing on the host.
        if (IS_MODIFIER_KEYCODE(keycode) || is_layer_key(keycode)) return false;
        return true;
    }

    rc_clear(s_held_at_start, row, col);   // a press means its release was missed
    if (keycode == KC_AI) {
        // A second AI press turns the mode off; its release is then swallowed.
        ai_leave("ai key");
        s_ai_down    = true;
        s_ai_row     = row;
        s_ai_col     = col;
        s_ai_t       = timer_read();
        s_ai_entered = false;
        return true;
    }
    rc_set(s_swallowed, row, col);
    const uint8_t key = ai_key_at(row, col);
    if (key < AI_NUM_KEYS) {
        ai_record(key);
        ai_leave("number");
        return true;
    }
    if (key == AI_KEY_ESC) {
        // The pager: the mode stays on. With one page there is nothing to page to, so
        // the press is swallowed and nothing is reported (the key is blank then).
        if (get_local_state()->ai_flags & AI_FLAG_MORE_PAGES) ai_record(AI_KEY_ESC);
        return true;
    }
    if (key == AI_KEY_MINUS || key == AI_KEY_PLUS) {
        // '-' dictation, '+' the next waiting agent: the host acts, the mode ends so
        // the keyboard types again (into the dictation field or the raised agent).
        ai_record(key);
        ai_leave(key == AI_KEY_MINUS ? "minus" : "plus");
        return true;
    }
    // Any other key: swallowed. Latched, it also ends the mode; while the AI key is
    // still held, the hold decides.
    if (!s_ai_down) ai_leave("other key");
    return true;
}

void ai_mode_tick(void) {
    if (s_num_pending && timer_elapsed(s_num_t) > get_poly_tapping_term()) {
        // Held: raise the waiting agent, as AI key + this number would. Its release
        // is swallowed; no digit is typed.
        s_num_pending = false;
        rc_set(s_swallowed, s_num_rec.event.key.row, s_num_rec.event.key.col);
        ai_record(s_num_key);
        // The user looks at the agent now: end its ring here. An agent that
        // reports done stops on the other half when the host marks it seen.
        s_done_left[s_num_key] = 0;
        if (s_done_last_key == s_num_key) s_done_last_key = AI_KEY_NONE;
        uprintf("ai: hold on key %u\n", (unsigned)s_num_key);
    }
    if (s_ai_down && s_ai_pending && timer_elapsed(s_ai_t) > get_poly_tapping_term()) {
        s_ai_pending = false;     // held: the mode is on while the key stays down
        s_ai_entered = true;
        ai_enter();
        return;
    }
    if (timer_elapsed32(s_keys_set_at) > AI_KEYS_HOST_TIMEOUT_MS) {
        ai_reset_keys("no host update");   // a no-op when nothing is set
        s_keys_set_at = timer_read32();    // and checked again only every 30 s
    }
    if (!ai_mode_active() || s_ai_down) return;
    if (timer_elapsed32(s_last_input) > AI_MODE_LATCH_TIMEOUT_MS) ai_leave("timeout");
}

// ---- drawing ----------------------------------------------------------------------

static const GFXfont *const s_fonts[] = {&NotoSans_Regular_Mid_19px7b};

static void draw_text_centred(const uint32_t *text) {
    int8_t xmin, xmax, ymin, ymax;
    kdisp_gfx_text_bbox(s_fonts, 1, text, &xmin, &xmax, &ymin, &ymax);
    kdisp_write_gfx_text(s_fonts, 1, (int8_t)(BUFFER_X + SCREEN_WIDTH / 2 - (xmax - xmin + 1) / 2 - xmin),
                         (int8_t)((SCREEN_HEIGHT - (ymax - ymin + 1)) / 2 - ymin), text);
}

bool ai_render_key(uint8_t row, uint8_t col) {
    if (!ai_mode_active()) return false;
    const uint8_t key = ai_key_at(row, col);
    if (key == AI_KEY_NONE) return false;
    kdisp_set_buffer(0x00);
    const poly_sync_t *s = get_local_state();
    if (key == AI_KEY_ESC) {
        // The pager, only when there is a page to go to; otherwise blank.
        if (s->ai_flags & AI_FLAG_MORE_PAGES) draw_text_centred(U"Next");
        return true;
    }
    if (key == AI_KEY_MINUS || key == AI_KEY_PLUS) {
        // The host's cards for dictation and "next waiting". Gated on the flag, not the
        // slot: a zeroed slot is the valid pool slot 0 (see below).
        const uint16_t slot = s->ai_slot[ai_card_of_key(key)];
        if ((s->ai_flags & AI_FLAG_EXTRA_CARDS) && slot < NUM_OVERLAY_SLOTS) {
            kdisp_draw_bitmap(BUFFER_X, 0, get_overlay(slot), SCREEN_WIDTH, SCREEN_HEIGHT);
        }
        return true;
    }
    // A card replaces the whole keycap, as a v25 no-legend overlay does. A key without
    // one stays blank.
    // ⚠️ The STATE decides, not the slot alone: poly_sync_t starts zeroed, so before a
    // host has sent op 1 (a host older than v31, or the feature off) every ai_slot[]
    // reads 0, a valid pool slot, and all ten keys showed pool slot 0, the ESC mark of
    // whatever application was in front (field, 2026-11-10: the VS Code logo).
    const uint16_t slot = s->ai_slot[key];
    if (s->ai_state[key] != AI_STATE_EMPTY && slot < NUM_OVERLAY_SLOTS) {
        kdisp_draw_bitmap(BUFFER_X, 0, get_overlay(slot), SCREEN_WIDTH, SCREEN_HEIGHT);
    }
    return true;
}

// ---- LEDs -------------------------------------------------------------------------

#ifdef RGB_MATRIX_ENABLE
// Raw LED values, like the flash cue (5..36): not scaled by the RGB matrix brightness.
#    define AI_LED_FULL       40u
// ⚠️ Not lower. At 8 the LEDs differ visibly from one another (first hardware
// test: idle keys side by side were not the same green), so "dim" is half of
// FULL, the lowest level at which they match. Idle and done stay apart by
// brightness (20 vs 40); stale is white at the same level.
#    define AI_LED_DIM        20u
#    define AI_BREATH_MS      1500u   // working: amber breathing
#    define AI_BLINK_HALF_MS  250u    // waiting: red blinking at 2 Hz
#    define AI_KEYS_RESCAN_MS 1000u   // the key positions follow a remap within this

static uint8_t  s_led_rc[AI_KEY_COUNT];
static uint32_t s_led_rc_t;
static bool     s_led_rc_valid;

static void paint_rc(uint8_t rc, uint8_t r, uint8_t g, uint8_t b) {
    if (rc == NO_KEY) return;
    const uint8_t led = g_led_config.matrix_co[rc >> 4][rc & 0x0F];
    if (led == NO_LED) return;
    rgb_matrix_set_color(led, r, g, b);
}

// Called only while the RGB matrix runs. With the matrix off the lights do not show
// (no borrow in this prototype; the plan's open question).
void ai_rgb_paint(void) {
    const poly_sync_t *s   = get_local_state();
    bool any = s->ai_mode && (s->ai_flags & (AI_FLAG_ESC_ATTENTION | AI_FLAG_ANY_WAITING));
    for (uint8_t i = 0; i < AI_NUM_KEYS && !any; ++i) any = s->ai_state[i] != AI_STATE_EMPTY;
    if (!any) return;

    const uint32_t now = timer_read32();
    if (!s_led_rc_valid || (uint32_t)(now - s_led_rc_t) >= AI_KEYS_RESCAN_MS) {
        ai_find_keys(s_led_rc);
        s_led_rc_t     = now;
        s_led_rc_valid = true;
    }
    const uint8_t phase  = (uint8_t)(((now % AI_BREATH_MS) * 256u) / AI_BREATH_MS);
    const uint8_t breath = (uint8_t)(4u + scale8(sin8(phase), AI_LED_FULL - 4u));
    const bool    on     = ((now / AI_BLINK_HALF_MS) & 1u) == 0u;

    for (uint8_t i = 0; i < AI_NUM_KEYS; ++i) {
        const uint8_t rc = s_led_rc[i];
        switch (s->ai_state[i]) {
            case AI_STATE_WORKING:
                paint_rc(rc, breath, (uint8_t)(breath / 3u), 0);
                break;
            case AI_STATE_WAITING:
                paint_rc(rc, on ? AI_LED_FULL : 0u, 0, 0);
                break;
            case AI_STATE_DONE:
                paint_rc(rc, 0, AI_LED_FULL, 0);
                break;
            case AI_STATE_IDLE:
                paint_rc(rc, 0, AI_LED_DIM, 0);
                break;
            case AI_STATE_STALE:
                paint_rc(rc, AI_LED_DIM, AI_LED_DIM, AI_LED_DIM);
                break;
            default:
                break;   // empty: the running effect stays
        }
    }
    if (s->ai_mode && (s->ai_flags & AI_FLAG_ESC_ATTENTION)) {
        paint_rc(s_led_rc[AI_KEY_ESC], on ? AI_LED_FULL : 0u, 0, 0);
    }
    if (s->ai_mode && (s->ai_flags & AI_FLAG_ANY_WAITING)) {
        paint_rc(s_led_rc[AI_KEY_PLUS], on ? AI_LED_FULL : 0u, 0, 0);   // '+' goes there
    }
}
#endif

// ---- HID --------------------------------------------------------------------------

void ai_hid_cmd(uint8_t *data, uint8_t length) {
    // data[2] op: 0 QUERY, 1 SET_KEYS, 2 READ_EVENTS. PROTOCOL_HISTORY.md, v31.
    const uint8_t op = length > 2 ? data[2] : 0xFFu;
    poly_sync_t  *ls = access_local_state();
    if (op == 0) {
        uint8_t rc[AI_KEY_COUNT];
        ai_find_keys(rc);
        uint8_t found = 0;
        for (uint8_t i = 0; i < AI_NUM_KEYS; ++i) found += rc[i] != NO_KEY;
        memset(data, 0, length);
        data[0] = 'P';
        data[1] = AI_CMD;
        data[2] = '.';
        data[3] = ls->ai_mode ? 1u : 0u;
        data[4] = s_ring.seq;
        data[5] = found;
        data[6] = rc[AI_KEY_ESC] != NO_KEY ? 1u : 0u;
    } else if (op == 1 && length >= 3u + AI_SET_KEYS_LEN && ai_set_keys_valid(&data[3], NUM_OVERLAY_SLOTS)) {
        uint8_t  state[AI_NUM_KEYS];
        uint8_t  flags;
        uint16_t slot[AI_NUM_CARDS];
        ai_set_keys_decode(&data[3], state, &flags, slot);
        // The host sends the keys on every pass (so a keyboard that cleared them, at
        // USB suspend, gets them back): redraw only on a change.
        const bool changed = memcmp(ls->ai_state, state, sizeof(state)) != 0
                          || memcmp(ls->ai_slot, slot, sizeof(slot)) != 0
                          || ls->ai_flags != flags;
        memcpy(ls->ai_state, state, sizeof(state));
        memcpy(ls->ai_slot, slot, sizeof(slot));
        ls->ai_flags = flags;
        s_keys_set_at = timer_read32();
        // The housekeeping state diff sends it to the slave; that also redraws the
        // slave's keys (user_sync_poly_data_handler).
        if (changed && ls->ai_mode) request_disp_refresh();
        memset(data, 0, length);
        data[0] = 'P';
        data[1] = AI_CMD;
        data[2] = '.';
    } else if (op == 2 && length > 3) {
        ai_event_t ev[AI_RING_SIZE];
        const uint8_t n = ai_ring_read(&s_ring, data[3], ev);
        memset(data, 0, length);
        data[0] = 'P';
        data[1] = AI_CMD;
        data[2] = '.';
        data[3] = s_ring.seq;
        data[4] = n;
        for (uint8_t i = 0; i < n; ++i) {
            data[5 + 2 * i] = ev[i].seq;
            data[6 + 2 * i] = ev[i].key;
        }
    } else {
        uprintf("ai: refused op %u\n", (unsigned)op);
        memset(data, 0, length);
        data[0] = 'P';
        data[1] = AI_CMD;
        data[2] = '!';
    }
    raw_hid_send(data, length);
}

void ai_hid_card(uint8_t *data, uint8_t length) {
    // data[2..3] header (bits 0..9 pool slot, 12..14 segment), data[4..63] 60 bytes.
    uint16_t slot = 0;
    uint8_t  seg  = 0;
    bool     ok   = length >= 4u + BYTES_PER_SEGMENT &&
              ai_card_header((uint16_t)(data[2] | (data[3] << 8)), NUM_OVERLAY_SLOTS, &slot, &seg);
    if (ok) {
        // Straight into the pool slot, NOT through display_to_pool: the cards are
        // written while an app's mapping is live, and the host reserves their slots.
        memcpy(get_overlay(slot) + seg * BYTES_PER_SEGMENT, &data[4], BYTES_PER_SEGMENT);
        overlay_sync_t transfer;
        transfer.adj_idx = slot;   // the slave's handler uses it as a pool slot, unmapped
        // OVERLAY_SEG_RAW_SLOT: the slave must not mark a display position for it
        // (mark_display_has_overlay_post_upload), which would show the card on the
        // key whose flat index equals the slot number.
        transfer.segment = (uint8_t)(seg | OVERLAY_SEG_RAW_SLOT);
        memcpy(transfer.overlay, &data[4], BYTES_PER_SEGMENT);
        if (!sync_succeeded(send_to_bridge(USER_SYNC_OVERLAY_DATA, (void *)&transfer, sizeof(transfer), 10))) {
            uprintf("ai: card slot %u segment %u did not reach the slave\n", (unsigned)slot, (unsigned)seg);
            // NACK, so the host sends the card again (controller._card drops its record
            // of a refused upload); an ACK here left the slave with half a card.
            ok = false;
        }
        if (ok && seg == NUM_SEGMENTS_PER_OVERLAY - 1 && ai_mode_active()) request_disp_refresh();
    } else {
        uprint("ai: refused card segment\n");
    }
    memset(data, 0, length);
    data[0] = 'P';
    data[1] = AI_CARD_CMD;
    data[2] = ok ? '.' : '!';
    raw_hid_send(data, length);
}

// ---- attention ripple ---------------------------------------------------------------

// One ripple runs TUT_RIPPLE_MS (2.2 s); the rest of the period is still, so the ring
// reads as a pulse, not a constant motion. Both halves run this from the synced key
// states with the same period (AI_ATTENTION_PERIOD_MS), so their rings start
// within the sync latency.

#ifdef KEYBOARD_polykybd_split72
// Start the ripple on agent key i. False when the key is not on the default layer
// or has no display.
static bool ai_ring_key(uint8_t i) {
    uint8_t rc[AI_KEY_COUNT];
    ai_find_keys(rc);
    if (rc[i] == NO_KEY) return false;
    const uint8_t slot = poly_slot_of((uint8_t)(rc[i] >> 4), (uint8_t)(rc[i] & 0x0Fu));
    if (slot == TUT_SLOT_NONE) return false;
    poly_focus_start(slot);
    return true;
}
#endif

void ai_attention_tick(void) {
#ifdef KEYBOARD_polykybd_split72
    static uint32_t s_last;
    static bool     s_started;
    static uint8_t  s_next;
    static uint8_t  s_next_done;
    static uint8_t  s_prev[AI_NUM_KEYS];        // the states of the last pass
    const poly_sync_t *s = get_local_state();
    // Before the gates below: a finish during a running ripple must still count.
    ai_done_rings_update(s_prev, s->ai_state, s_done_left);
    // Seen, busy again or gone: the last ripple's key is no longer a hold key.
    if (s_done_last_key < AI_NUM_KEYS && s_prev[s_done_last_key] != s->ai_state[s_done_last_key]) {
        s_done_last_key = AI_KEY_NONE;
    }
    memcpy(s_prev, s->ai_state, sizeof(s_prev));
    if (tutorial_active() || !poly_render_live() || poly_focus_active() || s->fw_confirm) return;
    if (key_attention_pending()) return;   // a host request (cmd 47) has the ring
    if (s_started && timer_elapsed32(s_last) < AI_ATTENTION_PERIOD_MS) return;
    // A waiting agent first: it cannot go on without the user.
    for (uint8_t k = 0; k < AI_NUM_KEYS; ++k) {
        const uint8_t i = (uint8_t)((s_next + k) % AI_NUM_KEYS);
        if (s->ai_state[i] != AI_STATE_WAITING || !ai_ring_key(i)) continue;
        s_last    = timer_read32();
        s_started = true;
        s_next    = (uint8_t)(i + 1u);
        return;
    }
    // Then an agent that finished (ai_done_rings_update), AI_DONE_RIPPLES times.
    for (uint8_t k = 0; k < AI_NUM_KEYS; ++k) {
        const uint8_t i = (uint8_t)((s_next_done + k) % AI_NUM_KEYS);
        if (s_done_left[i] == 0) continue;
        --s_done_left[i];   // also when the key cannot ring, so it does not block others
        if (!ai_ring_key(i)) continue;
        s_last          = timer_read32();
        s_started       = true;
        s_next_done     = (uint8_t)(i + 1u);
        s_done_last_key = i;
        s_done_last_t   = s_last;
        return;
    }
#endif
}
