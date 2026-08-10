// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// split42 VARIANT DATA ONLY. All shared keymap behaviour lives in
// keyboards/polykybd/poly_keymap.c (compiled for every variant), so this 42-key
// variant stays in lockstep with split72. This file defines only the data the
// linker pulls from the keymap TU:
//   * keymaps[]      — the layer definitions (LAYOUT_lr_stacked42)
//   * encoder_map[]  — the rotary-encoder action map
//   * g_led_config   — the RGB matrix LED layout (42 LEDs; see the RGB-experiment
//                      note in config.h — the subsystem is compiled in even though
//                      no WS2812 LEDs are populated on split42)
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

// Right-half mirror (POLY_SPLIT42_MIRROR_RIGHT, config.h). split42 currently ships as
// two LEFT boards; the one used as the RIGHT half is physically mirrored across the
// vertical axis AND its thumbs are wired to matrix cols 3-5 (like the left half), not
// the cols 0-2 that keyboard.json's LAYOUT_lr_stacked42 reserves for a *proper* right
// board. When the switch is on, POLY_LAYOUT therefore does NOT go through that generated
// macro (which hardcodes the right thumbs into cols 0-2 and leaves 3-5 as KC_NO). It
// expands DIRECTLY to the [MATRIX_ROWS][MATRIX_COLS] initializer, so it can:
//   * reverse each right-half letter row's columns (visual mirror), and
//   * place the right thumbs at cols 3-5 (row 7), where a left-board-as-right actually
//     scans — the left thumbs already sit at cols 3-5 (row 3), unchanged here.
// Off → straight pass-through to LAYOUT_lr_stacked42 (a real right board: thumbs 0-2).
// keyboard.json is untouched; this is compile-time only. Arg order = the
// LAYOUT_lr_stacked42 arg order: L rows 0-2 + L thumbs, then R rows 0-2 + R thumbs.
// ⚠️ Because the firmware runs the DYNAMIC (VIA) keymap seeded from this compiled keymap,
// a change to the switch only takes effect after an EEPROM reset (see config.h).
#if defined(POLY_SPLIT42_MIRROR_RIGHT)
#  define POLY_LAYOUT( \
     L00,L01,L02,L03,L04,L05,  L10,L11,L12,L13,L14,L15,  L20,L21,L22,L23,L24,L25,  LT0,LT1,LT2, \
     R00,R01,R02,R03,R04,R05,  R10,R11,R12,R13,R14,R15,  R20,R21,R22,R23,R24,R25,  RT0,RT1,RT2) \
   { { L00,L01,L02,L03,L04,L05 }, \
     { L10,L11,L12,L13,L14,L15 }, \
     { L20,L21,L22,L23,L24,L25 }, \
     { KC_NO,KC_NO,KC_NO, LT0,LT1,LT2 }, \
     { R05,R04,R03,R02,R01,R00 }, \
     { R15,R14,R13,R12,R11,R10 }, \
     { R25,R24,R23,R22,R21,R20 }, \
     { KC_NO,KC_NO,KC_NO, RT2,RT1,RT0 } }
#else
#  define POLY_LAYOUT(...) LAYOUT_lr_stacked42(__VA_ARGS__)
#endif

const uint16_t keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    /* Base layer 0 — Qwerty, faithful Corne outer columns + thumbs:
       left outer Tab / Ctrl / Shift, right pinky ' and Esc; thumbs
       GUI / MO(_FL0) / Space (left) and Enter / MO(_UL) / RAlt (right).
       Corne has no LANG on base — language-layer access lives on _FL0. */
    [_L0] = POLY_LAYOUT(
        KC_TAB,  KC_Q,    KC_W,    KC_E,    KC_R,    KC_T,
        KC_LCTL, KC_A,    KC_S,    KC_D,    KC_F,    KC_G,
        KC_LSFT, KC_Z,    KC_X,    KC_C,    KC_V,    KC_B,
        KC_LGUI, MO(_FL0),KC_SPC,
        KC_Y,    KC_U,    KC_I,    KC_O,    KC_P,    KC_BSPC,
        KC_H,    KC_J,    KC_K,    KC_L,    KC_SCLN, KC_QUOT,
        KC_N,    KC_M,    KC_COMM, KC_DOT,  KC_SLSH, KC_ESC,
        KC_ENT,  MO(_UL), KC_RALT
    ),
    /* Base layer 1 — Qwerty Staggered (split72 "QWERTY!": ANSI \ at home pinky,
       Enter on the thumb since the grid pinky holds \) */
    [_L1] = POLY_LAYOUT(
        KC_ESC,  KC_Q,    KC_W,    KC_E,    KC_R,    KC_T,
        MO(_FL0),KC_A,    KC_S,    KC_D,    KC_F,    KC_G,
        KC_LSFT, KC_Z,    KC_X,    KC_C,    KC_V,    KC_B,
        KC_LCTL, KC_SPC,  KC_DEL,
        KC_Y,    KC_U,    KC_I,    KC_O,    KC_P,    KC_BSPC,
        KC_H,    KC_J,    KC_K,    KC_L,    KC_SCLN, KC_BSLS,
        KC_N,    KC_M,    KC_COMM, KC_DOT,  KC_SLSH, KC_RSFT,
        KC_LANG, KC_ENTER,KC_LEFT
    ),
    /* Base layer 2 — Colemak DH */
    [_L2] = POLY_LAYOUT(
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
    [_L3] = POLY_LAYOUT(
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
    [_L4] = POLY_LAYOUT(
        KC_ESC,  KC_Q,    KC_D,    KC_R,    KC_W,    KC_B,
        MO(_FL1),KC_A,    KC_S,    KC_H,    KC_T,    KC_G,
        KC_LSFT, KC_Z,    KC_X,    KC_M,    KC_C,    KC_V,
        KC_LCTL, KC_SPC,  KC_DEL,
        KC_J,    KC_F,    KC_U,    KC_P,    KC_SCLN, KC_BSPC,
        KC_Y,    KC_N,    KC_E,    KC_O,    KC_I,    KC_ENTER,
        KC_K,    KC_L,    KC_COMM, KC_DOT,  KC_SLSH, KC_RSFT,
        KC_LANG, KC_BSPC, KC_LEFT
    ),
    /* Function layer 0 — number row 1..0 across the top, F1..F10 on the home row
       (higher F-keys dropped to make room for the numbers), plus caps / numpad /
       mouse / nav / utility access. */
    [_FL0] = POLY_LAYOUT(
        OSL(_UL),KC_1,    KC_2,    KC_3,    KC_4,    KC_5,
        _______, KC_F1,   KC_F2,   KC_F3,   KC_F4,   KC_F5,
        KC_CAPS, KC_LANG, _______, _______, _______, KC_DEL,
        _______, _______, TO(_UL),
        KC_6,    KC_7,    KC_8,    KC_9,    KC_0,    KC_BSPC,
        KC_F6,   KC_F7,   KC_F8,   KC_F9,   KC_F10,  _______,
        TO(_NL), MS_BTN1, MS_BTN2, _______, KC_INS,  _______,
        KC_HOME, KC_PGUP, KC_END
    ),
    /* Function layer 1 — identical to _FL0 so every base layer's function layer
       has the number row. */
    [_FL1] = POLY_LAYOUT(
        OSL(_UL),KC_1,    KC_2,    KC_3,    KC_4,    KC_5,
        _______, KC_F1,   KC_F2,   KC_F3,   KC_F4,   KC_F5,
        KC_CAPS, KC_LANG, _______, _______, _______, KC_DEL,
        _______, _______, TO(_UL),
        KC_6,    KC_7,    KC_8,    KC_9,    KC_0,    KC_BSPC,
        KC_F6,   KC_F7,   KC_F8,   KC_F9,   KC_F10,  _______,
        TO(_NL), MS_BTN1, MS_BTN2, _______, KC_INS,  _______,
        KC_HOME, KC_PGUP, KC_END
    ),
    /* Numpad layer */
    [_NL] = POLY_LAYOUT(
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
    [_UL] = POLY_LAYOUT(
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
    [_SL] = POLY_LAYOUT(
        KC_DDIM, KC_DMIN, KC_D1Q,  KC_DHLF, KC_D3Q,  KC_DMAX,
        KC_DAUTO,KC_NO,   KC_NO,   KC_NO,   KC_NO,   KC_DBRI,
        KC_NO,   KC_L0,   KC_L1,   KC_L2,   KC_L3,   KC_L4,
        KC_BASE, LBL_TEXT,KC_TOGMODS,
        KC_NO,   KC_NO,   KC_NO,   KC_NO,   QK_MAKE, QK_BOOT,
        KC_NO,   KC_NO,   KC_NO,   KC_NO,   KC_NO,   QK_RBT,
        EE_CLR,  KC_STORE_EE, KC_NO,   KC_NO,   KC_NO,   KC_NO,
        DB_TOGG, KC_DEADKEY, KC_BASE
    ),
    // Language selection layer — mirrors the emoji picker. LEFT half: row 0 = the
    // six continent region tabs (LCAT), row 1 = the six unicode-input mode keys,
    // row 2 = the MRU recents (LMRU, top-bar marked). RIGHT half: the active
    // region's language slots (LSLOT, 18 per page), paged via the thumb arrows.
    [_LL] = POLY_LAYOUT(
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
    [_ADDLANG1] = POLY_LAYOUT(
        KC_NO,   KC_NO,   KC_LAT0, KC_LAT1, KC_LAT2, KC_LAT3,
        KC_NO,   _______, _______, _______, _______, _______,
        // Shift picks which CASE the variation is stored for, so it has to reach
        // the base layer here too (it was KC_NO).  Note nothing currently selects
        // _ADDLANG1 on split42 -- there is no MO(_ADDLANG1) in this keymap -- so
        // this makes the layer correct for when one is added, it does not enable it.
        _______, _______, _______, _______, _______, _______,
        // Ctrl (LATIN_PICKER_MOD) must reach the base layer -- see the split72 note.
        _______, KC_NO,   _______,
        KC_LAT4, KC_LAT5, KC_LAT6, KC_LAT7, KC_LAT8, KC_LAT9,
        _______, _______, _______, _______, _______, KC_NO,
        _______, _______, _______, _______, _______, KC_NO,
        _______, _______, _______
    ),
    // Emoji layer — left half: 12 category tabs (rows 0-1) + 6 MRU recents on the
    // bottom-left row (top-bar marked); right half: the current tab's 18 slots
    // (3 rows). Paging on the right thumbs; Clear on the right thumb; left thumb exits.
    [_EMJ] = POLY_LAYOUT(
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


// RGB matrix LED layout (42 LEDs, 21 per side) — RE-ENABLED as an experiment.
// No WS2812 LEDs are physically populated on split42, so the physical positions
// here are derived from the keyboard.json key coordinates and are cosmetic only;
// what matters for the experiment is that the RGB subsystem is compiled in and
// running. LED order follows matrix order: LEFT rows 0-2 (6 each) + LEFT thumbs
// (3) = 0-20, then RIGHT rows 0-2 (6 each) + RIGHT thumbs (3) = 21-41.
//
// Placed in .rodata so the table sits in flash rather than RAM; QMK only reads
// g_led_config, so this is a placement override only (matching split72).
__attribute__((section(".rodata"))) led_config_t g_led_config = { {// Key Matrix to LED Index
                              {0,  1,  2,  3,  4,  5},
                              {6,  7,  8,  9,  10, 11},
                              {12, 13, 14, 15, 16, 17},
                              {NO_LED, NO_LED, NO_LED, 18, 19, 20},

                              {21, 22, 23, 24, 25, 26},
                              {27, 28, 29, 30, 31, 32},
                              {33, 34, 35, 36, 37, 38},
                              {39, 40, 41, NO_LED, NO_LED, NO_LED}
                             },
                             {
                                // LED Index to Physical Position
                                {0, 4},    {15, 4},   {30, 0},   {45, 0},   {60, 0},   {75, 0},
                                {0, 20},   {15, 20},  {30, 16},  {45, 16},  {60, 16},  {75, 16},
                                {0, 36},   {15, 36},  {30, 32},  {45, 32},  {60, 32},  {75, 32},
                                {52, 52},  {68, 56},  {82, 60},

                                {120, 0},  {135, 0},  {150, 0},  {165, 0},  {180, 4},  {195, 4},
                                {120, 16}, {135, 16}, {150, 16}, {165, 16}, {180, 20}, {195, 20},
                                {120, 32}, {135, 32}, {150, 32}, {165, 32}, {180, 36}, {195, 36},
                                {112, 60}, {128, 56}, {142, 52}
                             },
                             {
                                // LED Index to Flag
                                4, 4, 4, 4, 4, 4,
                                4, 4, 4, 4, 4, 4,
                                4, 4, 4, 4, 4, 4,
                                4, 4, 4,

                                4, 4, 4, 4, 4, 4,
                                4, 4, 4, 4, 4, 4,
                                4, 4, 4, 4, 4, 4,
                                4, 4, 4
                             } };
