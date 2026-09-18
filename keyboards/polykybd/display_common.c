// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#include "display_common.h"
#include "side.h"
#include <string.h>

// Declare matrix from quantum.h for display inversion
extern matrix_row_t matrix[];

void matrix_scan_display_common(void) {
    const uint8_t first = is_left_side() ? 0 : MATRIX_ROWS_PER_SIDE;
    
    static matrix_row_t last_matrix[MATRIX_ROWS_PER_SIDE];
    bool changed = false;

    for (uint8_t r = first; r < first + MATRIX_ROWS_PER_SIDE; r++) {
        if (last_matrix[r - first] != matrix[r]) {
            changed = true;
            for (uint8_t c = 0; c < MATRIX_COLS; c++) {
                bool old = ((last_matrix[r - first] >> c) & 1) == 1;
                bool current = ((matrix[r] >> c) & 1) == 1;

                // Skip if unchanged or no display at this position
                if (old == current || !key_has_display(r, c)) {
                    continue;
                }

                // Apply column adjustment for asymmetric layouts (split72)
                // POLY_DISPLAY_COL_ADJUST is defined per-variant in split72.h/split42.h
                uint8_t adj_c = c;
#if defined(POLY_DISPLAY_COL_ADJUST) && POLY_DISPLAY_COL_ADJUST != 255
                if (r >= POLY_DISPLAY_COL_ADJUST) {
                    adj_c--;
                }
#endif

                invert_display(r, adj_c, current);
            }
        }
    }

    if (changed) {
        memcpy(last_matrix, &matrix[first], sizeof(last_matrix));
    }
    matrix_scan_user();
}
