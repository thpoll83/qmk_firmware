#include "poly_util.h"

#include "quantum.h"

#include "base/disp_array.h"
#include "base/shift_reg.h"
#include "base/helpers.h"

const uint8_t* get_key_disp_bitmask(uint8_t index);

uint8_t get_disp_bitmask_size(void);

void display_message(uint8_t row, uint8_t col, const uint16_t* message, const GFXfont* font) {

    const GFXfont* displayFont[] = { font };
    uint8_t index = 0;
    for (uint8_t c = 0; c < MATRIX_COLS; ++c) {

        uint8_t disp_idx = LAYOUT_TO_INDEX(row, c);

        if (disp_idx != 255) {

            sr_shift_out_buffer_latch(get_key_disp_bitmask(disp_idx), get_disp_bitmask_size());
            kdisp_set_buffer(0x00);

            if (c >= col && message[index] != 0) {
                const uint16_t text[2] = { message[index], 0 };
                kdisp_write_gfx_text(displayFont, 1, 49, 38, text);
                index++;
            }
            kdisp_send_buffer();
        }
    }
}
