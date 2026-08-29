// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Stand-in for quantum.h in the standalone googletest build (OS_ACTIONS_UNIT_TEST):
// the five mod/tap primitives the emitter calls (implemented by the suite as a
// recording mock) and the keycode/modifier constants the chord table spells its
// entries with. Values are QMK's real ones (HID usage ids / MOD_BIT masks) so a
// recorded tap reads like the firmware's.
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint8_t get_mods(void);
void    set_mods(uint8_t mods);
void    clear_mods(void);
void    register_mods(uint8_t mods);
void    unregister_mods(uint8_t mods);
void    tap_code(uint8_t code);

#ifdef __cplusplus
}
#endif

#define MOD_LCTL 0x01
#define MOD_LSFT 0x02
#define MOD_LALT 0x04
#define MOD_LGUI 0x08

enum shim_keycodes {
    KC_NO    = 0x00,
    KC_A     = 0x04,
    KC_C     = 0x06,
    KC_F     = 0x09,
    KC_L     = 0x0F,
    KC_Q     = 0x14,
    KC_S     = 0x16,
    KC_V     = 0x19,
    KC_X     = 0x1B,
    KC_Y     = 0x1C,
    KC_Z     = 0x1D,
    KC_4     = 0x21,
    KC_TAB   = 0x2B,
    KC_SPACE = 0x2C,
    KC_DOT   = 0x37,
    KC_GRV   = 0x35,
    KC_PSCR  = 0x46,
    KC_END   = 0x4D,
    KC_HOME  = 0x4A,
    KC_RIGHT = 0x4F,
    KC_LEFT  = 0x50,
    KC_LGUI  = 0xE3,
};
