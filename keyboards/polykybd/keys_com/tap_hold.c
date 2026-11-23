// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// QMK's tap-hold hooks: the tapping term, quick tap term and flow tap term set over
// HID cmd 44 (base/tap_terms.h), Caps Word's letter test for the active language, and
// Chordal Hold's left/right hand per key. Split out of poly_keymap.c.

#include QMK_KEYBOARD_H

#include "lib/system/state.h"
#include "lang/lang_lut.h"
#include "base/caps_word_letter.h"
#include "base/tap_terms.h"
#include "poly_combos.h"

// ---- Tap-hold timing (HID cmd 44, base/tap_terms.h) --------------------------
//
// QMK asks these hooks for every tap-hold decision, so a value set over HID takes
// effect on the next key press. The compile-time TAPPING_TERM / FLOW_TAP_TERM are
// only the defaults now; the asserts keep them equal to what an unset EEPROM byte
// decodes to, so a board that never touches the setting behaves as before.
_Static_assert(TAPPING_TERM == POLY_DEFAULT_TAPPING_TERM_MS,
               "TAPPING_TERM and POLY_DEFAULT_TAPPING_TERM_MS have drifted");

#ifdef CAPS_WORD_ENABLE
// Caps Word's key test, with the letters of the active language instead of QMK's
// fixed KC_A..KC_Z: a key is a letter when its plain and shifted legends are the two
// cases of one letter (base/caps_word_letter.h). On de-DE that adds ö, ä, ü and
// leaves comma and period out; on fr-FR it makes KC_SCLN (m) a letter and KC_M (,)
// not. A KC_A..KC_Z key of a script without case (Hebrew, Arabic, ...) continues
// the word without Shift. Minus becomes underscore only where Shift+Minus types
// "_". Digits, Backspace, Delete and underscore continue the word without Shift, as
// in QMK's default; any other key ends it.
bool caps_word_press_user(uint16_t keycode) {
    const uint8_t lang = get_local_state()->lang;
    if ((keycode >= KC_A && keycode <= KC_SLASH) || keycode == KC_NONUS_BACKSLASH) {
        const uint32_t* plain   = translate_keycode(lang, keycode, false, false);
        const uint32_t* shifted = translate_keycode(lang, keycode, true, false);
        if (caps_word_is_letter(plain, shifted)) {
            add_weak_mods(MOD_BIT(KC_LSFT));
            return true;
        }
        if (keycode == KC_MINUS && legend_first_char(shifted) == '_') {
            add_weak_mods(MOD_BIT(KC_LSFT));
            return true;
        }
    }
    switch (keycode) {
        case KC_A ... KC_Z:
        case KC_1 ... KC_0:
        case KC_BSPC:
        case KC_DEL:
        case KC_UNDS:
            return true;
        default:
            return false;
    }
}
#endif

uint16_t get_tapping_term(uint16_t keycode, keyrecord_t *record) {
    // A runtime tap dance (cmd 48) can carry its own term; 0 means the global one.
    const uint16_t td_term = poly_tap_dance_term(keycode);
    return td_term != 0 ? td_term : get_poly_tapping_term();
}

// QUICK_TAP_TERM is still the compile-time value (it defaults to TAPPING_TERM);
// QMK requires it not to exceed the tapping term, so a shorter term pulls it down.
uint16_t get_quick_tap_term(uint16_t keycode, keyrecord_t *record) {
    return quick_tap_term_for(QUICK_TAP_TERM, get_poly_tapping_term());
}

#ifdef FLOW_TAP_TERM
_Static_assert(FLOW_TAP_TERM == POLY_DEFAULT_FLOW_TAP_TERM_MS,
               "FLOW_TAP_TERM and POLY_DEFAULT_FLOW_TAP_TERM_MS have drifted");

// Same key selection as QMK's default get_flow_tap_term() (is_flow_tap_key() for
// both keys), with the configured term in place of FLOW_TAP_TERM. 0 turns it off.
uint16_t get_flow_tap_term(uint16_t keycode, keyrecord_t *record, uint16_t prev_keycode) {
    if (is_flow_tap_key(keycode) && is_flow_tap_key(prev_keycode)) {
        return get_poly_flow_tap_term();
    }
    return 0;
}
#endif

#ifdef CHORDAL_HOLD
// Chordal Hold's opposite-hands rule needs to know which half a key is on. QMK's
// default reads a hand-maintained `chordal_hold_layout[MATRIX_ROWS][MATRIX_COLS]`
// table of 'L'/'R' chars -- 80 entries here, beside a keymap that already has five
// base layers to keep in step. On a split whose halves are exactly
// MATRIX_ROWS_PER_SIDE apart the hand is DERIVABLE, so derive it: the hook is weak
// precisely so a board can answer the question its own way, and a derivation cannot
// drift out of sync with the matrix the way a second table would. Same reasoning as
// sync_is_link_fault() refusing to enumerate its siblings.
// Rows 0..MATRIX_ROWS_PER_SIDE-1 are the left half, the rest the right -- the same
// split LAYOUT_TO_INDEX() and is_left_side() already assume.
char chordal_hold_handedness(keypos_t key) {
    return (key.row < MATRIX_ROWS_PER_SIDE) ? 'L' : 'R';
}
#endif
