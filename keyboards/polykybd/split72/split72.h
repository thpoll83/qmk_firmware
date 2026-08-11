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
    uint8_t bitmask[NUM_SHIFT_REGISTERS];
};

#define BITMASK1(x) .bitmask = {~0, ~0, ~0, ~0, (uint8_t)(~(1<<x))}
#define BITMASK2(x) .bitmask = {~0, ~0, ~0, (uint8_t)(~(1<<x)), ~0}
#define BITMASK3(x) .bitmask = {~0, ~0, (uint8_t)(~(1<<x)), ~0, ~0}
#define BITMASK4(x) .bitmask = {~0, (uint8_t)(~(1<<x)), ~0, ~0, ~0}
#define BITMASK5(x) .bitmask = {(uint8_t)(~(1<<x)), ~0, ~0, ~0, ~0}

// Variant parameters consumed by the shared poly_keymap.c:
//   POLY_DISP_ROW_0 / _3 — which keycap display the row-0 / second-half scan starts on
//   POLY_SPLASH_*        — the right-half boot splash (left half is always "POLY"/"KYBD")
//   LATIN_PICKER_SLOTS   — variation keys on the Intl picker row (see _ADDLANG1)
#define POLY_DISP_ROW_0   BITMASK1(0)
#define POLY_DISP_ROW_3   BITMASK4(0)
#define POLY_SPLASH_R1    U"SPLIT"
#define POLY_SPLASH_R2    U" 7 2"
#define POLY_SPLASH_R2_ROW 3

// The picker row is 7 positions per half.  Both page arrows sit on the outer left
// (matching split42, which has no room for one at each end), leaving 5 slots on the
// left block and 7 on the right.  Rows longer than this page; see latin_page_count().
#define LATIN_PICKER_SLOTS 12

void invert_display(uint8_t r, uint8_t c, bool state);

const uint8_t* get_key_disp_bitmask(uint8_t index);

uint8_t get_disp_bitmask_size(void);

