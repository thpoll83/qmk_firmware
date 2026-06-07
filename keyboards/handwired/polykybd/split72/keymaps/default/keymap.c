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
#include "split_fw_up.h"
#include "base/fw_staging.h"
#include "uni.h"
#include "side.h"
#include "fill_overlay.h"
#include "poly_util.h"

#include "base/com.h"
#include "polymod_rle.h"
#include "base/e2prom.h"
#include "base/overlay.h"
#include "base/disp_array.h"
#include "base/helpers.h"
#include "base/update.h"
#include "base/spi_helper.h"
#include "base/shift_reg.h"
#include "base/text_helper.h"
#include "base/fonts/gfx_used_fonts.h"
#include "base/fonts/flag_fonts.h"        // language-layer country flags (fonts/gen-lang-fonts.sh)
#include "base/fonts/lang_label_font.h"   // tiny label font under the flags
#include "base/multicore/core1.h"
#include "polymod_crc32.h"

#include "state.h"
#include "multicore_exec.h"
#include "split_sync.h"
#include "poly_util.h"

#include "lang/lang_lut.h"
#include "lang/lang_lut_ext.h"

#include "layers.h"
#include "keycode_helper.h"
#include "uni.h"
#include "emoji/emoji_layer.h"

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


// Initializes SPI hardware for display communication after hardware reset.
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
            // Backstop: force red even if rgb_matrix_config.enable gets cleared.
            rgb_matrix_set_color_all(24, 0, 0);
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
    // Freeze slave display while bootloader is active; re-assert RGB each cycle.
    if (!is_usb_host_side() && (get_local_state()->overlay_flags & BOOTLOADER_DISPLAY)) {
#ifdef RGB_MATRIX_ENABLE
        if (!rgb_matrix_is_enabled()) {
            rgb_matrix_enable_noeeprom();
        }
        if (rgb_matrix_get_mode() != RGB_MATRIX_SOLID_COLOR) {
            rgb_matrix_mode_noeeprom(RGB_MATRIX_SOLID_COLOR);
        }
        if (rgb_matrix_get_val() != 24) {
            rgb_matrix_sethsv_noeeprom(0, 255, 24);
        }
#endif
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

        access_local_state()->emj_category = emj_active_category();
        access_local_state()->emj_page     = emj_active_page();
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
    // fw_up state machine: apply on success path, advance deferred erase.
    // Both must run regardless of fw_up_active so the slave's erase actually
    // progresses and the master's apply-and-reboot fires after a successful
    // commit.
    if (fw_staging_commit_pending()) {
        fw_staging_apply_and_reboot();
    }
    if (fw_staging_reboot_pending()) {
        mcu_reset();   // QK_REBOOT slave path — clean full-chip reset; never returns
    }
    fw_staging_process_deferred();

    // While a fw_up is in progress, skip EEPROM saves (wear-leveling consolidate
    // is ~100 ms IRQ-off) and the display refresh path (slave update_displays
    // can be ~50-100 ms over SPI, master state-push uses 10 retries × 80 ms).
    // Both would starve the split UART that the chunk transport relies on.
    if (!fw_staging_fw_up_active()) {
        brightness_save_if_pending();
        default_layer_save_if_pending();
        sync_and_refresh_displays();
    }
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
        KC_LSFT,    KC_Z,       KC_X,       KC_C,       KC_V,       KC_B,       TO(_EMJ),   MO(_NL),
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
        KC_LSFT,    TO(_EMJ),   KC_Z,       KC_X,       KC_C,       KC_V,       KC_B,       MO(_NL),
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
        KC_LSFT,    KC_Z,       KC_X,       KC_C,       KC_D,       KC_V,       TO(_EMJ),    MO(_NL),
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
        MO(_FL1),   KC_A,       KC_S,       KC_H,       KC_T,       KC_G,       TO(_EMJ),     MS_BTN1,
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
        # Sort country-first then language so same-country variants (e.g. hi-IN, mr-IN) stay adjacent.
        slang = sorted(languages, key=lambda c: (c[2:], c[:2]))
        # 6 language slots per row x 10 rows = 60 capacity (rows 1-4 of each half
        # plus the bottom row of each half - lines[8]=left row5, lines[9]=right row5).
        lines = []
        for lidx in range(0, 10):
            line = ""
            for idx in range(0, 6):
                if (lidx*6+idx)>=len(slang):
                    line = f"{line}KC_NO,\t\t"
                else:
                    line = f'{line}KCL_{slang[(lidx*6+idx)].upper()},\t'
            lines.append(line)
        cog.outl(f"KC_NO,\t\t\t\t\t\t\t{lines[0]}");
        cog.outl(f"KC_NO,\t\t\t\t\t\t\t{lines[1]}");
        cog.outl(f"QK_UNICODE_MODE_WINCOMPOSE,\t\t{lines[2]}\tMS_BTN1,");
        cog.outl(f"QK_UNICODE_MODE_EMACS,\t\t\t{lines[3]}\tKC_NO,");
        cog.outl(f"KC_BASE,\t\t\t\t\t\t{lines[8]}");
        cog.outl("")
        cog.outl(f"\t\t\t\t\t{lines[4]}QK_UNICODE_MODE_MACOS,");
        cog.outl(f"\t\t\t\t\t{lines[5]}QK_UNICODE_MODE_LINUX,");
        cog.outl(f"_______,\t\t\t{lines[6]}QK_UNICODE_MODE_WINDOWS,");
        cog.outl(f"KC_NO,\t\t\t\t{lines[7]}QK_UNICODE_MODE_BSD,");
        cog.outl(f"{lines[9]}KC_BASE");
        ]]]*/
        KC_NO,							KCL_HYAM,	KCL_AZAZ,	KCL_FRBE,	KCL_BGBG,	KCL_PTBR,	KCL_BEBY,	
        KC_NO,							KCL_FRCA,	KCL_DECH,	KCL_ZHCN,	KCL_CSCZ,	KCL_DEDE,	KCL_DADK,	
        QK_UNICODE_MODE_WINCOMPOSE,		KCL_ETEE,	KCL_ESES,	KCL_FIFI,	KCL_FRFR,	KCL_ENGB,	KCL_KAGE,		MS_BTN1,
        QK_UNICODE_MODE_EMACS,			KCL_ELGR,	KCL_HRHR,	KCL_HUHU,	KCL_IDID,	KCL_HEIL,	KCL_BNIN,		KC_NO,
        KC_BASE,						KCL_ARSA,	KCL_SVSE,	KCL_SKSK,	KCL_THTH,	KCL_TRTR,	KCL_ZHTW,	

        					KCL_HIIN,	KCL_MRIN,	KCL_TAIN,	KCL_TEIN,	KCL_FAIR,	KCL_ISIS,	QK_UNICODE_MODE_MACOS,
        					KCL_ITIT,	KCL_JAJP,	KCL_KOKR,	KCL_KKKZ,	KCL_LTLT,	KCL_LVLV,	QK_UNICODE_MODE_LINUX,
        _______,			KCL_MKMK,	KCL_MNMN,	KCL_ESMX,	KCL_NLNL,	KCL_NNNO,	KCL_NENP,	QK_UNICODE_MODE_WINDOWS,
        KC_NO,				KCL_URPK,	KCL_PLPL,	KCL_PTPT,	KCL_RORO,	KCL_SRRS,	KCL_RURU,	QK_UNICODE_MODE_BSD,
        KCL_UKUA,	KCL_ENUS,	KCL_VIVN,	KC_NO,		KC_NO,		KC_NO,		KC_BASE
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
    [_EMJ] = LAYOUT_left_right_stacked(
        KC_EMJ_PAGE_PREV, KC_EMJ_CAT(0),  KC_EMJ_CAT(1),  KC_EMJ_CAT(2),  KC_EMJ_CAT(3),  KC_EMJ_CAT(4),  KC_EMJ_CAT(5),
        KC_NO,      ESLOT(0),       ESLOT(1),       ESLOT(2),       ESLOT(3),       ESLOT(4),       ESLOT(5),
        KC_NO,      ESLOT(12),      ESLOT(13),      ESLOT(14),      ESLOT(15),      ESLOT(16),      ESLOT(17),      KC_NO,
        KC_NO,      ESLOT(24),      ESLOT(25),      ESLOT(26),      ESLOT(27),      ESLOT(28),      ESLOT(29),      ESLOT(30),
        KC_BASE,    ESLOT(38),      ESLOT(39),      ESLOT(40),                      ESLOT(41),      ESLOT(42),      ESLOT(43),

                    KC_EMJ_CAT(6),  KC_EMJ_CAT(7),  KC_EMJ_CAT(8),  KC_EMJ_CAT(9),  KC_EMJ_CAT(10), KC_EMJ_CAT(11), KC_EMJ_PAGE_NEXT,
                    ESLOT(6),       ESLOT(7),       ESLOT(8),       ESLOT(9),       ESLOT(10),      ESLOT(11),      KC_NO,
        KC_NO,      ESLOT(18),      ESLOT(19),      ESLOT(20),      ESLOT(21),      ESLOT(22),      ESLOT(23),      KC_NO,
        ESLOT(31),  ESLOT(32),      ESLOT(33),      ESLOT(34),      ESLOT(35),      ESLOT(36),      ESLOT(37),      KC_NO,
        ESLOT(44),  ESLOT(45),      ESLOT(46),                      ESLOT(47),      ESLOT(48),      ESLOT(49),      KC_BASE
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
// Placed in .rodata so the 296-byte table sits in flash rather than RAM.
// QMK only reads g_led_config (verified in quantum/{led,rgb}_matrix/*.c); the
// type stays non-const to match the upstream extern declaration in
// quantum/rgb_matrix/rgb_matrix.h, so this is a placement override only.
__attribute__((section(".rodata"))) led_config_t g_led_config = { {// Key Matrix to LED Index
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
const uint32_t* to_static_text(uint16_t keycode, led_t state) {

    const uint32_t *emj = emj_display_text(keycode);
    if (emj != NULL) return emj;

    const uint32_t* emoji = keycode_to_emoji(keycode);
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
            case KC_NUM_LOCK: return U"";
            case KC_KP_7:     return U"7";
            case KC_KP_8:     return U"8";
            case KC_KP_9:     return U"9";
            case KC_KP_4:     return U"4";
            case KC_KP_5:     return U"5";
            case KC_KP_6:     return U"6";
            case KC_KP_1:     return U"1";
            case KC_KP_2:     return U"2";
            case KC_KP_3:     return U"3";
            case KC_KP_0:     return U"0";
            case KC_KP_DOT:   return U".";
            default: break;
        }
    }
#endif

    const uint32_t* text = keycode_to_static_text(keycode, state, local_state->flags);
    if(text!=NULL) {
        return text;
    }

    const poly_layer_t* local_layer = get_local_layer();
    switch (keycode) {
        case QK_UNICODE_MODE_MACOS:         return local_state->unicode_mode == UNICODE_MODE_MACOS ? U"Mac\r\v" ICON_SWITCH_ON : U"Mac\r\v" ICON_SWITCH_OFF;
        case QK_UNICODE_MODE_LINUX:         return local_state->unicode_mode == UNICODE_MODE_LINUX ? U"Lnx\r\v" ICON_SWITCH_ON : U"Lnx\r\v" ICON_SWITCH_OFF;
        case QK_UNICODE_MODE_WINDOWS:       return local_state->unicode_mode == UNICODE_MODE_WINDOWS ? U"Win\r\v" ICON_SWITCH_ON : U"Win\r\v" ICON_SWITCH_OFF;
        case QK_UNICODE_MODE_BSD:           return local_state->unicode_mode == UNICODE_MODE_BSD ? U"BSD\r\v" ICON_SWITCH_ON : U"BSD\r\v" ICON_SWITCH_OFF;
        case QK_UNICODE_MODE_WINCOMPOSE:    return local_state->unicode_mode == UNICODE_MODE_WINCOMPOSE ? U"WinC\r\v" ICON_SWITCH_ON : U"WinC\r\v" ICON_SWITCH_OFF;
        case QK_UNICODE_MODE_EMACS:         return local_state->unicode_mode == UNICODE_MODE_EMACS ? U"Emcs\r\v" ICON_SWITCH_ON : U"Emcs\r\v" ICON_SWITCH_OFF;
        case KC_L0:                         return local_layer->def_layer == _L0 ? U"Qwty\r\v" ICON_SWITCH_ON : U"Qwty\r\v" ICON_SWITCH_OFF;
        case KC_L1:                         return local_layer->def_layer == _L1 ? U"Qwty!\r\v" ICON_SWITCH_ON : U"Qwty!\r\v" ICON_SWITCH_OFF;
        case KC_L2:                         return local_layer->def_layer == _L2 ? U"Clmk\r\v" ICON_SWITCH_ON : U"Clmk\r\v" ICON_SWITCH_OFF;
        case KC_L3:                         return local_layer->def_layer == _L3 ? U"Neo\r\v" ICON_SWITCH_ON : U"Neo\r\v" ICON_SWITCH_OFF;
        case KC_L4:                         return local_layer->def_layer == _L4 ? U"Wkm\r\v" ICON_SWITCH_ON : U"Wkm\r\v" ICON_SWITCH_OFF;

        //Language selection keycodes
        // The flag + selection frame are drawn by render_lang_flag_key(); here we
        // only return the tiny language code shown under the flag.
        /*[[[cog
        for lang in languages:
            cog.outl(f'case KCL_{lang.upper()}: return U"{lang[0:2]}-{lang[2:]}";')
        ]]]*/
        case KCL_ENUS: return U"en-US";
        case KCL_DEDE: return U"de-DE";
        case KCL_FRFR: return U"fr-FR";
        case KCL_ESES: return U"es-ES";
        case KCL_PTPT: return U"pt-PT";
        case KCL_ITIT: return U"it-IT";
        case KCL_TRTR: return U"tr-TR";
        case KCL_KOKR: return U"ko-KR";
        case KCL_JAJP: return U"ja-JP";
        case KCL_ARSA: return U"ar-SA";
        case KCL_ELGR: return U"el-GR";
        case KCL_UKUA: return U"uk-UA";
        case KCL_RURU: return U"ru-RU";
        case KCL_BEBY: return U"be-BY";
        case KCL_KKKZ: return U"kk-KZ";
        case KCL_BGBG: return U"bg-BG";
        case KCL_PLPL: return U"pl-PL";
        case KCL_RORO: return U"ro-RO";
        case KCL_ZHCN: return U"zh-CN";
        case KCL_NLNL: return U"nl-NL";
        case KCL_HEIL: return U"he-IL";
        case KCL_SVSE: return U"sv-SE";
        case KCL_FIFI: return U"fi-FI";
        case KCL_NNNO: return U"nn-NO";
        case KCL_DADK: return U"da-DK";
        case KCL_HUHU: return U"hu-HU";
        case KCL_CSCZ: return U"cs-CZ";
        case KCL_HRHR: return U"hr-HR";
        case KCL_SKSK: return U"sk-SK";
        case KCL_LTLT: return U"lt-LT";
        case KCL_LVLV: return U"lv-LV";
        case KCL_ETEE: return U"et-EE";
        case KCL_PTBR: return U"pt-BR";
        case KCL_SRRS: return U"sr-RS";
        case KCL_MKMK: return U"mk-MK";
        case KCL_FAIR: return U"fa-IR";
        case KCL_HIIN: return U"hi-IN";
        case KCL_MRIN: return U"mr-IN";
        case KCL_NENP: return U"ne-NP";
        case KCL_MNMN: return U"mn-MN";
        case KCL_URPK: return U"ur-PK";
        case KCL_ENGB: return U"en-GB";
        case KCL_ESMX: return U"es-MX";
        case KCL_DECH: return U"de-CH";
        case KCL_FRBE: return U"fr-BE";
        case KCL_FRCA: return U"fr-CA";
        case KCL_THTH: return U"th-TH";
        case KCL_BNIN: return U"bn-IN";
        case KCL_TEIN: return U"te-IN";
        case KCL_TAIN: return U"ta-IN";
        case KCL_ZHTW: return U"zh-TW";
        case KCL_KAGE: return U"ka-GE";
        case KCL_HYAM: return U"hy-AM";
        case KCL_IDID: return U"id-ID";
        case KCL_AZAZ: return U"az-AZ";
        case KCL_ISIS: return U"is-IS";
        case KCL_VIVN: return U"vi-VN";
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

        const uint32_t* def_variation = latin_ex_map[offset+keycode-KC_A][0];
        if(def_variation!=NULL) {
            kdisp_write_gfx_text(ALL_FONTS, ALL_FONT_SIZE, BUFFER_X, 23, latin_ex_map[offset+keycode-KC_A][variation]);
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
            const uint32_t* variation = latin_ex_map[offset+local_last_latin_keycode-KC_A][keycode-KC_LAT0];
            if(variation!=NULL) {
                kdisp_write_gfx_text(ALL_FONTS, ALL_FONT_SIZE, BUFFER_X, 23, variation);
                return true;
            }
        }
        return false;
    }

    const poly_sync_t* local_state = get_local_state();
    if (mods & MOD_RALT) {
        const uint32_t* letter = translate_keycode_only_altgr(local_state->lang, keycode);
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
    const uint32_t* letter = translate_keycode(local_state->lang, keycode, shift, state.caps_lock);
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
        int8_t v_small = get_setting(v_set, local_state->lang, VAR_SMALL);
        int8_t h_small = get_setting(h_set, local_state->lang, VAR_SMALL);
        int8_t base_x = 28+h_small;
        int8_t base_v = v_small;

        // Resolve the shift preview BEFORE drawing, so a wide base + wide preview
        // (e.g. the Arabic SAD/DAD key — both ~39 px) can be laid out as a pair:
        // the preview is placed clear of the base and clamped on screen; if it then
        // still has to overlap (two such glyphs cannot both fit a 72 px window) the
        // flat base is lifted and the preview dropped so the two read diagonally
        // instead of as one connected glyph.  Only this unshifted preview view is
        // affected — when shift is held there is no preview and the active glyph
        // keeps the normal VAR_SMALL baseline, so tall letters / high marks never
        // clip.  All of this is generic and glyph-width driven (no per-language code).
        const uint32_t* shift_letter = NULL;
        int8_t preview_x = 0, preview_v = 0;
        if(!shift && !state.caps_lock) {
            int8_t v_pv = get_setting(v_set, local_state->lang, VAR_SHIFT);
            int8_t h_pv = get_setting(h_set, local_state->lang, VAR_SHIFT);
            if(v_pv!=HIDE_KEY && h_pv!=HIDE_KEY) {
                shift_letter = translate_keycode_only_shift(local_state->lang, keycode);
                if (shift_letter != NULL) {
                    int8_t bmin, bmax, pmin, pmax;
                    kdisp_gfx_text_bounds(ALL_FONTS, ALL_FONT_SIZE, letter, &bmin, &bmax);
                    kdisp_gfx_text_bounds(ALL_FONTS, ALL_FONT_SIZE, shift_letter, &pmin, &pmax);
                    preview_x = 28+h_pv;
                    if (preview_x + pmin < base_x + bmax + 2)             // keep clear of the base
                        preview_x = base_x + bmax + 2 - pmin;
                    if (preview_x + pmax > BUFFER_X + SCREEN_WIDTH - 1)   // clamp to the right edge
                        preview_x = (BUFFER_X + SCREEN_WIDTH - 1) - pmax;
                    preview_v = v_pv;
                    if (preview_x + pmin <= base_x + bmax) {              // forced to overlap -> stagger
                        base_v    -= 6;                                   // lift the flat base
                        preview_v += 4;                                   // drop the preview
                    }
                }
            }
        }

        kdisp_write_gfx_text(ALL_FONTS, ALL_FONT_SIZE, base_x, 23+base_v, letter);
        if (shift_letter != NULL)
            kdisp_write_gfx_text(ALL_FONTS, ALL_FONT_SIZE, preview_x, 23+preview_v, shift_letter);

        //preview alt representation
        letter = translate_keycode_only_altgr(local_state->lang, keycode);
        if (letter != NULL) {
            int8_t v_off = get_setting(v_set, local_state->lang, VAR_ALTGR);
            int8_t h_off = get_setting(h_set, local_state->lang, VAR_ALTGR);
            if(v_off!=HIDE_KEY && h_off!=HIDE_KEY) {
                kdisp_write_gfx_text(ALL_FONTS, ALL_FONT_SIZE, 28+h_off, 23+v_off, letter);
            }
        }
        return true;
    }
    return false;
}

// Returns builtin icon/symbol overlay text for keycode based on current modifiers and mod-tap states.
const uint32_t* keycode_to_disp_overlay(uint16_t keycode, led_t state) {
    switch (keycode)
    {
        case KC_F2: return U"      " PRIVATE_NOTE;
        case KC_F5: return U"     " ARROWS_CIRCLE;
        default: break;
    }

    uint8_t local_mods = get_local_layer()->mods;
    if( (local_mods & MOD_MASK_CTRL) != 0) {
        switch(keycode) {
            case KC_A: return U"      " BOX_WITH_CHECK_MARK;
            case KC_C: return U"     " CLIPBOARD_COPY;
            case KC_D: return U"\t " PRIVATE_DELETE;
            case KC_F: return U"    " PRIVATE_FIND;
            case KC_X: return U"\t\b\b" CLIPBOARD_CUT;
            case KC_V: return U"     " CLIPBOARD_PASTE;
            case KC_S: return U"\t" PRIVATE_FLOPPY;
            case KC_O: return U"\t" FILE_OPEN;
            case KC_P: return U"\t" PRIVATE_PRINTER;
            case KC_Z: return U"      " ARROWS_UNDO;
            case KC_Y: return U"      " ARROWS_REDO;
            default: break;
        }
    } else if((local_mods & MOD_MASK_GUI) != 0) {
        switch(keycode) {
            case KC_D:      return U"    " PRIVATE_PC;
            case KC_L:      return U"    " PRIVATE_LOCK;
            case KC_P:      return U"    " PRIVATE_SCREEN;
            case KC_UP:     return U"     " PRIVATE_MAXIMIZE;
            case KC_DOWN:   return U"     " PRIVATE_WINDOW;
            default: break;
        }
    }

    if(IS_QK_MOD_TAP(keycode)) {
        uint8_t mods = QK_MOD_TAP_GET_MODS(keycode);
        if((mods & MOD_MASK_CSAG) == MOD_MASK_CSAG) {
            return U"    " CURRENCY_SIGN ICON_SHIFT NOT_SIGN KATAKANA_MIDDLE_DOT;
        } else if((mods & MOD_MASK_SAG) == MOD_MASK_SAG) {
            return U"    " ICON_SHIFT NOT_SIGN KATAKANA_MIDDLE_DOT;
        } else if((mods & MOD_MASK_CAG) == MOD_MASK_CAG) {
            return U"    " CURRENCY_SIGN NOT_SIGN KATAKANA_MIDDLE_DOT;
        } else if((mods & MOD_MASK_CSG) == MOD_MASK_CSG) {
            return U"    " CURRENCY_SIGN ICON_SHIFT KATAKANA_MIDDLE_DOT;
        } else if((mods & MOD_MASK_CSA) == MOD_MASK_CSA) {
            return U"    " CURRENCY_SIGN ICON_SHIFT NOT_SIGN;
        } else if((mods & MOD_MASK_AG) == MOD_MASK_AG) {
            return U"    " NOT_SIGN KATAKANA_MIDDLE_DOT;
        } else if((mods & MOD_MASK_SG) == MOD_MASK_SG) {
            return U"    " ICON_SHIFT KATAKANA_MIDDLE_DOT;
        } else if((mods & MOD_MASK_SA) == MOD_MASK_SA) {
            return U"    " CURRENCY_SIGN NOT_SIGN;
        } else if((mods & MOD_MASK_CG) == MOD_MASK_CG) {
            return U"    " CURRENCY_SIGN KATAKANA_MIDDLE_DOT;
        } else if((mods & MOD_MASK_CA) == MOD_MASK_CA) {
            return U"    " CURRENCY_SIGN NOT_SIGN;
        } else if((mods & MOD_MASK_CS) == MOD_MASK_CS) {
            return U"    " CURRENCY_SIGN ICON_SHIFT;
        } else if(mods & MOD_MASK_CTRL) {
            return U"    " CURRENCY_SIGN;
        } else if(mods & MOD_MASK_ALT) {
            return U"    " NOT_SIGN;
        } else if (mods & MOD_MASK_SHIFT) {
            return U"    " ICON_SHIFT;
        } else {
            return U"   " KATAKANA_MIDDLE_DOT;
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
// ─── Language-layer flag rendering ──────────────────────────────────────────
// Each KCL_* key in the language layer (_LL) shows its country flag at full
// keycap height on the left, with the language code (e.g. U"en-US") running
// vertically up the right side in a tiny font.  The currently-selected language's
// code is drawn as dark text on a filled bar (see kdisp_write_gfx_vtext) rather
// than with a frame.  Flag glyphs live in flag_fonts.h at codepoints
// FLAG_CP_BASE + LANG_* (see fonts/gen-lang-fonts.sh); the label uses the tiny
// NotoSans font.
#define FLAG_CP_BASE   0xE000u
#define FLAG_LEFT_X    (BUFFER_X - 2)    // flag glyph left (keeps the left border on-screen)
#define LABEL_COL_X    (BUFFER_X + 66)   // baseline column of the vertical label

static const GFXfont* const lang_flag_fonts[] = { &NotoColorEmoji_Regular_LangFlags_20pt7b };

// Draw one language key: oversized country flag on the left (vertically centred
// and clipped so the flag content fills the keycap height), language code running
// vertically up the right side (inverted bar when it is the active language).
static void render_lang_flag_key(uint16_t keycode, const uint32_t* label, uint8_t current_lang) {
    const uint8_t  idx = (uint8_t)(keycode - KCL_ENUS);   // == LANG_* enum value
    const GFXfont* ff  = &NotoColorEmoji_Regular_LangFlags_20pt7b;

    // Flag: the glyph is taller than the keycap, so centre it vertically — the
    // empty top/bottom margins clip off and the flag content fills the height.
    const int8_t fh  = (int8_t)pgm_read_byte(&ff->glyph[idx].height);
    const int8_t fyo = (int8_t)pgm_read_byte(&ff->glyph[idx].yOffset);
    kdisp_write_gfx_char(lang_flag_fonts, 1, FLAG_LEFT_X,
                         (int8_t)((SCREEN_HEIGHT - fh) / 2 - fyo),
                         FLAG_CP_BASE + idx, false);

    // Language code: vertical, up the right side; inverted bar when selected.
    kdisp_write_gfx_vtext(&NotoSans_Regular_Tiny_6pt7b, LABEL_COL_X, label,
                          current_lang == idx);
}

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
                        if (keycode >= KCL_ENUS && keycode < KCL_ENUS + NUM_LANG) {
                            // Language layer: country flag + tiny language code.
                            kdisp_set_buffer(0x00);
                            render_lang_flag_key(keycode, to_static_text(keycode, state), local_state->lang);
                            kdisp_send_buffer();
                        } else {
                        const uint32_t* text = to_static_text(keycode, state);
                        kdisp_set_buffer(0x00);
                        if(text==NULL) {
                            if(!render_key(keycode, state, mods) && (keycode&QK_UNICODEMAP_PAIR)==QK_UNICODEMAP_PAIR){
                                uint16_t chr = capital_case ? QK_UNICODEMAP_PAIR_GET_SHIFTED_INDEX(keycode) : QK_UNICODEMAP_PAIR_GET_UNSHIFTED_INDEX(keycode);
                                kdisp_write_gfx_char(ALL_FONTS, ALL_FONT_SIZE, BUFFER_X, 23, unicode_map[chr], false);
                            }
                        } else {
                            kdisp_write_gfx_text(ALL_FONTS, ALL_FONT_SIZE, BUFFER_X, 23, text);
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
                            kdisp_write_gfx_text_cy(ALL_FONTS, ALL_FONT_SIZE, BUFFER_X, 23, text, true);
                        }
                        emj_draw_tab_indicator(keycode);
                        emj_draw_tab_bottom(keycode);
                        kdisp_send_buffer();
                        }
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

    if (emj_process_keycode(keycode, record->event.pressed)) return false;

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
            case QK_BOOTLOADER: {
                // Shared with the host-triggered bootloader HID command (hid_com.c case 23).
                poly_announce_bootloader();
                return true;
            }
            case QK_REBOOT: {
                uprintf("Reboot requested — rebooting both halves.\n");
                // Reboot the slave too, so both halves restart together (like a
                // replug).  A master-only reset leaves the slave running stale →
                // the rebooted master can't re-sync to it and hangs on the boot
                // splash.  QMK resets the master right after we return true (before
                // housekeeping runs again), so the slave must be told here.
                fw_up_apply_sync_t reboot_msg = { .crc32 = 0, .magic = FW_UP_SYNC_MAGIC };
                uint8_t ack = send_to_bridge(USER_SYNC_REBOOT, &reboot_msg, sizeof(reboot_msg), 5);
                uprintf("Master: slave reboot ack=%d\n", ack);
                return true;   // let QMK's QK_REBOOT handler reset the master
            }
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
        case KCL_HRHR: local_state->lang = LANG_HRHR; save_user_settings(); layer_off(_LL); break;
        case KCL_SKSK: local_state->lang = LANG_SKSK; save_user_settings(); layer_off(_LL); break;
        case KCL_LTLT: local_state->lang = LANG_LTLT; save_user_settings(); layer_off(_LL); break;
        case KCL_LVLV: local_state->lang = LANG_LVLV; save_user_settings(); layer_off(_LL); break;
        case KCL_ETEE: local_state->lang = LANG_ETEE; save_user_settings(); layer_off(_LL); break;
        case KCL_PTBR: local_state->lang = LANG_PTBR; save_user_settings(); layer_off(_LL); break;
        case KCL_SRRS: local_state->lang = LANG_SRRS; save_user_settings(); layer_off(_LL); break;
        case KCL_MKMK: local_state->lang = LANG_MKMK; save_user_settings(); layer_off(_LL); break;
        case KCL_FAIR: local_state->lang = LANG_FAIR; save_user_settings(); layer_off(_LL); break;
        case KCL_HIIN: local_state->lang = LANG_HIIN; save_user_settings(); layer_off(_LL); break;
        case KCL_MRIN: local_state->lang = LANG_MRIN; save_user_settings(); layer_off(_LL); break;
        case KCL_NENP: local_state->lang = LANG_NENP; save_user_settings(); layer_off(_LL); break;
        case KCL_MNMN: local_state->lang = LANG_MNMN; save_user_settings(); layer_off(_LL); break;
        case KCL_URPK: local_state->lang = LANG_URPK; save_user_settings(); layer_off(_LL); break;
        case KCL_ENGB: local_state->lang = LANG_ENGB; save_user_settings(); layer_off(_LL); break;
        case KCL_ESMX: local_state->lang = LANG_ESMX; save_user_settings(); layer_off(_LL); break;
        case KCL_DECH: local_state->lang = LANG_DECH; save_user_settings(); layer_off(_LL); break;
        case KCL_FRBE: local_state->lang = LANG_FRBE; save_user_settings(); layer_off(_LL); break;
        case KCL_FRCA: local_state->lang = LANG_FRCA; save_user_settings(); layer_off(_LL); break;
        case KCL_THTH: local_state->lang = LANG_THTH; save_user_settings(); layer_off(_LL); break;
        case KCL_BNIN: local_state->lang = LANG_BNIN; save_user_settings(); layer_off(_LL); break;
        case KCL_TEIN: local_state->lang = LANG_TEIN; save_user_settings(); layer_off(_LL); break;
        case KCL_TAIN: local_state->lang = LANG_TAIN; save_user_settings(); layer_off(_LL); break;
        case KCL_ZHTW: local_state->lang = LANG_ZHTW; save_user_settings(); layer_off(_LL); break;
        case KCL_KAGE: local_state->lang = LANG_KAGE; save_user_settings(); layer_off(_LL); break;
        case KCL_HYAM: local_state->lang = LANG_HYAM; save_user_settings(); layer_off(_LL); break;
        case KCL_IDID: local_state->lang = LANG_IDID; save_user_settings(); layer_off(_LL); break;
        case KCL_AZAZ: local_state->lang = LANG_AZAZ; save_user_settings(); layer_off(_LL); break;
        case KCL_ISIS: local_state->lang = LANG_ISIS; save_user_settings(); layer_off(_LL); break;
        case KCL_VIVN: local_state->lang = LANG_VIVN; save_user_settings(); layer_off(_LL); break;
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
        display_message(1, 1, U"POLY", &FreeSansBold24pt7b);
        display_message(2, 1, U"KYBD", &FreeSansBold24pt7b);
    } else {
        display_message(1, 1, U"SPLIT", &FreeSansBold24pt7b);
        display_message(3, 1, U" 7 2", &FreeSansBold24pt7b);
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

#ifdef FW_UP_BOOT_TRACE
// Diagnostic only (build with -DFW_UP_BOOT_TRACE): overwrite the keycaps with a
// single digit at boot milestones so a hang in early boot is visible — the last
// digit shown on each half tells us how far that half got before it stopped.
static void boot_trace(const uint32_t* digit) {
    clear_all_displays();
    display_message(1, 1, digit, &FreeSansBold24pt7b);
}
#endif

// Initializes keyboard state after reset: enables debug, sets CPI, loads layer/unicode defaults.
// Global variables: com
void keyboard_post_init_user(void) {
#ifdef FW_UP_BOOT_TRACE
    boot_trace(U"1");
#endif
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

    emj_init();

    reset_overlay_buffers();
    reset_overlay_usage();
    //standard mapping is 1:1
    reset_overlay_mapping();

#ifdef FW_UP_BOOT_TRACE
    boot_trace(U"2");
#endif
#ifdef USE_CORE1
    multicore_launch_core1();
#endif
#ifdef FW_UP_BOOT_TRACE
    boot_trace(U"3");
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
    transaction_register_rpc(USER_SYNC_FW_UP_QUERY,         user_sync_fw_up_query_handler);
    transaction_register_rpc(USER_SYNC_FW_UP_BEGIN,         user_sync_fw_up_begin_handler);
    transaction_register_rpc(USER_SYNC_FW_UP_CHUNK,         user_sync_fw_up_chunk_handler);
    transaction_register_rpc(USER_SYNC_FW_UP_COMMIT,        user_sync_fw_up_commit_handler);
    transaction_register_rpc(USER_SYNC_FW_UP_STATUS,        user_sync_fw_up_status_handler);
    transaction_register_rpc(USER_SYNC_FW_UP_APPLY,         user_sync_fw_up_apply_handler);
    transaction_register_rpc(USER_SYNC_REBOOT,              user_sync_reboot_handler);

    fw_staging_init();

    poly_eeconf_t ee = load_user_eeconf();
    poly_sync_t* local_state = access_local_state();
    local_state->lang = ee.lang;
    local_state->contrast = ee.brightness;
    local_state->flags = set_flag(STATUS_DISP_ON, RGB_ON, rgb_matrix_is_enabled());

    memcpy(access_global_latin_table()->ex, ee.latin_ex, sizeof(ee.latin_ex));

    set_displays(ee.brightness, false);
#ifdef FW_UP_BOOT_TRACE
    boot_trace(U"4");
#endif
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
    // Resolve the side BEFORE the splash so each half shows its own logo
    // (left = "POLY KYBD", right = "SPLIT 72") instead of both showing the
    // right-side text.  set_side() otherwise runs only in post_init, after the
    // splash, so the splash always saw side == UNDECIDED → both rendered "SPLIT 72".
    //
    // Read handedness with the pure eeconfig_read_handedness(), NOT
    // is_keyboard_left_impl(): the EE_HANDS branch of is_keyboard_left_impl() runs
    // `if (!eeconfig_is_enabled()) eeconfig_init();`.  Called this early — right
    // after eeprom_driver_init() in keyboard_setup, before the wear-leveling store
    // is validated — it can see eeconfig as "not enabled" and run eeconfig_init()
    // → nvm_eeconfig_erase() → eeprom_driver_format(), which wipes the *entire*
    // emulated EEPROM including the per-half EE_HANDS marker.  Both halves then
    // lose their stored side and fall back to a master-derived handedness.
    // eeprom_driver_init() has already run, so the direct read is valid here and,
    // being read-only, can never trigger that erase.
    set_side(eeconfig_read_handedness() ? LEFT_SIDE : RIGHT_SIDE);
    show_splash_screen();
#ifdef FW_UP_BOOT_TRACE
    boot_trace(U"0");
#endif

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
    // USB suspend fires on slave when master enters bootloader; skip to keep displays lit.
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
