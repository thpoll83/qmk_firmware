#include "corne42.h"

#include "quantum.h"

#include "../side.h"
#include "../base/com.h"
#include "../base/disp_array.h"
#include "../base/helpers.h"
#include "../base/spi_helper.h"
#include "../base/shift_reg.h"
#include "../base/text_helper.h"

#include <string.h>

/*
 * key_display[] maps matrix slot LAYOUT_TO_INDEX(row, col) to shift-register
 * bitmasks. 4 rows × 6 cols = 24 entries per side.
 *
 * CRKBD physical layout per side:
 *   Rows 0–2: 6 keys each (top / home / bottom alphanumeric rows)
 *   Row 3:    thumb cluster — only 3 of 6 col positions are physically wired.
 *             The remaining 3 slots are dummies (no display attached).
 *
 * TODO: Verify bitmask assignments against the actual PCB wiring:
 *   - Which shift register drives which row group
 *   - Which col positions in row 3 are the thumb keys
 *   - Whether the SR chain order matches BITMASK1/2/3 ordering in corne42.h
 */
static const struct display_info key_display[] = {
    /* Row 0 (top row, 6 keys): SR1 bits 0–5 */
    {BITMASK1(0)}, {BITMASK1(1)}, {BITMASK1(2)},
    {BITMASK1(3)}, {BITMASK1(4)}, {BITMASK1(5)},
    /* Row 1 (home row, 6 keys): SR1 bits 6–7, SR2 bits 0–3 */
    {BITMASK1(6)}, {BITMASK1(7)}, {BITMASK2(0)},
    {BITMASK2(1)}, {BITMASK2(2)}, {BITMASK2(3)},
    /* Row 2 (bottom row, 6 keys): SR2 bits 4–7, SR3 bits 0–1 */
    {BITMASK2(4)}, {BITMASK2(5)}, {BITMASK2(6)},
    {BITMASK2(7)}, {BITMASK3(0)}, {BITMASK3(1)},
    /* Row 3 (thumb cluster): cols 3–5 are the real thumb keys,
       cols 0–2 are unused matrix positions (no physical key/display).
       TODO: adjust col order to match PCB routing. */
    {BITMASK3(5)}, {BITMASK3(5)}, {BITMASK3(5)}, /* cols 0-2: unused dummies */
    {BITMASK3(2)}, {BITMASK3(3)}, {BITMASK3(4)}, /* cols 3-5: thumb keys */
};

const uint8_t* get_key_disp_bitmask(uint8_t index) {
    return key_display[index].bitmask;
}

uint8_t get_disp_bitmask_size(void) {
    return sizeof(key_display->bitmask);
}

void invert_display(uint8_t r, uint8_t c, bool state) {
    /*
     * Right-side rows are 4–7. On split72 the right side had col 0 absent,
     * requiring c--. Check whether corne42 needs the same adjustment.
     * TODO: update this offset if the right-half matrix layout requires it.
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
