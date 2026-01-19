#include "status_oled.h"

#include "split72.h"
#include "sync_helper.h"
#include "../state.h"
#include "../base/com.h"
#include "../base/disp_array.h"
#include "../base/text_helper.h"
//#include "../base/fonts/gfx_used_fonts.h"
#include "../base/fonts/NotoSans_Regular_Base_11pt.h"
#include "../base/fonts/NotoSans_Medium_Base_8pt.h"
#include "keymaps/default/lang/named_glyphs.h" //should move to base or similar

#include QMK_KEYBOARD_H

#include <stdint.h>
#include <stdio.h>

// Converts a number to a u16_string.
// Global variables: (none - uses passed parameters only)
void num_to_u16_string(char* buffer, uint8_t buffer_len, uint8_t value) {
    if(value<10) {
        snprintf((char*) buffer, buffer_len, "%d", value);
        buffer[1] = 0;
        buffer[2] = 0;
        buffer[3] = 0;
    } else if(value>99) {
        snprintf((char*) buffer, buffer_len, "%d %d %d", value/100, (value/10)%10, value%10);
        buffer[1] = 0;
        buffer[3] = 0;
        buffer[5] = 0;
        buffer[6] = 0;
        buffer[7] = 0;
    } else {
        snprintf((char*) buffer, buffer_len, "%d %d", value/10, value%10);
        buffer[1] = 0;
        buffer[3] = 0;
        buffer[4] = 0;
        buffer[5] = 0;
    }
}

// Converts a number to a hex formated u16_string.
// Global variables: (none - uses passed parameters only)
void hex_to_u16_string(char* buffer, uint8_t buffer_len, uint8_t value) {
    if(value<16) {
        snprintf((char*) buffer, buffer_len, "%X", value);
        buffer[1] = 0;
        buffer[2] = 0;
        buffer[3] = 0;
    } else {
        snprintf((char*) buffer, buffer_len, "%X %X", value/16, value%16);
        buffer[1] = 0;
        buffer[3] = 0;
        buffer[4] = 0;
        buffer[5] = 0;
    }
}

//this should go away with some font refactoring!
extern const GFXfont* ALL_FONTS [];
#define ALL_FONT_SIZE_OLED_INT 43

// Renders status screen with layer, lock states, RGB settings, display brightness, WPM, and language on OLED.
void oled_update_buffer(void) {
    uint16_t buffer[32];

    kdisp_set_buffer(0);

    const poly_layer_t* global_layer = get_global_layer();
    const GFXfont* displayFont[] = { &NotoSans_Regular11pt7b };
    const GFXfont* smallFont[] = { &NotoSans_Medium8pt7b };
    kdisp_write_gfx_text(ALL_FONTS, ALL_FONT_SIZE_OLED_INT, 0, 14, ICON_LAYER);
    hex_to_u16_string((char*) buffer, sizeof(buffer), get_highest_layer(global_layer->layer));
    kdisp_write_gfx_text(displayFont, 1, 20, 14, buffer);
    if(side_is_undecided()) {
        kdisp_write_gfx_text(displayFont, 1, 50, 14, u"Uknw");
    } else {
        kdisp_write_gfx_text(displayFont, 1, 38, 14, is_left_side() ? u"LEFT" : u"RIGHT");
    }

    kdisp_write_gfx_text(ALL_FONTS, ALL_FONT_SIZE_OLED_INT, 108, 16, global_layer->led_state.num_lock ? ICON_NUMLOCK_ON : ICON_NUMLOCK_OFF);
    kdisp_write_gfx_text(ALL_FONTS, ALL_FONT_SIZE_OLED_INT, 108, 38, global_layer->led_state.caps_lock ? ICON_CAPSLOCK_ON : ICON_CAPSLOCK_OFF);
    if(global_layer->led_state.scroll_lock) {
        kdisp_write_gfx_text(ALL_FONTS, ALL_FONT_SIZE_OLED_INT, 112, 54, ARROWS_DOWNSTOP);
    } else {
        kdisp_write_gfx_text(smallFont, 1, 112, 56, is_usb_host_side() ? u"H" : u"B");
    }

    if(is_right_side()) {
        kdisp_write_gfx_text(smallFont, 1, 0, 30, u"RGB");

        if(!rgb_matrix_is_enabled()) {
            kdisp_write_gfx_text(smallFont, 1, 34, 30, u"Off");
        } else {
            num_to_u16_string((char*) buffer, sizeof(buffer), rgb_matrix_get_mode());
            kdisp_write_gfx_text(smallFont, 1, 34, 30, buffer);
            kdisp_write_gfx_text(smallFont, 1, 58, 30, get_led_matrix_text(rgb_matrix_get_mode()));
            kdisp_write_gfx_text(smallFont, 1, 0, 44, u"HSV");
            hex_to_u16_string((char*) buffer, sizeof(buffer), rgb_matrix_get_hue());
            kdisp_write_gfx_text(smallFont, 1, 38, 44, buffer);
            hex_to_u16_string((char*) buffer, sizeof(buffer), rgb_matrix_get_sat());
            kdisp_write_gfx_text(smallFont, 1, 60, 44, buffer);
            hex_to_u16_string((char*) buffer, sizeof(buffer), rgb_matrix_get_val());
            kdisp_write_gfx_text(smallFont, 1, 82, 44, buffer);
            kdisp_write_gfx_text(smallFont, 1, 0, 58, u"Speed");
            num_to_u16_string((char*) buffer, sizeof(buffer), rgb_matrix_get_speed());
            kdisp_write_gfx_text(smallFont, 1, 58, 58, buffer);
        }
    } else {
        switch(get_local_layer()->def_layer) {
            case 0: kdisp_write_gfx_text(smallFont, 1, 0, 30, u"Qwerty"); break;
            case 1: kdisp_write_gfx_text(smallFont, 1, 0, 30, u"Qwerty Stag!"); break;
            case 2: kdisp_write_gfx_text(smallFont, 1, 0, 30, u"Colemak DH"); break;
            case 3: kdisp_write_gfx_text(smallFont, 1, 0, 30, u"Neo"); break;
            case 4: kdisp_write_gfx_text(smallFont, 1, 0, 30, u"Workman"); break;
            default: kdisp_write_gfx_text(smallFont, 1, 0, 30, u"Unknown"); break;
        }
        const poly_sync_t* local_state = get_local_state();
        kdisp_write_gfx_text(smallFont, 1, 0, 44, u"Dsp*");
        num_to_u16_string((char*) buffer, sizeof(buffer), local_state->contrast);
        kdisp_write_gfx_text(smallFont, 1, 42, 44, buffer);
        uint8_t i=0;
        for(;i<(local_state->contrast/5);++i) {
            buffer[i] = 'l';
        }
        buffer[i] = 0;
        buffer[i+1] = 0;
        kdisp_write_gfx_text(smallFont, 1, 64, 44, buffer);

        kdisp_write_gfx_text(smallFont, 1, 0, 58, u"WPM");
        num_to_u16_string((char*) buffer, sizeof(buffer), get_current_wpm());
        kdisp_write_gfx_text(smallFont, 1, 44, 58, buffer);

        kdisp_write_gfx_text(smallFont, 1, 68, 58, u"L");
        num_to_u16_string((char*) buffer, sizeof(buffer), local_state->lang);
        kdisp_write_gfx_text(smallFont, 1, 84, 58, buffer);
    }
}

// Updates and displays status screen on OLED, or turns off display if status disabled.

void oled_status_screen(void) {
    const poly_sync_t* local_state = get_local_state();
    if( (local_state->flags&STATUS_DISP_ON) == 0) {
        oled_off();
        return;
    } else if( (local_state->flags&STATUS_DISP_ON) != 0) {
        oled_on();
    }

    oled_update_buffer();
    oled_clear();
    oled_write_raw((char *)get_scratch_buffer(), get_scratch_buffer_size());
}

// Displays scrolling Polykybd logo on OLED with direction based on side (left/right).
void oled_render_logos(void) {
    if(is_left_side()) {
        oled_draw_poly();
        oled_scroll_right();
    } else {
        oled_draw_kybd();
        oled_scroll_left();
    }
}

// Main OLED task: displays logos during idle or status screen during normal operation.
// Global variables: l_state
bool oled_task_user(void) {
    if((get_local_state()->flags&DISP_IDLE) != 0) {
        oled_render_logos();
    } else {
        oled_scroll_off();
        oled_status_screen();
    }
    return false;
}
