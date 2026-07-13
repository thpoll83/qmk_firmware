// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// split42 VARIANT DATA ONLY. All shared keymap behaviour lives in
// keyboards/polykybd/poly_keymap.c (compiled for every variant), so this 42-key
// variant stays in lockstep with split72. This file defines only the data the
// linker pulls from the keymap TU:
//   * keymaps[]      — the layer definitions (LAYOUT_lr_stacked42)
//   * encoder_map[]  — the rotary-encoder action map
// (split42 has no RGB matrix, so there is no g_led_config here.)
//
// The 42-key layout macro is called LAYOUT_lr_stacked42 (see keyboard.json). Key order
// per layer: LEFT rows 0-2 (6 each) + LEFT thumbs (3), then RIGHT rows 0-2 (6 each)
// + RIGHT thumbs (3) = 42.
#include QMK_KEYBOARD_H
#include "split42/split42.h"
#include "layers.h"
#include "keycode_helper.h"
#include "emoji/emoji_layer.h"
#include "lang_layer.h"

const uint16_t keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    /* Base layer 0 — Qwerty */
    [_L0] = LAYOUT_lr_stacked42(
        KC_ESC,  KC_Q,    KC_W,    KC_E,    KC_R,    KC_T,
        MO(_FL0),KC_A,    KC_S,    KC_D,    KC_F,    KC_G,
        KC_LSFT, KC_Z,    KC_X,    KC_C,    KC_V,    KC_B,
        KC_LCTL, KC_SPC,  KC_DEL,
        KC_Y,    KC_U,    KC_I,    KC_O,    KC_P,    KC_BSPC,
        KC_H,    KC_J,    KC_K,    KC_L,    KC_EQUAL,KC_ENTER,
        KC_N,    KC_M,    KC_COMM, KC_SCLN, KC_UP,   KC_RSFT,
        KC_LANG, KC_SLSH, KC_LEFT
    ),
    /* Base layer 1 — Qwerty Staggered */
    [_L1] = LAYOUT_lr_stacked42(
        KC_ESC,  KC_Q,    KC_W,    KC_E,    KC_R,    KC_T,
        MO(_FL1),KC_A,    KC_S,    KC_D,    KC_F,    KC_G,
        KC_LSFT, KC_Z,    KC_X,    KC_C,    KC_V,    KC_B,
        KC_LCTL, KC_SPC,  KC_DEL,
        KC_Y,    KC_U,    KC_I,    KC_O,    KC_P,    KC_BSPC,
        KC_H,    KC_J,    KC_K,    KC_L,    KC_SCLN, KC_BSLS,
        KC_N,    KC_M,    KC_COMM, KC_DOT,  KC_SLSH, KC_RSFT,
        KC_ENTER,KC_BSPC, KC_LEFT
    ),
    /* Base layer 2 — Colemak DH */
    [_L2] = LAYOUT_lr_stacked42(
        KC_ESC,  KC_Q,    KC_W,    KC_F,    KC_P,    KC_B,
        MO(_FL1),KC_A,    KC_R,    KC_S,    KC_T,    KC_G,
        KC_LSFT, KC_Z,    KC_X,    KC_C,    KC_D,    KC_V,
        KC_LCTL, KC_SPC,  KC_DEL,
        KC_J,    KC_L,    KC_U,    KC_Y,    KC_SCLN, KC_BSPC,
        KC_M,    KC_N,    KC_E,    KC_I,    KC_O,    KC_ENTER,
        KC_K,    KC_H,    KC_COMM, KC_DOT,  KC_SLSH, KC_RSFT,
        KC_LANG, KC_BSPC, KC_LEFT
    ),
    /* Base layer 3 — Neo */
    [_L3] = LAYOUT_lr_stacked42(
        KC_ESC,  KC_X,    KC_V,    KC_L,    KC_C,    KC_W,
        MO(_FL0),KC_U,    KC_I,    KC_A,    KC_E,    KC_O,
        KC_LSFT, DE_HASH, DE_UDIA, DE_ODIA, DE_ADIA, KC_P,
        KC_LCTL, KC_SPC,  KC_DEL,
        KC_K,    KC_H,    KC_G,    KC_F,    KC_Q,    KC_BSPC,
        KC_S,    KC_N,    KC_R,    KC_T,    KC_D,    KC_ENTER,
        KC_B,    KC_M,    KC_COMM, KC_DOT,  DE_Y,    KC_RSFT,
        KC_LANG, KC_BSPC, KC_LEFT
    ),
    /* Base layer 4 — Workman */
    [_L4] = LAYOUT_lr_stacked42(
        KC_ESC,  KC_Q,    KC_D,    KC_R,    KC_W,    KC_B,
        MO(_FL1),KC_A,    KC_S,    KC_H,    KC_T,    KC_G,
        KC_LSFT, KC_Z,    KC_X,    KC_M,    KC_C,    KC_V,
        KC_LCTL, KC_SPC,  KC_DEL,
        KC_J,    KC_F,    KC_U,    KC_P,    KC_SCLN, KC_BSPC,
        KC_Y,    KC_N,    KC_E,    KC_O,    KC_I,    KC_ENTER,
        KC_K,    KC_L,    KC_COMM, KC_DOT,  KC_SLSH, KC_RSFT,
        KC_LANG, KC_BSPC, KC_LEFT
    ),
    /* Function layer 0 */
    [_FL0] = LAYOUT_lr_stacked42(
        OSL(_UL),KC_F1,   KC_F2,   KC_F3,   KC_F4,   KC_F5,
        _______, _______, _______, _______, _______, _______,
        KC_CAPS, _______, _______, _______, _______, _______,
        _______, _______, TO(_UL),
        KC_F6,   KC_F7,   KC_F8,   KC_F9,   KC_F10,  KC_F11,
        MS_BTN1, MS_BTN2, _______, _______, _______, KC_F12,
        TO(_NL), _______, _______, _______, _______, KC_INS,
        KC_HOME, KC_PGUP, KC_END
    ),
    /* Function layer 1 */
    [_FL1] = LAYOUT_lr_stacked42(
        OSL(_UL),KC_F1,   KC_F2,   KC_F3,   KC_F4,   KC_F5,
        _______, _______, _______, _______, _______, _______,
        _______, _______, _______, _______, _______, _______,
        _______, _______, KC_INS,
        KC_F6,   KC_F7,   KC_F8,   KC_F9,   KC_F10,  KC_F11,
        MS_BTN1, MS_BTN2, _______, _______, _______, KC_F12,
        TO(_NL), _______, _______, _______, KC_CAPS, _______,
        KC_HOME, KC_PGUP, KC_END
    ),
    /* Numpad layer */
    [_NL] = LAYOUT_lr_stacked42(
        KC_NO,   KC_NUM,  KC_PSLS, KC_PAST, KC_PMNS, KC_NO,
        MS_BTN1, KC_KP_7, KC_KP_8, KC_KP_9, KC_PPLS, KC_INS,
        KC_NO,   KC_KP_4, KC_KP_5, KC_KP_6, KC_PPLS, KC_DEL,
        KC_BASE, KC_KP_0, KC_PDOT,
        KC_NO,   KC_INS,  KC_KP_7, KC_KP_8, KC_KP_9, KC_PPLS,
        KC_NO,   KC_DEL,  KC_KP_4, KC_KP_5, KC_KP_6, KC_PPLS,
        MS_BTN2, KC_NO,   KC_KP_1, KC_KP_2, KC_KP_3, KC_PENT,
        KC_PENT, KC_KP_0, KC_BASE
    ),
    /* Utility layer */
    [_UL] = LAYOUT_lr_stacked42(
        KC_NO,   KC_F13,  KC_F14,  KC_F15,  KC_F16,  KC_F17,
        KC_MYCM, KC_CALC, KC_PSCR, KC_SCRL, KC_BRK,  KC_NO,
        KC_LSFT, KC_NO,   KC_NO,   KC_NO,   KC_NO,   KC_NO,
        KC_BASE, KC_NO,   KC_NO,
        KC_F18,  KC_F19,  KC_MPRV, KC_MPLY, KC_MSTP, KC_MNXT,
        KC_NO,   KC_NO,   KC_NO,   KC_NO,   KC_MUTE, KC_NO,
        KC_NO,   KC_VOLD, KC_VOLU, KC_NO,   KC_NO,   KC_RSFT,
        KC_NO,   KC_NO,   KC_BASE
    ),
    /* Settings layer */
    [_SL] = LAYOUT_lr_stacked42(
        KC_DDIM, KC_DMIN, KC_D1Q,  KC_DHLF, KC_D3Q,  KC_DMAX,
        KC_DAUTO,KC_NO,   KC_NO,   KC_NO,   KC_NO,   KC_DBRI,
        KC_NO,   KC_L0,   KC_L1,   KC_L2,   KC_L3,   KC_L4,
        KC_BASE, LBL_TEXT,KC_TOGMODS,
        KC_NO,   KC_NO,   KC_NO,   KC_NO,   QK_MAKE, QK_BOOT,
        KC_NO,   KC_NO,   KC_NO,   KC_NO,   KC_NO,   QK_RBT,
        EE_CLR,  KC_STORE_EE, KC_NO, KC_NO, KC_NO,   KC_NO,
        DB_TOGG, KC_DEADKEY, KC_BASE
    ),
    // Language selection layer — mirrors the emoji picker. LEFT half: row 0 = the
    // six continent region tabs (LCAT), row 1 = the six unicode-input mode keys,
    // row 2 = the MRU recents (LMRU, top-bar marked). RIGHT half: the active
    // region's language slots (LSLOT, 18 per page), paged via the thumb arrows.
    [_LL] = LAYOUT_lr_stacked42(
        LCAT(0),   LCAT(1),   LCAT(2),   LCAT(3),   LCAT(4),   LCAT(5),
        QK_UNICODE_MODE_WINCOMPOSE, QK_UNICODE_MODE_MACOS, QK_UNICODE_MODE_EMACS, QK_UNICODE_MODE_WINDOWS, QK_UNICODE_MODE_LINUX, QK_UNICODE_MODE_BSD,
        LMRU(0),   LMRU(1),   LMRU(2),   LMRU(3),   LMRU(4),   LMRU(5),
        KC_BASE,   KC_NO,     KC_NO,
        LSLOT(0),  LSLOT(1),  LSLOT(2),  LSLOT(3),  LSLOT(4),  LSLOT(5),
        LSLOT(6),  LSLOT(7),  LSLOT(8),  LSLOT(9),  LSLOT(10), LSLOT(11),
        LSLOT(12), LSLOT(13), LSLOT(14), LSLOT(15), LSLOT(16), LSLOT(17),
        KC_LANG_PAGE_PREV, KC_LANG_PAGE_NEXT, KC_LANG_CLEAR
    ),
    /* Additional latin variant layer */
    [_ADDLANG1] = LAYOUT_lr_stacked42(
        KC_NO,   KC_NO,   KC_LAT0, KC_LAT1, KC_LAT2, KC_LAT3,
        KC_NO,   _______, _______, _______, _______, _______,
        KC_NO,   _______, _______, _______, _______, _______,
        KC_NO,   KC_NO,   _______,
        KC_LAT4, KC_LAT5, KC_LAT6, KC_LAT7, KC_LAT8, KC_LAT9,
        _______, _______, _______, _______, _______, KC_NO,
        _______, _______, _______, _______, _______, KC_NO,
        _______, _______, _______
    ),
    // Emoji layer — left half: 12 category tabs (rows 0-1) + 6 MRU recents on the
    // bottom-left row (top-bar marked); right half: the current tab's 18 slots
    // (3 rows). Paging on the right thumbs; Clear on the right thumb; left thumb exits.
    [_EMJ] = LAYOUT_lr_stacked42(
        KC_EMJ_CAT(0),  KC_EMJ_CAT(1),  KC_EMJ_CAT(2),  KC_EMJ_CAT(3),  KC_EMJ_CAT(4),  KC_EMJ_CAT(5),
        KC_EMJ_CAT(6),  KC_EMJ_CAT(7),  KC_EMJ_CAT(8),  KC_EMJ_CAT(9),  KC_EMJ_CAT(10), KC_EMJ_CAT(11),
        EMRU(0),        EMRU(1),        EMRU(2),        EMRU(3),        EMRU(4),        EMRU(5),
        TO(_BL),        KC_NO,          KC_NO,
        ESLOT(0),       ESLOT(1),       ESLOT(2),       ESLOT(3),       ESLOT(4),       ESLOT(5),
        ESLOT(6),       ESLOT(7),       ESLOT(8),       ESLOT(9),       ESLOT(10),      ESLOT(11),
        ESLOT(12),      ESLOT(13),      ESLOT(14),      ESLOT(15),      ESLOT(16),      ESLOT(17),
        KC_EMJ_PAGE_PREV, KC_EMJ_PAGE_NEXT, KC_EMJ_CLEAR
    )
};


const uint16_t encoder_map[][NUM_ENCODERS][NUM_DIRECTIONS] = {
    [0]  = { ENCODER_CCW_CW(MS_WHLD, MS_WHLU) },
    [1]  = { ENCODER_CCW_CW(MS_WHLD, MS_WHLU) },
    [2]  = { ENCODER_CCW_CW(MS_WHLD, MS_WHLU) },
    [3]  = { ENCODER_CCW_CW(MS_WHLD, MS_WHLU) },
    [4]  = { ENCODER_CCW_CW(MS_WHLD, MS_WHLU) },
    [5]  = { ENCODER_CCW_CW(MS_WHLD, MS_WHLU) },
    [6]  = { ENCODER_CCW_CW(MS_WHLD, MS_WHLU) },
    [7]  = { ENCODER_CCW_CW(MS_WHLD, MS_WHLU) },
    [8]  = { ENCODER_CCW_CW(MS_WHLD, MS_WHLU) },
    [9]  = { ENCODER_CCW_CW(MS_WHLD, MS_WHLU) },
    [10] = { ENCODER_CCW_CW(MS_WHLD, MS_WHLU) },
    [11] = { ENCODER_CCW_CW(MS_WHLD, MS_WHLU) },
    [12] = { ENCODER_CCW_CW(MS_WHLD, MS_WHLU) },
};
