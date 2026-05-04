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
    uint8_t bitmask[NUM_SHIFT_REGISTERS];  /* 3 bytes for 3 shift registers */
};

/*
 * BITMASK macros select one keycap display via the shift-register chain.
 * Each macro pulls one bit low; all others stay high (displays deselected).
 * Ordering: bitmask[0] is the byte shifted in LAST (farthest from MCU in chain).
 * Verify this matches the actual PCB shift-register chain direction.
 */
#define BITMASK1(x) .bitmask = {~0, ~0, (uint8_t)(~(1 << (x)))}
#define BITMASK2(x) .bitmask = {~0, (uint8_t)(~(1 << (x))), ~0}
#define BITMASK3(x) .bitmask = {(uint8_t)(~(1 << (x))), ~0, ~0}

void invert_display(uint8_t r, uint8_t c, bool state);

const uint8_t* get_key_disp_bitmask(uint8_t index);

uint8_t get_disp_bitmask_size(void);
