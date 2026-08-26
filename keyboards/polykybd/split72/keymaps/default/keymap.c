// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// split72 VARIANT DATA ONLY. All shared keymap behaviour lives in
// keyboards/polykybd/poly_keymap.c (compiled for every variant).
// This file defines just the data the linker pulls from the keymap TU:
//   * keymaps[]      — the layer definitions (LAYOUT_left_right_stacked)
//   * g_led_config   — the RGB matrix LED layout (split72 has 72 LEDs)
//   * encoder_map[]  — the rotary-encoder action map
#include QMK_KEYBOARD_H
#include "split72/split72.h"
#include "layers.h"
#include "keycode_helper.h"
#include "emoji/emoji_layer.h"
#include "lang_layer.h"

const uint16_t keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    //Base Layers
/*
                                                              ┌────────────────┐
                                                              │     QWERTY     │
                                                              └────────────────┘
   ┌────────┬───────┬───────┬───────┬───────┬───────┬───────┐                    ┌───────┬───────┬───────┬───────┬───────┬───────┬────────┐
   │  Esc   │   1   │   2   │   3   │   4   │   5   │  Nubs │ ╭╮╭╮╭╮╭╮╭╮╭╮╭╮╭╮╭╮ │   6   │   7   │   8   │   9   │   0   │   -   │ BckSp  │
   ├────────┼───────┼───────┼───────┼───────┼───────┼───────┤ │╰╯╰╯╰╯╰╯╰╯╰╯╰╯╰╯│ ├───────┼───────┼───────┼───────┼───────┼───────┼────────┤
   │  TAB   │   q   │   w   │   e   │   r   │   t   │   `   ├─╯                ╰─┤  Hypr │   y   │   u   │   i   │   o   │   p   │   \    │
   ├────────┼───────┼───────┼───────┼───────┼───────┼───────┤                    ├───────┼───────┼───────┼───────┼───────┼───────┼────────┤
   │  FN    │   a   │   s   │   d   │   f   │   g   │   '   │  (MB1)             │  Intl │   h   │   j   │   k   │   l   │   =   │  Ret   │
   ├────────┼───────┼───────┼───────┼───────┼───────┼───────┼────────╮  ╭────────┼───────┼───────┼───────┼───────┼───────┼───────┼────────┤
   │ Shift  │   z   │   x   │   c   │   v   │   b   │  Nuhs │  Num!  │  │   [    │   ]   │   n   │   m   │   ,   │   ;   │  Up   │ Shift  │
   └┬───────┼───────┼───────┼───────┼──────┬┴───────┼───────┼────────┤  ├────────┼───────┼───────┴┬──────┼───────┼───────┼───────┼───────┬┘
    │ Ctrl  │  Os   │  Alt  │  Ctx  │      │  Space │  Del  │   Ret  │  │  Lang  │   /   │ Space  │      │   .   │  Left │  Down │ Right │
    └───────┴───────┴───────┴───────┘      └────────┴───────┴────────╯  └────────┴───────┴────────┘      └───────┴───────┴───────┴───────┘
*/
    [_L0] = LAYOUT_left_right_stacked(
        KC_ESC,     KC_1,       KC_2,       KC_3,       KC_4,       KC_5,       KC_NUBS,
        KC_TAB,     KC_Q,       KC_W,       KC_E,       KC_R,       KC_T,       KC_GRAVE,
        MO(_FL0),   KC_A,       KC_S,       KC_D,       KC_F,       KC_G,       KC_QUOTE,   MS_BTN1,
        KC_LSFT,    KC_Z,       KC_X,       KC_C,       KC_V,       KC_B,       TO(_EMJ),   MO(_NL),
        KC_LCTL,    KC_LWIN,    KC_LALT,    KC_APP,                 KC_SPACE,   KC_DEL,     KC_ENTER,

                    KC_6,       KC_7,       KC_8,       KC_9,       KC_0,       KC_MINUS,   KC_BSPC,
                    KC_HYPR,    KC_Y,       KC_U,       KC_I,       KC_O,       KC_P,       KC_BSLS,
        KC_NO,      MO(_ADDLANG1),KC_H,     KC_J,       KC_K,       KC_L,       KC_EQUAL,   KC_ENTER,
        KC_LBRC,    KC_RBRC,    KC_N,       KC_M,       KC_COMMA,   KC_SCLN,    KC_UP,      KC_RSFT,
        KC_LANG,    KC_SLASH,    KC_SPC,                KC_DOT,     KC_LEFT,    KC_DOWN,    KC_RIGHT
        ),

/*
                                                              ┌────────────────┐
                                                              │     QWERTY!    │
                                                              └────────────────┘
   ┌────────┬───────┬───────┬───────┬───────┬───────┬───────┐                    ┌───────┬───────┬───────┬───────┬───────┬───────┬────────┐
   │  Esc   │   1   │   2   │   3   │   4   │   5   │   6   │ ╭╮╭╮╭╮╭╮╭╮╭╮╭╮╭╮╭╮ │   7   │   8   │   9   │   0   │   -   │   =   │  Hypr  │
   ├────────┼───────┼───────┼───────┼───────┼───────┼───────┤ │╰╯╰╯╰╯╰╯╰╯╰╯╰╯╰╯│ ├───────┼───────┼───────┼───────┼───────┼───────┼────────┤
   │  TAB   │   q   │   w   │   e   │   r   │   t   │   `   ├─╯                ╰─┤   y   │   u   │   i   │   o   │   p   │   [   │  Nubs  │
   ├────────┼───────┼───────┼───────┼───────┼───────┼───────┤                    ├───────┼───────┼───────┼───────┼───────┼───────┼────────┤
   │  FN    │   a   │   s   │   d   │   f   │   g   │   '   │  (MB1)             │   h   │   j   │   k   │   l   │   ;   │   ]   │    \   │
   ├────────┼───────┼───────┼───────┼───────┼───────┼───────┼────────╮  ╭────────┼───────┼───────┼───────┼───────┼───────┼───────┼────────┤
   │ Shift  │ Nuhs  │   z   │   x   │   c   │   v   │   b   │  Num!  │  │  Lang  │  Ctx  │   n   │   m   │   ,   │   .   │   /   │ Shift  │
   └┬───────┼───────┼───────┼───────┼──────┬┴───────┼───────┼────────┤  ├────────┼───────┼───────┴┬──────┼───────┼───────┼───────┼───────┬┘
    │ Ctrl  │  Os   │  Alt  │  Intl │      │  Space │  Del  │   Ins  │  │  Ret   │ BckSp │ Space  │      │ Left  │   Up  │  Down │ Right │
    └───────┴───────┴───────┴───────┘      └────────┴───────┴────────╯  └────────┴───────┴────────┘      └───────┴───────┴───────┴───────┘
*/

    [_L1] = LAYOUT_left_right_stacked(
        KC_ESC,     KC_1,       KC_2,       KC_3,       KC_4,       KC_5,       KC_6,
        KC_TAB,     KC_Q,       KC_W,       KC_E,       KC_R,       KC_T,       KC_GRAVE,
        MO(_FL1),   KC_A,       KC_S,       KC_D,       KC_F,       KC_G,       KC_QUOTE,   MS_BTN1,
        KC_LSFT,    TO(_EMJ),   KC_Z,       KC_X,       KC_C,       KC_V,       KC_B,       MO(_NL),
        KC_LCTL,    KC_LWIN,    KC_LALT,    MO(_ADDLANG1),          KC_SPACE,   KC_DEL,     KC_ENTER,

                    KC_7,       KC_8,       KC_9,       KC_0,       KC_MINUS,   KC_EQUAL,   KC_HYPR,
                    KC_Y,       KC_U,       KC_I,       KC_O,       KC_P,       KC_LBRC,    KC_NUBS,
        KC_NO,      KC_H,       KC_J,       KC_K,       KC_L,       KC_SCLN,    KC_RBRC,    KC_BSLS,
        KC_LANG,    KC_APP,     KC_N,       KC_M,       KC_COMMA,   KC_DOT,     KC_SLASH,   KC_RSFT,
        KC_ENTER,   KC_BSPC,    KC_SPC,                 KC_LEFT,    KC_UP,      KC_DOWN,    KC_RIGHT
        ),
/*
                                                              ┌────────────────┐
                                                              │   Colemak DH   │
                                                              └────────────────┘
   ┌────────┬───────┬───────┬───────┬───────┬───────┬───────┐                    ┌───────┬───────┬───────┬───────┬───────┬───────┬────────┐
   │  Esc   │   1   │   2   │   3   │   4   │   5   │  Nub  │ ╭╮╭╮╭╮╭╮╭╮╭╮╭╮╭╮╭╮ │   6   │   7   │   8   │   9   │   0   │   -   │   =    │
   ├────────┼───────┼───────┼───────┼───────┼───────┼───────┤ │╰╯╰╯╰╯╰╯╰╯╰╯╰╯╰╯│ ├───────┼───────┼───────┼───────┼───────┼───────┼────────┤
   │  TAB   │   q   │   w   │   f   │   p   │   b   │   `   ├─╯                ╰─┤   j   │   l   │   u   │   y   │   ;   │   [   │  Intl  │
   ├────────┼───────┼───────┼───────┼───────┼───────┼───────┤                    ├───────┼───────┼───────┼───────┼───────┼───────┼────────┤
   │  FN    │   a   │   r   │   s   │   t   │   g   │   '   │  (MB1)             │   m   │   n   │   e   │   i   │   o   │   ]   │    \   │
   ├────────┼───────┼───────┼───────┼───────┼───────┼───────┼────────╮  ╭────────┼───────┼───────┼───────┼───────┼───────┼───────┼────────┤
   │ Shift  │   z   │   x   │   c   │   d   │   v   │  Nuhs |  Num!  │  │  Lang  │  Hypr │   k   │   h   │   ,   │   .   │   /   │ Shift  │
   └┬───────┼───────┼───────┼───────┼──────┬┴───────┼───────┼────────┤  ├────────┼───────┼───────┴┬──────┼───────┼───────┼───────┼───────┬┘
    │ Ctrl  │  Os   │  Alt  │  Ctx  │      │  Space │  Del  │   Ret  │  │  Ret   │ BckSp │ Space  │      │ Left  │   Up  │  Down │ Right │
    └───────┴───────┴───────┴───────┘      └────────┴───────┴────────╯  └────────┴───────┴────────┘      └───────┴───────┴───────┴───────┘
*/
    [_L2] = LAYOUT_left_right_stacked(
        KC_ESC,     KC_1,       KC_2,       KC_3,       KC_4,       KC_5,       KC_NUBS,
        KC_TAB,     KC_Q,       KC_W,       KC_F,       KC_P,       KC_B,       KC_GRAVE,
        MO(_FL1),   KC_A,       KC_R,       KC_S,       KC_T,       KC_G,       KC_QUOTE,   MS_BTN1,
        KC_LSFT,    KC_Z,       KC_X,       KC_C,       KC_D,       KC_V,       TO(_EMJ),    MO(_NL),
        KC_LCTL,    KC_LWIN,    KC_LALT,    KC_APP,                 KC_SPACE,   KC_DEL,     KC_ENTER,

                    KC_6,       KC_7,       KC_8,       KC_9,       KC_0,       KC_MINUS,   KC_EQUAL,
                    KC_J,       KC_L,       KC_U,       KC_Y,       KC_SCLN,    KC_LBRC,    MO(_ADDLANG1),
        KC_NO,      KC_M,       KC_N,       KC_E,       KC_I,       KC_O,       KC_RBRC,    KC_BSLS,
        KC_LANG,    KC_HYPR,    KC_K,       KC_H,       KC_COMMA,   KC_DOT,     KC_SLASH,   KC_RSFT,
        KC_ENTER,   KC_BSPC,    KC_SPC,                 KC_LEFT,    KC_UP,      KC_DOWN,    KC_RIGHT
        ),
        /*
                                                              ┌────────────────┐
                                                              │       Neo      │
                                                              └────────────────┘
   ┌────────┬───────┬───────┬───────┬───────┬───────┬───────┐                    ┌───────┬───────┬───────┬───────┬───────┬───────┬────────┐
   │  Esc   │   1   │   2   │   3   │   4   │   5   │   <   │ ╭╮╭╮╭╮╭╮╭╮╭╮╭╮╭╮╭╮ │   6   │   7   │   8   │   9   │   0   │   -   │   `    │
   ├────────┼───────┼───────┼───────┼───────┼───────┼───────┤ │╰╯╰╯╰╯╰╯╰╯╰╯╰╯╰╯│ ├───────┼───────┼───────┼───────┼───────┼───────┼────────┤
   │  TAB   │   x   │   v   │   l   │   c   │   w   │   ^   ├─╯                ╰─┤   k   │   h   │   g   │   f   │   q   │   ß   │   ´    │
   ├────────┼───────┼───────┼───────┼───────┼───────┼───────┤                    ├───────┼───────┼───────┼───────┼───────┼───────┼────────┤
   │  FN    │   u   │   i   │   a   │   e   │   o   │   '   │  (MB1)             │   s   │   n   │   r   │   t   │   d   │   y   │   \    │
   ├────────┼───────┼───────┼───────┼───────┼───────┼───────┼────────╮  ╭────────┼───────┼───────┼───────┼───────┼───────┼───────┼────────┤
   │ Shift  │   #   │   ü   │   ö   │   ä   │   p   │   z   │  Num!  │  │  Lang  │   +   │   b   │   m   │   ,   │   .   │   j   │ Shift  │
   └┬───────┼───────┼───────┼───────┼──────┬┴───────┼───────┼────────┤  ├────────┼───────┼───────┴┬──────┼───────┼───────┼───────┼───────┬┘
    │ Ctrl  │  Os   │  Alt  │  Ctx  │      │  Space │  Del  │   Ret  │  │  Ret   │ BckSp │ Space  │      │ Left  │   Up  │  Down │ Right │
    └───────┴───────┴───────┴───────┘      └────────┴───────┴────────╯  └────────┴───────┴────────┘      └───────┴───────┴───────┴───────┘
*/
    [_L3] = LAYOUT_left_right_stacked(
        KC_ESC,     KC_1,       KC_2,       KC_3,       KC_4,       KC_5,       DE_LABK,
        KC_TAB,     KC_X,       KC_V,       KC_L,       KC_C,       KC_W,       DE_CIRC,
        MO(_FL0),   KC_U,       KC_I,       KC_A,       KC_E,       KC_O,       KC_QUOTE,   MS_BTN1,
        KC_LSFT,    DE_HASH,    DE_UDIA,    DE_ODIA,    DE_ADIA,    KC_P,       DE_Z,       MO(_NL),
        KC_LCTL,    KC_LWIN,    KC_LALT,    KC_APP,                 KC_SPACE,   KC_DEL,     KC_ENTER,

                    KC_6,       KC_7,       KC_8,       KC_9,       KC_0,       DE_MINS,    DE_GRV,
                    KC_K,       KC_H,       KC_G,       KC_F,       KC_Q,       DE_SS,      DE_ACUT,
        KC_NO,      KC_S,       KC_N,       KC_R,       KC_T,       KC_D,       DE_Y,       KC_BSLS,
        KC_LANG,    DE_PLUS,    KC_B,       KC_M,       KC_COMMA,   KC_DOT,     KC_J,       KC_RSFT,
        KC_ENTER,   KC_BSPC,    KC_SPC,                 KC_LEFT,    KC_UP,      KC_DOWN,    KC_RIGHT
        ),
        /*
                                                              ┌────────────────┐
                                                              │    Workman     │
                                                              └────────────────┘
   ┌────────┬───────┬───────┬───────┬───────┬───────┬───────┐                    ┌───────┬───────┬───────┬───────┬───────┬───────┬────────┐
   │  Esc   │   1   │   2   │   3   │   4   │   5   │   `   │ ╭╮╭╮╭╮╭╮╭╮╭╮╭╮╭╮╭╮ │   6   │   7   │   8   │   9   │   0   │   -   │   =    │
   ├────────┼───────┼───────┼───────┼───────┼───────┼───────┤ │╰╯╰╯╰╯╰╯╰╯╰╯╰╯╰╯│ ├───────┼───────┼───────┼───────┼───────┼───────┼────────┤
   │  TAB   │   q   │   d   │   r   │   w   │   b   │  Hypr ├─╯                ╰─┤   j   │   f   │   u   │   p   │   ;   │   [   │   ]    │
   ├────────┼───────┼───────┼───────┼───────┼───────┼───────┤                    ├───────┼───────┼───────┼───────┼───────┼───────┼────────┤
   │  FN    │   a   │   s   │   h   │   t   │   g   │  Meh  │  (MB1)             │   y   │   n   │   e   │   o   │   i   │   '   │   \    │
   ├────────┼───────┼───────┼───────┼───────┼───────┼───────┼────────╮  ╭────────┼───────┼───────┼───────┼───────┼───────┼───────┼────────┤
   │ Shift  │   z   │   x   │   m   │   c   │   v   │  Intl │  Num!  │  │  Lang  │   k   │   b   │   l   │   ,   │   .   │   /   │ Shift  │
   └┬───────┼───────┼───────┼───────┼──────┬┴───────┼───────┼────────┤  ├────────┼───────┼───────┴┬──────┼───────┼───────┼───────┼───────┬┘
    │ Ctrl  │  Os   │  Alt  │  Ctx  │      │  Space │  Del  │   Ret  │  │  Ret   │ BckSp │ Space  │      │ Left  │   Up  │  Down │ Right │
    └───────┴───────┴───────┴───────┘      └────────┴───────┴────────╯  └────────┴───────┴────────┘      └───────┴───────┴───────┴───────┘
*/
    [_L4] = LAYOUT_left_right_stacked(
        KC_ESC,     KC_1,       KC_2,       KC_3,       KC_4,       KC_5,       KC_GRAVE,
        KC_TAB,     KC_Q,       KC_D,       KC_R,       KC_W,       KC_B,       KC_HYPR,
        MO(_FL1),   KC_A,       KC_S,       KC_H,       KC_T,       KC_G,       TO(_EMJ),     MS_BTN1,
        KC_LSFT,    KC_Z,       KC_X,       KC_M,       KC_C,       KC_V,       MO(_ADDLANG1), MO(_NL),
        KC_LCTL,    KC_LWIN,    KC_LALT,    KC_APP,                 KC_SPACE,   KC_DEL,     KC_ENTER,

                    KC_6,       KC_7,       KC_8,       KC_9,       KC_0,       KC_MINUS,   KC_EQUAL,
                    KC_J,       KC_F,       KC_U,       KC_P,       KC_SCLN,    KC_LBRC,    KC_RBRC,
        KC_NO,      KC_Y,       KC_N,       KC_E,       KC_O,       KC_I,       KC_QUOTE,   KC_BSLS,
        KC_LANG,    KC_K,       KC_B,       KC_L,       KC_COMMA,   KC_DOT,     KC_SLASH,   KC_RSFT,
        KC_ENTER,   KC_BSPC,    KC_SPC,                 KC_LEFT,    KC_UP,      KC_DOWN,    KC_RIGHT
        ),
    //Function Layer (Fn)
    [_FL0] = LAYOUT_left_right_stacked(
        OSL(_UL),   KC_F1,      KC_F2,      KC_F3,      KC_F4,      KC_F5,     TO(_UL),
        _______,    _______,    _______,    _______,    _______,    _______,    _______,
        _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,
        KC_CAPS,    _______,    _______,    _______,    _______,    _______,    _______,    _______,
        _______,    _______,    _______,    _______,                _______,    _______,    _______,

                    KC_F6,      KC_F7,      KC_F8,      KC_F9,      KC_F10,      KC_F11,    KC_F12,
                    MS_BTN1,    MS_BTN2,    _______,    _______,    _______,    _______,    TO(_SL),
        _______,    MS_BTN3,    _______,    _______,    _______,   _______,    _______,    _______,
        TO(_NL),    _______,    _______,    _______,    _______,    _______,    _______,    KC_INS,
        KC_RALT,    KC_RWIN,    KC_RCTL,                KC_HOME,    KC_PGUP,    KC_PGDN,    KC_END
        ),
    [_FL1] = LAYOUT_left_right_stacked(
        OSL(_UL),   KC_F1,      KC_F2,      KC_F3,      KC_F4,      KC_F5,      KC_F6,
        _______,    _______,    _______,    _______,    _______,    _______,    _______,
        _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,
        _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,
        _______,    _______,    _______,    _______,                _______,    _______,    KC_INS,

                    KC_F7,      KC_F8,      KC_F9,      KC_F10,     KC_F11,     KC_F12,     TO(_UL),
                    MS_BTN1,    MS_BTN2,    _______,    _______,    _______,    _______,    TO(_SL),
        _______,    MS_BTN3,    _______,    _______,    _______,    _______,    _______,    KC_CAPS,
        TO(_NL),    _______,    _______,    _______,    _______,    _______,    _______,    _______,
        KC_RALT,    KC_RWIN,    KC_RCTL,                KC_HOME,    KC_PGUP,    KC_PGDN,    KC_END
        ),
     //Num Layer
    [_NL] = LAYOUT_left_right_stacked(
        KC_NO,      KC_NUM,     KC_PSLS,    KC_PAST,    KC_PMNS,    KC_NO,      KC_NO,
        MS_BTN1,    KC_KP_7,    KC_KP_8,    KC_KP_9,    KC_PPLS,    KC_INS,     KC_NO,
        KC_NO,      KC_KP_4,    KC_KP_5,    KC_KP_6,    KC_PPLS,    KC_DEL,     KC_NO,     _______,
        KC_NO,      KC_KP_1,    KC_KP_2,    KC_KP_3,    KC_PENT,    KC_NO,      KC_NO,     _______,
        KC_BASE,    KC_KP_0,    KC_PDOT,    KC_PENT,                MS_BTN2, KC_NO,     KC_NO,

                    KC_NO,      KC_NO,      KC_NUM,     KC_PSLS,    KC_PAST,    KC_PMNS,   KC_NO,
                    KC_NO,      KC_INS,     KC_KP_7,    KC_KP_8,    KC_KP_9,    KC_PPLS,   KC_NO,
        _______,    KC_NO,      KC_DEL,     KC_KP_4,    KC_KP_5,    KC_KP_6,    KC_PPLS,   KC_NO,
        _______,    _______,    KC_NO,      KC_KP_1,    KC_KP_2,    KC_KP_3,    KC_PENT,   KC_NO,
        _______,    KC_NO,      KC_NO,                  KC_KP_0,    KC_PDOT,    KC_PENT,   KC_BASE
        ),
    //Util Layer
    [_UL] = LAYOUT_left_right_stacked(
        KC_NO,      KC_F13,     KC_F14,     KC_F15,     KC_F16,     KC_F17,     KC_F18,
        // Shifted one column INWARD so the outer column (Tab on the base layers)
        // stays free, and each key lines up under an F-key above it.
        KC_NO,      KC_MYCM,    KC_CALC,    KC_PSCR,    KC_SCRL,    KC_BRK,     KC_IDDQD,
        KC_NO,      KC_DMIN,    KC_D1Q,     KC_DHLF,    KC_D3Q,     KC_DMAX,    KC_NO,      _______,
        // Centred under the five presets above (cols 1-5): three keys centre on col 3.
        // Auto/manual sits BETWEEN - and +, so the row reads dimmer / mode / brighter.
        KC_LSFT,    KC_NO,      KC_DDIM,    KC_DAUTO,   KC_DBRI,    KC_NO,      KC_NO,      KC_NO,
        KC_BASE,    KC_NO,      KC_NO,      KC_NO,                  KC_NO,      KC_NO,      KC_NO,

                    KC_F19,     KC_F20,     KC_F21,     KC_F22,     KC_F23,     KC_F24,     KC_NO,
                    KC_NO,      KC_MPRV,    KC_MPLY,    KC_MSTP,    KC_MNXT,    KC_IDDQD,   TO(_SL),
        // Sound sits directly under the transport row so the whole media block reads as
        // one group: MRWD/MFFD bracket the volume trio, each under its transport key.
        // Shifted one column OUTWARD to make that alignment true - MRWD now sits under
        // MPRV, MUTE under MPLY, and so on to MFFD under the IDDQD slot.
        _______,    KC_NO,      KC_MRWD,    KC_MUTE,    KC_VOLD,    KC_VOLU,    KC_MFFD,    KC_NO,
        // KC_GLYPH_SIZE_UP is the SINGLE legend-size key - Shift reverses the direction
        // and the legend carries the current tier as a digit, so no second key is needed.
        // It sits where Ctx (KC_APP) lives on the base layers.
        KC_NO,      KC_GLYPH_SIZE_UP, KC_NO, KC_NO,     KC_NO,      KC_NO,      KC_NO,      KC_RSFT,
        KC_NO,      KC_NO,      KC_NO,                  KC_NO,      KC_NO,      KC_NO,      KC_BASE
        ),
    //Settings Layer — row 2 hosts the OS selection keys: KC_OS_SET_AUTO returns to
    // auto (host/USB detection) and the rest pin a specific OS. They are radio-style
    // (the active choice shows a lit toggle in its legend). This replaces the single
    // cycling KC_OS_ICON; the per-OS semantic action keys are not mapped here.
    //
    // ⚠️ EE_CLR and KC_STORE_EE are deliberately NOT mapped. Both cost more than they
    // give on a board whose persistence is automatic: KC_STORE_EE only forces a flush
    // the next suspend/shutdown would do anyway (save_all_dirty() runs on suspend, on
    // the host shutdown signal and before a firmware apply), so its whole value is
    // saving a few seconds — while EE_CLR wipes every stored setting AND the dynamic
    // keymap, with no confirmation and no undo, from a layer reached by two taps.
    // A destructive key that near-duplicates a no-op key is the wrong trade; both stay
    // reachable from PolyKybdHost, where a mis-click can be reconsidered.
    [_SL] = LAYOUT_left_right_stacked(
        // ROW 3 (both halves) is the ADVANCED row: every key on it is blank and inert
        // until KC_SETTINGS_MORE is tapped, and re-hides itself on leaving the layer
        // (see settings_key_is_gated / layer_state_set_user). Rows 0-2 keep the
        // everyday settings — OS pins, base-layer picks — visible at all times.
        //
        // The gated keys were scattered across rows 0 and 4 before; collecting them
        // on one row is what makes "these are the ones behind the button" legible
        // from the board rather than from this file. QK_MAKE is gone entirely — it
        // rebuilds firmware from a keypress, which is a developer affordance that has
        // no business one tap from the layer a user opens to change the OS pin.
        KC_NO,      KC_NO,      KC_NO,      KC_NO,      KC_NO,      KC_NO,      KC_NO,
        KC_NO,      KC_OS_SET_AUTO, KC_OS_SET_WINDOWS, KC_OS_SET_MACOS, KC_OS_SET_LINUX, KC_OS_SET_ANDROID, KC_NO,
        KC_NO,      KC_L0,      KC_L1,      KC_L2,      KC_L3,      KC_L4,      KC_NO,      _______,
        // More sits on the OUTER edge, where the row starts reading.
        KC_SETTINGS_MORE, KC_IDLE_STYLE, KC_GLYPH_SCRIPT, LBL_TEXT, KC_TOGMODS, KC_TOGTEXT, KC_NO, KC_NO,
        KC_BASE,    KC_NO,      KC_NO,      KC_NO,                  KC_NO,      KC_NO,      KC_NO,


        //             RM_PREV,    RGB_M_SW,   RGB_M_R,    KC_RGB_TOG, RGB_M_P,    RGB_M_B,    RM_NEXT,
        //             KC_NO,      RM_SPDD,    RM_SPDU,    KC_NO,      RM_HUED,    RM_HUEU,    KC_NO,
        // _______,    KC_NO,      RM_VALD,    RM_VALU,    KC_NO,      RM_SATD,    RM_SATU,    KC_NO,
                    RM_PREV,   RGB_M_SW,   RGB_M_R,    KC_RGB_TOG, RGB_M_P,    RGB_M_B,    RM_NEXT,
                    KC_NO,      RM_SPDD,    RM_SPDU,    KC_NO,      RM_HUED,    RM_HUEU,    KC_NO,
        _______,    KC_NO,      RM_VALD,    RM_VALU,    KC_NO,      RM_SATD,    RM_SATU,    KC_NO,
        // EE_CLR and KC_STORE_EE are deliberately UNMAPPED — see the note above [_SL].
        // Row 3 again: the two IRREVERSIBLE keys (Restart, Boot) sit at the far OUTER
        // end, as far from the toggles as the row allows, so a slip while reaching for
        // Dbg cannot land on them.
        KC_NO,      KC_NO,      DB_TOGG,    KC_DEADKEY, KC_EDEN,    KC_NO,      QK_RBT,     QK_BOOT,
        KC_NO,      KC_NO,      KC_NO,                  KC_NO,      KC_NO,      KC_NO,      KC_BASE
        ),
    // Language Selection Layer — mirrors the emoji picker. TOP row of the LEFT
    // block = the six continent region tabs (LCAT) with the wrapping page-prev
    // arrow on the outer end; TOP row of the RIGHT block = the six unicode-input
    // mode keys with page-next on the outer end. The active region's language
    // slots (LSLOT) fill the middle rows; the 12 MRU recents sit on the BOTTOM
    // row of each block (top-bar marked). No Preset key — Clear is on the right
    // thumb (former right base key); the left base key still exits.
    //
    // Slot numbering is ROW-MAJOR ACROSS BOTH HALVES: each physical row is filled
    // left-half then right-half before moving down, so languages read
    // left-to-right across the whole keyboard rather than down the left half and
    // then down the right. Per-row counts are 6+6, 6+6, 7+7 (= 38 slots):
    //   row 1  left 0..5   right 6..11
    //   row 2  left 12..17 right 18..23
    //   row 3  left 24..30 right 31..37
    [_LL] = LAYOUT_left_right_stacked(
        KC_LANG_PAGE_PREV, LCAT(0),    LCAT(1),    LCAT(2),    LCAT(3),    LCAT(4),    LCAT(5),
        KC_NO,             LSLOT(0),   LSLOT(1),   LSLOT(2),   LSLOT(3),   LSLOT(4),   LSLOT(5),
        KC_NO,      LSLOT(12),  LSLOT(13),  LSLOT(14),  LSLOT(15),  LSLOT(16),  LSLOT(17),  MS_BTN1,
        KC_NO,      LSLOT(24),  LSLOT(25),  LSLOT(26),  LSLOT(27),  LSLOT(28),  LSLOT(29),  LSLOT(30),
        KC_BASE,    LMRU(0),    LMRU(1),    LMRU(2),                LMRU(3),    LMRU(4),    LMRU(5),

                    QK_UNICODE_MODE_MACOS, QK_UNICODE_MODE_WINCOMPOSE, QK_UNICODE_MODE_EMACS, QK_UNICODE_MODE_WINDOWS, QK_UNICODE_MODE_LINUX, QK_UNICODE_MODE_BSD, KC_LANG_PAGE_NEXT,
                    LSLOT(6),   LSLOT(7),   LSLOT(8),   LSLOT(9),   LSLOT(10),  LSLOT(11),  KC_NO,
        KC_NO,      LSLOT(18),  LSLOT(19),  LSLOT(20),  LSLOT(21),  LSLOT(22),  LSLOT(23),  KC_NO,
        LSLOT(31),  LSLOT(32),  LSLOT(33),  LSLOT(34),  LSLOT(35),  LSLOT(36),  LSLOT(37),  KC_NO,
        LMRU(6),    LMRU(7),    LMRU(8),                LMRU(9),    LMRU(10),   LMRU(11),   KC_LANG_CLEAR
        ),
    [_ADDLANG1] = LAYOUT_left_right_stacked(
        // Page arrows on the OUTER ENDS — prev at the far left of this block, next at
        // the far right of the other one — with the 12 variation slots between them.
        // That is the same arrangement as the emoji and language layers below
        // (KC_EMJ_PAGE_PREV / _NEXT, KC_LANG_PAGE_PREV / _NEXT), so paging is the same
        // gesture everywhere; split42 matches it too.
        KC_LAT_PAGE_PREV, KC_LAT0, KC_LAT1, KC_LAT2, KC_LAT3, KC_LAT4, KC_LAT5,
        KC_NO,      _______,    _______,    _______,    _______,    _______,    _______,
        KC_NO,      _______,    _______,    _______,    _______,    _______,    _______,    _______,
        _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,
        // Ctrl is the variation-picker modifier (LATIN_PICKER_MOD), so it MUST fall
        // through to the base layer here -- it used to be KC_NO, which is why the
        // picker was on Alt.  Alt is now KC_NO instead: the picker swallows the keys
        // it handles, so the host saw a bare Alt tap and Windows moved focus to the
        // menu bar, right in the middle of typing an accented letter.
        // [4,1] (GUI on the base layers) carries KC_LAT_REMAP: it is dead on this
        // layer and sits directly beside the Ctrl at [4,0], so the two picker
        // gestures -- Ctrl picks another FORM, this picks another LETTER -- are
        // adjacent on the same hand.
        _______,    KC_LAT_REMAP, KC_NO,    _______,                _______,    _______,    _______,

                    KC_LAT6,    KC_LAT7,    KC_LAT8,    KC_LAT9,    KC_LAT10,   KC_LAT11,   KC_LAT_PAGE_NEXT,
                    _______,    _______,    _______,    _______,    _______,    _______,    KC_NO,
        _______,    _______,    _______,    _______,    _______,    _______,    _______,    KC_NO,
        _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,
        _______,    _______,    _______,                _______,    _______,    _______,    _______
        ),
    // Emoji picker. TOP row = the 12 category tabs with the wrapping page arrows
    // on the outer ends; the 38 emoji slots of the current category/page fill the
    // middle rows; the 12 MRU recents sit on the BOTTOM row of each block
    // (top-bar marked). No Preset key — Clear is on the right thumb (former right
    // base key); the left base key still exits.
    [_EMJ] = LAYOUT_left_right_stacked(
        KC_EMJ_PAGE_PREV, KC_EMJ_CAT(0),  KC_EMJ_CAT(1),  KC_EMJ_CAT(2),  KC_EMJ_CAT(3),  KC_EMJ_CAT(4),  KC_EMJ_CAT(5),
        KC_NO,            ESLOT(0),       ESLOT(1),       ESLOT(2),       ESLOT(3),       ESLOT(4),       ESLOT(5),
        KC_NO,      ESLOT(6),       ESLOT(7),       ESLOT(8),       ESLOT(9),       ESLOT(10),      ESLOT(11),      KC_NO,
        KC_NO,      ESLOT(12),      ESLOT(13),      ESLOT(14),      ESLOT(15),      ESLOT(16),      ESLOT(17),      ESLOT(18),
        KC_BASE,    EMRU(0),        EMRU(1),        EMRU(2),                        EMRU(3),        EMRU(4),        EMRU(5),

                    KC_EMJ_CAT(6),  KC_EMJ_CAT(7),  KC_EMJ_CAT(8),  KC_EMJ_CAT(9),  KC_EMJ_CAT(10), KC_EMJ_CAT(11), KC_EMJ_PAGE_NEXT,
                    ESLOT(19),      ESLOT(20),      ESLOT(21),      ESLOT(22),      ESLOT(23),      ESLOT(24),      KC_NO,
        KC_NO,      ESLOT(25),      ESLOT(26),      ESLOT(27),      ESLOT(28),      ESLOT(29),      ESLOT(30),      KC_NO,
        ESLOT(31),  ESLOT(32),      ESLOT(33),      ESLOT(34),      ESLOT(35),      ESLOT(36),      ESLOT(37),      KC_NO,
        EMRU(6),    EMRU(7),        EMRU(8),                        EMRU(9),        EMRU(10),       EMRU(11),       KC_EMJ_CLEAR
        )
};


#define LX(x,y) ((x)/2),y
// Placed in .rodata so the 296-byte table sits in flash rather than RAM.
// QMK only reads g_led_config (verified in quantum/{led,rgb}_matrix/*.c); the
// type stays non-const to match the upstream extern declaration in
// quantum/rgb_matrix/rgb_matrix.h, so this is a placement override only.
__attribute__((section(".rodata"))) led_config_t g_led_config = { {// Key Matrix to LED Index
                              {6, 5, 4, 3, 2, 1, 0, NO_LED},
                              {13, 12, 11, 10, 9, 8, 7, NO_LED},
                              {20, 19, 18, 17, 16, 15, 14, NO_LED},
                              {27, 26, 25, 24, 23, 22, 21, NO_LED},
                              {35, 34, 33, 32, 31, 30, 29, 28},

                              {NO_LED, 42, 41, 40, 39, 38, 37, 36},
                              {NO_LED, 49, 48, 47, 46, 45, 44, 43},
                              {NO_LED, 56, 55, 54, 53, 52, 51, 50},
                              {NO_LED, 63, 62, 61, 60, 59, 58, 57},
                              {71, 70, 69, 68, 67, 66, 65, 64}
                             },
                             {
                                // LED Index to Physical Position
                                                {LX(144, 9)},   {LX(129, 9)},   {LX(104, 5)},   {LX(79, 1)},    {LX(55, 5)},    {LX(30, 9)},    {LX(0, 9)},
                                                {LX(144, 33)},  {LX(129, 33)},  {LX(104, 19)},  {LX(79, 25)},   {LX(55, 29)},   {LX(30, 33)},   {LX(0, 33)},
                                                {LX(144, 58)},  {LX(129, 58)},  {LX(104, 54)},  {LX(79, 50)},   {LX(55, 54)},   {LX(30, 58)},   {LX(0, 58)},
                                                {LX(144, 83)},  {LX(129, 83)},  {LX(104, 79)},  {LX(79, 75)},   {LX(55, 79)},   {LX(30, 83)},   {LX(0, 83)},
                {LX(170, 99)},  {LX(170, 127)}, {LX(144, 118)}, {LX(129, 113)},                 {LX(79, 99)},   {LX(55, 103)},  {LX(30, 107)},  {LX(6, 107)},

                                                {LX(446, 9)},   {LX(415, 9)},   {LX(390, 5)},   {LX(365, 1)},   {LX(341, 5)},   {LX(316, 9)},   {LX(286, 9)},
                                                {LX(446, 33)},  {LX(415, 33)},  {LX(390, 19)},  {LX(365, 25)},  {LX(341, 29)},  {LX(316, 33)},  {LX(286, 33)},
                                                {LX(446, 58)},  {LX(415, 58)},  {LX(390, 54)},  {LX(365, 50)},  {LX(341, 54)},  {LX(316, 58)},  {LX(286, 58)},
                                                {LX(446, 83)},  {LX(415, 83)},  {LX(390, 79)},  {LX(365, 75)},  {LX(341, 79)},  {LX(316, 83)},  {LX(286, 83)},
                                                {LX(440, 107)}, {LX(415, 107)}, {LX(390, 103)}, {LX(365, 99)},                  {LX(324, 113)}, {LX(290, 118)}, {LX(264, 127)},  {LX(264, 99)}
                             },
                             {
                                 // LED Index to Flag
                                 4, 4, 4, 4, 4, 4, 4,
                                 4, 4, 4, 4, 4, 4, 4,
                                 4, 4, 4, 4, 4, 4, 4,
                                 4, 4, 4, 4, 4, 4, 4,
                                 4, 4, 4, 4, 4, 4, 4, 4,

                                 4, 4, 4, 4, 4, 4, 4,
                                 4, 4, 4, 4, 4, 4, 4,
                                 4, 4, 4, 4, 4, 4, 4,
                                 4, 4, 4, 4, 4, 4, 4,
                                 4, 4, 4, 4, 4, 4, 4, 4
                             } };


const uint16_t encoder_map[][NUM_ENCODERS][NUM_DIRECTIONS] = {
    [0] =  { ENCODER_CCW_CW(MS_WHLD, MS_WHLU)},
    [1] =  { ENCODER_CCW_CW(MS_WHLD, MS_WHLU)},
    [2] =  { ENCODER_CCW_CW(MS_WHLD, MS_WHLU)},
    [3] =  { ENCODER_CCW_CW(MS_WHLD, MS_WHLU)},
    [4] =  { ENCODER_CCW_CW(MS_WHLD, MS_WHLU)},
    [5] =  { ENCODER_CCW_CW(MS_WHLD, MS_WHLU)},
    [6] =  { ENCODER_CCW_CW(MS_WHLD, MS_WHLU)},
    [7] =  { ENCODER_CCW_CW(MS_WHLD, MS_WHLU)},
    [8] =  { ENCODER_CCW_CW(MS_WHLD, MS_WHLU)},
    [9] =  { ENCODER_CCW_CW(MS_WHLD, MS_WHLU)},
    [10] =  { ENCODER_CCW_CW(MS_WHLD, MS_WHLU)},
    [11] =  { ENCODER_CCW_CW(MS_WHLD, MS_WHLU)},
    [12] =  { ENCODER_CCW_CW(MS_WHLD, MS_WHLU)},
};

