// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#include "split42.h"

#include "quantum.h"

#include "side.h"
#include "base/com.h"
#include "base/disp_array.h"
#include "base/helpers.h"
#include "base/spi_helper.h"
#include "base/shift_reg.h"
#include "base/text_helper.h"

#include <string.h>

/*
 * key_display[] maps matrix slot LAYOUT_TO_INDEX(r,c) = r*MATRIX_COLS + c = r*6 + c
 * to the shift-register bitmask that selects that key's OLED. split42 is CRKBD:
 * 4 matrix rows x 6 cols per side.
 *
 * ⚠️ The table MUST be laid out 6-WIDE (one matrix row per line). split72's table
 * is 8-wide because split72 has MATRIX_COLS == 8; copying that 8-wide layout here
 * would drift every row past row 0 by 2 and invert the wrong keycap display.
 * Each matrix row maps to its own shift register's low 6 bits:
 *   row 0 -> BITMASK1(0..5)
 *   row 1 -> BITMASK2(0..5)
 *   row 2 -> BITMASK3(0..5)
 * The thumb row (row 3): only cols 3,4,5 are physically wired (idx 21,22,23);
 * cols 0,1,2 (idx 18,19,20) have no key. The 6 free SR bits after rows 0-2 are
 * BITMASK1(6),(7) / BITMASK2(6),(7) / BITMASK3(6),(7).
 *
 * ⚠️ HARDWARE-VERIFY (bench): the thumb entries (idx 18-23) are a best guess and the
 * whole map needs confirming against the physical shift-register wiring (press each
 * key, note which OLED inverts). This affects keycap-OLED selection only, not the
 * matrix scan / typing / the split link.
 */
static const struct display_info key_display[] = {
        /* row 0 (idx  0.. 5) */ {BITMASK1(0)}, {BITMASK1(1)}, {BITMASK1(2)}, {BITMASK1(3)}, {BITMASK1(4)}, {BITMASK1(5)},
        /* row 1 (idx  6..11) */ {BITMASK2(0)}, {BITMASK2(1)}, {BITMASK2(2)}, {BITMASK2(3)}, {BITMASK2(4)}, {BITMASK2(5)},
        /* row 2 (idx 12..17) */ {BITMASK3(0)}, {BITMASK3(1)}, {BITMASK3(2)}, {BITMASK3(3)}, {BITMASK3(4)}, {BITMASK3(5)},
        /*
         * row 3 thumbs — bench-verified 2026-07-17 on the master (left) half:
         * each physical thumb OLED is bit 6 of successive shift registers, in
         * physical left→right order (bit 7 of every register is an unused phantom):
         *   thumb pos 1/2/3 (left→right) = BITMASK1(6) / BITMASK2(6) / BITMASK3(6).
         * The LEFT half's thumbs are matrix cols 3,4,5 (idx 21,22,23 = Ctrl/Space/Del);
         * cols 0-2 (idx 18-20) are KC_NO on the left, so those entries only matter for
         * the RIGHT half (matrix cols 0,1,2). Right col→position order is the symmetric
         * guess — ⚠️ VERIFY on a real right half (none available yet).
         */
        /* idx 18-20 (RIGHT thumbs, cols 0-2) */ {BITMASK1(6)}, {BITMASK2(6)}, {BITMASK3(6)},
        /* idx 21-23 (LEFT  thumbs, cols 3-5) */ {BITMASK1(6)}, {BITMASK2(6)}, {BITMASK3(6)}
};

const uint8_t* get_key_disp_bitmask(uint8_t index) {
    return key_display[index].bitmask;
}

uint8_t get_disp_bitmask_size(void) {
    return sizeof(key_display->bitmask);
}

void invert_display(uint8_t r, uint8_t c, bool state) {
    /*
     * split42 is a symmetric CRKBD: the right-half matrix rows (4-7) carry all 6
     * columns, so there is NO absent col-0 and NO c-- shift like split72 needs on
     * its upper-right rows. Fold the matrix row into this half's 0..3 range and
     * index the table directly, bounding to the table size for safety.
     */
    r = r % MATRIX_ROWS_PER_SIDE;
    const uint8_t disp_idx = LAYOUT_TO_INDEX(r, c);
    const uint8_t table_size = (uint8_t)(sizeof(key_display) / sizeof(key_display[0]));
    if (disp_idx >= table_size) return;
    const uint8_t* bitmask = get_key_disp_bitmask(disp_idx);
    sr_shift_out_buffer_latch(bitmask, sizeof(key_display->bitmask));

    kdisp_invert(state);
}

/* invert displays directly on key press/release (no split sync needed) */
extern matrix_row_t matrix[MATRIX_ROWS];
static matrix_row_t last_matrix[MATRIX_ROWS_PER_SIDE];

void matrix_scan_kb(void) {
    const uint8_t first = is_left_side() ? 0 : MATRIX_ROWS_PER_SIDE;
    bool changed = false;
    for (uint8_t r = first; r < first + MATRIX_ROWS_PER_SIDE; r++) {
        if (last_matrix[r - first] != matrix[r]) {
            changed = true;
            for (uint8_t c = 0; c < MATRIX_COLS; c++) {
                bool old     = ((last_matrix[r - first] >> c) & 1) == 1;
                bool current = ((matrix[r] >> c) & 1) == 1;
                if (!old && current) {
                    invert_display(r, c, true);
                } else if (old && !current) {
                    invert_display(r, c, false);
                }
            }
        }
    }
    if (changed) {
        memcpy(last_matrix, &matrix[first], sizeof(last_matrix));
    }
    matrix_scan_user();
}

void matrix_slave_scan_kb(void) {
    matrix_scan_kb();
}
