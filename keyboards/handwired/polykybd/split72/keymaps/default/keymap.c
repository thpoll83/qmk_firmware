// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#include QMK_KEYBOARD_H

#include "quantum/quantum_keycodes.h"
#include "quantum/keymap_extras/keymap_german.h"
#include "quantum/keymap_introspection.h"
#include "quantum/via.h"

#include "raw_hid.h"
#include "oled_driver.h"
#include "version.h"
#include "print.h"
#include "debug.h"

#include <transactions.h>
#include <hardware/flash.h>

#include "polykybd.h"
#include "split72/split72.h"
#include "split72/status_oled.h"
#include "bridge_helper.h"
#include "uni.h"
#include "side.h"
#include "fill_overlay.h"
#include "poly_util.h"

#include "base/com.h"
#include "base/rle.h"
#include "base/e2prom.h"
#include "base/overlay.h"
#include "base/disp_array.h"
#include "base/helpers.h"
#include "base/update.h"
#include "base/spi_helper.h"
#include "base/shift_reg.h"
#include "base/text_helper.h"
#include "base/fonts/gfx_used_fonts.h"
#include "base/multicore/core1.h"
#include "base/crc32.h"

#include "state.h"
#include "multicore_exec.h"
#include "split_sync.h"

#include "lang/lang_lut.h"
#include "lang/lang_lut_ext.h"

#include "layers.h"
#include "keycode_helper.h"
#include "uni.h"

#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#ifdef RGB_MATRIX_ENABLE
// Forward-declare this helper function
void rgb_matrix_update_pwm_buffers(void);
#endif

/*[[[cog
import cog
import os
from textwrap import wrap
from openpyxl import load_workbook
wb = load_workbook(filename = os.path.join(os.path.abspath(os.path.dirname(cog.inFile)), "..", "..", "..", "lang", "lang_lut.xlsx"))
sheet = wb['key_lut']

languages = []
lang_index = 0
lang_key = sheet["B1"].value
while lang_key:
    lang_key = lang_key.replace("-", "")
    languages.append(lang_key)
    lang_index = lang_index + 1
    lang_key = sheet.cell(row = 1, column = 2 + lang_index*4).value
]]]*/
//[[[end]]]


//not used at the moment
#define FLASH_TARGET_OFFSET (4 * 1024 * 1024) //we start at 4MB and use the remaining 4MB for resource data
const uint8_t *flash_target_contents = (const uint8_t *) (XIP_BASE + FLASH_TARGET_OFFSET);
static_assert(FLASH_PAGE_SIZE==256, "Flash page size changed");

static enum lang_layer g_lang_init = INIT_LANG;

const struct display_info disp_row_0 = { BITMASK1(0) };
const struct display_info disp_row_3 = { BITMASK4(0) };


bool display_wakeup(keyrecord_t* record);
void update_displays(enum refresh_mode mode);
void set_displays(uint8_t contrast, bool idle);
void set_selected_displays(int8_t old_value, int8_t new_value);
void toggle_stagger(bool new_state);
void oled_update_buffer(void);
void poly_suspend(void);


// Selects all shift registers to communicate with all displays.
// Global variables: (none - uses SPI functions only)
void select_all_displays(void) {
    // make sure we are talking to all shift registers
    sr_shift_out_0_latch(NUM_SHIFT_REGISTERS);
}

// Clears all displays by setting buffer to zero and sending to all shift registers.
// Global variables: (none - delegates to display functions)
void clear_all_displays(void) {
    select_all_displays();

    kdisp_set_buffer(0x00);
    kdisp_send_buffer();
}

void display_bootloader_message(void) {
    clear_all_displays();
    // Drive the OLEDs at minimum contrast so the bootloader screen is as dim
    // as possible — once the master is in ROM bootloader nothing else will
    // restore the brightness, so we want to consume as little current as
    // possible while keeping the message readable.
    set_displays(MIN_BRIGHT, false);
    display_message(1, 1, u"BOOT-",   &FreeSansBold24pt7b);
    display_message(3, 0, u"LOADER!", &FreeSansBold24pt7b);
}

// Initializes SPI hardware for display communication after hardware reset.
// Global variables: (none - initializes hardware only)
void early_hardware_init_post(void) {
    spi_hw_setup();
}

//void oled_on_off(bool on);

#define BYTE_TO_BINARY_PATTERN "|%s%s%s%s%s%s%s%s"
#define BYTE_TO_FLAGS(byte)  \
  ((byte) & 0x80 ? " RGB |" : " --- |"), \
  ((byte) & 0x40 ? "Txt2 |" : " --- |"), \
  ((byte) & 0x20 ? "Txt1 |" : " --- |"), \
  ((byte) & 0x10 ? " Dbg |" : " --- |"), \
  ((byte) & 0x08 ? "DeadK|" : " --- |"), \
  ((byte) & 0x04 ? "Idle |" : " --- |"), \
  ((byte) & 0x02 ? "Trans|" : " --- |"), \
  ((byte) & 0x01 ? "StatD|" : " --- |")

  #define BYTE_TO_OVERLAY_FLAGS(byte)  \
  ((byte) & 0x80 ? "MpRst|" : " --- |"), \
  ((byte) & 0x40 ? "UsRst|" : " --- |"), \
  ((byte) & 0x20 ? "Reset|" : " --- |"), \
  ((byte) & 0x10 ? "ClrRB|" : " --- |"), \
  ((byte) & 0x08 ? "ClrRT|" : " --- |"), \
  ((byte) & 0x04 ? "ClrLB|" : " --- |"), \
  ((byte) & 0x02 ? "ClrLT|" : " --- |"), \
  ((byte) & 0x01 ? "Disp |" : " --- |")

//helpers
static uint8_t flags = 0;
static uint8_t overlay_flags = 0;

// Continuously suppress RGB on the bridge when display is off.
// The split transport may re-enable RGB by copying master's rgb_matrix_config; this
// indicator callback runs every render cycle (before flush) and zeros the LED buffer,
// ensuring LEDs stay dark regardless of what the transport wrote to enable.
#ifdef RGB_MATRIX_ENABLE
bool rgb_matrix_indicators_kb(void) {
    if (!is_keyboard_master()) {
        if (get_local_state()->overlay_flags & BOOTLOADER_DISPLAY) {
            // Backstop in case the SOLID_COLOR mode change in the sync
            // handler didn't take effect — value matched to the sethsv val.
            rgb_matrix_set_color_all(8, 0, 0);
            return false;
        }
        if ((get_local_state()->flags & STATUS_DISP_ON) == 0) {
            rgb_matrix_set_color_all(0, 0, 0);
            return false;
        }
    }
    return rgb_matrix_indicators_user();
}

#define RGB_REPEAT_INITIAL_DELAY_MS 400
#define RGB_REPEAT_RATE_MS          40

static uint16_t rgb_held_keycode        = KC_NO;
static deferred_token rgb_repeat_token  = INVALID_DEFERRED_TOKEN;

static void apply_rgb_adjust(uint16_t keycode) {
    switch (keycode) {
        case RM_VALU: rgb_matrix_increase_val_noeeprom();   break;
        case RM_VALD: rgb_matrix_decrease_val_noeeprom();   break;
        case RM_HUEU: rgb_matrix_increase_hue_noeeprom();   break;
        case RM_HUED: rgb_matrix_decrease_hue_noeeprom();   break;
        case RM_SATU: rgb_matrix_increase_sat_noeeprom();   break;
        case RM_SATD: rgb_matrix_decrease_sat_noeeprom();   break;
        case RM_SPDU: rgb_matrix_increase_speed_noeeprom(); break;
        case RM_SPDD: rgb_matrix_decrease_speed_noeeprom(); break;
        default: break;
    }
}

static uint32_t rgb_repeat_callback(uint32_t trigger_time, void* cb_arg) {
    if (rgb_held_keycode == KC_NO) return 0;
    apply_rgb_adjust(rgb_held_keycode);
    return RGB_REPEAT_RATE_MS;
}
#endif

// Synchronizes local and global display state, handling idle transitions, contrast changes, and display updates.
// Global variables: flags, overlay_flags
void sync_and_refresh_displays(void) {
    // On the slave, once the bootloader screen is up we want nothing else to
    // touch the keycap OLEDs — otherwise the next state_diff branch redraws
    // every keycap from the overlay buffers and wipes "BOOT-LOADER!".
    // The master will never recover from this (it's in the ROM bootloader),
    // so freezing the slave display state until power-cycle is fine.
    if (!is_usb_host_side() && (get_local_state()->overlay_flags & BOOTLOADER_DISPLAY)) {
        return;
    }

    bool layer_diff = false;
    bool state_diff = false;

    uint8_t local_flags;
    uint8_t local_overlay_flags = get_local_state()->overlay_flags;
    uint8_t global_flags = get_global_state()->flags;

    if (is_usb_host_side()) {

        local_flags = set_flag(get_local_state()->flags, DBG_ON, debug_enable);
        access_local_state()->flags = local_flags;

        const bool back_from_idle_transition = flag_turned_on(local_flags, global_flags, IDLE_TRANSITION);
        if (back_from_idle_transition) {
            poly_eeconf_t ee   = load_user_eeconf();
            access_local_state()->contrast = ee.brightness;
        }

        if(flags!=local_flags) {
            //uprintf("Poly State Flags: 0x%02x " BYTE_TO_BINARY_PATTERN "\n", local_flags, BYTE_TO_FLAGS(local_flags));
            flags=local_flags;
        }
        if(overlay_flags!=local_overlay_flags) {
            //uprintf("Poly Ovrly Flags: 0x%02x " BYTE_TO_BINARY_PATTERN "\n", local_overlay_flags, BYTE_TO_OVERLAY_FLAGS(local_overlay_flags));
            overlay_flags=local_overlay_flags;
        }

        state_diff = differ(get_local_state(), get_global_state(), sizeof(poly_sync_t));
        if ( state_diff ) {
            if(!send_to_bridge(USER_SYNC_POLY_DATA, (void *)access_local_state(), sizeof(poly_sync_t), 10)) {
                state_diff = false; // if failed to sync, do not consider it a diff and try again later
                uprint("USER_SYNC_POLY_DATA failed to send\n");
            }
        }

        access_local_layer()->led_state = host_keyboard_led_state();
        access_local_layer()->mods = get_mods();
        layer_diff = differ(get_local_layer(), get_global_layer(), sizeof(poly_layer_t));
        if ( layer_diff ) {
            if(!send_to_bridge(USER_SYNC_LAYER_DATA, (void *)access_local_layer(), sizeof(poly_layer_t), 10)) {
                layer_diff = false; // if failed to sync, do not consider it a diff and try again later
                uprint("USER_SYNC_LAYER_DATA failed to send\n");
            }
        }
        if ( differ(get_local_last_latin(), get_global_last_latin(), sizeof(poly_last_t)) ) {
            if(!send_to_bridge(USER_SYNC_LASTKEY_DATA, access_local_last_latin(), sizeof(poly_last_t), 5)) {
                // if failed to sync, do not consider it a diff and try again later
                uprint("USER_SYNC_LASTKEY_DATA failed to send\n");
            } else {
                copy_global_last_latin(get_local_last_latin());
            }
        }
    } else {
        layer_diff = differ(get_local_layer(), get_global_layer(), sizeof(poly_layer_t));
        state_diff = differ(get_local_state(), get_global_state(), sizeof(poly_sync_t));

        local_flags = get_local_state()->flags;
    }

    const bool in_idle_mode = (local_flags & DISP_IDLE) != 0;

    if(state_diff) {
        const bool idle_changed         = has_flag_changed(local_flags, global_flags, DISP_IDLE);
        const bool contrast_changed     = get_local_state()->contrast != get_global_state()->contrast;
        const bool status_disp_changed  = has_flag_changed(local_flags, global_flags, STATUS_DISP_ON);
        const bool status_disp_on       = test_flag(local_flags, STATUS_DISP_ON);

        if(idle_changed) {
            if(in_idle_mode) {
                oled_set_brightness(0);
            }
        }

        if(status_disp_changed) {
            if(status_disp_on) {
                oled_set_brightness(OLED_BRIGHTNESS);
                if(test_flag(local_flags, RGB_ON)) {
                    rgb_matrix_enable_noeeprom();
                }
            } else {
                oled_off();
                rgb_matrix_set_color_all(0, 0, 0);
                rgb_matrix_update_pwm_buffers();
                rgb_matrix_disable_noeeprom();
            }
        }

        if(has_flag_changed(local_flags, global_flags, RGB_ON)) {
            if (test_flag(local_flags, RGB_ON)) {
                rgb_matrix_enable();
            } else {
                rgb_matrix_disable();
            }
        }

        // Overlay action flags (RESET_BUFFERS / USAGE_RESET / MAPPING_RESET /
        // MAPPING_ALLSET) are dispatched and cleared at their entry points —
        // hid_com.c case 11 on the master, user_sync_poly_data_handler on the
        // slave. Nothing for us to do here.

        if (contrast_changed || idle_changed) {
            set_displays(get_local_state()->contrast, in_idle_mode);
        }
        copy_global_state(get_local_state());
        request_disp_refresh();
    }

    if(layer_diff) {
        copy_global_layer(get_local_layer());
        request_disp_refresh();
    }

    enum refresh_mode refresh = get_refresh_mode();
    if (refresh == START_FIRST_HALF) {
        update_displays(START_FIRST_HALF);
        set_disp_refresh(START_SECOND_HALF);
    }
    else if (refresh == START_SECOND_HALF || refresh == ALL_AT_ONCE) {
        update_displays(refresh);
        set_disp_refresh(DONE_ALL);
    }
}

// Sets layer state variable tracking the active keyboard layer.
layer_state_t layer_state_set_user(layer_state_t state) {
    access_local_layer()->layer = state;
    return state;
}

// Continuously monitors for idle timeout and dims/pulsates display accordingly.
void housekeeping_task_user(void) {
    brightness_save_if_pending();
    default_layer_save_if_pending();
    sync_and_refresh_displays();
    int32_t update = get_last_update();
    if(update>=0) {
        //turn off displays
        uint32_t elapsed_time_since_update = timer_elapsed32(update);
        if (is_usb_host_side()) {
            poly_sync_t* local_state = access_local_state();
            uint8_t  contrast = local_state->contrast;
            uint8_t  flags = local_state->flags;

            flags |= STATUS_DISP_ON;
            flags &= ~((uint8_t)IDLE_TRANSITION);

            if(elapsed_time_since_update > FADE_OUT_TIME && contrast >= MIN_BRIGHT && (flags & DISP_IDLE)==0) {
                poly_eeconf_t ee = load_user_eeconf();
                int32_t time_after = elapsed_time_since_update - FADE_OUT_TIME;
                int16_t brightness = ((FADE_TRANSITION_TIME - time_after) * ee.brightness) / FADE_TRANSITION_TIME;

                //transition to pulsing mode
                if(brightness<=MIN_BRIGHT) {
                    contrast = DISP_OFF;
                    flags |= DISP_IDLE;
                    uprint("Transition to pulsing\n");
                } else if(brightness>FULL_BRIGHT) {
                    contrast = FULL_BRIGHT;
                    uprint("Limiting brightness\n");
                } else{
                    contrast = brightness;
                }
                flags |= IDLE_TRANSITION;
            } else if(elapsed_time_since_update > TURN_OFF_TIME) {
                uprint("Turning off\n");
                poly_suspend();
                set_last_update(-1);
                contrast = local_state->contrast;
                flags = local_state->flags;
            } else if((flags & DISP_IDLE)!=0) {
                int32_t time_after = PK_MAX(elapsed_time_since_update - FADE_OUT_TIME - FADE_TRANSITION_TIME, 0)/300;
                contrast = time_after%50;
            } else {
                flags &= ~((uint8_t)DISP_IDLE);
            }

            local_state->contrast = contrast;
            local_state->flags = flags;
        }
    }
}


const uint16_t keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    //Base Layers
/*
                                                              ┌────────────────┐
                                                              │     QWERTY     │
                                                              └────────────────┘
   ┌────────┬───────┬───────┬───────┬───────┬───────┬───────┐                    ┌───────┬───────┬───────┬───────┬───────┬───────┬────────┐
   │  Esc   │   1   │   2   │   3   │   4   │   5   │  Nubs │ ╭╮╭╮╭╮╭╮╭╮╭╮╭╮╭╮╭╮ │   6   │   7   │   8   │   9   │   0   │   -   │ BckSp  │
   ├────────┼───────┼───────┼───────┼───────┼───────┼───────┤ │╰╯╰╯╰╯╰╯╰╯╰╯╰╯╰╯│ ├───────┼───────┼───────┼───────┼───────┼───────┼────────┤
   │  TAB   │   q   │   w   │   e   │   r   │   t   │   `   ├─╯                ╰─┤  Hypr │   y   │   u   │   i   │   o   │   p   │   \    │
   ├────────┼───────┼───────┼───────┼───────┼───────┼───────┤                    ├───────┼───────┼───────┼───────┼───────┼───────┼────────┤
   │  FN    │   a   │   s   │   d   │   f   │   g   │   '   │  (MB1)             │  Intl │   h   │   j   │   k   │   l   │   =   │  Ret   │
   ├────────┼───────┼───────┼───────┼───────┼───────┼───────┼────────╮  ╭────────┼───────┼───────┼───────┼───────┼───────┼───────┼────────┤
   │ Shift  │   z   │   x   │   c   │   v   │   b   │  Nuhs │  Num!  │  │   [    │   ]   │   n   │   m   │   ,   │   ;   │  Up   │ Shift  │
   └┬───────┼───────┼───────┼───────┼──────┬┴───────┼───────┼────────┤  ├────────┼───────┼───────┴┬──────┼───────┼───────┼───────┼───────┬┘
    │ Ctrl  │  Os   │  Alt  │  Ctx  │      │  Space │  Del  │   Ret  │  │  Lang  │   /   │ Space  │      │   .   │  Left │  Down │ Right │
    └───────┴───────┴───────┴───────┘      └────────┴───────┴────────╯  └────────┴───────┴────────┘      └───────┴───────┴───────┴───────┘
*/
    [_L0] = LAYOUT_left_right_stacked(
        KC_ESC,     KC_1,       KC_2,       KC_3,       KC_4,       KC_5,       KC_NUBS,
        KC_TAB,     KC_Q,       KC_W,       KC_E,       KC_R,       KC_T,       KC_GRAVE,
        MO(_FL0),   KC_A,       KC_S,       KC_D,       KC_F,       KC_G,       KC_QUOTE,   MS_BTN1,
        KC_LSFT,    KC_Z,       KC_X,       KC_C,       KC_V,       KC_B,       TO(_EMJ0),   MO(_NL),
        KC_LCTL,    KC_LWIN,    KC_LALT,    KC_APP,                 KC_SPACE,   KC_DEL,     KC_ENTER,

                    KC_6,       KC_7,       KC_8,       KC_9,       KC_0,       KC_MINUS,   KC_BSPC,
                    KC_HYPR,    KC_Y,       KC_U,       KC_I,       KC_O,       KC_P,       KC_BSLS,
        KC_NO,      MO(_ADDLANG1),KC_H,     KC_J,       KC_K,       KC_L,       KC_EQUAL,   KC_ENTER,
        KC_LBRC,    KC_RBRC,    KC_N,       KC_M,       KC_COMMA,   KC_SCLN,    KC_UP,      KC_RSFT,
        KC_LANG,    KC_SLASH,    KC_SPC,                KC_DOT,     KC_LEFT,    KC_DOWN,    KC_RIGHT
        ),

/*
                                                              ┌────────────────┐
                                                              │     QWERTY!    │
                                                              └────────────────┘
   ┌────────┬───────┬───────┬───────┬───────┬───────┬───────┐                    ┌───────┬───────┬───────┬───────┬───────┬───────┬────────┐
   │  Esc   │   1   │   2   │   3   │   4   │   5   │   6   │ ╭╮╭╮╭╮╭╮╭╮╭╮╭╮╭╮╭╮ │   7   │   8   │   9   │   0   │   -   │   =   │  Hypr  │
   ├────────┼───────┼───────┼───────┼───────┼───────┼───────┤ │╰╯╰╯╰╯╰╯╰╯╰╯╰╯╰╯│ ├───────┼───────┼───────┼───────┼───────┼───────┼────────┤
   │  TAB   │   q   │   w   │   e   │   r   │   t   │   `   ├─╯                ╰─┤   y   │   u   │   i   │   o   │   p   │   [   │  Nubs  │
   ├────────┼───────┼───────┼───────┼───────┼───────┼───────┤                    ├───────┼───────┼───────┼───────┼───────┼───────┼────────┤
   │  FN    │   a   │   s   │   d   │   f   │   g   │   '   │  (MB1)             │   h   │   j   │   k   │   l   │   ;   │   ]   │    \   │
   ├────────┼───────┼───────┼───────┼───────┼───────┼───────┼────────╮  ╭────────┼───────┼───────┼───────┼───────┼───────┼───────┼────────┤
   │ Shift  │ Nuhs  │   z   │   x   │   c   │   v   │   b   │  Num!  │  │  Lang  │  Ctx  │   n   │   m   │   ,   │   .   │   /   │ Shift  │
   └┬───────┼───────┼───────┼───────┼──────┬┴───────┼───────┼────────┤  ├────────┼───────┼───────┴┬──────┼───────┼───────┼───────┼───────┬┘
    │ Ctrl  │  Os   │  Alt  │  Intl │      │  Space │  Del  │   Ins  │  │  Ret   │ BckSp │ Space  │      │ Left  │   Up  │  Down │ Right │
    └───────┴───────┴───────┴───────┘      └────────┴───────┴────────╯  └────────┴───────┴────────┘      └───────┴───────┴───────┴───────┘
*/

    [_L1] = LAYOUT_left_right_stacked(
        KC_ESC,     KC_1,       KC_2,       KC_3,       KC_4,       KC_5,       KC_6,
        KC_TAB,     KC_Q,       KC_W,       KC_E,       KC_R,       KC_T,       KC_GRAVE,
        MO(_FL1),   KC_A,       KC_S,       KC_D,       KC_F,       KC_G,       KC_QUOTE,   MS_BTN1,
        KC_LSFT,    TO(_EMJ0),   KC_Z,       KC_X,       KC_C,       KC_V,       KC_B,       MO(_NL),
        KC_LCTL,    KC_LWIN,    KC_LALT,    MO(_ADDLANG1),          KC_SPACE,   KC_DEL,     KC_ENTER,

                    KC_7,       KC_8,       KC_9,       KC_0,       KC_MINUS,   KC_EQUAL,   KC_HYPR,
                    KC_Y,       KC_U,       KC_I,       KC_O,       KC_P,       KC_LBRC,    KC_NUBS,
        KC_NO,      KC_H,       KC_J,       KC_K,       KC_L,       KC_SCLN,    KC_RBRC,    KC_BSLS,
        KC_LANG,    KC_APP,     KC_N,       KC_M,       KC_COMMA,   KC_DOT,     KC_SLASH,   KC_RSFT,
        KC_ENTER,   KC_BSPC,    KC_SPC,                 KC_LEFT,    KC_UP,      KC_DOWN,    KC_RIGHT
        ),
/*
                                                              ┌────────────────┐
                                                              │   Colemak DH   │
                                                              └────────────────┘
   ┌────────┬───────┬───────┬───────┬───────┬───────┬───────┐                    ┌───────┬───────┬───────┬───────┬───────┬───────┬────────┐
   │  Esc   │   1   │   2   │   3   │   4   │   5   │  Nub  │ ╭╮╭╮╭╮╭╮╭╮╭╮╭╮╭╮╭╮ │   6   │   7   │   8   │   9   │   0   │   -   │   =    │
   ├────────┼───────┼───────┼───────┼───────┼───────┼───────┤ │╰╯╰╯╰╯╰╯╰╯╰╯╰╯╰╯│ ├───────┼───────┼───────┼───────┼───────┼───────┼────────┤
   │  TAB   │   q   │   w   │   f   │   p   │   b   │   `   ├─╯                ╰─┤   j   │   l   │   u   │   y   │   ;   │   [   │  Intl  │
   ├────────┼───────┼───────┼───────┼───────┼───────┼───────┤                    ├───────┼───────┼───────┼───────┼───────┼───────┼────────┤
   │  FN    │   a   │   r   │   s   │   t   │   g   │   '   │  (MB1)             │   m   │   n   │   e   │   i   │   o   │   ]   │    \   │
   ├────────┼───────┼───────┼───────┼───────┼───────┼───────┼────────╮  ╭────────┼───────┼───────┼───────┼───────┼───────┼───────┼────────┤
   │ Shift  │   z   │   x   │   c   │   d   │   v   │  Nuhs |  Num!  │  │  Lang  │  Hypr │   k   │   h   │   ,   │   .   │   /   │ Shift  │
   └┬───────┼───────┼───────┼───────┼──────┬┴───────┼───────┼────────┤  ├────────┼───────┼───────┴┬──────┼───────┼───────┼───────┼───────┬┘
    │ Ctrl  │  Os   │  Alt  │  Ctx  │      │  Space │  Del  │   Ret  │  │  Ret   │ BckSp │ Space  │      │ Left  │   Up  │  Down │ Right │
    └───────┴───────┴───────┴───────┘      └────────┴───────┴────────╯  └────────┴───────┴────────┘      └───────┴───────┴───────┴───────┘
*/
    [_L2] = LAYOUT_left_right_stacked(
        KC_ESC,     KC_1,       KC_2,       KC_3,       KC_4,       KC_5,       KC_NUBS,
        KC_TAB,     KC_Q,       KC_W,       KC_F,       KC_P,       KC_B,       KC_GRAVE,
        MO(_FL1),   KC_A,       KC_R,       KC_S,       KC_T,       KC_G,       KC_QUOTE,   MS_BTN1,
        KC_LSFT,    KC_Z,       KC_X,       KC_C,       KC_D,       KC_V,       TO(_EMJ0),    MO(_NL),
        KC_LCTL,    KC_LWIN,    KC_LALT,    KC_APP,                 KC_SPACE,   KC_DEL,     KC_ENTER,

                    KC_6,       KC_7,       KC_8,       KC_9,       KC_0,       KC_MINUS,   KC_EQUAL,
                    KC_J,       KC_L,       KC_U,       KC_Y,       KC_SCLN,    KC_LBRC,    MO(_ADDLANG1),
        KC_NO,      KC_M,       KC_N,       KC_E,       KC_I,       KC_O,       KC_RBRC,    KC_BSLS,
        KC_LANG,    KC_HYPR,    KC_K,       KC_H,       KC_COMMA,   KC_DOT,     KC_SLASH,   KC_RSFT,
        KC_ENTER,   KC_BSPC,    KC_SPC,                 KC_LEFT,    KC_UP,      KC_DOWN,    KC_RIGHT
        ),
        /*
                                                              ┌────────────────┐
                                                              │       Neo      │
                                                              └────────────────┘
   ┌────────┬───────┬───────┬───────┬───────┬───────┬───────┐                    ┌───────┬───────┬───────┬───────┬───────┬───────┬────────┐
   │  Esc   │   1   │   2   │   3   │   4   │   5   │   <   │ ╭╮╭╮╭╮╭╮╭╮╭╮╭╮╭╮╭╮ │   6   │   7   │   8   │   9   │   0   │   -   │   `    │
   ├────────┼───────┼───────┼───────┼───────┼───────┼───────┤ │╰╯╰╯╰╯╰╯╰╯╰╯╰╯╰╯│ ├───────┼───────┼───────┼───────┼───────┼───────┼────────┤
   │  TAB   │   x   │   v   │   l   │   c   │   w   │   ^   ├─╯                ╰─┤   k   │   h   │   g   │   f   │   q   │   ß   │   ´    │
   ├────────┼───────┼───────┼───────┼───────┼───────┼───────┤                    ├───────┼───────┼───────┼───────┼───────┼───────┼────────┤
   │  FN    │   u   │   i   │   a   │   e   │   o   │   '   │  (MB1)             │   s   │   n   │   r   │   t   │   d   │   y   │   \    │
   ├────────┼───────┼───────┼───────┼───────┼───────┼───────┼────────╮  ╭────────┼───────┼───────┼───────┼───────┼───────┼───────┼────────┤
   │ Shift  │   #   │   ü   │   ö   │   ä   │   p   │   z   │  Num!  │  │  Lang  │   +   │   b   │   m   │   ,   │   .   │   j   │ Shift  │
   └┬───────┼───────┼───────┼───────┼──────┬┴───────┼───────┼────────┤  ├────────┼───────┼───────┴┬──────┼───────┼───────┼───────┼───────┬┘
    │ Ctrl  │  Os   │  Alt  │  Ctx  │      │  Space │  Del  │   Ret  │  │  Ret   │ BckSp │ Space  │      │ Left  │   Up  │  Down │ Right │
    └───────┴───────┴───────┴───────┘      └────────┴───────┴────────╯  └────────┴───────┴────────┘      └───────┴───────┴───────┴───────┘
*/
    [_L3] = LAYOUT_left_right_stacked(
        KC_ESC,     KC_1,       KC_2,       KC_3,       KC_4,       KC_5,       DE_LABK,
        KC_TAB,     KC_X,       KC_V,       KC_L,       KC_C,       KC_W,       DE_CIRC,
        MO(_FL0),   KC_U,       KC_I,       KC_A,       KC_E,       KC_O,       KC_QUOTE,   MS_BTN1,
        KC_LSFT,    DE_HASH,    DE_UDIA,    DE_ODIA,    DE_ADIA,    KC_P,       DE_Z,       MO(_NL),
        KC_LCTL,    KC_LWIN,    KC_LALT,    KC_APP,                 KC_SPACE,   KC_DEL,     KC_ENTER,

                    KC_6,       KC_7,       KC_8,       KC_9,       KC_0,       DE_MINS,    DE_GRV,
                    KC_K,       KC_H,       KC_G,       KC_F,       KC_Q,       DE_SS,      DE_ACUT,
        KC_NO,      KC_S,       KC_N,       KC_R,       KC_T,       KC_D,       DE_Y,       KC_BSLS,
        KC_LANG,    DE_PLUS,    KC_B,       KC_M,       KC_COMMA,   KC_DOT,     KC_J,       KC_RSFT,
        KC_ENTER,   KC_BSPC,    KC_SPC,                 KC_LEFT,    KC_UP,      KC_DOWN,    KC_RIGHT
        ),
        /*
                                                              ┌────────────────┐
                                                              │    Workman     │
                                                              └────────────────┘
   ┌────────┬───────┬───────┬───────┬───────┬───────┬───────┐                    ┌───────┬───────┬───────┬───────┬───────┬───────┬────────┐
   │  Esc   │   1   │   2   │   3   │   4   │   5   │   `   │ ╭╮╭╮╭╮╭╮╭╮╭╮╭╮╭╮╭╮ │   6   │   7   │   8   │   9   │   0   │   -   │   =    │
   ├────────┼───────┼───────┼───────┼───────┼───────┼───────┤ │╰╯╰╯╰╯╰╯╰╯╰╯╰╯╰╯│ ├───────┼───────┼───────┼───────┼───────┼───────┼────────┤
   │  TAB   │   q   │   d   │   r   │   w   │   b   │  Hypr ├─╯                ╰─┤   j   │   f   │   u   │   p   │   ;   │   [   │   ]    │
   ├────────┼───────┼───────┼───────┼───────┼───────┼───────┤                    ├───────┼───────┼───────┼───────┼───────┼───────┼────────┤
   │  FN    │   a   │   s   │   h   │   t   │   g   │  Meh  │  (MB1)             │   y   │   n   │   e   │   o   │   i   │   '   │   \    │
   ├────────┼───────┼───────┼───────┼───────┼───────┼───────┼────────╮  ╭────────┼───────┼───────┼───────┼───────┼───────┼───────┼────────┤
   │ Shift  │   z   │   x   │   m   │   c   │   v   │  Intl │  Num!  │  │  Lang  │   k   │   b   │   l   │   ,   │   .   │   /   │ Shift  │
   └┬───────┼───────┼───────┼───────┼──────┬┴───────┼───────┼────────┤  ├────────┼───────┼───────┴┬──────┼───────┼───────┼───────┼───────┬┘
    │ Ctrl  │  Os   │  Alt  │  Ctx  │      │  Space │  Del  │   Ret  │  │  Ret   │ BckSp │ Space  │      │ Left  │   Up  │  Down │ Right │
    └───────┴───────┴───────┴───────┘      └────────┴───────┴────────╯  └────────┴───────┴────────┘      └───────┴───────┴───────┴───────┘
*/
    [_L4] = LAYOUT_left_right_stacked(
        KC_ESC,     KC_1,       KC_2,       KC_3,       KC_4,       KC_5,       KC_GRAVE,
        KC_TAB,     KC_Q,       KC_D,       KC_R,       KC_W,       KC_B,       KC_HYPR,
        MO(_FL1),   KC_A,       KC_S,       KC_H,       KC_T,       KC_G,       TO(_EMJ0),     MS_BTN1,
        KC_LSFT,    KC_Z,       KC_X,       KC_M,       KC_C,       KC_V,       MO(_ADDLANG1), MO(_NL),
        KC_LCTL,    KC_LWIN,    KC_LALT,    KC_APP,                 KC_SPACE,   KC_DEL,     KC_ENTER,

                    KC_6,       KC_7,       KC_8,       KC_9,       KC_0,       KC_MINUS,   KC_EQUAL,
                    KC_J,       KC_F,       KC_U,       KC_P,       KC_SCLN,    KC_LBRC,    KC_RBRC,
        KC_NO,      KC_Y,       KC_N,       KC_E,       KC_O,       KC_I,       KC_QUOTE,   KC_BSLS,
        KC_LANG,    KC_K,       KC_B,       KC_L,       KC_COMMA,   KC_DOT,     KC_SLASH,   KC_RSFT,
        KC_ENTER,   KC_BSPC,    KC_SPC,                 KC_LEFT,    KC_UP,      KC_DOWN,    KC_RIGHT
        ),
    //Function Layer (Fn)
    [_FL0] = LAYOUT_left_right_stacked(
        OSL(_UL),   KC_F1,      KC_F2,      KC_F3,      KC_F4,      KC_F5,     TO(_UL),
        _______,    _______,    _______,    _______,    _______,    _______,    _______,
        _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,
        KC_CAPS,    _______,    _______,    _______,    _______,    _______,    _______,    _______,
        _______,    _______,    _______,    _______,                _______,    _______,    _______,

                    KC_F6,      KC_F7,      KC_F8,      KC_F9,      KC_F10,      KC_F11,    KC_F12,
                    MS_BTN1,    MS_BTN2,    _______,    _______,    _______,    _______,    TO(_SL),
        _______,    MS_BTN3,    _______,    _______,    _______,   _______,    _______,    _______,
        TO(_NL),    _______,    _______,    _______,    _______,    _______,    _______,    KC_INS,
        KC_RALT,    KC_RWIN,    KC_RCTL,                KC_HOME,    KC_PGUP,    KC_PGDN,    KC_END
        ),
    [_FL1] = LAYOUT_left_right_stacked(
        OSL(_UL),   KC_F1,      KC_F2,      KC_F3,      KC_F4,      KC_F5,      KC_F6,
        _______,    _______,    _______,    _______,    _______,    _______,    _______,
        _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,
        _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,
        _______,    _______,    _______,    _______,                _______,    _______,    KC_INS,

                    KC_F7,      KC_F8,      KC_F9,      KC_F10,     KC_F11,     KC_F12,     TO(_UL),
                    MS_BTN1,    MS_BTN2,    _______,    _______,    _______,    _______,    TO(_SL),
        _______,    MS_BTN3,    _______,    _______,    _______,    _______,    _______,    KC_CAPS,
        TO(_NL),    _______,    _______,    _______,    _______,    _______,    _______,    _______,
        KC_RALT,    KC_RWIN,    KC_RCTL,                KC_HOME,    KC_PGUP,    KC_PGDN,    KC_END
        ),
     //Num Layer
    [_NL] = LAYOUT_left_right_stacked(
        KC_NO,      KC_NUM,     KC_PSLS,    KC_PAST,    KC_PMNS,    KC_NO,      KC_NO,
        MS_BTN1,    KC_KP_7,    KC_KP_8,    KC_KP_9,    KC_PPLS,    KC_INS,     KC_NO,
        KC_NO,      KC_KP_4,    KC_KP_5,    KC_KP_6,    KC_PPLS,    KC_DEL,     KC_NO,     _______,
        KC_NO,      KC_KP_1,    KC_KP_2,    KC_KP_3,    KC_PENT,    KC_NO,      KC_NO,     _______,
        KC_BASE,    KC_KP_0,    KC_PDOT,    KC_PENT,                MS_BTN2, KC_NO,     KC_NO,

                    KC_NO,      KC_NO,      KC_NUM,     KC_PSLS,    KC_PAST,    KC_PMNS,   KC_NO,
                    KC_NO,      KC_INS,     KC_KP_7,    KC_KP_8,    KC_KP_9,    KC_PPLS,   KC_NO,
        _______,    KC_NO,      KC_DEL,     KC_KP_4,    KC_KP_5,    KC_KP_6,    KC_PPLS,   KC_NO,
        _______,    _______,    KC_NO,      KC_KP_1,    KC_KP_2,    KC_KP_3,    KC_PENT,   KC_NO,
        _______,    KC_NO,      KC_NO,                  KC_KP_0,    KC_PDOT,    KC_PENT,   KC_BASE
        ),
    //Util Layer
    [_UL] = LAYOUT_left_right_stacked(
        KC_NO,      KC_F13,     KC_F14,     KC_F15,     KC_F16,     KC_F17,     KC_F18,
        KC_MYCM,    KC_CALC,    KC_PSCR,    KC_SCRL,    KC_BRK,     KC_NO,      KC_NO,
        KC_NO,      KC_NO,      KC_NO,      KC_NO,      KC_NO,      KC_NO,      KC_NO,      _______,
        KC_LSFT,    KC_NO,      KC_NO,      KC_NO,      KC_NO,      KC_NO,      KC_NO,      KC_NO,
        KC_BASE,    KC_NO,      KC_NO,      KC_NO,                  KC_NO,      KC_NO,      KC_NO,

                    KC_F19,     KC_F20,     KC_F21,     KC_F22,     KC_F23,     KC_F24,     KC_NO,
                    KC_NO,      KC_MPRV,    KC_MPLY,    KC_MSTP,    KC_MNXT,    KC_NO,      TO(_SL),
        _______,    KC_NO,      KC_NO,      KC_NO,      KC_NO,      KC_NO,      KC_NO,      KC_NO,
        KC_NO,      KC_NO,      KC_MUTE,    KC_VOLD,    KC_VOLU,    KC_NO,      KC_NO,      KC_RSFT,
        KC_NO,      KC_NO,      KC_NO,                  KC_NO,      KC_NO,      KC_NO,      KC_BASE
        ),
    //Settings Layer
    [_SL] = LAYOUT_left_right_stacked(
        KC_DDIM,    KC_DMIN,    KC_D1Q,     KC_DHLF,    KC_D3Q,     KC_DMAX,    KC_DBRI,
        KC_NO,      KC_NO,      KC_NO,      KC_NO,      KC_NO,      KC_NO,      KC_NO,
        KC_NO,      KC_L0,      KC_L1,      KC_L2,      KC_L3,      KC_L4,      KC_NO,      _______,
        KC_NO,      KC_NO,      KC_NO,      KC_NO,      KC_NO,      KC_NO,      KC_NO,      QK_RBT,
        KC_BASE,    LBL_TEXT,   KC_TOGMODS, KC_TOGTEXT,             KC_NO,      QK_MAKE,    QK_BOOT,


        //             RM_PREV,    RGB_M_SW,   RGB_M_R,    KC_RGB_TOG, RGB_M_P,    RGB_M_B,    RM_NEXT,
        //             KC_NO,      RM_SPDD,    RM_SPDU,    KC_NO,      RM_HUED,    RM_HUEU,    KC_NO,
        // _______,    KC_NO,      RM_VALD,    RM_VALU,    KC_NO,      RM_SATD,    RM_SATU,    KC_NO,
                    RM_PREV,   RGB_M_SW,   RGB_M_R,    KC_RGB_TOG, RGB_M_P,    RGB_M_B,    RM_NEXT,
                    KC_NO,      RM_SPDD,    RM_SPDU,    KC_NO,      RM_HUED,    RM_HUEU,    KC_NO,
        _______,    KC_NO,      RM_VALD,    RM_VALU,    KC_NO,      RM_SATD,    RM_SATU,    KC_NO,
        EE_CLR,     KC_NO,      KC_NO,      KC_NO,      KC_NO,      KC_NO,      KC_NO,      KC_NO,
        DB_TOGG,    KC_DEADKEY, KC_NO,                  KC_NO,      KC_NO,      KC_NO,      KC_BASE
        ),
    //Language Selection Layer
    [_LL] = LAYOUT_left_right_stacked(
        /*[[[cog
        slang = sorted(languages)
        lines = []
        for lidx in range(0, 8):
            line = ""
            for idx in range(0, 5):
                if (lidx*5+idx)>=len(slang):
                    line = f"{line}KC_NO,\t\t"
                else:
                    line = f'{line}KCL_{slang[(lidx*5+idx)].upper()},\t'
            lines.append(line)
        cog.outl(f"KC_NO,\t\t\t\t\t\t\tKC_NO,\t\t{lines[0]}");
        cog.outl(f"KC_NO,\t\t\t\t\t\t\tKC_NO,\t\t{lines[1]}");
        cog.outl(f"QK_UNICODE_MODE_WINCOMPOSE,\t\tKC_NO,\t\t{lines[2]}\tKC_MS_BTN1,");
        cog.outl(f"QK_UNICODE_MODE_EMACS,\t\t\tKC_NO,\t\t{lines[3]}\tKC_NO,");
        cog.outl("KC_BASE,\t\t\t\t\t\tKC_NO,\t\tKC_NO,\t\tKC_NO,\t\t\t\t\tKC_NO,\t\tKC_NO,\t\t\tKC_NO,");
        cog.outl("")
        cog.outl(f"\t\t\t\t\t{lines[4]}KC_NO,\t\tQK_UNICODE_MODE_MACOS,");
        cog.outl(f"\t\t\t\t\t{lines[5]}KC_NO,\t\tQK_UNICODE_MODE_LINUX,");
        cog.outl(f"_______,\t\t\t{lines[6]}KC_NO,\t\tQK_UNICODE_MODE_WINDOWS,");
        cog.outl(f"KC_NO,\t\t\t\t{lines[7]}KC_NO,\t\tQK_UNICODE_MODE_BSD,");
        cog.outl("KC_NO,\t\t\t\tKC_NO,\t\tKC_NO,\t\t\t\t\tKC_NO,\t\tKC_NO,\t\tKC_NO,\t\tKC_BASE");
        ]]]*/
        KC_NO,							KC_NO,		KCL_ARSA,	KCL_BEBY,	KCL_BGBG,	KCL_CSCZ,	KCL_DADK,
        KC_NO,							KC_NO,		KCL_DEDE,	KCL_ELGR,	KCL_ENUS,	KCL_ESES,	KCL_FIFI,
        QK_UNICODE_MODE_WINCOMPOSE,		KC_NO,		KCL_FRFR,	KCL_HEIL,	KCL_HUHU,	KCL_ITIT,	KCL_JAJP,		MS_BTN1,
        QK_UNICODE_MODE_EMACS,			KC_NO,		KCL_KKKZ,	KCL_KOKR,	KCL_NLNL,	KCL_NNNO,	KCL_PLPL,		KC_NO,
        KC_BASE,						KC_NO,		KC_NO,		KC_NO,					KC_NO,		KC_NO,			KC_NO,

        					KCL_PTPT,	KCL_RORO,	KCL_RURU,	KCL_SVSE,	KCL_TRTR,	KC_NO,		QK_UNICODE_MODE_MACOS,
        					KCL_UKUA,	KCL_ZHCN,	KC_NO,		KC_NO,		KC_NO,		KC_NO,		QK_UNICODE_MODE_LINUX,
        _______,			KC_NO,		KC_NO,		KC_NO,		KC_NO,		KC_NO,		KC_NO,		QK_UNICODE_MODE_WINDOWS,
        KC_NO,				KC_NO,		KC_NO,		KC_NO,		KC_NO,		KC_NO,		KC_NO,		QK_UNICODE_MODE_BSD,
        KC_NO,				KC_NO,		KC_NO,					KC_NO,		KC_NO,		KC_NO,		KC_BASE
        //[[[end]]]
        ),
    [_ADDLANG1] = LAYOUT_left_right_stacked(
        KC_NO,      KC_NO,      KC_LAT0,    KC_LAT1,    KC_LAT2,    KC_LAT3,    KC_LAT4,
        KC_NO,      _______,    _______,    _______,    _______,    _______,    _______,
        KC_NO,      _______,    _______,    _______,    _______,    _______,    _______,    _______,
        _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,
        KC_NO,      KC_NO,      _______,    _______,                _______,    _______,    _______,

                    KC_LAT5,    KC_LAT6,    KC_LAT7,    KC_LAT8,    KC_LAT9,    KC_NO,      KC_NO,
                    _______,    _______,    _______,    _______,    _______,    _______,    KC_NO,
        _______,    _______,    _______,    _______,    _______,    _______,    _______,    KC_NO,
        _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,
        _______,    _______,    _______,                _______,    _______,    _______,    _______
        ),
    [_EMJ0] = LAYOUT_left_right_stacked(
        EMJ(0),    EMJ(1),    EMJ(2),    EMJ(3),    EMJ(4),    EMJ(5),    EMJ(6),
        EMJ(14),   EMJ(15),   EMJ(16),   EMJ(17),   EMJ(18),   EMJ(19),   EMJ(20),
        EMJ(28),   EMJ(29),   EMJ(30),   EMJ(31),   EMJ(32),   EMJ(33),   EMJ(34),   _______,
        EMJ(42),   EMJ(43),   EMJ(44),   EMJ(45),   EMJ(46),   EMJ(47),   EMJ(48),   EMJ(49),
        KC_BASE,   EMJ(58),   EMJ(59),   EMJ(60),              EMJ(61),   EMJ(62),   EMJ(63),

                  EMJ(7),    EMJ(8),    EMJ(9),    EMJ(10),   EMJ(11),   EMJ(12),   EMJ(13),
                  EMJ(21),   EMJ(22),   EMJ(23),   EMJ(24),   EMJ(25),   EMJ(26),   EMJ(27),
        _______,  EMJ(35),   EMJ(36),   EMJ(37),   EMJ(38),   EMJ(39),   EMJ(40),   EMJ(41),
        EMJ(50),  EMJ(51),   EMJ(52),   EMJ(53),   EMJ(54),   EMJ(55),   EMJ(56),   EMJ(57),
        EMJ(64),  EMJ(65),   EMJ(66),              EMJ(67),   EMJ(68),   EMJ(69),   TO(_EMJ1)
        ),
    [_EMJ1] = LAYOUT_left_right_stacked(
        EMJ(70+0),    EMJ(70+1),    EMJ(70+2),    EMJ(70+3),    EMJ(70+4),    EMJ(70+5),    EMJ(70+6),
        EMJ(70+14),   EMJ(70+15),   EMJ(70+16),   EMJ(70+17),   EMJ(70+18),   EMJ(70+19),   EMJ(70+20),
        EMJ(70+28),   EMJ(70+29),   EMJ(70+30),   EMJ(70+31),   EMJ(70+32),   EMJ(70+33),   EMJ(70+34),   _______,
        EMJ(70+42),   EMJ(70+43),   EMJ(70+44),   EMJ(70+45),   EMJ(70+46),   EMJ(70+47),   EMJ(70+48),   EMJ(70+49),
        KC_BASE,      EMJ(70+58),   EMJ(70+59),   EMJ(70+60),                 EMJ(70+61),   EMJ(70+62),   EMJ(70+63),

                     EMJ(70+7),    EMJ(70+8),    EMJ(70+9),    EMJ(70+10),   EMJ(70+11),   EMJ(70+12),   EMJ(70+13),
                     EMJ(70+21),   EMJ(70+22),   EMJ(70+23),   EMJ(70+24),   EMJ(70+25),   EMJ(70+26),   EMJ(70+27),
        _______,     EMJ(70+35),   EMJ(70+36),   EMJ(70+37),   EMJ(70+38),   EMJ(70+39),   EMJ(70+40),   EMJ(70+41),
        EMJ(70+50),  EMJ(70+51),   EMJ(70+52),   EMJ(70+53),   EMJ(70+54),   EMJ(70+55),   EMJ(70+56),   EMJ(70+57),
        EMJ(70+64),  EMJ(70+65),   EMJ(70+66),                 EMJ(70+67),   EMJ(70+68),   EMJ(70+69),   TO(_EMJ0)
        )
};

// Maps default layer to corresponding function layer (FL0 or FL1).
// Global variables: (none - uses passed parameters only)
layer_state_t get_function_layer(layer_state_t def_layer) {
    switch (def_layer) {
        case _L0:
        case _L3:
            return _FL0;
        case _L1:
        case _L2:
        case _L4:
            return _FL1;
        default:
            return 0;

    }
}

#define LX(x,y) ((x)/2),y
led_config_t g_led_config = { {// Key Matrix to LED Index
                              {6, 5, 4, 3, 2, 1, 0, NO_LED},
                              {13, 12, 11, 10, 9, 8, 7, NO_LED},
                              {20, 19, 18, 17, 16, 15, 14, NO_LED},
                              {27, 26, 25, 24, 23, 22, 21, NO_LED},
                              {35, 34, 33, 32, 31, 30, 29, 28},

                              {NO_LED, 42, 41, 40, 39, 38, 37, 36},
                              {NO_LED, 49, 48, 47, 46, 45, 44, 43},
                              {NO_LED, 56, 55, 54, 53, 52, 51, 50},
                              {NO_LED, 63, 62, 61, 60, 59, 58, 57},
                              {71, 70, 69, 68, 67, 66, 65, 64}
                             },
                             {
                                // LED Index to Physical Position
                                                {LX(144, 9)},   {LX(129, 9)},   {LX(104, 5)},   {LX(79, 1)},    {LX(55, 5)},    {LX(30, 9)},    {LX(0, 9)},
                                                {LX(144, 33)},  {LX(129, 33)},  {LX(104, 19)},  {LX(79, 25)},   {LX(55, 29)},   {LX(30, 33)},   {LX(0, 33)},
                                                {LX(144, 58)},  {LX(129, 58)},  {LX(104, 54)},  {LX(79, 50)},   {LX(55, 54)},   {LX(30, 58)},   {LX(0, 58)},
                                                {LX(144, 83)},  {LX(129, 83)},  {LX(104, 79)},  {LX(79, 75)},   {LX(55, 79)},   {LX(30, 83)},   {LX(0, 83)},
                {LX(170, 99)},  {LX(170, 127)}, {LX(144, 118)}, {LX(129, 113)},                 {LX(79, 99)},   {LX(55, 103)},  {LX(30, 107)},  {LX(6, 107)},

                                                {LX(446, 9)},   {LX(415, 9)},   {LX(390, 5)},   {LX(365, 1)},   {LX(341, 5)},   {LX(316, 9)},   {LX(286, 9)},
                                                {LX(446, 33)},  {LX(415, 33)},  {LX(390, 19)},  {LX(365, 25)},  {LX(341, 29)},  {LX(316, 33)},  {LX(286, 33)},
                                                {LX(446, 58)},  {LX(415, 58)},  {LX(390, 54)},  {LX(365, 50)},  {LX(341, 54)},  {LX(316, 58)},  {LX(286, 58)},
                                                {LX(446, 83)},  {LX(415, 83)},  {LX(390, 79)},  {LX(365, 75)},  {LX(341, 79)},  {LX(316, 83)},  {LX(286, 83)},
                                                {LX(440, 107)}, {LX(415, 107)}, {LX(390, 103)}, {LX(365, 99)},                  {LX(324, 113)}, {LX(290, 118)}, {LX(264, 127)},  {LX(264, 99)}
                             },
                             {
                                 // LED Index to Flag
                                 4, 4, 4, 4, 4, 4, 4,
                                 4, 4, 4, 4, 4, 4, 4,
                                 4, 4, 4, 4, 4, 4, 4,
                                 4, 4, 4, 4, 4, 4, 4,
                                 4, 4, 4, 4, 4, 4, 4, 4,

                                 4, 4, 4, 4, 4, 4, 4,
                                 4, 4, 4, 4, 4, 4, 4,
                                 4, 4, 4, 4, 4, 4, 4,
                                 4, 4, 4, 4, 4, 4, 4,
                                 4, 4, 4, 4, 4, 4, 4, 4
                             } };

// Returns display text for special keys.
const uint16_t* to_static_text(uint16_t keycode, led_t state) {

    const uint16_t* emoji = keycode_to_emoji(keycode);
    if(emoji!=NULL) {
        return emoji;
    }

    if(IS_QK_MOD_TAP(keycode)) {
        keycode = QK_MOD_TAP_GET_TAP_KEYCODE(keycode);
    }

    const poly_sync_t* local_state = get_local_state();
#ifndef ENABLE_NUMLOCK_FOR_OSX
    if(local_state->unicode_mode == UNICODE_MODE_MACOS && keycode >= KC_NUM_LOCK && keycode <=KC_KP_DOT) {
        switch(keycode) {
            case KC_NUM_LOCK: return u"";
            case KC_KP_7:     return u"7";
            case KC_KP_8:     return u"8";
            case KC_KP_9:     return u"9";
            case KC_KP_4:     return u"4";
            case KC_KP_5:     return u"5";
            case KC_KP_6:     return u"6";
            case KC_KP_1:     return u"1";
            case KC_KP_2:     return u"2";
            case KC_KP_3:     return u"3";
            case KC_KP_0:     return u"0";
            case KC_KP_DOT:   return u".";
            default: break;
        }
    }
#endif

    const uint16_t* text = keycode_to_static_text(keycode, state, local_state->flags);
    if(text!=NULL) {
        return text;
    }

    const poly_layer_t* local_layer = get_local_layer();
    switch (keycode) {
        case QK_UNICODE_MODE_MACOS:         return local_state->unicode_mode == UNICODE_MODE_MACOS ? u"Mac\r\v" ICON_SWITCH_ON : u"Mac\r\v" ICON_SWITCH_OFF;
        case QK_UNICODE_MODE_LINUX:         return local_state->unicode_mode == UNICODE_MODE_LINUX ? u"Lnx\r\v" ICON_SWITCH_ON : u"Lnx\r\v" ICON_SWITCH_OFF;
        case QK_UNICODE_MODE_WINDOWS:       return local_state->unicode_mode == UNICODE_MODE_WINDOWS ? u"Win\r\v" ICON_SWITCH_ON : u"Win\r\v" ICON_SWITCH_OFF;
        case QK_UNICODE_MODE_BSD:           return local_state->unicode_mode == UNICODE_MODE_BSD ? u"BSD\r\v" ICON_SWITCH_ON : u"BSD\r\v" ICON_SWITCH_OFF;
        case QK_UNICODE_MODE_WINCOMPOSE:    return local_state->unicode_mode == UNICODE_MODE_WINCOMPOSE ? u"WinC\r\v" ICON_SWITCH_ON : u"WinC\r\v" ICON_SWITCH_OFF;
        case QK_UNICODE_MODE_EMACS:         return local_state->unicode_mode == UNICODE_MODE_EMACS ? u"Emcs\r\v" ICON_SWITCH_ON : u"Emcs\r\v" ICON_SWITCH_OFF;
        case KC_L0:                         return local_layer->def_layer == _L0 ? u"Qwty\r\v" ICON_SWITCH_ON : u"Qwty\r\v" ICON_SWITCH_OFF;
        case KC_L1:                         return local_layer->def_layer == _L1 ? u"Qwty!\r\v" ICON_SWITCH_ON : u"Qwty!\r\v" ICON_SWITCH_OFF;
        case KC_L2:                         return local_layer->def_layer == _L2 ? u"Clmk\r\v" ICON_SWITCH_ON : u"Clmk\r\v" ICON_SWITCH_OFF;
        case KC_L3:                         return local_layer->def_layer == _L3 ? u"Neo\r\v" ICON_SWITCH_ON : u"Neo\r\v" ICON_SWITCH_OFF;
        case KC_L4:                         return local_layer->def_layer == _L4 ? u"Wkm\r\v" ICON_SWITCH_ON : u"Wkm\r\v" ICON_SWITCH_OFF;

        //Language selection keycodes
        /*[[[cog
        for lang in languages:
            pretty = f"{lang[0:2]}\\r\\t{lang[2:]}"
            cog.outl(f'case KCL_{lang.upper()}: return local_state->lang == LANG_{lang.upper()} ? u"{pretty}\\r\\x05\\x05" BLACK_RECTANGLE BLACK_RECTANGLE : u"{pretty}";')
        ]]]*/
        case KCL_ENUS: return local_state->lang == LANG_ENUS ? u"en\r\tUS\r\x05\x05" BLACK_RECTANGLE BLACK_RECTANGLE : u"en\r\tUS";
        case KCL_DEDE: return local_state->lang == LANG_DEDE ? u"de\r\tDE\r\x05\x05" BLACK_RECTANGLE BLACK_RECTANGLE : u"de\r\tDE";
        case KCL_FRFR: return local_state->lang == LANG_FRFR ? u"fr\r\tFR\r\x05\x05" BLACK_RECTANGLE BLACK_RECTANGLE : u"fr\r\tFR";
        case KCL_ESES: return local_state->lang == LANG_ESES ? u"es\r\tES\r\x05\x05" BLACK_RECTANGLE BLACK_RECTANGLE : u"es\r\tES";
        case KCL_PTPT: return local_state->lang == LANG_PTPT ? u"pt\r\tPT\r\x05\x05" BLACK_RECTANGLE BLACK_RECTANGLE : u"pt\r\tPT";
        case KCL_ITIT: return local_state->lang == LANG_ITIT ? u"it\r\tIT\r\x05\x05" BLACK_RECTANGLE BLACK_RECTANGLE : u"it\r\tIT";
        case KCL_TRTR: return local_state->lang == LANG_TRTR ? u"tr\r\tTR\r\x05\x05" BLACK_RECTANGLE BLACK_RECTANGLE : u"tr\r\tTR";
        case KCL_KOKR: return local_state->lang == LANG_KOKR ? u"ko\r\tKR\r\x05\x05" BLACK_RECTANGLE BLACK_RECTANGLE : u"ko\r\tKR";
        case KCL_JAJP: return local_state->lang == LANG_JAJP ? u"ja\r\tJP\r\x05\x05" BLACK_RECTANGLE BLACK_RECTANGLE : u"ja\r\tJP";
        case KCL_ARSA: return local_state->lang == LANG_ARSA ? u"ar\r\tSA\r\x05\x05" BLACK_RECTANGLE BLACK_RECTANGLE : u"ar\r\tSA";
        case KCL_ELGR: return local_state->lang == LANG_ELGR ? u"el\r\tGR\r\x05\x05" BLACK_RECTANGLE BLACK_RECTANGLE : u"el\r\tGR";
        case KCL_UKUA: return local_state->lang == LANG_UKUA ? u"uk\r\tUA\r\x05\x05" BLACK_RECTANGLE BLACK_RECTANGLE : u"uk\r\tUA";
        case KCL_RURU: return local_state->lang == LANG_RURU ? u"ru\r\tRU\r\x05\x05" BLACK_RECTANGLE BLACK_RECTANGLE : u"ru\r\tRU";
        case KCL_BEBY: return local_state->lang == LANG_BEBY ? u"be\r\tBY\r\x05\x05" BLACK_RECTANGLE BLACK_RECTANGLE : u"be\r\tBY";
        case KCL_KKKZ: return local_state->lang == LANG_KKKZ ? u"kk\r\tKZ\r\x05\x05" BLACK_RECTANGLE BLACK_RECTANGLE : u"kk\r\tKZ";
        case KCL_BGBG: return local_state->lang == LANG_BGBG ? u"bg\r\tBG\r\x05\x05" BLACK_RECTANGLE BLACK_RECTANGLE : u"bg\r\tBG";
        case KCL_PLPL: return local_state->lang == LANG_PLPL ? u"pl\r\tPL\r\x05\x05" BLACK_RECTANGLE BLACK_RECTANGLE : u"pl\r\tPL";
        case KCL_RORO: return local_state->lang == LANG_RORO ? u"ro\r\tRO\r\x05\x05" BLACK_RECTANGLE BLACK_RECTANGLE : u"ro\r\tRO";
        case KCL_ZHCN: return local_state->lang == LANG_ZHCN ? u"zh\r\tCN\r\x05\x05" BLACK_RECTANGLE BLACK_RECTANGLE : u"zh\r\tCN";
        case KCL_NLNL: return local_state->lang == LANG_NLNL ? u"nl\r\tNL\r\x05\x05" BLACK_RECTANGLE BLACK_RECTANGLE : u"nl\r\tNL";
        case KCL_HEIL: return local_state->lang == LANG_HEIL ? u"he\r\tIL\r\x05\x05" BLACK_RECTANGLE BLACK_RECTANGLE : u"he\r\tIL";
        case KCL_SVSE: return local_state->lang == LANG_SVSE ? u"sv\r\tSE\r\x05\x05" BLACK_RECTANGLE BLACK_RECTANGLE : u"sv\r\tSE";
        case KCL_FIFI: return local_state->lang == LANG_FIFI ? u"fi\r\tFI\r\x05\x05" BLACK_RECTANGLE BLACK_RECTANGLE : u"fi\r\tFI";
        case KCL_NNNO: return local_state->lang == LANG_NNNO ? u"nn\r\tNO\r\x05\x05" BLACK_RECTANGLE BLACK_RECTANGLE : u"nn\r\tNO";
        case KCL_DADK: return local_state->lang == LANG_DADK ? u"da\r\tDK\r\x05\x05" BLACK_RECTANGLE BLACK_RECTANGLE : u"da\r\tDK";
        case KCL_HUHU: return local_state->lang == LANG_HUHU ? u"hu\r\tHU\r\x05\x05" BLACK_RECTANGLE BLACK_RECTANGLE : u"hu\r\tHU";
        case KCL_CSCZ: return local_state->lang == LANG_CSCZ ? u"cs\r\tCZ\r\x05\x05" BLACK_RECTANGLE BLACK_RECTANGLE : u"cs\r\tCZ";
        //[[[end]]]
        default:
            return NULL;
    }
}

// Renders key character to display using language translation, including modifiers etc.
bool render_key(uint16_t keycode, led_t state, uint8_t mods) {
    const poly_layer_t* local_layer = get_local_layer();

    const bool shift = ((local_layer->mods & MOD_MASK_SHIFT) != 0);
    const bool add_lang = get_highest_layer(local_layer->layer)==_ADDLANG1;
    const bool alt = ((local_layer->mods & MOD_MASK_ALT) != 0);
    const bool is_letter = keycode>=KC_A && keycode<=KC_Z;
    if(is_letter && add_lang) {
        //display the previously selected latin variation of the letter
        const latin_sync_t* global_latin_table = get_global_latin_table();
        const uint8_t offset = (shift || state.caps_lock) ? 0 : 26;
        uint8_t variation = (shift || state.caps_lock) ? global_latin_table->ex[keycode-KC_A]>>4 : global_latin_table->ex[keycode-KC_A]&0xf;

        const uint16_t* def_variation = latin_ex_map[offset+keycode-KC_A][0];
        if(def_variation!=NULL) {
            kdisp_write_gfx_text(ALL_FONTS, ALL_FONT_SIZE, 28, 23, latin_ex_map[offset+keycode-KC_A][variation]);
            return true;
        }
        return false;
    }

    //variation selection on 0~9
    uint16_t local_last_latin_keycode = get_local_last_latin_keycode();
    if(keycode>=KC_LAT0 && keycode<=KC_LAT9) {
        if(add_lang && alt && local_last_latin_keycode!=0) {
            //show all available alternatives for selected latin letter
            const uint8_t offset = (shift || state.caps_lock) ? 0 : 26;
            const uint16_t* variation = latin_ex_map[offset+local_last_latin_keycode-KC_A][keycode-KC_LAT0];
            if(variation!=NULL) {
                kdisp_write_gfx_text(ALL_FONTS, ALL_FONT_SIZE, 28, 23, variation);
                return true;
            }
        }
        return false;
    }

    const poly_sync_t* local_state = get_local_state();
    if (mods & MOD_RALT) {
        const uint16_t* letter = translate_keycode_only_altgr(local_state->lang, keycode);
        if (letter != NULL) {
            const bool is_num = keycode>=KC_1 && keycode<=KC_0; // yes the first is 1 and the last is 0
            int8_t v_set;
            int8_t h_set;
            if(is_num){
                v_set = SETTING_NUM_VOFFSET;
                h_set = SETTING_NUM_HOFFSET;
            } else {
                v_set = SETTING_SYM_VOFFSET;
                h_set = SETTING_SYM_HOFFSET;
            }
            int8_t v_off = get_setting(v_set, local_state->lang, VAR_SMALL);
            int8_t v_off_alt = get_setting(v_set, local_state->lang, VAR_ALTGR);
            v_off = PK_MIN(v_off, v_off_alt);
            int8_t h_off = get_setting(h_set, local_state->lang, VAR_SMALL);
            if(v_off!=HIDE_KEY && h_off!=HIDE_KEY) {
                kdisp_write_gfx_text(ALL_FONTS, ALL_FONT_SIZE, 28+h_off, 23+v_off, letter);
                return true;
            }
        }
    }

    //translate to current language
    const uint16_t* letter = translate_keycode(local_state->lang, keycode, shift, state.caps_lock);
    if (letter != NULL) {
        int8_t v_set;
        int8_t h_set;
        if(is_letter) {
            v_set = SETTING_LETTER_VOFFSET;
            h_set = SETTING_LETTER_HOFFSET;
        } else {
            const bool is_num = keycode>=KC_1 && keycode<=KC_0; // yes the first is 1 and the last is 0
            if(is_num){
                v_set = SETTING_NUM_VOFFSET;
                h_set = SETTING_NUM_HOFFSET;
            } else {
                v_set = SETTING_SYM_VOFFSET;
                h_set = SETTING_SYM_HOFFSET;
            }
        }
        int8_t v_off = get_setting(v_set, local_state->lang, VAR_SMALL);
        int8_t h_off = get_setting(h_set, local_state->lang, VAR_SMALL);

        kdisp_write_gfx_text(ALL_FONTS, ALL_FONT_SIZE, 28+h_off, 23+v_off, letter);

        //preview capital letter?
        if(!shift && !state.caps_lock) {
            v_off = get_setting(v_set, local_state->lang, VAR_SHIFT);
            h_off = get_setting(h_set, local_state->lang, VAR_SHIFT);
            if(v_off!=HIDE_KEY && h_off!=HIDE_KEY) {
                letter = translate_keycode_only_shift(local_state->lang, keycode);
                if (letter != NULL) {
                    kdisp_write_gfx_text(ALL_FONTS, ALL_FONT_SIZE, 28+h_off, 23+v_off, letter);
                }
            }
        }
        //preview alt representation
        letter = translate_keycode_only_altgr(local_state->lang, keycode);
        if (letter != NULL) {
            v_off = get_setting(v_set, local_state->lang, VAR_ALTGR);
            h_off = get_setting(h_set, local_state->lang, VAR_ALTGR);
            if(v_off!=HIDE_KEY && h_off!=HIDE_KEY) {
                kdisp_write_gfx_text(ALL_FONTS, ALL_FONT_SIZE, 28+h_off, 23+v_off, letter);
            }
        }
        return true;
    }
    return false;
}

// Returns builtin icon/symbol overlay text for keycode based on current modifiers and mod-tap states.
const uint16_t* keycode_to_disp_overlay(uint16_t keycode, led_t state) {
    switch (keycode)
    {
        case KC_F2: return u"      " PRIVATE_NOTE;
        case KC_F5: return u"     " ARROWS_CIRCLE;
        default: break;
    }

    uint8_t local_mods = get_local_layer()->mods;
    if( (local_mods & MOD_MASK_CTRL) != 0) {
        switch(keycode) {
            case KC_A: return u"      " BOX_WITH_CHECK_MARK;
            case KC_C: return u"     " CLIPBOARD_COPY;
            case KC_D: return u"\t " PRIVATE_DELETE;
            case KC_F: return u"    " PRIVATE_FIND;
            case KC_X: return u"\t\b\b" CLIPBOARD_CUT;
            case KC_V: return u"     " CLIPBOARD_PASTE;
            case KC_S: return u"\t" PRIVATE_FLOPPY;
            case KC_O: return u"\t" FILE_OPEN;
            case KC_P: return u"\t" PRIVATE_PRINTER;
            case KC_Z: return u"      " ARROWS_UNDO;
            case KC_Y: return u"      " ARROWS_REDO;
            default: break;
        }
    } else if((local_mods & MOD_MASK_GUI) != 0) {
        switch(keycode) {
            case KC_D:      return u"    " PRIVATE_PC;
            case KC_L:      return u"    " PRIVATE_LOCK;
            case KC_P:      return u"    " PRIVATE_SCREEN;
            case KC_UP:     return u"     " PRIVATE_MAXIMIZE;
            case KC_DOWN:   return u"     " PRIVATE_WINDOW;
            default: break;
        }
    }

    if(IS_QK_MOD_TAP(keycode)) {
        uint8_t mods = QK_MOD_TAP_GET_MODS(keycode);
        if((mods & MOD_MASK_CSAG) == MOD_MASK_CSAG) {
            return u"    " CURRENCY_SIGN ICON_SHIFT NOT_SIGN KATAKANA_MIDDLE_DOT;
        } else if((mods & MOD_MASK_SAG) == MOD_MASK_SAG) {
            return u"    " ICON_SHIFT NOT_SIGN KATAKANA_MIDDLE_DOT;
        } else if((mods & MOD_MASK_CAG) == MOD_MASK_CAG) {
            return u"    " CURRENCY_SIGN NOT_SIGN KATAKANA_MIDDLE_DOT;
        } else if((mods & MOD_MASK_CSG) == MOD_MASK_CSG) {
            return u"    " CURRENCY_SIGN ICON_SHIFT KATAKANA_MIDDLE_DOT;
        } else if((mods & MOD_MASK_CSA) == MOD_MASK_CSA) {
            return u"    " CURRENCY_SIGN ICON_SHIFT NOT_SIGN;
        } else if((mods & MOD_MASK_AG) == MOD_MASK_AG) {
            return u"    " NOT_SIGN KATAKANA_MIDDLE_DOT;
        } else if((mods & MOD_MASK_SG) == MOD_MASK_SG) {
            return u"    " ICON_SHIFT KATAKANA_MIDDLE_DOT;
        } else if((mods & MOD_MASK_SA) == MOD_MASK_SA) {
            return u"    " CURRENCY_SIGN NOT_SIGN;
        } else if((mods & MOD_MASK_CG) == MOD_MASK_CG) {
            return u"    " CURRENCY_SIGN KATAKANA_MIDDLE_DOT;
        } else if((mods & MOD_MASK_CA) == MOD_MASK_CA) {
            return u"    " CURRENCY_SIGN NOT_SIGN;
        } else if((mods & MOD_MASK_CS) == MOD_MASK_CS) {
            return u"    " CURRENCY_SIGN ICON_SHIFT;
        } else if(mods & MOD_MASK_CTRL) {
            return u"    " CURRENCY_SIGN;
        } else if(mods & MOD_MASK_ALT) {
            return u"    " NOT_SIGN;
        } else if (mods & MOD_MASK_SHIFT) {
            return u"    " ICON_SHIFT;
        } else {
            return u"   " KATAKANA_MIDDLE_DOT;
        }
    }

    return NULL;
}

bool copy_overlay_to_buffer(uint16_t keycode, uint8_t mods) {
    if(keycode>KC_RGUI || (keycode>KC_NUM_LOCK && keycode<KC_NUBS) || (keycode>KC_APP && keycode<KC_LEFT_CTRL)) {
        return false;
    }
    uint16_t idx = (keycode>KC_APP) ? (keycode - KC_LEFT_CTRL + 82) : (keycode>KC_NUM_LOCK ? keycode - KC_NUBS + 80 : keycode - KC_A);
    if(idx>=90) {
        return false;
    }
    idx = adjust_overlay_idx_to_mod(idx, mods);
    // use_overlay[] is from-indexed (see set_10bit_overlay_mapping): check it
    // here on the display position, before resolving to the pool slot.
    if(!is_overlay_used(idx)) {
        return false;
    }
    idx = get_overlay_mapping(idx);

    kdisp_clear_bitmap_courtyard(28, 0, get_overlay(idx), 72, 40);
    kdisp_draw_bitmap(28, 0, get_overlay(idx), 72, 40); //don't understnad why we start at offset 28... need to think about it
    return true;
}

// Updates all display based on current layer and modifiers.
// Global variables: keymaps
void update_displays(enum refresh_mode mode) {
    const poly_sync_t* local_state = get_local_state();
    if(local_state->contrast<=DISP_OFF || (local_state->flags&DISP_IDLE)!=0) {
        return;
    }

    //uint8_t layer = get_highest_layer(layer_state);
    const poly_layer_t* local_layer = get_local_layer();

    const led_t state = local_layer->led_state;
    const uint8_t mods = local_layer->mods;
    const bool capital_case = ((mods & MOD_MASK_SHIFT) != 0) || state.caps_lock;
    const bool display_overlays = test_flag(local_state->overlay_flags, DISPLAY_OVERLAYS);
    //the left side has an offset of 0, the right side an offset of MATRIX_ROWS_PER_SIDE
    const uint8_t offset = is_left_side() ? 0 : MATRIX_ROWS_PER_SIDE;
    uint8_t start_row = 0;

    //select first display (and later on shift that 0 till the end)
    if (mode == START_SECOND_HALF) {
        sr_shift_out_buffer_latch(disp_row_3.bitmask, sizeof(struct display_info));
        start_row = 3;
    }
    else {
        sr_shift_out_buffer_latch(disp_row_0.bitmask, sizeof(struct display_info));
    }

    const uint8_t max_rows = mode == START_FIRST_HALF ? 3 : MATRIX_ROWS_PER_SIDE;

    uint8_t skip = 0;
    for (uint8_t r = start_row; r < max_rows; ++r) {
        for (uint8_t c = 0; c < MATRIX_COLS; ++c) {
            uint8_t  disp_idx = LAYOUT_TO_INDEX(r, c);

            //since MATRIX_COLS==8 we don't need to shift multiple times at the end of the row
            //except there was a leading and missing physical key (KC_NO on base layer)
            uint16_t keycode = keymaps[_BL][r + offset][c];
            if (keycode == KC_NO) {
                skip++;
            }
            else {
                if (disp_idx != 255) {
                    uint8_t layer = get_highest_layer(local_layer->layer);
                    uint16_t highest_kc = keycode_at_keymap_location(layer,r + offset,c); //if we encounter a transparent key go down one layer (but only one!)
                    keycode = (highest_kc == KC_TRNS) ? keycode_at_keymap_location(get_highest_layer(local_layer->layer&~(1<<layer)),r + offset,c) : highest_kc;
                    kdisp_enable(true);
                    kdisp_set_contrast(local_state->contrast-1);
                    if(keycode!=KC_TRNS) {
                        const uint16_t* text = to_static_text(keycode, state);
                        kdisp_set_buffer(0x00);
                        if(text==NULL) {
                            if(!render_key(keycode, state, mods) && (keycode&QK_UNICODEMAP_PAIR)==QK_UNICODEMAP_PAIR){
                                uint16_t chr = capital_case ? QK_UNICODEMAP_PAIR_GET_SHIFTED_INDEX(keycode) : QK_UNICODEMAP_PAIR_GET_UNSHIFTED_INDEX(keycode);
                                kdisp_write_gfx_char(ALL_FONTS, ALL_FONT_SIZE, 28, 23, unicode_map[chr], false);
                            }
                        } else {
                            kdisp_write_gfx_text(ALL_FONTS, ALL_FONT_SIZE, 28, 23, text);
                        }
                        text = NULL;
                        if(display_overlays) {
                            if(!copy_overlay_to_buffer(keycode, mods)) {
                                text = keycode_to_disp_overlay(keycode, state); //fallback to hardcoded
                            }
                        } else {
                            text = keycode_to_disp_overlay(keycode, state); //this should maybe go away - or setting?
                        }
                        if(text) {
                            kdisp_write_gfx_text_cy(ALL_FONTS, ALL_FONT_SIZE, 28, 23, text, true);
                        }
                        kdisp_send_buffer();
                    }
                }
                sr_shift_once_latch();
            }

        }
        for (;skip > 0;skip--) {
            sr_shift_once_latch();
        }
    }
}

// Converts brightness level 0-7 to pulsating contrast value for idle display animation.
uint8_t to_brightness(uint8_t b) {
    switch(b) {
        case 23: case 24: case 25: case 26: case 27: return 7;
        case 22: case 28: return 6;
        case 21: case 29: return 5;
        case 20: case 30: return 4;
        case 19: case 31: return 3;
        case 18: case 32: return 2;
        case 1: case 7: return 1;
        case 2: case 6: return 3;
        case 3: case 4: case 5: return 5;
        default: return 0;
    }
}

// Updates all displays to show idle pulsating animation with varying brightness pattern.
void kdisp_idle(uint8_t contrast) {
    uint8_t offset = is_left_side() ? 0 : MATRIX_ROWS_PER_SIDE;
    uint8_t skip = 0;
    sr_shift_out_buffer_latch(disp_row_0.bitmask, sizeof(struct display_info));

    //uint8_t idx = 0;
    for (uint8_t r = 0; r < MATRIX_ROWS_PER_SIDE; ++r) {
        for (uint8_t c = 0; c < MATRIX_COLS; ++c) {
            uint8_t  disp_idx = LAYOUT_TO_INDEX(r, c);

            //since MATRIX_COLS==8 we don't need to shift multiple times at the end of the row
            //except there was a leading and missing physical key (KC_NO on base layer)
            uint16_t keycode = keymaps[_BL][r + offset][c];
            if (keycode == KC_NO) {
                skip++;
            } else {
                if (disp_idx != 255) {
                    uint8_t idle_brightness = to_brightness((contrast+(c%3+r)*keycode+offset+r)%50);
                    if(idle_brightness==0) {
                        kdisp_enable(false);
                    } else {
                        kdisp_enable(true);
                        kdisp_set_contrast(idle_brightness-1);
                    }
                }
                sr_shift_once_latch();
            }

        }
        for (;skip > 0;skip--) {
            sr_shift_once_latch();
        }
    }
}

// Handles keypress events including unicode input, language modifications, and special commands.
bool process_record_user(uint16_t keycode, keyrecord_t* record) {

    uint32_t t = get_time_since_last_update();
    if(record->event.pressed) {
        uprintf("wait %ld.%03ld\n", t/1000, t%1000);
        uprintf("press 0x%04x\n", keycode);
    } else {
        uprintf("wait %ld.%03ld\n", t/1000, t%1000);
        uprintf("release 0x%04x\n", keycode);
    }

    if(process_unicodemap_poly(keycode, record)) {
        return  false;
    }

    switch (keycode) {
#ifdef RGB_MATRIX_ENABLE
        case RM_VALU: case RM_VALD:
        case RM_HUEU: case RM_HUED:
        case RM_SATU: case RM_SATD:
        case RM_SPDU: case RM_SPDD:
            if (record->event.pressed) {
                rgb_held_keycode = keycode;
                apply_rgb_adjust(keycode);
                rgb_repeat_token = defer_exec(RGB_REPEAT_INITIAL_DELAY_MS, rgb_repeat_callback, NULL);
            } else {
                rgb_held_keycode = KC_NO;
                cancel_deferred_exec(rgb_repeat_token);
                rgb_repeat_token = INVALID_DEFERRED_TOKEN;
                eeconfig_update_rgb_matrix(&rgb_matrix_config);
            }
            return false;
#endif
        case KC_CRSEL:
            if (record->event.pressed) { SEND_STRING(SS_TAP(X_HOME) SS_TAP(X_HOME) SS_LSFT(SS_TAP(X_END)) SS_TAP(X_BACKSPACE) SS_TAP(X_BACKSPACE) SS_TAP(X_DOWN)); }
            uprint("Delete Line.\n");
            return false;
        case KC_SELECT:
            if (record->event.pressed) { SEND_STRING(SS_LCTL(SS_TAP(X_LEFT) SS_LSFT(SS_TAP(X_RGHT)))); }
            uprint("Select Word.\n");
            return false;
        case KC_EXSEL:
            if (record->event.pressed) { SEND_STRING(SS_TAP(X_HOME) SS_LSFT(SS_TAP(X_END))); }
            uprint("Select Line.\n");
            return false;
        case KC_OPER:
            if (record->event.pressed) {
                SEND_STRING( // Go to the end of the line and tap delete.
                    SS_TAP(X_END) SS_TAP(X_DEL)
                    SS_TAP(X_SPC) // In case this has joined two wormatrix toaend_string splhhhhds together, insert one space.
                    SS_LCTL(
                        // Go to the beginning of the next word.
                        SS_TAP(X_RGHT) SS_TAP(X_LEFT)
                        // Select back to the end of the previous word. This should select
                        // all spaces and tabs between the joined lines from indentation
                        // or trailing whitespace, including the space inserted earlier.
                        SS_LSFT(SS_TAP(X_LEFT) SS_TAP(X_RGHT))
                    )
                    SS_TAP(X_SPC) // Replace the selection with a single space.
                );
                uprint("Join Line.\n");
            }
            return false;
        default:
            break;
    }

    const bool addlang = get_highest_layer(get_local_layer()->layer)==_ADDLANG1;
    const poly_layer_t* global_layer = get_global_layer();
    if (record->event.pressed) {
        switch (keycode) {
            case QK_BOOTLOADER:
                uprintf("Bootloader entered. Please copy new Firmware.\n");
                // Tell the slave first — once we return true, QMK calls
                // reset_keyboard() and the master goes dark before housekeeping
                // could push a deferred state diff over UART.
                access_local_state()->overlay_flags |= BOOTLOADER_DISPLAY;
                send_to_bridge(USER_SYNC_POLY_DATA, (void *)access_local_state(), sizeof(poly_sync_t), 10);
                display_bootloader_message();
#ifdef RGB_MATRIX_ENABLE
                // Mode-switch latches the slave into solid red; set_color_all
                // + update_pwm_buffers immediately flushes red to the master's
                // LED driver (no further frames will render before reset).
                rgb_matrix_enable_noeeprom();
                rgb_matrix_mode_noeeprom(RGB_MATRIX_SOLID_COLOR);
                rgb_matrix_sethsv_noeeprom(0, 255, 8);
                rgb_matrix_set_color_all(8, 0, 0);
                rgb_matrix_update_pwm_buffers();
#endif
                return true;
            case KC_A ... KC_Z:
                set_local_last_latin_keycode(keycode);
                if((get_mods() & MOD_MASK_ALT) == 0 && addlang) {
                    const bool lshift = get_mods() == MOD_BIT(KC_LEFT_SHIFT);
                    const bool rshift = get_mods() == MOD_BIT(KC_RIGHT_SHIFT);
                    const bool upper_case = lshift || rshift || global_layer->led_state.caps_lock;
                    const uint8_t offset = upper_case ? 0 : 26;
                    if(latin_ex_map[offset+keycode-KC_A][0]) {
                        const latin_sync_t* global_latin_table = get_global_latin_table();
                        uint8_t variation = upper_case ? global_latin_table->ex[keycode-KC_A]>>4 : global_latin_table->ex[keycode-KC_A]&0xf;

                        //this is a work-around (at least for I-Bus on Linux we need to remove the shift, otherwise the Unicode sequence will not be recognized!)
                        if(lshift) unregister_code16(KC_LEFT_SHIFT);
                        if(rshift) unregister_code16(KC_RIGHT_SHIFT);
                        register_unicode(latin_ex_map[offset+keycode-KC_A][variation][0]);
                        if(lshift) register_code16(KC_LEFT_SHIFT);
                        if(rshift) register_code16(KC_RIGHT_SHIFT);
                        return false;
                    }
                }
                break;
            default:
                break;
        }
        uint16_t last_latin_keycode = get_local_last_latin_keycode();

        if((get_mods() & MOD_MASK_ALT) != 0 && addlang) {
            switch(keycode) {
                case KC_LAT0 ... KC_LAT9:
                    if( last_latin_keycode!=0) {
                        latin_sync_t* global_latin_table = access_global_latin_table();
                        uint8_t current = global_latin_table->ex[last_latin_keycode-KC_A];
                        if((get_mods() & MOD_MASK_SHIFT) || global_layer->led_state.caps_lock) {
                            global_latin_table->ex[last_latin_keycode-KC_A] = ((keycode-KC_LAT0)<<4) | (current&0xf);
                        } else {
                            global_latin_table->ex[last_latin_keycode-KC_A] = (keycode-KC_LAT0) | (current&0xf0);
                        }
                        send_to_bridge(USER_SYNC_LATIN_EX_DATA, (void*)global_latin_table, sizeof(*global_latin_table), 10);

                        save_user_latin();
                        request_disp_refresh();
                    }
                    break;
                case KC_A ... KC_Z:
                    request_disp_refresh();
                    break;
                default:
                    break;
            }

            return false;
        } else {
            set_local_last_latin_keycode(0);
        }
    }

    return display_wakeup(record);
}

// Post-processes keystrokes to handle display and state changes for various special keycodes.
void post_process_record_user(uint16_t keycode, keyrecord_t* record) {
    if (keycode == KC_CAPS_LOCK) {
        request_disp_refresh();
    }
    poly_sync_t* local_state = access_local_state();
    poly_layer_t* local_layer = access_local_layer();
    if (!record->event.pressed) {
        switch (keycode) {
        case KC_RGB_TOG:
            local_state->flags = toggle_flag(local_state->flags, RGB_ON);
            break;
        case KC_DEADKEY:
            local_state->flags = toggle_flag(local_state->flags, DEAD_KEY_ON_WAKEUP);
            request_disp_refresh();
            break;
        case KC_TOGMODS:
            local_state->flags = toggle_flag(local_state->flags, MODS_AS_TEXT);
            request_disp_refresh();
            break;
        case KC_TOGTEXT:
            local_state->flags = toggle_flag(local_state->flags, MORE_TEXT);
            request_disp_refresh();
            break;
        case KC_L0:
            local_layer->def_layer = _L0;
            persistent_default_layer_set(local_layer->def_layer);
            request_disp_refresh();
            break;
        case KC_L1:
            local_layer->def_layer = _L1;
            persistent_default_layer_set(local_layer->def_layer);
            request_disp_refresh();
            break;
        case KC_L2:
            local_layer->def_layer = _L2;
            persistent_default_layer_set(local_layer->def_layer);
            request_disp_refresh();
            break;
        case KC_L3:
            local_layer->def_layer = _L3;
            persistent_default_layer_set(local_layer->def_layer);
            request_disp_refresh();
            break;
        case KC_L4:
            local_layer->def_layer = _L4;
            persistent_default_layer_set(local_layer->def_layer);
            request_disp_refresh();
            break;
        case KC_BASE:
            layer_clear();
            layer_on(local_layer->def_layer);
            break;
        case KC_RIGHT_SHIFT:
        case KC_LEFT_SHIFT:
            request_disp_refresh();
            break;
        case KC_D1Q:
            local_state->contrast = FULL_BRIGHT/4;
            save_user_settings();
            break;
        case KC_D3Q:
            local_state->contrast = (FULL_BRIGHT/4)*3;
            save_user_settings();
            break;
        case KC_DHLF:
            local_state->contrast = FULL_BRIGHT/2;
            save_user_settings();
            break;
        case KC_DMAX:
            local_state->contrast = FULL_BRIGHT;
            save_user_settings();
            break;
        case KC_DMIN:
            local_state->contrast = 2;
            save_user_settings();
            break;
        case KC_DDIM:
            dec_brightness();
            break;
        case KC_DBRI:
            inc_brightness();
            break;
        /*[[[cog
            for lang in languages:
                cog.outl(f'case KCL_{lang.upper()}: local_state->lang = LANG_{lang.upper()}; save_user_settings(); layer_off(_LL); break;')
            ]]]*/
        case KCL_ENUS: local_state->lang = LANG_ENUS; save_user_settings(); layer_off(_LL); break;
        case KCL_DEDE: local_state->lang = LANG_DEDE; save_user_settings(); layer_off(_LL); break;
        case KCL_FRFR: local_state->lang = LANG_FRFR; save_user_settings(); layer_off(_LL); break;
        case KCL_ESES: local_state->lang = LANG_ESES; save_user_settings(); layer_off(_LL); break;
        case KCL_PTPT: local_state->lang = LANG_PTPT; save_user_settings(); layer_off(_LL); break;
        case KCL_ITIT: local_state->lang = LANG_ITIT; save_user_settings(); layer_off(_LL); break;
        case KCL_TRTR: local_state->lang = LANG_TRTR; save_user_settings(); layer_off(_LL); break;
        case KCL_KOKR: local_state->lang = LANG_KOKR; save_user_settings(); layer_off(_LL); break;
        case KCL_JAJP: local_state->lang = LANG_JAJP; save_user_settings(); layer_off(_LL); break;
        case KCL_ARSA: local_state->lang = LANG_ARSA; save_user_settings(); layer_off(_LL); break;
        case KCL_ELGR: local_state->lang = LANG_ELGR; save_user_settings(); layer_off(_LL); break;
        case KCL_UKUA: local_state->lang = LANG_UKUA; save_user_settings(); layer_off(_LL); break;
        case KCL_RURU: local_state->lang = LANG_RURU; save_user_settings(); layer_off(_LL); break;
        case KCL_BEBY: local_state->lang = LANG_BEBY; save_user_settings(); layer_off(_LL); break;
        case KCL_KKKZ: local_state->lang = LANG_KKKZ; save_user_settings(); layer_off(_LL); break;
        case KCL_BGBG: local_state->lang = LANG_BGBG; save_user_settings(); layer_off(_LL); break;
        case KCL_PLPL: local_state->lang = LANG_PLPL; save_user_settings(); layer_off(_LL); break;
        case KCL_RORO: local_state->lang = LANG_RORO; save_user_settings(); layer_off(_LL); break;
        case KCL_ZHCN: local_state->lang = LANG_ZHCN; save_user_settings(); layer_off(_LL); break;
        case KCL_NLNL: local_state->lang = LANG_NLNL; save_user_settings(); layer_off(_LL); break;
        case KCL_HEIL: local_state->lang = LANG_HEIL; save_user_settings(); layer_off(_LL); break;
        case KCL_SVSE: local_state->lang = LANG_SVSE; save_user_settings(); layer_off(_LL); break;
        case KCL_FIFI: local_state->lang = LANG_FIFI; save_user_settings(); layer_off(_LL); break;
        case KCL_NNNO: local_state->lang = LANG_NNNO; save_user_settings(); layer_off(_LL); break;
        case KCL_DADK: local_state->lang = LANG_DADK; save_user_settings(); layer_off(_LL); break;
        case KCL_HUHU: local_state->lang = LANG_HUHU; save_user_settings(); layer_off(_LL); break;
        case KCL_CSCZ: local_state->lang = LANG_CSCZ; save_user_settings(); layer_off(_LL); break;
        //[[[end]]]
        case KC_F1:case KC_F2:case KC_F3:case KC_F4:case KC_F5:case KC_F6:
        case KC_F7:case KC_F8:case KC_F9:case KC_F10:case KC_F11:case KC_F12:
            layer_off(_LL);
            break;
        default:
            break;
        }
    }
    else {
        switch (keycode)
        {
        case KC_RIGHT_SHIFT:
        case KC_LEFT_SHIFT:
            request_disp_refresh();
            break;
        case KC_LANG:
            if (IS_LAYER_ON(_LL)) {
                local_state->lang = (local_state->lang + 1) % NUM_LANG;
                save_user_settings();
                layer_off(_LL);
            }
            else {
                layer_on(_LL);
            }
            break;
        case RM_NEXT:
        case RM_PREV:
            request_disp_refresh();
            break;
        default:
            break;
        }
    }

    // uprintf("Key 0x%04X, col/row: %u/%u, %s, time: %u, int: %d, cnt: %u\n",
    //     keycode, record->event.key.col, record->event.key.row, record->event.pressed ? "DN" : "UP",
    //     record->event.time, record->tap.interrupted ? 1 : 0, record->tap.count);

    update_performed();
};

// Displays splash screen with polykybd/split72 logo and initializes displays with refresh.
void show_splash_screen(void) {
    clear_all_displays();
    if(is_left_side()) {
        display_message(1, 1, u"POLY", &FreeSansBold24pt7b);
        display_message(2, 1, u"KYBD", &FreeSansBold24pt7b);
    } else {
        display_message(1, 1, u"SPLIT", &FreeSansBold24pt7b);
        display_message(3, 1, u" 7 2", &FreeSansBold24pt7b);
    }
    wait_ms(400);
    update_displays(ALL_AT_ONCE);
}

// Configures all displays with contrast level; shows idle pulsating animation if enabled.
void set_displays(uint8_t contrast, bool idle) {
    if(idle) {
        kdisp_idle(contrast);
    } else {
        select_all_displays();
        if(contrast==DISP_OFF) {
            kdisp_enable(false);
        } else {
            kdisp_enable(true);
            kdisp_set_contrast(contrast - 1);
        }
    }
}

// Disables keypress if displays are turned off/in idle mode; restores brightness on wakeup.
bool display_wakeup(keyrecord_t* record) {
    poly_sync_t* local_state = access_local_state();
    bool accept_keypress = true;
    if ((local_state->contrast==DISP_OFF || (local_state->flags & DISP_IDLE)!=0) && record->event.pressed) {
        if(local_state->contrast==DISP_OFF && (local_state->flags&DEAD_KEY_ON_WAKEUP)!=0) {
            accept_keypress = get_time_since_last_update()<= TURN_OFF_TIME;
        }
        poly_eeconf_t ee = load_user_eeconf();
        local_state->contrast = ee.brightness;
        local_state->flags &= ~((uint8_t)DISP_IDLE);
        local_state->flags |= STATUS_DISP_ON;
        update_performed();
        request_disp_refresh();
    }

    return accept_keypress;
}

// Updates local unicode input mode state and requests display refresh on mode change.
void unicode_input_mode_set_user(uint8_t unicode_mode) {
    access_local_state()->unicode_mode = unicode_mode;
    request_disp_refresh();
}

// Initializes keyboard state after reset: enables debug, sets CPI, loads layer/unicode defaults.
// Global variables: com
void keyboard_post_init_user(void) {
    // Customise these values to desired behaviour
    debug_enable = true;
    debug_matrix = false;
    debug_keyboard = false;
    debug_mouse = false;

    //pointing_device_set_cpi(20000);
    pointing_device_set_cpi(650);
    //pimoroni_trackball_set_rgbw(0,0,255,100);
    layer_state_t default_layer = persistent_default_layer_get();
    access_local_layer()->def_layer = default_layer;
    access_local_state()->unicode_mode = get_unicode_input_mode();
    layer_clear();
    layer_on(default_layer);

    //set these values, they will never change
    set_com_state(is_keyboard_master() ? USB_HOST : BRIDGE);
    set_side(is_keyboard_left() ? LEFT_SIDE : RIGHT_SIDE);


    //encoder pins
    gpio_set_pin_input_high(GP25);
    gpio_set_pin_input_high(GP29);

    //srand(halGetCounterValue());

    reset_overlay_buffers();
    reset_overlay_usage();
    //standard mapping is 1:1
    reset_overlay_mapping();

#ifdef USE_CORE1
    multicore_launch_core1();
#endif

    transaction_register_rpc(USER_SYNC_POLY_DATA,           user_sync_poly_data_handler);
    transaction_register_rpc(USER_SYNC_LAYER_DATA,          user_sync_layer_data_handler);
    transaction_register_rpc(USER_SYNC_LASTKEY_DATA,        user_sync_lastkey_data_handler);
    transaction_register_rpc(USER_SYNC_LATIN_EX_DATA,       user_sync_latin_ex_data_handler);
    transaction_register_rpc(USER_SYNC_OVERLAY_DATA,        user_sync_overlay_data_handler);
    transaction_register_rpc(USER_SYNC_COMPRESSED_DATA,     user_sync_compressed_overlay_data_handler);
    transaction_register_rpc(USER_SYNC_ROI_DATA,            user_sync_roi_data_handler);
    transaction_register_rpc(USER_SYNC_DYNAMIC_KEYMAP_DATA, user_sync_dynamic_keymap_data_handler);
    transaction_register_rpc(USER_SYNC_OVERLAY_MAP_DATA,    user_sync_overlay_map_data_handler);

    poly_eeconf_t ee = load_user_eeconf();
    poly_sync_t* local_state = access_local_state();
    local_state->lang = ee.lang;
    local_state->contrast = ee.brightness;
    local_state->flags = set_flag(STATUS_DISP_ON, RGB_ON, rgb_matrix_is_enabled());

    memcpy(access_global_latin_table()->ex, ee.latin_ex, sizeof(ee.latin_ex));

    set_displays(ee.brightness, false);
}

// Pre-initialization setup: initializes display hardware, loads EEPROM config, shows splash screen.
void keyboard_pre_init_user(void) {
    kdisp_hw_setup();
    kdisp_init(NUM_SHIFT_REGISTERS);
    peripherals_reset();
    kdisp_setup(true);

    select_all_displays();
    kdisp_scroll_vlines(47);
    kdisp_scroll_modehv(true, 3, 1);
    kdisp_scroll(false);

    reset_all_states_and_layers();

    set_displays(50, false);
    set_local_last_latin_keycode(0);
    show_splash_screen();

    gpio_set_pin_input_high(I2C1_SDA_PIN);
}

// Initializes EEPROM configuration with default language, brightness, and latin extension settings.
void eeconfig_init_user(void) {
    uprint("Init EE config\n");
    poly_eeconf_t ee;
    ee.lang = g_lang_init;
    ee.brightness = ~FULL_BRIGHT;
    ee.unused = 0;
    memset(ee.latin_ex, 0, sizeof(ee.latin_ex));
    eeconfig_update_user_datablock(&ee, 0, sizeof(ee));
}

const uint16_t encoder_map[][NUM_ENCODERS][NUM_DIRECTIONS] = {
    [0] =  { ENCODER_CCW_CW(MS_WHLD, MS_WHLU)},
    [1] =  { ENCODER_CCW_CW(MS_WHLD, MS_WHLU)},
    [2] =  { ENCODER_CCW_CW(MS_WHLD, MS_WHLU)},
    [3] =  { ENCODER_CCW_CW(MS_WHLD, MS_WHLU)},
    [4] =  { ENCODER_CCW_CW(MS_WHLD, MS_WHLU)},
    [5] =  { ENCODER_CCW_CW(MS_WHLD, MS_WHLU)},
    [6] =  { ENCODER_CCW_CW(MS_WHLD, MS_WHLU)},
    [7] =  { ENCODER_CCW_CW(MS_WHLD, MS_WHLU)},
    [8] =  { ENCODER_CCW_CW(MS_WHLD, MS_WHLU)},
    [9] =  { ENCODER_CCW_CW(MS_WHLD, MS_WHLU)},
    [10] =  { ENCODER_CCW_CW(MS_WHLD, MS_WHLU)},
    [11] =  { ENCODER_CCW_CW(MS_WHLD, MS_WHLU)},
    [12] =  { ENCODER_CCW_CW(MS_WHLD, MS_WHLU)},
    [13] =  { ENCODER_CCW_CW(MS_WHLD, MS_WHLU)},
};

// Initializes OLED display: turns off, clears buffer, sets scroll speed, shows logos, then enables.
oled_rotation_t oled_init_user(oled_rotation_t rotation){
    oled_off();
    oled_clear();
    oled_render();
    oled_scroll_set_speed(0);
    oled_render_logos();
    oled_on();
    return rotation;
}

// Clears overlay display flags, disables overlays and status display, sets contrast to OFF.
void poly_suspend(void) {
    poly_sync_t* local_state = access_local_state();
    local_state->overlay_flags = flag_off(local_state->overlay_flags, DISPLAY_OVERLAYS);
    local_state->flags &= ~((uint8_t)STATUS_DISP_ON) & ~((uint8_t)DISP_IDLE) & ~((uint8_t)IDLE_TRANSITION);// & ~((uint8_t)RGB_ON);
    local_state->contrast = DISP_OFF;
}

// Suspends keyboard: suspends power down, disables RGB, calls housekeeping, resets update timer.
void suspend_power_down_kb(void) {
    // Master entering the RP2040 ROM bootloader trips USB suspend on the
    // slave a few ms after the sync handler painted the bootloader screen.
    // Without this guard, poly_suspend() + rgb_matrix_disable_noeeprom()
    // immediately wipe the red and queue contrast=0 → the slave goes black.
    // Once BOOTLOADER_DISPLAY is set the only way out is power-cycle, so
    // freezing the suspend path is correct.
    if (get_local_state()->overlay_flags & BOOTLOADER_DISPLAY) {
        return;
    }
    poly_suspend();
    rgb_matrix_disable_noeeprom();
    sync_and_refresh_displays();
    suspend_power_down_user();
    set_last_update(-1);
}


// Resumes keyboard on wakeup: restores display state, brightness, RGB settings, calls housekeeping.
void suspend_wakeup_init_kb(void) {
    poly_sync_t* local_state = access_local_state();
    local_state->flags |= STATUS_DISP_ON;
    local_state->flags &= ~((uint8_t)DISP_IDLE);
    poly_eeconf_t ee = load_user_eeconf();
    local_state->contrast = ee.brightness;
    set_last_update(0);

    //rgb_matrix_reload_from_eeprom();
    if(test_flag(local_state->flags, RGB_ON)) {
        rgb_matrix_enable_noeeprom();
    }

    update_performed();
    housekeeping_task_user();
    suspend_wakeup_init_user();
}
