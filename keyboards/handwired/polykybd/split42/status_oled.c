// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#include "status_oled.h"
#include "../oled_helper.h"

#include "split42.h"
#include "../bridge_helper.h"
#include "../state.h"
#include "../side.h"
#include "../base/com.h"
#include "../base/disp_array.h"
#include "../base/text_helper.h"
#include "../base/fonts/NotoSans_Regular_Base_11pt.h"
#include "../base/fonts/NotoSans_Medium_Base_8pt.h"
#include "../lang/named_glyphs.h"

#include QMK_KEYBOARD_H
#include "quantum.h"

#include <stdint.h>
#include <stdio.h>

extern const GFXfont* const ALL_FONTS[];
#define ALL_FONT_SIZE_OLED_INT 43

/*
 * Status screen layout for 128×32 OLED:
 *   Row 1 (y≈11): layer icon + layer number + side (L/R) | lock icons
 *   Row 2 (y≈22): default layout name (left) / brightness bar (right)
 *   Row 3 (y≈31): WPM + language
 */
void oled_update_buffer(void) {
    uint32_t buffer[32];

    kdisp_set_buffer(0);

    const poly_layer_t* global_layer = get_global_layer();
    const GFXfont* displayFont[] = { &NotoSans_Regular11pt7b };
    const GFXfont* smallFont[]   = { &NotoSans_Medium8pt7b };

    /* Row 1: layer icon + layer number + side */
    kdisp_write_gfx_text(ALL_FONTS, ALL_FONT_SIZE_OLED_INT, 0, 11, ICON_LAYER);
    hex_to_u32_string((char*)buffer, sizeof(buffer), get_highest_layer(global_layer->layer));
    kdisp_write_gfx_text(displayFont, 1, 16, 11, buffer);
    if (side_is_undecided()) {
        kdisp_write_gfx_text(smallFont, 1, 40, 11, U"?");
    } else {
        kdisp_write_gfx_text(smallFont, 1, 36, 11, is_left_side() ? U"L" : U"R");
    }

    /* Row 1 right: lock indicators */
    kdisp_write_gfx_text(ALL_FONTS, ALL_FONT_SIZE_OLED_INT, 100, 11,
        global_layer->led_state.num_lock  ? ICON_NUMLOCK_ON  : ICON_NUMLOCK_OFF);
    kdisp_write_gfx_text(ALL_FONTS, ALL_FONT_SIZE_OLED_INT, 114, 11,
        global_layer->led_state.caps_lock ? ICON_CAPSLOCK_ON : ICON_CAPSLOCK_OFF);

    /* Row 2: default layout name (both sides — no RGB on split42) */
    switch (get_local_layer()->def_layer) {
        case 0: kdisp_write_gfx_text(smallFont, 1, 0, 22, U"Qwerty");      break;
        case 1: kdisp_write_gfx_text(smallFont, 1, 0, 22, U"Qwerty Stag!"); break;
        case 2: kdisp_write_gfx_text(smallFont, 1, 0, 22, U"Colemak DH");  break;
        case 3: kdisp_write_gfx_text(smallFont, 1, 0, 22, U"Neo");          break;
        case 4: kdisp_write_gfx_text(smallFont, 1, 0, 22, U"Workman");      break;
        default: kdisp_write_gfx_text(smallFont, 1, 0, 22, U"Unknown");     break;
    }

    /* Row 2 right: USB/Batt indicator */
    kdisp_write_gfx_text(smallFont, 1, 112, 22, is_usb_host_side() ? U"H" : U"B");

    /* Row 3: display brightness bar + WPM + language */
    const poly_sync_t* local_state = get_local_state();
    kdisp_write_gfx_text(smallFont, 1, 0, 31, U"Dsp*");
    num_to_u32_string((char*)buffer, sizeof(buffer), local_state->contrast);
    kdisp_write_gfx_text(smallFont, 1, 40, 31, buffer);

    kdisp_write_gfx_text(smallFont, 1, 62, 31, U"W");
    num_to_u32_string((char*)buffer, sizeof(buffer), get_current_wpm());
    kdisp_write_gfx_text(smallFont, 1, 74, 31, buffer);

    kdisp_write_gfx_text(smallFont, 1, 96, 31, U"L");
    num_to_u32_string((char*)buffer, sizeof(buffer), local_state->lang);
    kdisp_write_gfx_text(smallFont, 1, 108, 31, buffer);
}

/*
 * 128×32 logo bitmaps (512 bytes each = 128*32/8).
 * TODO: Replace these placeholder bitmaps with actual 128×32 artwork.
 * Use images/png_to_code.py to generate byte arrays from PNG files.
 * For now, a minimal placeholder pattern is used so the firmware compiles.
 */
void oled_draw_kybd(void) {
    static const char kybd_bitmap[512] = { 0 };
    oled_write_raw_P(kybd_bitmap, sizeof(kybd_bitmap));
}

void oled_draw_poly(void) {
    static const char poly_bitmap[512] = { 0 };
    oled_write_raw_P(poly_bitmap, sizeof(poly_bitmap));
}
