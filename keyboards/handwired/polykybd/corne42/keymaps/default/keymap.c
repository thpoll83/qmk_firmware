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
#include "corne42/corne42.h"
#include "corne42/status_oled.h"
#include "bridge_helper.h"
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
void rgb_matrix_update_pwm_buffers(void);
#endif

#define FLASH_TARGET_OFFSET (4 * 1024 * 1024)
const uint8_t *flash_target_contents = (const uint8_t *) (XIP_BASE + FLASH_TARGET_OFFSET);
static_assert(FLASH_PAGE_SIZE==256, "Flash page size changed");

static enum lang_layer g_lang_init = INIT_LANG;

/* disp_row_0 selects chain position 0 (row 0, col 0).
   disp_row_3 selects chain position 18 (first thumb key, row 3 col 3).
   Both must match key_display[] in corne42.c — update if PCB wiring changes. */
const struct display_info disp_row_0 = { BITMASK1(0) };
const struct display_info disp_row_3 = { BITMASK3(2) };


bool display_wakeup(keyrecord_t* record);
void update_displays(enum refresh_mode mode);
void set_displays(uint8_t contrast, bool idle);
void set_selected_displays(int8_t old_value, int8_t new_value);
void oled_update_buffer(void);
void poly_suspend(void);

void early_hardware_init_post(void) {
    spi_hw_setup();
}

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

static uint8_t flags = 0;
static uint8_t overlay_flags = 0;

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

void sync_and_refresh_displays(void) {
    // Freeze slave display while bootloader is active.
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
            flags=local_flags;
        }
        if(overlay_flags!=local_overlay_flags) {
            overlay_flags=local_overlay_flags;
        }

        access_local_state()->emj_category = emj_active_category();
        access_local_state()->emj_page     = emj_active_page();
        state_diff = differ(get_local_state(), get_global_state(), sizeof(poly_sync_t));
        if ( state_diff ) {
            if(!send_to_bridge(USER_SYNC_POLY_DATA, (void *)access_local_state(), sizeof(poly_sync_t), 10)) {
                state_diff = false;
                uprint("USER_SYNC_POLY_DATA failed to send\n");
            }
        }

        access_local_layer()->led_state = host_keyboard_led_state();
        access_local_layer()->mods = get_mods();
        layer_diff = differ(get_local_layer(), get_global_layer(), sizeof(poly_layer_t));
        if ( layer_diff ) {
            if(!send_to_bridge(USER_SYNC_LAYER_DATA, (void *)access_local_layer(), sizeof(poly_layer_t), 10)) {
                layer_diff = false;
                uprint("USER_SYNC_LAYER_DATA failed to send\n");
            }
        }
        if ( differ(get_local_last_latin(), get_global_last_latin(), sizeof(poly_last_t)) ) {
            if(!send_to_bridge(USER_SYNC_LASTKEY_DATA, access_local_last_latin(), sizeof(poly_last_t), 5)) {
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
#ifdef RGB_MATRIX_ENABLE
                if(test_flag(local_flags, RGB_ON)) {
                    rgb_matrix_enable_noeeprom();
                }
#endif
            } else {
                oled_off();
#ifdef RGB_MATRIX_ENABLE
                rgb_matrix_set_color_all(0, 0, 0);
                rgb_matrix_update_pwm_buffers();
                rgb_matrix_disable_noeeprom();
#endif
            }
        }

#ifdef RGB_MATRIX_ENABLE
        if(has_flag_changed(local_flags, global_flags, RGB_ON)) {
            if (test_flag(local_flags, RGB_ON)) {
                rgb_matrix_enable();
            } else {
                rgb_matrix_disable();
            }
        }
#endif

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

layer_state_t layer_state_set_user(layer_state_t state) {
    access_local_layer()->layer = state;
    return state;
}

void housekeeping_task_user(void) {
    brightness_save_if_pending();
    default_layer_save_if_pending();
    sync_and_refresh_displays();
    int32_t update = get_last_update();
    if(update>=0) {
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
    /*
     * Base layer 0 — Qwerty
     *
     * ┌─────┬────┬────┬────┬────┬────┐               ┌────┬────┬────┬────┬────┬──────┐
     * │ Esc │ Q  │ W  │ E  │ R  │ T  │               │ Y  │ U  │ I  │ O  │ P  │ BkSp │
     * ├─────┼────┼────┼────┼────┼────┤               ├────┼────┼────┼────┼────┼──────┤
     * │ FN  │ A  │ S  │ D  │ F  │ G  │               │ H  │ J  │ K  │ L  │ =  │ Ret  │
     * ├─────┼────┼────┼────┼────┼────┤               ├────┼────┼────┼────┼────┼──────┤
     * │LSft │ Z  │ X  │ C  │ V  │ B  │               │ N  │ M  │ ,  │ ;  │ Up │RSft  │
     * └─────┴────┴────┴──┬─┴──┬─┴──┬─┘               └──┬─┴──┬─┴──┬─┴────┴────┴──────┘
     *                    │LCtl│Spc │Del              Lang│ /  │Left│
     *                    └────┴────┴───                  └────┴────┘
     */
    [_L0] = LAYOUT_crkbd(
        KC_ESC,  KC_Q,    KC_W,    KC_E,    KC_R,    KC_T,
        MO(_FL0),KC_A,    KC_S,    KC_D,    KC_F,    KC_G,
        KC_LSFT, KC_Z,    KC_X,    KC_C,    KC_V,    KC_B,
        KC_LCTL, KC_SPC,  KC_DEL,
        KC_Y,    KC_U,    KC_I,    KC_O,    KC_P,    KC_BSPC,
        KC_H,    KC_J,    KC_K,    KC_L,    KC_EQUAL,KC_ENTER,
        KC_N,    KC_M,    KC_COMM, KC_SCLN, KC_UP,   KC_RSFT,
        KC_LANG, KC_SLSH, KC_LEFT
    ),
    /*
     * Base layer 1 — Qwerty Staggered
     */
    [_L1] = LAYOUT_crkbd(
        KC_ESC,  KC_Q,    KC_W,    KC_E,    KC_R,    KC_T,
        MO(_FL1),KC_A,    KC_S,    KC_D,    KC_F,    KC_G,
        KC_LSFT, KC_Z,    KC_X,    KC_C,    KC_V,    KC_B,
        KC_LCTL, KC_SPC,  KC_DEL,
        KC_Y,    KC_U,    KC_I,    KC_O,    KC_P,    KC_BSPC,
        KC_H,    KC_J,    KC_K,    KC_L,    KC_SCLN, KC_BSLS,
        KC_N,    KC_M,    KC_COMM, KC_DOT,  KC_SLSH, KC_RSFT,
        KC_ENTER,KC_BSPC, KC_LEFT
    ),
    /*
     * Base layer 2 — Colemak DH
     */
    [_L2] = LAYOUT_crkbd(
        KC_ESC,  KC_Q,    KC_W,    KC_F,    KC_P,    KC_B,
        MO(_FL1),KC_A,    KC_R,    KC_S,    KC_T,    KC_G,
        KC_LSFT, KC_Z,    KC_X,    KC_C,    KC_D,    KC_V,
        KC_LCTL, KC_SPC,  KC_DEL,
        KC_J,    KC_L,    KC_U,    KC_Y,    KC_SCLN, KC_BSPC,
        KC_M,    KC_N,    KC_E,    KC_I,    KC_O,    KC_ENTER,
        KC_K,    KC_H,    KC_COMM, KC_DOT,  KC_SLSH, KC_RSFT,
        KC_LANG, KC_BSPC, KC_LEFT
    ),
    /*
     * Base layer 3 — Neo
     */
    [_L3] = LAYOUT_crkbd(
        KC_ESC,  KC_X,    KC_V,    KC_L,    KC_C,    KC_W,
        MO(_FL0),KC_U,    KC_I,    KC_A,    KC_E,    KC_O,
        KC_LSFT, DE_HASH, DE_UDIA, DE_ODIA, DE_ADIA, KC_P,
        KC_LCTL, KC_SPC,  KC_DEL,
        KC_K,    KC_H,    KC_G,    KC_F,    KC_Q,    KC_BSPC,
        KC_S,    KC_N,    KC_R,    KC_T,    KC_D,    KC_ENTER,
        KC_B,    KC_M,    KC_COMM, KC_DOT,  DE_Y,    KC_RSFT,
        KC_LANG, KC_BSPC, KC_LEFT
    ),
    /*
     * Base layer 4 — Workman
     */
    [_L4] = LAYOUT_crkbd(
        KC_ESC,  KC_Q,    KC_D,    KC_R,    KC_W,    KC_B,
        MO(_FL1),KC_A,    KC_S,    KC_H,    KC_T,    KC_G,
        KC_LSFT, KC_Z,    KC_X,    KC_M,    KC_C,    KC_V,
        KC_LCTL, KC_SPC,  KC_DEL,
        KC_J,    KC_F,    KC_U,    KC_P,    KC_SCLN, KC_BSPC,
        KC_Y,    KC_N,    KC_E,    KC_O,    KC_I,    KC_ENTER,
        KC_K,    KC_L,    KC_COMM, KC_DOT,  KC_SLSH, KC_RSFT,
        KC_LANG, KC_BSPC, KC_LEFT
    ),
    /*
     * Function layer 0
     */
    [_FL0] = LAYOUT_crkbd(
        OSL(_UL),KC_F1,   KC_F2,   KC_F3,   KC_F4,   KC_F5,
        _______,  _______,_______,_______,_______,_______,
        KC_CAPS,  _______,_______,_______,_______,_______,
        _______,  _______,TO(_UL),
        KC_F6,   KC_F7,   KC_F8,   KC_F9,   KC_F10,  KC_F11,
        MS_BTN1, MS_BTN2, _______,  _______,  _______,  KC_F12,
        TO(_NL),  _______,  _______,  _______,  _______,  KC_INS,
        KC_HOME,  KC_PGUP, KC_END
    ),
    /*
     * Function layer 1
     */
    [_FL1] = LAYOUT_crkbd(
        OSL(_UL),KC_F1,   KC_F2,   KC_F3,   KC_F4,   KC_F5,
        _______,  _______,_______,_______,_______,_______,
        _______,  _______,_______,_______,_______,_______,
        _______,  _______,KC_INS,
        KC_F6,   KC_F7,   KC_F8,   KC_F9,   KC_F10,  KC_F11,
        MS_BTN1, MS_BTN2, _______,  _______,  _______,  KC_F12,
        TO(_NL),  _______,  _______,  _______,  KC_CAPS,  _______,
        KC_HOME,  KC_PGUP, KC_END
    ),
    /*
     * Numpad layer
     */
    [_NL] = LAYOUT_crkbd(
        KC_NO,   KC_NUM,  KC_PSLS, KC_PAST, KC_PMNS, KC_NO,
        MS_BTN1, KC_KP_7, KC_KP_8, KC_KP_9, KC_PPLS, KC_INS,
        KC_NO,   KC_KP_4, KC_KP_5, KC_KP_6, KC_PPLS, KC_DEL,
        KC_BASE, KC_KP_0, KC_PDOT,
        KC_NO,   KC_INS,  KC_KP_7, KC_KP_8, KC_KP_9, KC_PPLS,
        KC_NO,   KC_DEL,  KC_KP_4, KC_KP_5, KC_KP_6, KC_PPLS,
        MS_BTN2, KC_NO,   KC_KP_1, KC_KP_2, KC_KP_3, KC_PENT,
        KC_PENT, KC_KP_0, KC_BASE
    ),
    /*
     * Utility layer
     */
    [_UL] = LAYOUT_crkbd(
        KC_NO,   KC_F13,  KC_F14,  KC_F15,  KC_F16,  KC_F17,
        KC_MYCM, KC_CALC, KC_PSCR, KC_SCRL, KC_BRK,  KC_NO,
        KC_LSFT, KC_NO,   KC_NO,   KC_NO,   KC_NO,   KC_NO,
        KC_BASE, KC_NO,   KC_NO,
        KC_F18,  KC_F19,  KC_MPRV, KC_MPLY, KC_MSTP, KC_MNXT,
        KC_NO,   KC_NO,   KC_NO,   KC_NO,   KC_MUTE, KC_NO,
        KC_NO,   KC_VOLD, KC_VOLU, KC_NO,   KC_NO,   KC_RSFT,
        KC_NO,   KC_NO,   KC_BASE
    ),
    /*
     * Settings layer
     */
    [_SL] = LAYOUT_crkbd(
        KC_DDIM, KC_DMIN, KC_D1Q,  KC_DHLF, KC_D3Q,  KC_DMAX,
        KC_NO,   KC_NO,   KC_NO,   KC_NO,   KC_NO,   KC_DBRI,
        KC_NO,   KC_L0,   KC_L1,   KC_L2,   KC_L3,   KC_L4,
        KC_BASE, LBL_TEXT,KC_TOGMODS,
        KC_NO,   KC_NO,   KC_NO,   KC_NO,   QK_MAKE, QK_BOOT,
        KC_NO,   KC_NO,   KC_NO,   KC_NO,   KC_NO,   KC_NO,
        EE_CLR,  KC_NO,   KC_NO,   KC_NO,   KC_NO,   KC_NO,
        DB_TOGG, KC_DEADKEY, KC_BASE
    ),
    /*
     * Language selection layer
     */
    [_LL] = LAYOUT_crkbd(
        KC_NO,              KCL_ARSA,  KCL_BEBY,  KCL_BGBG,  KCL_CSCZ,  KCL_DADK,
        KC_NO,              KCL_DEDE,  KCL_ELGR,  KCL_ENUS,  KCL_ESES,  KCL_FIFI,
        QK_UNICODE_MODE_WINCOMPOSE, KCL_FRFR, KCL_HEIL, KCL_HUHU, KCL_ITIT, KCL_JAJP,
        KC_BASE,            KC_NO,     KC_NO,
        KCL_KKKZ,  KCL_KOKR,  KCL_NLNL,  KCL_NNNO,  KCL_PLPL,  QK_UNICODE_MODE_MACOS,
        KCL_PTPT,  KCL_RORO,  KCL_RURU,  KCL_SVSE,  KCL_TRTR,  QK_UNICODE_MODE_LINUX,
        KCL_UKUA,  KCL_ZHCN,  KC_NO,     KC_NO,     KC_NO,     QK_UNICODE_MODE_WINDOWS,
        QK_UNICODE_MODE_EMACS, QK_UNICODE_MODE_BSD, KC_BASE
    ),
    /*
     * Additional latin variant layer
     */
    [_ADDLANG1] = LAYOUT_crkbd(
        KC_NO,   KC_NO,   KC_LAT0, KC_LAT1, KC_LAT2, KC_LAT3,
        KC_NO,   _______,  _______,  _______,  _______,  _______,
        KC_NO,   _______,  _______,  _______,  _______,  _______,
        KC_NO,   KC_NO,   _______,
        KC_LAT4, KC_LAT5, KC_LAT6, KC_LAT7, KC_LAT8, KC_LAT9,
        _______,  _______,  _______,  _______,  _______,  KC_NO,
        _______,  _______,  _______,  _______,  _______,  KC_NO,
        _______,  _______,  _______
    ),
    /*
     * Emoji layer — 10 category tabs, 24 slots per page
     */
    [_EMJ] = LAYOUT_crkbd(
        KC_EMJ_PAGE_PREV, KC_EMJ_CAT(0), KC_EMJ_CAT(1), KC_EMJ_CAT(2), KC_EMJ_CAT(3), KC_EMJ_CAT(4),
        ESLOT(0),         ESLOT(1),      ESLOT(2),      ESLOT(3),      ESLOT(4),      ESLOT(5),
        ESLOT(12),        ESLOT(13),     ESLOT(14),     ESLOT(15),     ESLOT(16),     ESLOT(17),
        TO(_BL),          KC_NO,         KC_NO,
        KC_EMJ_CAT(5),    KC_EMJ_CAT(6), KC_EMJ_CAT(7), KC_EMJ_CAT(8), KC_EMJ_CAT(9), KC_EMJ_PAGE_NEXT,
        ESLOT(6),         ESLOT(7),      ESLOT(8),      ESLOT(9),      ESLOT(10),     ESLOT(11),
        ESLOT(18),        ESLOT(19),     ESLOT(20),     ESLOT(21),     ESLOT(22),     ESLOT(23),
        KC_NO,            KC_NO,         TO(_BL)
    )
};

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

const uint16_t* to_static_text(uint16_t keycode, led_t state) {

    const uint16_t *emj = emj_display_text(keycode);
    if (emj != NULL) return emj;

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
        default:
            return NULL;
    }
}

bool render_key(uint16_t keycode, led_t state, uint8_t mods) {
    const poly_layer_t* local_layer = get_local_layer();

    const bool shift = ((local_layer->mods & MOD_MASK_SHIFT) != 0);
    const bool add_lang = get_highest_layer(local_layer->layer)==_ADDLANG1;
    const bool alt = ((local_layer->mods & MOD_MASK_ALT) != 0);
    const bool is_letter = keycode>=KC_A && keycode<=KC_Z;
    if(is_letter && add_lang) {
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

    uint16_t local_last_latin_keycode = get_local_last_latin_keycode();
    if(keycode>=KC_LAT0 && keycode<=KC_LAT9) {
        if(add_lang && alt && local_last_latin_keycode!=0) {
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
            const bool is_num = keycode>=KC_1 && keycode<=KC_0;
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

    const uint16_t* letter = translate_keycode(local_state->lang, keycode, shift, state.caps_lock);
    if (letter != NULL) {
        int8_t v_set;
        int8_t h_set;
        if(is_letter) {
            v_set = SETTING_LETTER_VOFFSET;
            h_set = SETTING_LETTER_HOFFSET;
        } else {
            const bool is_num = keycode>=KC_1 && keycode<=KC_0;
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
    kdisp_draw_bitmap(28, 0, get_overlay(idx), 72, 40);
    return true;
}

void update_displays(enum refresh_mode mode) {
    const poly_sync_t* local_state = get_local_state();
    if(local_state->contrast<=DISP_OFF || (local_state->flags&DISP_IDLE)!=0) {
        return;
    }

    const poly_layer_t* local_layer = get_local_layer();

    const led_t state = local_layer->led_state;
    const uint8_t mods = local_layer->mods;
    const bool capital_case = ((mods & MOD_MASK_SHIFT) != 0) || state.caps_lock;
    const bool display_overlays = test_flag(local_state->overlay_flags, DISPLAY_OVERLAYS);
    const uint8_t offset = is_left_side() ? 0 : MATRIX_ROWS_PER_SIDE;
    uint8_t start_row = 0;

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

            uint16_t keycode = keymaps[_BL][r + offset][c];
            if (keycode == KC_NO) {
                skip++;
            }
            else {
                if (disp_idx != 255) {
                    uint8_t layer = get_highest_layer(local_layer->layer);
                    uint16_t highest_kc = keycode_at_keymap_location(layer,r + offset,c);
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
                                text = keycode_to_disp_overlay(keycode, state);
                            }
                        } else {
                            text = keycode_to_disp_overlay(keycode, state);
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

void kdisp_idle(uint8_t contrast) {
    uint8_t offset = is_left_side() ? 0 : MATRIX_ROWS_PER_SIDE;
    uint8_t skip = 0;
    sr_shift_out_buffer_latch(disp_row_0.bitmask, sizeof(struct display_info));

    for (uint8_t r = 0; r < MATRIX_ROWS_PER_SIDE; ++r) {
        for (uint8_t c = 0; c < MATRIX_COLS; ++c) {
            uint8_t  disp_idx = LAYOUT_TO_INDEX(r, c);

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
                SEND_STRING(
                    SS_TAP(X_END) SS_TAP(X_DEL)
                    SS_TAP(X_SPC)
                    SS_LCTL(
                        SS_TAP(X_RGHT) SS_TAP(X_LEFT)
                        SS_LSFT(SS_TAP(X_LEFT) SS_TAP(X_RGHT))
                    )
                    SS_TAP(X_SPC)
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
        default:
            break;
        }
    }

    update_performed();
};

void show_splash_screen(void) {
    clear_all_displays();
    if(is_left_side()) {
        display_message(1, 1, u"POLY", &FreeSansBold24pt7b);
        display_message(2, 1, u"KYBD", &FreeSansBold24pt7b);
    } else {
        display_message(1, 1, u"CORNE", &FreeSansBold24pt7b);
        display_message(2, 1, u" 4 2", &FreeSansBold24pt7b);
    }
    wait_ms(400);
    update_displays(ALL_AT_ONCE);
}

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

void unicode_input_mode_set_user(uint8_t unicode_mode) {
    access_local_state()->unicode_mode = unicode_mode;
    request_disp_refresh();
}

void keyboard_post_init_user(void) {
    debug_enable = true;
    debug_matrix = false;
    debug_keyboard = false;
    debug_mouse = false;

    layer_state_t default_layer = persistent_default_layer_get();
    access_local_layer()->def_layer = default_layer;
    access_local_state()->unicode_mode = get_unicode_input_mode();
    layer_clear();
    layer_on(default_layer);

    set_com_state(is_keyboard_master() ? USB_HOST : BRIDGE);
    set_side(is_keyboard_left() ? LEFT_SIDE : RIGHT_SIDE);

    /* encoder pins — update to actual PCB pins if different */
    gpio_set_pin_input_high(GP25);
    gpio_set_pin_input_high(GP29);

    emj_init();

    reset_overlay_buffers();
    reset_overlay_usage();
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
    local_state->flags = STATUS_DISP_ON;  /* no RGB on corne42 */

    memcpy(access_global_latin_table()->ex, ee.latin_ex, sizeof(ee.latin_ex));

    set_displays(ee.brightness, false);
}

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

    /* I2C SDA pin for the status OLED — verify pin matches PCB */
    gpio_set_pin_input_high(I2C1_SDA_PIN);
}

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
    [0]  = { ENCODER_CCW_CW(MS_WHLD, MS_WHLU) },
    [1]  = { ENCODER_CCW_CW(MS_WHLD, MS_WHLU) },
    [2]  = { ENCODER_CCW_CW(MS_WHLD, MS_WHLU) },
    [3]  = { ENCODER_CCW_CW(MS_WHLD, MS_WHLU) },
    [4]  = { ENCODER_CCW_CW(MS_WHLD, MS_WHLU) },
    [5]  = { ENCODER_CCW_CW(MS_WHLD, MS_WHLU) },
    [6]  = { ENCODER_CCW_CW(MS_WHLD, MS_WHLU) },
    [7]  = { ENCODER_CCW_CW(MS_WHLD, MS_WHLU) },
    [8]  = { ENCODER_CCW_CW(MS_WHLD, MS_WHLU) },
    [9]  = { ENCODER_CCW_CW(MS_WHLD, MS_WHLU) },
    [10] = { ENCODER_CCW_CW(MS_WHLD, MS_WHLU) },
    [11] = { ENCODER_CCW_CW(MS_WHLD, MS_WHLU) },
    [12] = { ENCODER_CCW_CW(MS_WHLD, MS_WHLU) },
};

oled_rotation_t oled_init_user(oled_rotation_t rotation){
    oled_off();
    oled_clear();
    oled_render();
    oled_scroll_set_speed(0);
    oled_render_logos();
    oled_on();
    return rotation;
}

void poly_suspend(void) {
    poly_sync_t* local_state = access_local_state();
    local_state->overlay_flags = flag_off(local_state->overlay_flags, DISPLAY_OVERLAYS);
    local_state->flags &= ~((uint8_t)STATUS_DISP_ON) & ~((uint8_t)DISP_IDLE) & ~((uint8_t)IDLE_TRANSITION);
    local_state->contrast = DISP_OFF;
}

void suspend_power_down_kb(void) {
    // USB suspend fires on slave when master enters bootloader; skip to keep displays lit.
    if (get_local_state()->overlay_flags & BOOTLOADER_DISPLAY) {
        return;
    }
    poly_suspend();
    sync_and_refresh_displays();
    suspend_power_down_user();
    set_last_update(-1);
}

void suspend_wakeup_init_kb(void) {
    poly_sync_t* local_state = access_local_state();
    local_state->flags |= STATUS_DISP_ON;
    local_state->flags &= ~((uint8_t)DISP_IDLE);
    poly_eeconf_t ee = load_user_eeconf();
    local_state->contrast = ee.brightness;
    set_last_update(0);

    update_performed();
    housekeeping_task_user();
    suspend_wakeup_init_user();
}
