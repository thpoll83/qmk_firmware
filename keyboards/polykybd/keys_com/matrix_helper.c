// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#include "matrix_helper.h"

#include QMK_KEYBOARD_H

#include "lib/split_link/side.h"
#include "poly_keymap_internal.h"   // poly_keycode_at()

// Both lookups go through poly_keycode_at(), the resolver the key events and the
// legends use. ⚠️ Not keycode_at_keymap_location(): that reads the EEPROM keymap, and
// layers from _SL up (DYNAMIC_KEYMAP_UPDATE_MAX_LAYER_COUNT) are never stored there,
// so on those layers it returned unrelated EEPROM bytes and cmd 14 could not find a key.
enum key_split_pos get_split_matrix_pos(uint16_t keycode, uint8_t layer, uint8_t* row, uint8_t* col, bool prefer_rc_left) {
    enum key_split_pos pos = POS_NOT_FOUND;

    for (uint8_t r = 0; r < MATRIX_ROWS_PER_SIDE; r++) {
        for (uint8_t c = 0; c < MATRIX_COLS; c++) {
            if (poly_keycode_at(layer, r, c) == keycode) {
                *row = r;
                *col = c;
                pos = POS_LEFT;
                break;
            }
        }
        if(pos!=POS_NOT_FOUND) {
            break;
        }
    }

    for (uint8_t r = MATRIX_ROWS_PER_SIDE; r < MATRIX_ROWS_PER_SIDE*2; r++) {
        for (uint8_t c = 0; c < MATRIX_COLS; c++) {
            if (poly_keycode_at(layer, r, c) == keycode) {
                if(!prefer_rc_left || pos==POS_NOT_FOUND) {
                    *row = r;
                    *col = c;
                }
                return pos == POS_LEFT ? POS_ON_BOTH : POS_RIGHT;
            }
        }
    }

    return pos;
}

enum key_split_pos get_split_matrix_side(uint16_t keycode, uint8_t layer) {
    enum key_split_pos pos = POS_NOT_FOUND;

    for (uint8_t r = 0; r < MATRIX_ROWS_PER_SIDE; r++) {
        for (uint8_t c = 0; c < MATRIX_COLS; c++) {
            if (poly_keycode_at(layer, r, c) == keycode) {
                pos = POS_LEFT;
                break;
            }
        }
        if(pos!=POS_NOT_FOUND) {
            break;
        }
    }

    for (uint8_t r = MATRIX_ROWS_PER_SIDE; r < MATRIX_ROWS_PER_SIDE*2; r++) {
        for (uint8_t c = 0; c < MATRIX_COLS; c++) {
            if (poly_keycode_at(layer, r, c) == keycode) {
                return pos == POS_LEFT ? POS_ON_BOTH : POS_RIGHT;
            }
        }
    }

    return pos;
}


