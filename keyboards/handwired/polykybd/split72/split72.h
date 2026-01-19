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

enum side_state { UNDECIDED, LEFT_SIDE, RIGHT_SIDE };

bool side_is_undecided(void);

void set_side(enum side_state s);

bool is_left_side(void);

bool is_right_side(void);

struct display_info {
    uint8_t bitmask[NUM_SHIFT_REGISTERS];
};

#define BITMASK1(x) .bitmask = {~0, ~0, ~0, ~0, (uint8_t)(~(1<<x))}
#define BITMASK2(x) .bitmask = {~0, ~0, ~0, (uint8_t)(~(1<<x)), ~0}
#define BITMASK3(x) .bitmask = {~0, ~0, (uint8_t)(~(1<<x)), ~0, ~0}
#define BITMASK4(x) .bitmask = {~0, (uint8_t)(~(1<<x)), ~0, ~0, ~0}
#define BITMASK5(x) .bitmask = {(uint8_t)(~(1<<x)), ~0, ~0, ~0, ~0}

#define HID_CMD_IDX 1
#define HID_DATA_IDX 2
#define LANG_TO_UI32(a,b,c,d) (((uint32_t)(a))<<24 | ((uint32_t)(b))<<16 | ((uint32_t)(c))<<8 | (d))
#define LANG_TO_UI32_ARR(arr) (((uint32_t)(arr[0]))<<24 | ((uint32_t)(arr[1]))<<16 | ((uint32_t)(arr[2]))<<8 | (arr[3]))
#define LANGSTR_TO_UI32(str) LANG_TO_UI32(str[0],str[1],str[2],str[3])


#ifdef VIA_ENABLE
typedef struct _via_sync_t {
    uint32_t crc32;
    uint8_t  via_commands[32];
} via_sync_t;
#endif

enum key_split_pos { POS_NOT_FOUND, POS_LEFT, POS_RIGHT, POS_ON_BOTH };

const char* pos_to_str(enum key_split_pos pos);

bool is_on_current_side(enum key_split_pos pos);

bool is_on_other_side(enum key_split_pos pos);

enum key_split_pos get_split_matrix_pos(uint16_t keycode, uint8_t layer, uint8_t* row, uint8_t* col, bool prefer_rc_left);

enum key_split_pos get_split_matrix_side(uint16_t keycode, uint8_t layer);

//tells if the given keycode is on the current side (still there could be the same key on the other side)
bool is_on_current_split_matrix_side(uint16_t keycode, uint8_t layer);

void display_message(uint8_t row, uint8_t col, const uint16_t* message, const GFXfont* font);

void invert_display(uint8_t r, uint8_t c, bool state);

void oled_draw_kybd(void);

void oled_draw_poly(void);
