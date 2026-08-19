// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#include "split72.h"

#include "quantum.h"
//#include "usb_device_state.h"

#include "side.h"
#include "base/com.h"
#include "base/disp_array.h"
#include "base/helpers.h"
#include "base/spi_helper.h"
#include "base/shift_reg.h"
#include "base/text_helper.h"

#include <string.h>

static const struct display_info key_display[] = {
        {BITMASK1(0)}, {BITMASK1(1)}, {BITMASK1(2)}, {BITMASK1(3)}, {BITMASK1(4)}, {BITMASK1(5)}, {BITMASK1(6)}, {BITMASK1(7)},
        {BITMASK2(0)}, {BITMASK2(1)}, {BITMASK2(2)}, {BITMASK2(3)}, {BITMASK2(4)}, {BITMASK2(5)}, {BITMASK2(6)}, {BITMASK2(7)},
        {BITMASK3(0)}, {BITMASK3(1)}, {BITMASK3(2)}, {BITMASK3(3)}, {BITMASK3(4)}, {BITMASK3(5)}, {BITMASK3(6)}, {BITMASK3(7)},
        {BITMASK4(0)}, {BITMASK4(1)}, {BITMASK4(2)}, {BITMASK4(3)}, {BITMASK4(4)}, {BITMASK4(5)}, {BITMASK4(6)}, {BITMASK4(7)},
        {BITMASK5(0)}, {BITMASK5(1)}, {BITMASK5(2)}, {BITMASK5(3)}, {BITMASK5(4)}, {BITMASK5(5)}, {BITMASK5(6)}, {BITMASK5(7)}
};

const uint8_t* get_key_disp_bitmask(uint8_t index) {
    return key_display[index].bitmask;
}

uint8_t get_disp_bitmask_size(void) {
    return sizeof(key_display->bitmask);
}

bool key_has_display(uint8_t r, uint8_t c) {
    // The inner key at (3,7) on the left half and (8,0) on the right have no
    // OLED and no RGB LED. Every other key on both halves has both.
    return !((r == 3 && c == 7) || (r == 8 && c == 0));
}

void invert_display(uint8_t r, uint8_t c, bool state) {
    if (r>=5 && r<=8) {
        c--; //on the right side of the slit layout the first 4 rows have no key
    }

    r = r % MATRIX_ROWS_PER_SIDE;
    const uint8_t disp_idx = LAYOUT_TO_INDEX(r, c);
    // Bounds guard only, matching split42 — callers screen out the keys that
    // have no display via key_has_display(). This replaces an `if (disp_idx !=
    // 255)` test placed AFTER the indexed read, which could therefore never
    // prevent one, and which only ever matched r%5==0 anyway.
    const uint8_t table_size = (uint8_t)(sizeof(key_display) / sizeof(key_display[0]));
    if (disp_idx >= table_size) return;
    const uint8_t* bitmask = get_key_disp_bitmask(disp_idx);
    sr_shift_out_buffer_latch(bitmask, sizeof(key_display->bitmask));

    kdisp_invert(state);
}

// invert displays directly when pressed (no need to do split sync)
extern matrix_row_t matrix[MATRIX_ROWS];
static matrix_row_t last_matrix[MATRIX_ROWS_PER_SIDE];

void matrix_scan_kb(void) {
    const uint8_t first   = is_left_side() ? 0 : MATRIX_ROWS_PER_SIDE;
    bool    changed = false;
    for (uint8_t r = first; r < first + MATRIX_ROWS_PER_SIDE; r++) {
        if (last_matrix[r - first] != matrix[r]) {
            changed = true;
            for (uint8_t c = 0; c < MATRIX_COLS; c++) {
                bool old     = ((last_matrix[r - first] >> c) & 1) == 1;
                bool current = ((matrix[r] >> c) & 1) == 1;
                // Unchanged, or a key with no OLED behind it (see key_has_display).
                if (old == current || !key_has_display(r, c)) {
                    continue;
                }
                invert_display(r, c, current);
            }
        }
    }
    if (changed) {
        memcpy(last_matrix, &matrix[first], sizeof(last_matrix));
    }
    matrix_scan_user();
}

void matrix_slave_scan_kb(void) {
    //if (usb_device_state != USB_DEVICE_STATE_CONFIGURED) {
    //    rgb_matrix_disable_noeeprom();
    //}
    matrix_scan_kb();
}
