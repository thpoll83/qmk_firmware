// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#include "oled_helper.h"

#include "state.h"
#include "side.h"
#include "base/com.h"
#include "base/disp_array.h"

#include QMK_KEYBOARD_H
#include "quantum.h"

#include <stdio.h>

void num_to_u16_string(char* buffer, uint8_t buffer_len, uint8_t value) {
    if (value < 10) {
        snprintf((char*)buffer, buffer_len, "%d", value);
        buffer[1] = 0; buffer[2] = 0; buffer[3] = 0;
    } else if (value > 99) {
        snprintf((char*)buffer, buffer_len, "%d %d %d", value/100, (value/10)%10, value%10);
        buffer[1] = 0; buffer[3] = 0; buffer[5] = 0; buffer[6] = 0; buffer[7] = 0;
    } else {
        snprintf((char*)buffer, buffer_len, "%d %d", value/10, value%10);
        buffer[1] = 0; buffer[3] = 0; buffer[4] = 0; buffer[5] = 0;
    }
}

void hex_to_u16_string(char* buffer, uint8_t buffer_len, uint8_t value) {
    if (value < 16) {
        snprintf((char*)buffer, buffer_len, "%X", value);
        buffer[1] = 0; buffer[2] = 0; buffer[3] = 0;
    } else {
        snprintf((char*)buffer, buffer_len, "%X %X", value/16, value%16);
        buffer[1] = 0; buffer[3] = 0; buffer[4] = 0; buffer[5] = 0;
    }
}

void oled_status_screen(void) {
    const poly_sync_t* local_state = get_local_state();
    if ((local_state->flags & STATUS_DISP_ON) == 0) {
        oled_off();
        return;
    } else if ((local_state->flags & STATUS_DISP_ON) != 0) {
        oled_on();
    }
    oled_update_buffer();
    oled_clear();
    oled_write_raw((char*)get_scratch_buffer(), get_scratch_buffer_size());
}

void oled_render_logos(void) {
    if (is_left_side()) {
        oled_draw_poly();
        oled_scroll_right();
    } else {
        oled_draw_kybd();
        oled_scroll_left();
    }
}

bool oled_task_user(void) {
    if ((get_local_state()->flags & DISP_IDLE) != 0) {
        oled_render_logos();
    } else {
        oled_scroll_off();
        oled_status_screen();
    }
    return false;
}
