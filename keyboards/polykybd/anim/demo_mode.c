// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#include "demo_mode.h"

#include "quantum.h"
#include QMK_KEYBOARD_H            // invert_display / key_has_display / MATRIX_ROWS_PER_SIDE
#include "base/demo_plan.h"
#include "base/update.h"           // update_performed / backdate_last_update / request_disp_refresh
#include "bridge_helper.h"         // is_usb_host_side()
#include "side.h"                  // is_left_side()
#include "state.h"
#include "layers.h"
#include "keycode_helper.h"        // KC_LANG
#include "lang/lang_lut.h"         // LANG_*
#include "lang_layer.h"            // lang_select_region / lang_pack_state / lang_apply_sync
#include "emoji/emoji_layer.h"     // emj_apply_sync / EMJ_NUM_CATS_VISIBLE
#include "poly_keymap.h"           // poly_fw_screen / poly_wake_from_idle / poly_preview_renderable
#include "poly_macro.h"
#include "poly_macro_record.h"
#include "anim/tutorial.h"
#include "anim/startup_anim.h"
#include "base/fw_staging.h"
#include "doom/doom_mode.h"

_Static_assert(sizeof(((poly_sync_t *)0)->demo) == DEMO_SYNC_BYTES,
               "poly_sync_t.demo does not match DEMO_SYNC_BYTES");
_Static_assert(sizeof(demo_playlist[0].look) == 1 && DEMO_LOOK_COUNT <= 255, "look ids fit a byte");

// ---- abstract plan ids -> firmware ids -----------------------------------------
typedef struct {
    bool    script;   // value is a glyph script, else a language
    uint8_t value;
} demo_look_map_t;

static const demo_look_map_t s_looks[DEMO_LOOK_COUNT] = {
    [DEMO_LOOK_OWN]       = {false, 0},
    [DEMO_LOOK_GERMAN]    = {false, LANG_DEDE},
    [DEMO_LOOK_FRENCH]    = {false, LANG_FRFR},
    [DEMO_LOOK_GREEK]     = {false, LANG_ELGR},
    [DEMO_LOOK_RUSSIAN]   = {false, LANG_RURU},
    [DEMO_LOOK_UKRAINIAN] = {false, LANG_UKUA},
    [DEMO_LOOK_ARABIC]    = {false, LANG_ARSA},
    [DEMO_LOOK_HEBREW]    = {false, LANG_HEIL},
    [DEMO_LOOK_HINDI]     = {false, LANG_HIIN},
    [DEMO_LOOK_THAI]      = {false, LANG_THTH},
    [DEMO_LOOK_JAPANESE]  = {false, LANG_JAJP},
    [DEMO_LOOK_KOREAN]    = {false, LANG_KOKR},
    [DEMO_LOOK_GEORGIAN]  = {false, LANG_KAGE},
    [DEMO_LOOK_ARMENIAN]  = {false, LANG_HYAM},
    [DEMO_LOOK_TENGWAR]   = {true, GLYPH_TENGWAR},
    [DEMO_LOOK_RUNES]     = {true, GLYPH_RUNES},
    [DEMO_LOOK_AUREBESH]  = {true, GLYPH_AUREBESH},
    [DEMO_LOOK_CIRTH]     = {true, GLYPH_CIRTH},
    [DEMO_LOOK_SGA]       = {true, GLYPH_SGA},
    [DEMO_LOOK_IBMVGA]    = {true, GLYPH_IBMVGA},
    [DEMO_LOOK_C64]       = {true, GLYPH_C64},
    [DEMO_LOOK_AMIGA]     = {true, GLYPH_AMIGA},
    [DEMO_LOOK_BRAILLE]   = {true, GLYPH_BRAILLE},
};

typedef struct {
    uint8_t  layer;     // shown on top of the user's own base layout
    uint16_t trigger;   // the key that opens it on the base layout; KC_NO = none shown
} demo_view_map_t;

static const demo_view_map_t s_views[DEMO_VIEW_COUNT] = {
    [DEMO_VIEW_BASE]      = {_BL,       KC_NO},
    [DEMO_VIEW_FN]        = {_FL,       MO(_FL)},
    [DEMO_VIEW_NUM]       = {_NL,       MO(_NL)},
    [DEMO_VIEW_INTL]      = {_ADDLANG1, MO(_ADDLANG1)},
    [DEMO_VIEW_LANG_MENU] = {_LL,       KC_LANG},
    [DEMO_VIEW_EMOJI]     = {_EMJ,      TO(_EMJ)},
    [DEMO_VIEW_UTIL]      = {_UL,       KC_NO},
    [DEMO_VIEW_SETTINGS]  = {_SL,       KC_NO},
};

// ---- master state ---------------------------------------------------------------
static bool     s_active;
static uint8_t  s_seg;
static uint32_t s_seg_t0;        // when the master entered s_seg
static uint32_t s_last_tick;     // to notice a suspend (housekeeping does not run then)
static uint8_t  s_page;          // the tab a paged SHOW segment is on
static bool     s_look_on;       // the segment's look is drawable on this board
static uint8_t  s_mods;          // display-only Shift
static uint32_t s_esc_since;     // 0 = Esc not held
static uint8_t  s_esc_row = 0xFF, s_esc_col = 0xFF;
static bool     s_swallow_esc_release;
// What the menus showed before the demo, handed back on exit.
static uint8_t  s_saved_emj_cat, s_saved_emj_page, s_saved_lang_pack;

// ---- key demo: what the host receives (master only) ----------------------------
static bool     s_keys;          // KC_DEMO_KEYS: typed keys also reach the host
static uint16_t s_stroke;        // next stroke of the current TYPE segment to send
static uint8_t  s_held;          // usage held down at the host; 0 = none
static uint32_t s_held_until;    // ms into the segment when s_held goes up

// ---- per-half highlight state ---------------------------------------------------
#define DEMO_NO_POS 0xFFu
#define DEMO_HL_MAX 2u
static uint8_t  s_hl[DEMO_HL_MAX] = {DEMO_NO_POS, DEMO_NO_POS};   // positions inverted now
static uint8_t  s_rx_seg = 0xFF;   // segment index this half is timing
static bool     s_rx_on;
static uint32_t s_rx_t0;           // when this half saw s_rx_seg arrive

bool demo_active(void) {
    return s_active;
}

bool demo_sync_active(void) {
    return (get_local_state()->demo[0] & DEMO_SYNC_ACTIVE) != 0;
}

bool demo_sync_sends_keys(void) {
    const uint8_t f = get_local_state()->demo[0];
    return (f & DEMO_SYNC_ACTIVE) != 0 && (f & DEMO_SYNC_KEYS) != 0;
}

uint8_t demo_display_mods(void) {
    return s_active ? s_mods : 0;
}

bool demo_preview(bool *script, uint8_t *value) {
    if (!s_active || !s_look_on) return false;
    const demo_look_map_t *m = &s_looks[demo_playlist[s_seg].look];
    *script = m->script;
    *value  = m->value;
    return true;
}

// ---- key positions --------------------------------------------------------------

static uint8_t def_layer(void) {
    return (uint8_t)get_local_layer()->def_layer;
}

static uint16_t base_keycode(uint8_t row, uint8_t col) {
    uint16_t kc = keymap_key_to_keycode(def_layer(), (keypos_t){.row = row, .col = col});
    kc = poly_mt_tap(kc);
    return kc;
}

// Where `kc` sits on the user's own base layout (row * MATRIX_COLS + col), first match
// in matrix order; DEMO_NO_POS when the layout has no such key. Searched from the
// LAYOUT, not a fixed table, so a Colemak or Neo board presses its own keys.
static uint8_t find_key(uint16_t kc) {
    if (kc == KC_NO) return DEMO_NO_POS;
    for (uint8_t r = 0; r < MATRIX_ROWS; ++r) {
        for (uint8_t c = 0; c < MATRIX_COLS; ++c) {
            if (base_keycode(r, c) == kc) return (uint8_t)(r * MATRIX_COLS + c);
        }
    }
    return DEMO_NO_POS;
}

// Memoised per wanted keycode: the search walks the whole matrix, and the wanted key
// only changes a few times a second while the highlight is re-evaluated every pass.
static uint8_t find_key_cached(uint8_t slot, uint16_t kc) {
    static uint16_t s_kc[DEMO_HL_MAX]  = {KC_NO, KC_NO};
    static uint8_t  s_pos[DEMO_HL_MAX] = {DEMO_NO_POS, DEMO_NO_POS};
    static uint8_t  s_def[DEMO_HL_MAX] = {0xFF, 0xFF};
    if (s_kc[slot] != kc || s_def[slot] != def_layer()) {
        s_kc[slot]  = kc;
        s_def[slot] = def_layer();
        s_pos[slot] = find_key(kc);
    }
    return s_pos[slot];
}

static bool pos_on_this_half(uint8_t pos) {
    if (pos == DEMO_NO_POS) return false;
    const uint8_t r     = pos / MATRIX_COLS;
    const uint8_t c     = pos % MATRIX_COLS;
    const uint8_t first = is_left_side() ? 0 : MATRIX_ROWS_PER_SIDE;
    return r >= first && r < first + MATRIX_ROWS_PER_SIDE && key_has_display(r, c);
}

static void set_highlight(uint8_t pos, bool on) {
    if (!pos_on_this_half(pos)) return;
    invert_display(pos / MATRIX_COLS, pos % MATRIX_COLS, on);
}

// Move this half's highlights to `want`, touching only the keys that changed: an
// un-invert and an invert are one SPI command each, a repaint is ~100 ms.
static void apply_highlights(const uint8_t want[DEMO_HL_MAX]) {
    for (uint8_t i = 0; i < DEMO_HL_MAX; ++i) {
        if (s_hl[i] == DEMO_NO_POS) continue;
        bool kept = false;
        for (uint8_t j = 0; j < DEMO_HL_MAX; ++j) kept |= (want[j] == s_hl[i]);
        if (!kept) set_highlight(s_hl[i], false);
    }
    for (uint8_t j = 0; j < DEMO_HL_MAX; ++j) {
        if (want[j] == DEMO_NO_POS) continue;
        bool had = false;
        for (uint8_t i = 0; i < DEMO_HL_MAX; ++i) had |= (s_hl[i] == want[j]);
        if (!had) set_highlight(want[j], true);
    }
    for (uint8_t i = 0; i < DEMO_HL_MAX; ++i) s_hl[i] = want[i];
}

// Both halves: what is pressed `into` ms into segment `seg`.
static void highlights_for(uint8_t seg, uint32_t into, uint8_t want[DEMO_HL_MAX]) {
    want[0] = want[1] = DEMO_NO_POS;
    if (seg >= demo_playlist_len) return;
    const demo_seg_t *s = &demo_playlist[seg];
    if (s->kind == DEMO_TYPE) {
        const demo_keys_t k = demo_type_keys(s->text, into);
        if (k.key_down) {
            bool shift = false;
            want[0] = find_key_cached(0, demo_ascii_usage(k.ch, &shift));
        }
        if (k.shift_down) {
            uint8_t pos = find_key_cached(1, KC_LEFT_SHIFT);
            if (pos == DEMO_NO_POS) pos = find_key(KC_RIGHT_SHIFT);
            want[1] = pos;
        }
    } else if (s->kind == DEMO_SHOW && demo_show_trigger_down(s, into)) {
        want[0] = find_key_cached(0, s_views[s->view].trigger);
    }
}

// ---- key demo: keystrokes to the host --------------------------------------------

static void host_release(void) {
    if (s_held == 0) return;
    unregister_code(s_held);
    s_held = 0;
}

static void host_press(uint8_t usage, uint32_t until) {
    host_release();
    if (usage == 0) return;
    // Never a modifier. Nothing should have registered one (clear_keyboard() ran at
    // the start and every real key is swallowed), but a Shift or GUI riding along on
    // one keystroke is exactly what would switch a window or fire a shortcut.
    // ⚠️ clear_keyboard() does NOT clear a pending one-shot modifier, and
    // get_mods_for_report() ORs it into the next report — so a remapped OSM() tapped
    // before the demo started would ride on its first keystroke. Clear all three.
    if ((get_mods() | get_weak_mods()) != 0) {
        clear_mods();
        clear_weak_mods();
    }
#ifndef NO_ACTION_ONESHOT
    if (get_oneshot_mods() != 0) clear_oneshot_mods();
#endif
    register_code(usage);   // a basic keycode: goes straight into the report, no layers
    s_held       = usage;
    s_held_until = until;
}

// Send every stroke of `s` that is due `into` ms in, then lift the held one when its
// DEMO_DOWN_MS are over. Strokes are counted, not sampled from the highlight: a slow
// housekeeping pass (a segment repaint is ~100 ms) can step right over an 85 ms press,
// and a typing test must not lose that character. A late stroke is then sent at once,
// and the next stroke's press lifts it.
static void host_keys_tick(const demo_seg_t *s, uint32_t into) {
    if (!s_keys || s->kind != DEMO_TYPE) {
        host_release();
        return;
    }
    const uint16_t n = demo_type_strokes(s->text);
    while (s_stroke < n) {
        const uint32_t at = demo_stroke_press_ms(s->text, s_stroke);
        if (at > into) break;
        host_press(demo_stroke_usage(s->text, s_stroke), at + DEMO_DOWN_MS);
        ++s_stroke;
    }
    if (s_held != 0 && into >= s_held_until) host_release();
}

// ---- the master's segment machine -----------------------------------------------

static void show_view(const demo_seg_t *s) {
    layer_clear();
    layer_on(def_layer());
    if (s->kind == DEMO_SHOW && s->view != DEMO_VIEW_BASE) {
        layer_on(s_views[s->view].layer);
    }
}

static void show_page(const demo_seg_t *s, uint8_t page) {
    s_page = page;
    if (s->kind != DEMO_SHOW) return;
    if (s->view == DEMO_VIEW_LANG_MENU) {
        lang_select_region((uint8_t)(page % NUM_LANG_REGIONS));
    } else if (s->view == DEMO_VIEW_EMOJI) {
        emj_apply_sync((uint8_t)(page % EMJ_NUM_CATS_VISIBLE), 0);
    }
}

static void enter_segment(uint8_t seg, uint32_t now) {
    const bool        was_idle = demo_playlist[s_seg].kind == DEMO_IDLE;
    const demo_seg_t *s        = &demo_playlist[seg];
    s_seg    = seg;
    s_seg_t0 = now;
    s_mods   = 0;
    host_release();
    s_stroke = 0;
    if (was_idle && s->kind != DEMO_IDLE) {
        poly_wake_from_idle();   // the same wake the host's "stop idle" performs
    }
    show_view(s);
    show_page(s, 0);
    // A look the flashed fonts cannot draw is skipped rather than shown as blanks: a
    // board without the font pack still demos, just in its own language.
    const demo_look_map_t *m = &s_looks[s->look];
    s_look_on = s->look != DEMO_LOOK_OWN && poly_preview_renderable(m->script, m->value);
    if (s->kind == DEMO_IDLE) {
        // Due NOW: the real idle machinery fades over FADE_TRANSITION_TIME and then
        // runs whatever idle style the user picked — the demo shows THEIR screensaver.
        backdate_last_update(get_idle_timeout_ms());
    } else {
        update_performed();
    }
    access_local_state()->demo[1] = seg;
    request_disp_refresh();
    uprintf("Demo: segment %u/%u (kind %u)\n", (unsigned)seg + 1u, (unsigned)demo_playlist_len,
            (unsigned)s->kind);
}

static bool demo_blocked(void) {
    return tutorial_active() || startup_anim_active() || fw_staging_fw_up_active() ||
           poly_fw_screen() != POLY_FW_SCREEN_NONE || doom_mode_active() ||
           poly_macro_rec_state() != POLY_REC_IDLE;
}

bool demo_start(bool send_keys) {
    if (!is_usb_host_side() || s_active) return false;
    if (demo_blocked()) {
        uprint("Demo: refused, another mode owns the board\n");
        return false;
    }
    if (poly_macro_active()) poly_macro_abort();
    // Nothing the user was holding may stay registered while every event is swallowed.
    clear_keyboard();
    s_saved_emj_cat   = emj_active_category();
    s_saved_emj_page  = emj_active_page();
    s_saved_lang_pack = lang_pack_state();
    s_active              = true;
    s_esc_since           = 0;
    s_swallow_esc_release = false;
    s_last_tick           = timer_read32();
    s_keys                = send_keys;
    s_held                = 0;
    access_local_state()->demo[0] = (uint8_t)(DEMO_SYNC_ACTIVE | (send_keys ? DEMO_SYNC_KEYS : 0u));
    s_seg = 0;
    enter_segment(0, s_last_tick);
    uprintf("Demo: started%s, %lu s per loop\n", send_keys ? " (keys to host)" : "",
            (unsigned long)(demo_cycle_ms() / 1000u));
    return true;
}

void demo_stop(void) {
    if (!s_active) return;
    const bool was_idle = demo_playlist[s_seg].kind == DEMO_IDLE;
    host_release();   // a key left down at the host would auto-repeat until USB drops
    s_keys    = false;
    s_active  = false;
    s_look_on = false;
    s_mods    = 0;
    poly_sync_t *ls = access_local_state();
    ls->demo[0] = 0;
    ls->demo[1] = 0;
    if (was_idle || (ls->flags & DISP_IDLE) != 0) poly_wake_from_idle();
    layer_clear();
    layer_on(def_layer());
    emj_apply_sync(s_saved_emj_cat, s_saved_emj_page);
    lang_apply_sync(s_saved_lang_pack);
    update_performed();
    request_disp_refresh();
    uprint("Demo: stopped\n");
}

static void master_tick(uint32_t now) {
    if (!s_active) return;
    // Something with a stronger claim took the board (a host-driven flash, its
    // signing prompt): get out of its way rather than fight it for the keycaps.
    if (fw_staging_fw_up_active() || poly_fw_screen() != POLY_FW_SCREEN_NONE) {
        demo_stop();
        return;
    }
    // Housekeeping does not run while USB is suspended. Resume the segment where it
    // was instead of fast-forwarding through everything the sleep "missed".
    const uint32_t gap = now - s_last_tick;
    if (gap > 1000u) s_seg_t0 += gap;
    s_last_tick = now;

    if (s_esc_since != 0 && timer_elapsed32(s_esc_since) >= DEMO_EXIT_HOLD_MS) {
        s_swallow_esc_release = true;
        demo_stop();
        return;
    }

    const demo_seg_t *s    = &demo_playlist[s_seg];
    const uint32_t    into = now - s_seg_t0;
    // Before the segment change, so a late pass still sends this segment's last
    // characters and its Enter rather than dropping them.
    host_keys_tick(s, into);
    if (into >= demo_seg_ms(s)) {
        enter_segment((uint8_t)((s_seg + 1u) % demo_playlist_len), now);
        return;
    }
    if (s->kind == DEMO_IDLE) return;   // the idle block owns the board; let it run
    update_performed();                 // everything else is activity
    if (s->kind == DEMO_SHOW) {
        const uint8_t page = demo_show_page(s, into);
        if (page != s_page) show_page(s, page);
    } else if (s->kind == DEMO_TYPE) {
        s_mods = demo_type_keys(s->text, into).shift_down ? MOD_BIT(KC_LEFT_SHIFT) : 0;
    }
}

// Both halves, every pass: time the synced segment locally and move the highlights.
static void highlight_tick(uint32_t now) {
    const poly_sync_t *ls  = get_local_state();
    const bool         on  = (ls->demo[0] & DEMO_SYNC_ACTIVE) != 0;
    const uint8_t      seg = ls->demo[1];
    uint8_t            want[DEMO_HL_MAX] = {DEMO_NO_POS, DEMO_NO_POS};
    if (on) {
        if (!s_rx_on || seg != s_rx_seg) {
            s_rx_on  = true;
            s_rx_seg = seg;
            s_rx_t0  = now;
        }
        highlights_for(seg, now - s_rx_t0, want);
    } else {
        s_rx_on  = false;
        s_rx_seg = 0xFF;
    }
    apply_highlights(want);
}

void demo_tick(void) {
    const uint32_t now = timer_read32();
    if (is_usb_host_side()) master_tick(now);
    highlight_tick(now);
}

// ---- input ------------------------------------------------------------------------

static bool is_exit_key(keyrecord_t *record) {
    const uint16_t kc = base_keycode(record->event.key.row, record->event.key.col);
    return kc == KC_ESC || kc == QK_GRAVE_ESCAPE;
}

bool demo_process_record(uint16_t keycode, keyrecord_t *record) {
    (void)keycode;   // the BASE layout's key decides, not whatever layer the demo shows
    const uint8_t row = record->event.key.row, col = record->event.key.col;
    if (!s_active) {
        // The exit fires while Esc is still down; its release must not reach the host
        // as an Esc nobody typed.
        if (s_swallow_esc_release && !record->event.pressed && row == s_esc_row && col == s_esc_col) {
            s_swallow_esc_release = false;
            s_esc_row = s_esc_col = 0xFF;
            return true;
        }
        return false;
    }
    if (is_exit_key(record)) {
        if (record->event.pressed) {
            s_esc_since = timer_read32() | 1u;   // 0 means "not held"
            s_esc_row   = row;
            s_esc_col   = col;
        } else {
            s_esc_since = 0;   // let go early: the hold has to be continuous
        }
    }
    // The demo IS the board: no visitor's key reaches the host. In the key demo the
    // host still receives the demo's own strokes, which bypass this path entirely.
    return true;
}
