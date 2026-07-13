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
 * key_display[] maps matrix slot LAYOUT_TO_INDEX(row, col) to shift-register
 * bitmasks. split42 is CRKBD: 4 matrix rows × 6 cols per side.
 *
 * ⚠️ The table is indexed LAYOUT_TO_INDEX(r,c) = r*MATRIX_COLS + c = r*6 + c, so it
 * MUST be laid out 6-WIDE (one row per matrix row). It was originally copied from
 * split72's table, which is 8-wide because split72 has MATRIX_COLS = 8 — with 6-wide
 * indexing that 8-wide fill drifted every row past row 0 by 2 (8-6), so pressing a
 * key inverted the display two positions earlier (field, 2026-07-10). Fixed by making
 * each matrix row map to its own shift register's low 6 bits:
 *   Row 0 -> BITMASK1(0..5)   (hardware-verified at bring-up: esc q w e r t)
 *   Row 1 -> BITMASK2(0..5)   (confirmed: shift(r2c0) lit BITMASK2(4)=f, z lit (5)=g)
 *   Row 2 -> BITMASK3(0..5)   (natural completion; verify on hardware)
 *
 * CRKBD physical layout per side: rows 0–2 are 6 keys each; row 3 is the thumb
 * cluster — only cols 3,4,5 are wired (idx 21,22,23); cols 0,1,2 (idx 18,19,20) have
 * no key and are never inverted, so their entries are unused placeholders.
 *
 * TODO(bring-up): the 3 thumb displays (idx 21-23) are a best guess — the 6 free SR
 * bits after rows 0–2 are BITMASK1(6),(7) / BITMASK2(6),(7) / BITMASK3(6),(7). Press
 * each thumb, see which display inverts, and correct these three entries.
 */
static const struct display_info key_display[] = {
        /* row 0 (idx  0.. 5) */ {BITMASK1(0)}, {BITMASK1(1)}, {BITMASK1(2)}, {BITMASK1(3)}, {BITMASK1(4)}, {BITMASK1(5)},
        /* row 1 (idx  6..11) */ {BITMASK2(0)}, {BITMASK2(1)}, {BITMASK2(2)}, {BITMASK2(3)}, {BITMASK2(4)}, {BITMASK2(5)},
        /* row 2 (idx 12..17) */ {BITMASK3(0)}, {BITMASK3(1)}, {BITMASK3(2)}, {BITMASK3(3)}, {BITMASK3(4)}, {BITMASK3(5)},
        /* row 3: idx 18-20 = no key (placeholders); idx 21-23 = thumbs (VERIFY) */
        {BITMASK1(6)}, {BITMASK1(7)}, {BITMASK2(6)}, {BITMASK2(7)}, {BITMASK3(6)}, {BITMASK3(7)}
};

const uint8_t* get_key_disp_bitmask(uint8_t index) {
    return key_display[index].bitmask;
}

uint8_t get_disp_bitmask_size(void) {
    return sizeof(key_display->bitmask);
}

void invert_display(uint8_t r, uint8_t c, bool state) {
    /*
     * split42 is a symmetric CRKBD: the right-half rows (4–7) carry all 6 columns,
     * so there is NO absent col-0 and NO c-- shift like split72 needs on its upper
     * right rows. Fold the matrix row into this half's 0..3 range and index directly.
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
