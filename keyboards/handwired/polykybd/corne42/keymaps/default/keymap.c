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
#include "base/fonts/flag_fonts.h"        // language-layer country flags (fonts/gen-lang-fonts.sh)
#include "base/fonts/lang_label_font.h"   // tiny label font under the flags
#include "base/multicore/core1.h"
#include "polymod_crc32.h"

#include "state.h"
#include "multicore_exec.h"
#include "split_sync.h"
#include "split_fw_up.h"
#include "poly_util.h"

#include "lang/lang_lut.h"
#include "lang/lang_lut_ext.h"

#include "layers.h"
#include "keycode_helper.h"
#include "uni.h"
#include "emoji/emoji_layer.h"
#include "lang_layer.h"
#include "mru.h"

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
            access_local_state()->contrast = get_user_brightness();
        }

        if(flags!=local_flags) {
            flags=local_flags;
        }
        if(overlay_flags!=local_overlay_flags) {
            overlay_flags=local_overlay_flags;
        }

        access_local_state()->emj_category = emj_active_category();
        access_local_state()->emj_page     = emj_active_page();
        access_local_state()->lang_page    = lang_pack_state();
        state_diff = differ(get_local_state(), get_global_state(), sizeof(poly_sync_t));
        if ( state_diff ) {
            // Single attempt (was 10): these periodic syncs re-fire every
            // housekeeping pass while a diff persists, so in-call retries only
            // pile up full ~40 ms UART timeouts and stall the main loop (and
            // USB/HID) when the slave is transiently unreachable — e.g. the
            // post-cold-flash settling window, kept "connected" for ~4 s by the
            // raised SPLIT_MAX_CONNECTION_ERRORS. See split72 keymap for detail.
            if(!send_to_bridge(USER_SYNC_POLY_DATA, (void *)access_local_state(), sizeof(poly_sync_t), 1)) {
                // Failed: clearing state_diff skips the copy_global_state() below,
                // so global stays != local and differ() re-fires the send next
                // pass. The diff IS the retry queue; global only advances to local
                // on a successful sync — so 1 vs 10 attempts changes only where
                // retries happen, never whether the update is delivered.
                state_diff = false;
                uprint("USER_SYNC_POLY_DATA failed to send\n");
            }
        }

        // Push the MRU recents to the slave only when they changed, so both
        // halves render the recents identically (multiplexed onto the overlay-map
        // transaction id by its distinct payload size, as on split72).
        if (mru_sync_pending()) {
            mru_sync_t mru_msg;
            mru_emoji_pack(mru_msg.emoji);
            mru_lang_pack(mru_msg.lang);
            uint8_t mru_ack = send_to_bridge(USER_SYNC_OVERLAY_MAP_DATA, &mru_msg, MRU_SYNC_BYTES, 1);
            if (mru_ack == SYNC_ACK || mru_ack == SYNC_ACK_SIG) {
                mru_clear_sync_pending();
            } else {
                uprint("USER_SYNC_MRU_DATA failed to send\n");
            }
        }

        access_local_layer()->led_state = host_keyboard_led_state();
        access_local_layer()->mods = get_mods();
        layer_diff = differ(get_local_layer(), get_global_layer(), sizeof(poly_layer_t));
        if ( layer_diff ) {
            if(!send_to_bridge(USER_SYNC_LAYER_DATA, (void *)access_local_layer(), sizeof(poly_layer_t), 1)) {
                layer_diff = false; // skip copy_global_layer() below; diff persists, re-fires next pass
                uprint("USER_SYNC_LAYER_DATA failed to send\n");
            }
        }
        if ( differ(get_local_last_latin(), get_global_last_latin(), sizeof(poly_last_t)) ) {
            if(!send_to_bridge(USER_SYNC_LASTKEY_DATA, access_local_last_latin(), sizeof(poly_last_t), 1)) {
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
        copy_global_state(get_local_state());   // advance global only on a synced change (see send site)
        request_disp_refresh();
    }

    if(layer_diff) {
        copy_global_layer(get_local_layer());   // advance global only on a synced change (see send site)
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
    // Slave path for the handedness-change command (USER_SYNC_REBOOT): the
    // reboot is armed in the transaction handler and fired here, so both halves
    // restart together onto the new left/right assignment.
    if (fw_staging_reboot_pending()) {
        save_all_dirty();   // persist before the full-chip reset — this path skips shutdown_quantum
        mcu_reset();   // clean full-chip reset; never returns
    }
    // User state is flushed to EEPROM at suspend (save_all_dirty) or on demand
    // via the store key (KC_STORE_EE); the only housekeeping write is draining
    // that one-shot store request (locally on master, via SAVE_EEPROM on slave).
    save_all_if_requested();
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
                int32_t time_after = elapsed_time_since_update - FADE_OUT_TIME;
                int16_t brightness = ((FADE_TRANSITION_TIME - time_after) * get_user_brightness()) / FADE_TRANSITION_TIME;

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
        KC_NO,   KC_NO,   KC_NO,   KC_NO,   KC_NO,   QK_RBT,
        EE_CLR,  KC_STORE_EE, KC_NO, KC_NO, KC_NO,   KC_NO,
        DB_TOGG, KC_DEADKEY, KC_BASE
    ),
    /*
     * Language selection layer
     */
    // Language selection layer — mirrors the emoji picker. LEFT half: row 0 = the
    // six continent region tabs (LCAT), row 1 = the six unicode-input mode keys,
    // row 2 = the MRU recents (LMRU, top-bar marked). RIGHT half: the active
    // region's language slots (LSLOT), paged via the thumb arrows. No Preset key —
    // Clear is on the right thumb (former right base key); the left thumb exits.
    [_LL] = LAYOUT_crkbd(
        LCAT(0),   LCAT(1),   LCAT(2),   LCAT(3),   LCAT(4),   LCAT(5),
        QK_UNICODE_MODE_WINCOMPOSE, QK_UNICODE_MODE_MACOS, QK_UNICODE_MODE_EMACS, QK_UNICODE_MODE_WINDOWS, QK_UNICODE_MODE_LINUX, QK_UNICODE_MODE_BSD,
        LMRU(0),   LMRU(1),   LMRU(2),   LMRU(3),   LMRU(4),   LMRU(5),
        KC_BASE,   KC_NO,     KC_NO,
        LSLOT(0),  LSLOT(1),  LSLOT(2),  LSLOT(3),  LSLOT(4),  LSLOT(5),
        LSLOT(6),  LSLOT(7),  LSLOT(8),  LSLOT(9),  LSLOT(10), LSLOT(11),
        LSLOT(12), LSLOT(13), LSLOT(14), LSLOT(15), LSLOT(16), LSLOT(17),
        KC_LANG_PAGE_PREV, KC_LANG_PAGE_NEXT, KC_LANG_CLEAR
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
     * Emoji layer — left half: 12 category tabs (rows 0-1) + 6 MRU recents on the
     * bottom-left row (top-bar marked); right half: the current tab's 18 slots
     * (3 rows). Paging on the right thumbs; no Preset — Clear is on the right
     * thumb (former right base key); the left thumb still exits.
     */
    [_EMJ] = LAYOUT_crkbd(
        KC_EMJ_CAT(0),  KC_EMJ_CAT(1),  KC_EMJ_CAT(2),  KC_EMJ_CAT(3),  KC_EMJ_CAT(4),  KC_EMJ_CAT(5),
        KC_EMJ_CAT(6),  KC_EMJ_CAT(7),  KC_EMJ_CAT(8),  KC_EMJ_CAT(9),  KC_EMJ_CAT(10), KC_EMJ_CAT(11),
        EMRU(0),        EMRU(1),        EMRU(2),        EMRU(3),        EMRU(4),        EMRU(5),
        TO(_BL),        KC_NO,          KC_NO,
        ESLOT(0),       ESLOT(1),       ESLOT(2),       ESLOT(3),       ESLOT(4),       ESLOT(5),
        ESLOT(6),       ESLOT(7),       ESLOT(8),       ESLOT(9),       ESLOT(10),      ESLOT(11),
        ESLOT(12),      ESLOT(13),      ESLOT(14),      ESLOT(15),      ESLOT(16),      ESLOT(17),
        KC_EMJ_PAGE_PREV, KC_EMJ_PAGE_NEXT, KC_EMJ_CLEAR
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

const uint32_t* to_static_text(uint16_t keycode, led_t state) {

    const uint32_t *emj = emj_display_text(keycode);
    if (emj != NULL) return emj;

    const uint32_t *lng = lang_display_text(keycode);
    if (lng != NULL) return lng;

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
        // The flag + selection frame are drawn by render_lang_flag_key(); here we
        // only return the tiny language code shown under the flag.
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
        case KCL_ZHHK: return U"zh-HK";
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

        const uint32_t* def_variation = latin_ex_map[offset+keycode-KC_A][0];
        if(def_variation!=NULL) {
            kdisp_write_gfx_text(ALL_FONTS, ALL_FONT_SIZE, BUFFER_X, 23, latin_ex_map[offset+keycode-KC_A][variation]);
            return true;
        }
        return false;
    }

    uint16_t local_last_latin_keycode = get_local_last_latin_keycode();
    if(keycode>=KC_LAT0 && keycode<=KC_LAT9) {
        if(add_lang && alt && local_last_latin_keycode!=0) {
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

    const uint32_t* letter = translate_keycode(local_state->lang, keycode, shift, state.caps_lock);
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
    kdisp_draw_bitmap(28, 0, get_overlay(idx), 72, 40);
    return true;
}

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

static const GFXfont* const lang_flag_fonts[]  = { &NotoColorEmoji_Regular_LangFlags_20pt7b };
static const GFXfont* const lang_label_fonts[] = { &NotoSans_Regular_Tiny_6pt7b };

// Draw a Preset/Clear MRU control key: a tiny label plus a left/right arrow icon.
static void render_mru_ctrl_key(bool preset) {
    if (preset) {
        kdisp_write_gfx_text(lang_label_fonts, 1, BUFFER_X + 14, 18, U"Preset");
        kdisp_write_gfx_text(ALL_FONTS, ALL_FONT_SIZE, BUFFER_X + 44, 23, ICON_RIGHT);
    } else {
        // Mirror of "Preset": back-arrow points left into the recents row, label in
        // the small keycap-label font (not the full-size font, which overflowed).
        kdisp_write_gfx_text(ALL_FONTS, ALL_FONT_SIZE, BUFFER_X + 10, 23, ICON_LEFT);
        kdisp_write_gfx_text(lang_label_fonts, 1, BUFFER_X + 26, 18, U"Clear");
    }
}

// Language region tab — the continent name centred in the keycap (continent
// silhouettes will replace the text later). The active-tab frame / inactive
// bottom bar is drawn separately by lang_draw_tab_indicator/bottom.
static void render_lang_region_tab(uint16_t keycode) {
    const uint32_t* label = lang_region_label((uint8_t)(keycode - KC_LANG_CAT_BASE));
    int8_t lo = 0, hi = 0;
    kdisp_gfx_text_bounds(lang_label_fonts, 1, label, &lo, &hi);
    int8_t w = (int8_t)(hi - lo);
    int8_t x = (int8_t)(BUFFER_X + (SCREEN_WIDTH - w) / 2 - lo);
    kdisp_write_gfx_text(lang_label_fonts, 1, x, 22, label);
}

// MRU recents (emoji or language) get a full-width bar along the TOP edge to set
// the recents row apart (the mirror of the category tabs' bottom bar).
static void draw_mru_top_bar(uint16_t keycode) {
    bool is_mru = (keycode >= KC_EMJ_MRU_BASE  && keycode < KC_EMJ_MRU_BASE  + MRU_CAP) ||
                  (keycode >= KC_LANG_MRU_BASE && keycode < KC_LANG_MRU_BASE + MRU_CAP);
    if (is_mru) kdisp_fill_rect(BUFFER_X, 0, SCREEN_WIDTH, 3);
}

// Layers below the host-write cap are remappable and live in the dynamic keymap
// (EEPROM); layers at/above it (the language/emoji function layers) are served straight
// from the compiled keymap in flash, so they never read the dynamic keymap and always
// reflect the flashed firmware. Used by both the display and the key-event path.
static uint16_t poly_keycode_at(uint8_t layer, uint8_t row, uint8_t col) {
    if (layer >= DYNAMIC_KEYMAP_UPDATE_MAX_LAYER_COUNT) {
        return keymaps[layer][row][col];   // static, compiled-in (flash)
    }
    return keycode_at_keymap_location(layer, row, col);
}

// Override QMK's weak resolver so key events on the static function layers come from the
// compiled keymap too (no encoder/dip maps are enabled on this board).
uint16_t keymap_key_to_keycode(uint8_t layer, keypos_t key) {
    if (key.row >= MATRIX_ROWS || key.col >= MATRIX_COLS) return KC_NO;
    return poly_keycode_at(layer, key.row, key.col);
}

// Draw one language key: oversized country flag on the left (vertically centred
// and clipped so the flag content fills the keycap height), language code running
// vertically up the right side (inverted bar when it is the active language).
// idx is the LANG_* enum value (resolved via lang_index_for_keycode for slot/MRU keys).
static void render_lang_flag_key(uint8_t idx, const uint32_t* label, uint8_t current_lang) {
    const GFXfont* ff  = &NotoColorEmoji_Regular_LangFlags_20pt7b;

    // Flag: the glyph is taller than the keycap, so centre it vertically — the
    // empty top/bottom margins clip off and the flag content fills the height.
    const int8_t fh  = (int8_t)pgm_read_byte(&ff->glyph[idx].height);
    const int8_t fyo = (int8_t)pgm_read_byte(&ff->glyph[idx].yOffset);
    kdisp_write_gfx_char(lang_flag_fonts, 1, FLAG_LEFT_X,
                         (int8_t)((SCREEN_HEIGHT - fh) / 2 - fyo),
                         FLAG_CP_BASE + idx, true);

    // Language code: vertical, up the right side; inverted bar when selected.
    kdisp_write_gfx_vtext(&NotoSans_Regular_Tiny_6pt7b, LABEL_COL_X, label,
                          current_lang == idx);
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
                    uint16_t highest_kc = poly_keycode_at(layer,r + offset,c);
                    keycode = (highest_kc == KC_TRNS) ? poly_keycode_at(get_highest_layer(local_layer->layer&~(1<<layer)),r + offset,c) : highest_kc;
                    kdisp_enable(true);
                    kdisp_set_contrast(local_state->contrast-1);
                    if(keycode!=KC_TRNS) {
                        int16_t lang_idx = lang_index_for_keycode(keycode);
                        if (lang_idx >= 0) {
                            // Language layer (KCL_/LMRU/LSLOT): flag + tiny code.
                            kdisp_set_buffer(0x00);
                            draw_mru_top_bar(keycode);
                            render_lang_flag_key((uint8_t)lang_idx, to_static_text((uint16_t)(KCL_ENUS + lang_idx), state), local_state->lang);
                            kdisp_send_buffer();
                        } else if (keycode == KC_EMJ_PRESET || keycode == KC_LANG_PRESET ||
                                   keycode == KC_EMJ_CLEAR  || keycode == KC_LANG_CLEAR) {
                            // MRU Preset/Clear control keys.
                            kdisp_set_buffer(0x00);
                            render_mru_ctrl_key(keycode == KC_EMJ_PRESET || keycode == KC_LANG_PRESET);
                            kdisp_send_buffer();
                        } else if (keycode >= KC_LANG_CAT_BASE && keycode < KC_LANG_PAGE_PREV) {
                            // Language region tab — continent label + active frame.
                            kdisp_set_buffer(0x00);
                            lang_draw_tab_indicator(keycode);
                            lang_draw_tab_bottom(keycode);
                            render_lang_region_tab(keycode);
                            kdisp_send_buffer();
                        } else {
                        const uint32_t* text = to_static_text(keycode, state);
                        kdisp_set_buffer(0x00);
                        // Draw the row bar FIRST, then the emoji glyph with courtyard
                        // clearing so the icon punches a clean margin through it.
                        draw_mru_top_bar(keycode);
                        if(text==NULL) {
                            if(!render_key(keycode, state, mods) && (keycode&QK_UNICODEMAP_PAIR)==QK_UNICODEMAP_PAIR){
                                uint16_t chr = capital_case ? QK_UNICODEMAP_PAIR_GET_SHIFTED_INDEX(keycode) : QK_UNICODEMAP_PAIR_GET_UNSHIFTED_INDEX(keycode);
                                kdisp_write_gfx_char(ALL_FONTS, ALL_FONT_SIZE, BUFFER_X, 23, unicode_map[chr], false);
                            }
                        } else {
                            kdisp_write_gfx_text_cy(ALL_FONTS, ALL_FONT_SIZE, BUFFER_X, 23, text, true);
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
                            kdisp_write_gfx_text_cy(ALL_FONTS, ALL_FONT_SIZE, BUFFER_X, 23, text, true);
                        }
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

                        mark_latin_dirty();
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
            default_layer_set(local_layer->def_layer);
            defer_default_layer_save(local_layer->def_layer);
            request_disp_refresh();
            break;
        case KC_L1:
            local_layer->def_layer = _L1;
            default_layer_set(local_layer->def_layer);
            defer_default_layer_save(local_layer->def_layer);
            request_disp_refresh();
            break;
        case KC_L2:
            local_layer->def_layer = _L2;
            default_layer_set(local_layer->def_layer);
            defer_default_layer_save(local_layer->def_layer);
            request_disp_refresh();
            break;
        case KC_L3:
            local_layer->def_layer = _L3;
            default_layer_set(local_layer->def_layer);
            defer_default_layer_save(local_layer->def_layer);
            request_disp_refresh();
            break;
        case KC_L4:
            local_layer->def_layer = _L4;
            default_layer_set(local_layer->def_layer);
            defer_default_layer_save(local_layer->def_layer);
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
            set_user_brightness(FULL_BRIGHT/4);
            break;
        case KC_D3Q:
            set_user_brightness((FULL_BRIGHT/4)*3);
            break;
        case KC_DHLF:
            set_user_brightness(FULL_BRIGHT/2);
            break;
        case KC_DMAX:
            set_user_brightness(FULL_BRIGHT);
            break;
        case KC_DMIN:
            set_user_brightness(2);
            break;
        case KC_DDIM:
            dec_brightness();
            break;
        case KC_DBRI:
            inc_brightness();
            break;
        case KC_STORE_EE:
            // Manual "commit everything to EEPROM" — defer our own write to
            // housekeeping and signal the slave via the SAVE_EEPROM sync flag
            // (set, push state, clear locally), mirroring split72.
            request_eeprom_save();
            local_state->overlay_flags |= SAVE_EEPROM;
            send_to_bridge(USER_SYNC_POLY_DATA, (void *)local_state, sizeof(poly_sync_t), 10);
            local_state->overlay_flags &= ~SAVE_EEPROM;
            break;
        // ── Language layer: region tabs, paging, MRU controls, slot/MRU select ──
        case KC_LANG_CAT_BASE ... KC_LANG_PAGE_PREV - 1:
            lang_select_region((uint8_t)(keycode - KC_LANG_CAT_BASE));
            break;
        case KC_LANG_PAGE_PREV:
            lang_page_prev();
            request_disp_refresh();
            break;
        case KC_LANG_PAGE_NEXT:
            lang_page_next();
            request_disp_refresh();
            break;
        case KC_LANG_PRESET:
            mru_lang_preset();
            break;
        case KC_LANG_CLEAR:
            mru_lang_clear();
            break;
        case KC_LANG_MRU_BASE ... KC_LANG_SLOT_BASE - 1:
        case KC_LANG_SLOT_BASE ... KC_LANG_END - 1: {
            int16_t li = lang_index_for_keycode(keycode);
            if (li >= 0) {
                local_state->lang = (uint8_t)li;
                mru_lang_push((uint8_t)li);
                mark_settings_dirty();
                layer_off(_LL);
            }
            break;
        }
        // (Direct KCL_* language selection removed — the _LL layer now selects
        //  via the LMRU/LSLOT keys handled above.)
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
                mark_settings_dirty();
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
        display_message(1, 1, U"POLY", &FreeSansBold24pt7b);
        display_message(2, 1, U"KYBD", &FreeSansBold24pt7b);
    } else {
        display_message(1, 1, U"CORNE", &FreeSansBold24pt7b);
        display_message(2, 1, U" 4 2", &FreeSansBold24pt7b);
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
        local_state->contrast = get_user_brightness();
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
    lang_init();
    mru_init();

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
    // Reboot coordination — also the carrier for the host's handedness-change
    // command (hid_com.c case 25), so the slave persists its new EE_HANDS marker
    // and reboots together with the master.
    transaction_register_rpc(USER_SYNC_REBOOT,              user_sync_reboot_handler);

    poly_eeconf_t ee = load_user_eeconf();
    poly_sync_t* local_state = access_local_state();
    local_state->lang = ee.lang;
    local_state->contrast = ee.brightness;
    note_user_brightness(ee.brightness);
    local_state->flags = STATUS_DISP_ON;  /* no RGB on corne42 */

    memcpy(access_global_latin_table()->ex, ee.latin_ex, sizeof(ee.latin_ex));

    // Restore the MRU recents (emoji-slot selection on _EMJ already pushes to the
    // emoji MRU, which is persisted on suspend via save_all_dirty).
    mru_load(ee.mru_emoji, ee.mru_lang);

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
    // Resolve the side BEFORE the splash so each half shows its own logo (set_side()
    // otherwise runs only in post_init, after the splash). Use the pure
    // eeconfig_read_handedness() rather than is_keyboard_left_impl() — the latter's
    // EE_HANDS branch can run eeconfig_init() this early and wipe the per-half marker.
    // See split72 keymap.c for the full rationale.
    set_side(eeconfig_read_handedness() ? LEFT_SIDE : RIGHT_SIDE);
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
    // Empty MRU recents: the serialised form uses 0 == empty for both lists, so
    // a zeroed block reads back as "no recent" (no stray category-0 / lang-0).
    memset(ee.mru_emoji, 0, sizeof(ee.mru_emoji));
    memset(ee.mru_lang, 0, sizeof(ee.mru_lang));
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
    // Flush all dirty user state to EEPROM on real power suspension (each half
    // independently). Dirty-gated, and after the final sync so a flash
    // consolidation can't corrupt a live split transaction.
    save_all_dirty();
    suspend_power_down_user();
    set_last_update(-1);
}

// Called by QMK before every reset (QK_REBOOT, QK_BOOTLOADER, and the host-triggered
// HID bootloader command, all via shutdown_quantum). Flush dirty user state to EEPROM
// here too — without this, a reboot/bootloader jump that isn't preceded by a USB
// suspend would discard any MRU/settings/layer changes still held in RAM.
bool shutdown_user(bool jump_to_bootloader) {
    save_all_dirty();
    return true;
}

void suspend_wakeup_init_kb(void) {
    poly_sync_t* local_state = access_local_state();
    local_state->flags |= STATUS_DISP_ON;
    local_state->flags &= ~((uint8_t)DISP_IDLE);
    local_state->contrast = get_user_brightness();
    set_last_update(0);

    update_performed();
    housekeeping_task_user();
    suspend_wakeup_init_user();
}
