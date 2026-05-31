#include "poly_util.h"

#include "quantum.h"

#include "state.h"
#include "bridge_helper.h"
#include "base/com.h"
#include "base/disp_array.h"
#include "base/shift_reg.h"
#include "base/helpers.h"
#include "base/fonts/FreeSansBold24pt7b.h"

#include <print.h>
#include <transactions.h>

const uint8_t* get_key_disp_bitmask(uint8_t index);
uint8_t get_disp_bitmask_size(void);

extern void set_displays(uint8_t contrast, bool idle);

void select_all_displays(void) {
    sr_shift_out_0_latch(NUM_SHIFT_REGISTERS);
}

void clear_all_displays(void) {
    select_all_displays();
    kdisp_set_buffer(0x00);
    kdisp_send_buffer();
}

void display_bootloader_message(void) {
    #ifdef RGB_MATRIX_ENABLE
        rgb_matrix_enable_noeeprom();
        rgb_matrix_mode_noeeprom(RGB_MATRIX_SOLID_COLOR);
        rgb_matrix_set_color_all(0, 5, 3);
        rgb_matrix_update_pwm_buffers();
    #endif

    clear_all_displays();
    // Contrast 10 → SETCONTRAST 9/255; contrast 1 tested unreadable.
    set_displays(20, false);
    display_message(1, 1, u"BOOT-",   &FreeSansBold24pt7b);
    display_message(3, 0, u"LOADER!", &FreeSansBold24pt7b);
}

// Announce an imminent bootloader jump: flag it, sync the slave half so it
// shows the bootloader message too, then show the message locally. The caller
// resets afterwards — via QMK for the QK_BOOTLOADER keymap path, or via
// reset_keyboard() for the host-triggered HID command (hid_com.c case 23).
void poly_announce_bootloader(void) {
    uprintf("Bootloader entered. Please copy new Firmware.\n");
    // Sync slave before the reset — QMK resets before housekeeping runs.
    access_local_state()->overlay_flags |= BOOTLOADER_DISPLAY;
    uint8_t ack = send_to_bridge(USER_SYNC_POLY_DATA, (void *)access_local_state(), sizeof(poly_sync_t), 10);
    uprintf("Master: BOOTLOADER_DISPLAY sync ack=%d\n", ack);
    display_bootloader_message();
}

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
