#include "matrix_helper.h"

#include QMK_KEYBOARD_H

#include "side.h"
#include "quantum/keymap_introspection.h"

enum key_split_pos get_split_matrix_pos(uint16_t keycode, uint8_t layer, uint8_t* row, uint8_t* col, bool prefer_rc_left) {
    enum key_split_pos pos = POS_NOT_FOUND;

    for (uint8_t r = 0; r < MATRIX_ROWS_PER_SIDE; r++) {
        for (uint8_t c = 0; c < MATRIX_COLS; c++) {
            if (keycode_at_keymap_location(layer, r, c) == keycode) {
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
            if (keycode_at_keymap_location(layer, r, c) == keycode) {
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
            if (keycode_at_keymap_location(layer, r, c) == keycode) {
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
            if (keycode_at_keymap_location(layer, r, c) == keycode) {
                return pos == POS_LEFT ? POS_ON_BOTH : POS_RIGHT;
                break;
            }
        }
    }

    return pos;
}

//tells if the given keycode is on the current side (still there could be the same key on the other side)
bool is_on_current_split_matrix_side(uint16_t keycode, uint8_t layer) {
    const uint8_t first = is_left_side() ? 0 : MATRIX_ROWS_PER_SIDE;
    for (uint8_t r = first; r < first + MATRIX_ROWS_PER_SIDE; r++) {
        for (uint8_t c = 0; c < MATRIX_COLS; c++) {
            if (keycode_at_keymap_location(layer, r, c) == keycode) {
                return true;
            }
        }
    }
    return false;
}

