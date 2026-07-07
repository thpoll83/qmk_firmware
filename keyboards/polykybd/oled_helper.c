// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#include "oled_helper.h"

#include "state.h"
#include "side.h"
#include "base/com.h"
#include "base/disp_array.h"
#include "base/fw_staging.h"
#ifdef POLYKYBD_DOOM
#include "doom/doom_mode.h"
#include "doom/doom_logo_oled.h"
#endif

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

// Widen an ASCII C string into the 32-bit codepoint string the kdisp text pipeline
// expects (kdisp_write_gfx_text takes const uint32_t*). NUL-terminated, never
// overruns `buffer_len`. Used for the font-pack bundle name on the flash screen.
void ascii_to_u32_string(char* buffer, uint8_t buffer_len, const char* s) {
    uint32_t* out = (uint32_t*)buffer;
    uint8_t   cap = buffer_len / (uint8_t)sizeof(uint32_t);
    uint8_t   i   = 0;
    if (s) {
        for (; s[i] && (i + 1u) < cap; ++i) out[i] = (uint32_t)(uint8_t)s[i];
    }
    if (i < cap) out[i] = 0;
}

void hex_to_u32_string(char* buffer, uint8_t buffer_len, uint8_t value) {
    uint32_t* out = (uint32_t*)buffer;
    uint8_t   cap = buffer_len / (uint8_t)sizeof(uint32_t);
    uint8_t   i   = 0;
    if (value >= 16 && i < cap) { uint8_t hi = value / 16; out[i++] = (hi < 10 ? U'0' + hi : U'A' + hi - 10); }
    if (i < cap) { uint8_t lo = value % 16; out[i++] = (lo < 10 ? U'0' + lo : U'A' + lo - 10); }
    if (i < cap) out[i] = 0;
}

void oled_draw_layout_name(const GFXfont* const* font, int8_t x, int8_t y, uint8_t def_layer) {
    // Indexed by def_layer (the _L0.._L4 default-layout enum in each variant's
    // keymaps/default/layers.h). Keep in sync with the KC_L0..KC_L4 selectors in
    // poly_keymap.c; an out-of-range value falls back to "Unknown".
    static const uint32_t* const names[] = {
        U"Qwerty", U"Qwerty Stag!", U"Colemak DH", U"Neo", U"Workman",
    };
    const uint32_t* name = (def_layer < ARRAY_SIZE(names)) ? names[def_layer] : U"Unknown";
    kdisp_write_gfx_text(font, 1, x, y, name);
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

// Progress bar drawn into the kdisp scratch buffer (call from
// oled_update_buffer_fw_update before the blit).
void oled_fw_update_progress_bar(int8_t top_y, int8_t bottom_y, uint8_t pct) {
    if (bottom_y <= top_y) return;   // guard: inverted bounds would wrap the unsigned height
    if (pct > 100) pct = 100;
    uint8_t fill = (uint8_t)((uint16_t)pct * 127u / 100u);   // 0..127 across the display
    uint8_t height = (uint8_t)(bottom_y - top_y);
    if (fill) kdisp_fill_rect(0, top_y, fill, height);
}

// Draw `pct` (0..100) as digits RIGHT-ALIGNED so the number's right edge ends a
// couple px before `pct_sign_x` — the fixed x where the caller then draws the
// "%" sign. The number grows leftward as it gains digits, so a 2- or 3-digit
// value no longer overruns the "%" (the old left-anchored draw overwrote it).
void oled_fw_update_percent(const GFXfont *const *font, int8_t pct_sign_x, int8_t y, uint8_t pct) {
    uint32_t buf[6];
    num_to_u32_string((char*) buf, sizeof(buf), pct);
    int8_t lo = 0, hi = 0;
    kdisp_gfx_text_bounds(font, 1, buf, &lo, &hi);   // pixel extents at draw-origin 0
    int8_t x = (int8_t)(pct_sign_x - 2 - hi);        // right edge ~2 px before the "%"
    if (x < 0) x = 0;
    kdisp_write_gfx_text(font, 1, x, y, buf);
}

// Shared 0..100 progress of the in-flight flash (current bundle's bytes).
uint8_t fw_update_percent(void) {
    uint32_t total = fw_staging_image_size();
    uint32_t done  = fw_staging_next_offset();
    return total ? (uint8_t)(((uint64_t)done * 100) / total) : 0;
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
#ifdef POLYKYBD_DOOM
    } else if (doom_mode_active() || get_local_state()->doom_ctl) {
        // Game mode status OLED — master directly, slave via the synced
        // control-pad flag. In a level the MASTER shows the doomguy face
        // (redrawn only when the face index changes); otherwise the DOOM
        // logo, whose HARDWARE scroll runs only during the attract (the
        // driver activates scroll once the buffer is clean, and repeated
        // identical writes stay non-dirty — zero traffic while scrolling).
        int face = doom_status_face_render((uint8_t *)get_scratch_buffer());
        if (face == 2) {
            oled_scroll_off();
            oled_write_raw((char *)get_scratch_buffer(), get_scratch_buffer_size());
        } else if (face == 0) {
            oled_write_raw((const char *)DOOM_LOGO_OLED, sizeof(DOOM_LOGO_OLED));
            if (doom_status_scroll()) {
                oled_scroll_left();
            } else {
                oled_scroll_off();
            }
        }
        // face == 1: the panel already shows the current face — leave it be.
#endif
    } else if ((get_local_state()->flags & DISP_IDLE) != 0) {
        oled_render_logos();
    } else {
        oled_scroll_off();
        oled_status_screen();
    }
    return false;
}
