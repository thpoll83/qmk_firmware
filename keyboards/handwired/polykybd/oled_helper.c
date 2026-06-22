// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#include "oled_helper.h"

#include "state.h"
#include "side.h"
#include "base/com.h"
#include "base/disp_array.h"
#include "base/fw_staging.h"

#include QMK_KEYBOARD_H
#include "quantum.h"

#include <stdio.h>

// Render `value` as a char32 (U"...") display string into `buffer`. The display
// pipeline is 32-bit (kdisp_write_gfx_text takes const uint32_t*), so each digit
// glyph is one uint32_t codepoint. `buffer_len` is the byte size of the buffer.
static inline void digits_to_u32_string(char* buffer, uint8_t buffer_len, uint8_t value, uint8_t base) {
    uint32_t* out = (uint32_t*)buffer;
    uint8_t   cap = buffer_len / (uint8_t)sizeof(uint32_t);
    uint8_t   i   = 0;
    if (value >= base * base && i < cap) out[i++] = U'0' + (value / (base * base)) % base;
    if (value >= base       && i < cap) out[i++] = U'0' + (value / base) % base;
    if (i < cap) out[i++] = U'0' + (value % base);
    if (i < cap) out[i] = 0;
}

void num_to_u32_string(char* buffer, uint8_t buffer_len, uint8_t value) {
    digits_to_u32_string(buffer, buffer_len, value, 10);
}

void hex_to_u32_string(char* buffer, uint8_t buffer_len, uint8_t value) {
    uint32_t* out = (uint32_t*)buffer;
    uint8_t   cap = buffer_len / (uint8_t)sizeof(uint32_t);
    uint8_t   i   = 0;
    if (value >= 16 && i < cap) { uint8_t hi = value / 16; out[i++] = (hi < 10 ? U'0' + hi : U'A' + hi - 10); }
    if (i < cap) { uint8_t lo = value % 16; out[i++] = (lo < 10 ? U'0' + lo : U'A' + lo - 10); }
    if (i < cap) out[i] = 0;
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

// Shown on both halves while a font-pack / firmware flash is in progress, so the
// user knows the keyboard is busy updating (it can't service keys meanwhile) and
// must not be unplugged. Forced on regardless of the display-off state. The status
// fonts are resident, so this renders even while the font pack is mid-flash.
void oled_fw_update_screen(void) {
    oled_on();
    oled_update_buffer_fw_update();
    oled_clear();
    oled_write_raw((char*)get_scratch_buffer(), get_scratch_buffer_size());
}

bool oled_task_user(void) {
    if (fw_staging_fw_up_active()) {
        oled_scroll_off();
        oled_fw_update_screen();
    } else if ((get_local_state()->flags & DISP_IDLE) != 0) {
        oled_render_logos();
    } else {
        oled_scroll_off();
        oled_status_screen();
    }
    return false;
}
