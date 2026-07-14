/*
This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include QMK_KEYBOARD_H
#include "quantum/keymap_introspection.h"

#include "../base/fonts/gfxfont.h"

#include <stdint.h>
#include <stdbool.h>


struct display_info {
    uint8_t bitmask[NUM_SHIFT_REGISTERS];  /* split42: 3 shift registers (vs split72's 5) */
};

/*
 * BITMASK macros select ONE keycap display by pulling one shift-register output
 * low (all others high). split42 has 3 registers, so 3 mask bytes. bitmask[0] is
 * the byte shifted in LAST (farthest from the MCU in the chain) — same convention
 * as split72's 5-register macros.
 */
#define BITMASK1(x) .bitmask = {~0, ~0, (uint8_t)(~(1<<(x)))}
#define BITMASK2(x) .bitmask = {~0, (uint8_t)(~(1<<(x))), ~0}
#define BITMASK3(x) .bitmask = {(uint8_t)(~(1<<(x))), ~0, ~0}

// Variant parameters consumed by the shared poly_keymap.c:
//   POLY_DISP_ROW_0 / _3 — the shift-register seed update_displays() latches to
//                          select the FIRST keycap display of the row-0 scan and of
//                          the second-half (row-3) scan, then shifts across.
//   POLY_SPLASH_*        — the right-half boot splash (128x32 status OLED, 2 rows).
//
// ⚠️ HARDWARE-VERIFY (bench): update_displays() assumes MATRIX_COLS==8 (one full
// shift register per matrix row) — true on split72, NOT on split42 (6 cols per
// 8-bit register). POLY_DISP_ROW_3 (the thumb-row scan seed) and the key_display[]
// map in split42.c therefore need confirming against the physical shift-register
// wiring. This only affects keycap-OLED rendering, not the matrix scan / typing /
// split link. Value below is a reasoned starting point, to be checked on hardware.
#define POLY_DISP_ROW_0    BITMASK1(0)
#define POLY_DISP_ROW_3    BITMASK3(2)
#define POLY_SPLASH_R1     U"SPLIT"
#define POLY_SPLASH_R2     U" 4 2"
#define POLY_SPLASH_R2_ROW 2

// Board name reported in the GET_ID (cmd 6) string, so the host shows the right
// variant. hid_com.c falls back to "Split72" when a variant doesn't define this,
// so split72's GET_ID stays byte-identical (it relies on that fallback).
#define POLY_KB_NAME "Split42"

void invert_display(uint8_t r, uint8_t c, bool state);

const uint8_t* get_key_disp_bitmask(uint8_t index);

uint8_t get_disp_bitmask_size(void);
