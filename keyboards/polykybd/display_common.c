// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#include "display_common.h"
#include "side.h"
#include <string.h>

// Declare matrix from quantum.h for display inversion
extern matrix_row_t matrix[];

static const display_config_t* g_display_config = NULL;

void display_register_config(const display_config_t* config) {
    g_display_config = config;
}

void matrix_scan_display_common(void) {
    if (!g_display_config) {
        // Fallback: call matrix_scan_user directly if not configured
        matrix_scan_user();
        return;
    }

    const display_config_t* cfg = g_display_config;
    const uint8_t first = is_left_side() ? 0 : cfg->matrix_rows_per_side;
    
    static matrix_row_t last_matrix[MATRIX_ROWS_PER_SIDE];
    bool changed = false;

    for (uint8_t r = first; r < first + cfg->matrix_rows_per_side; r++) {
        if (last_matrix[r - first] != matrix[r]) {
            changed = true;
            for (uint8_t c = 0; c < cfg->matrix_cols; c++) {
                bool old = ((last_matrix[r - first] >> c) & 1) == 1;
                bool current = ((matrix[r] >> c) & 1) == 1;

                // Skip if unchanged or no display at this position
                if (old == current || !cfg->key_has_display(r, c)) {
                    continue;
                }

                // Apply column adjustment for asymmetric layouts (split72)
                uint8_t adj_c = c;
                if (cfg->needs_col_adjustment && r >= cfg->col_adjustment_start_row) {
                    adj_c--;
                }

                cfg->invert_display(r, adj_c, current);
            }
        }
    }

    if (changed) {
        memcpy(last_matrix, &matrix[first], sizeof(last_matrix));
    }
    matrix_scan_user();
}
