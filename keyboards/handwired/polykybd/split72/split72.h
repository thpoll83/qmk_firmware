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

typedef struct _poly_eeconf_t {
    uint8_t lang;
    uint8_t brightness;
    uint16_t unused;
    uint8_t latin_ex[26];
} poly_eeconf_t;


static_assert(sizeof(poly_eeconf_t) == EECONFIG_USER_DATA_SIZE, "Mismatch in keyboard EECONFIG stored data");

typedef struct _poly_layer_t {
    uint32_t      crc32;
    layer_state_t layer;
    layer_state_t def_layer;
    led_t         led_state;
    uint8_t       mods;
} poly_layer_t;

typedef struct _poly_sync_t {
    uint32_t crc32;
    uint8_t  lang;
    uint8_t  contrast;
    uint8_t  flags;
    uint8_t  overlay_flags;
    uint8_t  unicode_mode;
} poly_sync_t;

typedef struct _poly_last_t {
    uint32_t crc32;
    uint16_t latin_kc;
} poly_last_t;

typedef struct _latin_sync_t {
    uint32_t crc32;
    uint8_t  ex[26];
} latin_sync_t;

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
