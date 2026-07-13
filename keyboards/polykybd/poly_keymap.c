// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Shared PolyKybd keymap logic — compiled for EVERY variant (split72, split42)
// via the keyboard-level rules.mk. Only the per-variant DATA lives in each
// variant's keymaps/default/keymap.c: the layer keymaps[], the encoder_map[],
// and (RGB variants only) g_led_config. Everything behavioural — rendering,
// HID/overlay handling, language selection, idle/suspend, split sync, the
// firmware-update state machine — lives here so the two variants can never
// drift apart again.
//
// Variant differences are resolved at compile time:
//   * QMK_KEYBOARD_H              -> the variant main header (split72.h/split42.h)
//   * POLY_DISP_ROW_0/3, POLY_SPLASH_* -> macros from that header
//   * RGB_MATRIX_ENABLE / POINTING_DEVICE_ENABLE -> #ifdef-guarded blocks
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
#include "split_util.h"   // is_transport_connected() — gate the boot layer-resync one-shot
#include <hardware/flash.h>

#include "polykybd.h"
#include "status_oled.h"
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
#include "base/fontpack.h"                // g_all_fonts/g_all_font_count + loader
// Country flags (NotoColorEmoji_Regular_LangFlags, codepoints FLAG_CP_BASE+idx)
// now ship in the external-flash font pack, resolved via g_all_fonts — they are
// NOT compiled in. The tiny label font stays resident (no-pack fallback label).
#include "base/fonts/lang_label_font.h"   // tiny (6px) label font under the flags
#include "base/fonts/util_font.h"         // mid (10px) utility-label font
#include "base/multicore/core1.h"
#include "base/ltr559.h"
#include "polymod_crc32.h"

#ifdef POLYKYBD_LTR559
#    define LTR559_LOG_MS 600000   // sensor telemetry log cadence: 10 min
#endif

#include "state.h"
#include "multicore_exec.h"
#include "split_sync.h"
#include "poly_util.h"

#include "lang/lang_lut.h"
#include "lang/lang_lut_ext.h"

#include "layers.h"
#include "keycode_helper.h"
#include "doom/doom_mode.h"   // Doom easter egg (inline no-ops unless POLYKYBD_DOOM)
#include "os_actions.h"
#include "uni.h"
#include "emoji/emoji_layer.h"
#include "lang_layer.h"
#include "mru.h"

#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#ifdef RGB_MATRIX_ENABLE
// Forward-declare this helper function
void rgb_matrix_update_pwm_buffers(void);
#endif

// In-call retry count for the periodic master->slave state pushes in
// sync_and_refresh_displays(). Kept small: each failed attempt pays a full UART
// timeout (SPLIT_MAX_CONNECTION_ERRORS is raised, so failures don't fast-fail),
// so this bounds the worst-case main-loop stall while still riding through a
// single transient UART glitch within the same housekeeping pass — which the
// diff re-fire alone does NOT cover for transient state (see the comment at the
// USER_SYNC_POLY_DATA send site).
#define PERIODIC_SYNC_RETRIES 3

/*[[[cog
import cog
import os
from textwrap import wrap
from openpyxl import load_workbook
wb = load_workbook(filename = os.path.join(os.path.abspath(os.path.dirname(cog.inFile)), "lang", "lang_lut.xlsx"))
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


static_assert(FLASH_PAGE_SIZE==256, "Flash page size changed");

// The KCL_* (keycode_helper.h) and LANG_* (lang_lut.h) enums are both generated
// from the SAME ordered language list, so LANG_xx == (KCL_xx - KCL_ENUS). The
// language-keycode handling in to_static_text()/process_record_user() relies on
// that identity to index a table / compute local_state->lang from the keycode
// offset instead of a 160-case switch. Assert it at both ends so a future
// divergence between the two generators is a build error, not a silent mis-map.
static_assert((int)LANG_DEDE == (int)(KCL_DEDE - KCL_ENUS), "KCL_/LANG_ enum order drift");
static_assert((int)LANG_CKUS == (int)(KCL_CKUS - KCL_ENUS), "KCL_/LANG_ enum order drift");

static enum lang_layer g_lang_init = INIT_LANG;

// keymaps[] / encoder_map[] / g_led_config are variant data, defined in each
// variant's keymaps/default/keymap.c. disp_row_* select the first display of
// row 0 / the second-half start row via macros from the variant header.
extern const uint16_t keymaps[][MATRIX_ROWS][MATRIX_COLS];
const struct display_info disp_row_0 = { POLY_DISP_ROW_0 };
const struct display_info disp_row_3 = { POLY_DISP_ROW_3 };


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
// Font-pack / firmware flash: light the whole matrix (breathing cyan) so the user
// sees the board is busy updating and must not unplug it. The override below only
// writes the per-frame LED buffer — the persistent mode/hue/sat/val config is
// untouched, so the previous effect resumes automatically when flashing ends.
// housekeeping_task_user() drives s_flash_rgb_active with a short hold (so the
// brief gaps between the six bundles don't flicker it) and re-enables RGB if it
// was off, disabling it again afterwards.
#define FLASH_RGB_HOLD_MS 2500
static bool     s_flash_rgb_active      = false;
static bool     s_flash_rgb_was_enabled = false;
static uint16_t s_flash_rgb_seen        = 0;

bool rgb_matrix_indicators_kb(void) {
    if (s_flash_rgb_active) {
        uint8_t phase = (uint8_t)(timer_read32() >> 3);         // ~2 s cycle
        uint8_t tri   = phase < 128 ? phase : (uint8_t)(255 - phase);  // triangle breath 0..127..0
        uint8_t v     = 5 + (tri >> 2);                         // ~5..36 (half brightness — bright enough)
        if (fw_staging_commit_pending()) {
            // Applying the staged firmware → reboot imminent, you can't type → orange.
            rgb_matrix_set_color_all(v, v >> 2, 0);            // breathing orange
        } else {
            // Staging a font pack OR firmware: the board still runs and you can keep
            // typing, so use the calm cyan/green-blue cue (orange is reserved for the
            // apply + bootloader = "can't type" states).
            rgb_matrix_set_color_all(0, v, v);                 // breathing cyan
        }
        return false;
    }
    // Doom sound->RGB cue (doom_mode.c): while game mode runs, the matrix is
    // the game's "speaker" — yellow fire flash, blue world sounds, red base
    // as health degrades. False = it painted this frame. Inline pass-through
    // when the game isn't compiled in.
    if (!doom_rgb_indicators()) {
        return false;
    }
    if (!is_keyboard_master()) {
        if (get_local_state()->overlay_flags & BOOTLOADER_DISPLAY) {
            // Bootloader: no typing possible — force orange (matches the firmware-flash cue).
            rgb_matrix_set_color_all(24, 6, 0);
            return false;
        }
        if ((get_local_state()->flags & STATUS_DISP_ON) == 0) {
            rgb_matrix_set_color_all(0, 0, 0);
            return false;
        }
    }
    return rgb_matrix_indicators_user();
}

// Drive the flash RGB attention effect from the fw_up state (master + slave).
static void flash_rgb_tick(void) {
    // Light the matrix while a flash is staging, and also while an apply is pending
    // (commit_pending) so a standalone "apply staged firmware" still shows the orange
    // reboot cue even though no chunks are streaming.
    if (fw_staging_fw_up_active() || fw_staging_commit_pending()) {
        s_flash_rgb_seen   = timer_read();
    }
    bool want = (s_flash_rgb_seen != 0) && (timer_elapsed(s_flash_rgb_seen) < FLASH_RGB_HOLD_MS);
    if (want && !s_flash_rgb_active) {
        s_flash_rgb_active      = true;
        s_flash_rgb_was_enabled = rgb_matrix_is_enabled();
        if (!s_flash_rgb_was_enabled) rgb_matrix_enable_noeeprom();
    } else if (!want && s_flash_rgb_active) {
        s_flash_rgb_active = false;
        s_flash_rgb_seen   = 0;
        if (!s_flash_rgb_was_enabled) rgb_matrix_disable_noeeprom();  // mode/color auto-restore
    }
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
// Force the master to push the default layer / layer state to the slave after boot,
// regardless of diff. Armed at boot (and again in init), cleared on the first
// successful layer sync OR once the bounded try budget is spent. See the use site
// in sync_and_refresh_displays.
//
// BOUNDED retry (not a true one-shot): the previous true-one-shot version fired
// exactly once the transport reported connected, then cleared regardless of ACK —
// but right after a fw-apply reboot the slave's split-sync handler may not be ready
// to ACK yet even though the transport already reads connected, so that single send
// dropped and the slave kept its STALE default layer with nothing to re-fire it (no
// later "real" layer diff if the user never changes layers — the recurring
// wrong-slave-default-layer-after-fw-update field bug). So retry until the slave
// actually ACKs, but cap the attempts so a slow-ACK link (the HIL rig at boot) can't
// spin the main loop through the host's first HID queries the way the old UNBOUNDED
// re-fire-every-pass version did.
#define FORCE_LAYER_RESYNC_TRIES 12
static bool g_force_layer_resync = true;
static uint8_t g_force_resync_tries = FORCE_LAYER_RESYNC_TRIES;

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
            access_local_state()->contrast = get_active_brightness();
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
        access_local_state()->lang_page    = lang_pack_state();
        state_diff = differ(get_local_state(), get_global_state(), sizeof(poly_sync_t));
        if ( state_diff ) {
            // Periodic syncs use a SMALL retry count (PERIODIC_SYNC_RETRIES, was
            // briefly 1, originally 10). The diff re-fires every housekeeping pass
            // while a diff persists, but that re-fire only guarantees delivery of
            // state that PERSISTS: the re-fire sends the CURRENT snapshot, and
            // global advances to local only on a successful sync. A TRANSIENT that
            // reverts to == global before the next successful sync (a briefly-held
            // momentary layer, or an RGB/disp transition seen during the
            // post-flash settling window) is therefore dropped, or leaves the
            // slave visibly stale for a pass or more. A few in-call retries ride
            // through a single UART glitch within the same pass so the transient
            // lands. The stall this guards against: with SPLIT_MAX_CONNECTION_ERRORS
            // raised to 200 (for the fw-update erase) a failed attempt no longer
            // fast-fails — it pays a full ~40 ms UART timeout — so the worst-case
            // stall is bounded to ~PERIODIC_SYNC_RETRIES × 40 ms (and the active
            // fw-update path skips this code entirely). No effect on the normal
            // path, where the slave ACKs on the first attempt.
            if(!sync_succeeded(send_to_bridge(USER_SYNC_POLY_DATA, (void *)access_local_state(), sizeof(poly_sync_t), PERIODIC_SYNC_RETRIES))) {
                // Failed: clear state_diff so the copy_global_state() below is
                // SKIPPED — global stays != local, so next pass differ() is still
                // true and re-fires the send. The diff IS the retry queue; global
                // only advances to local on a successful sync.
                state_diff = false;
                uprint("USER_SYNC_POLY_DATA failed to send\n");
            }
        }

        // Push the MRU recents to the slave only when they changed, so both
        // halves render the top row identically.
        if (mru_sync_pending()) {
            mru_sync_t mru_msg;
            mru_emoji_pack(mru_msg.emoji);
            mru_lang_pack(mru_msg.lang);
            // Multiplexed onto the overlay-map transaction id (distinct payload
            // size) to stay within QMK's 32-transaction limit. Only the packed
            // bytes are sent (MRU_SYNC_BYTES), not the struct's crc tail padding.
            uint8_t mru_ack = send_to_bridge(USER_SYNC_OVERLAY_MAP_DATA, &mru_msg, MRU_SYNC_BYTES, PERIODIC_SYNC_RETRIES);
            if (sync_succeeded(mru_ack)) {
                mru_clear_sync_pending();
            } else {
                uprint("USER_SYNC_MRU_DATA failed to send\n");
            }
        }

        access_local_layer()->led_state = host_keyboard_led_state();
        access_local_layer()->mods = get_mods();
        layer_diff = differ(get_local_layer(), get_global_layer(), sizeof(poly_layer_t));
        // Force one layer push to the slave after boot even with no diff: each half
        // loads its OWN default layer from EEPROM, and the master only pushes on a
        // diff — so if the active default layer equals the master's last-synced
        // `global` (e.g. _L0/Qwerty = all-zero after a fresh boot/fw-apply reboot),
        // a slave that booted with a stale default layer would never be corrected
        // until the next manual layer change. The one-shot resync fixes that.
        //
        // It is a TRUE one-shot: fired only once the split transport is connected,
        // then cleared regardless of the ACK. The old version re-fired every
        // housekeeping pass until the slave ACKed, which on a slow-ACK link spun the
        // master main loop for ~3 × bridge-timeout per pass and stalled the host's
        // first HID queries (the HIL boot-window flake). Gating on
        // is_transport_connected() gives the single try a real chance; if it still
        // drops, a genuinely-stale slave is corrected by the next real layer diff.
        bool force_resync = g_force_layer_resync && is_transport_connected();
        if ( layer_diff || force_resync ) {
            bool sent = sync_succeeded(send_to_bridge(USER_SYNC_LAYER_DATA, (void *)access_local_layer(), sizeof(poly_layer_t), PERIODIC_SYNC_RETRIES));
            if (force_resync) {
                // Clear on success, else burn one try; give up once the budget is
                // spent (a genuinely-stale slave is then corrected by the next real
                // layer diff). Bounded so a slow-ACK link can't spin indefinitely.
                if (sent) {
                    g_force_layer_resync = false;
                } else if (g_force_resync_tries == 0 || --g_force_resync_tries == 0) {
                    g_force_layer_resync = false;
                }
            }
            if (sent) {
                layer_diff = true;             // ensure global advances (copy_global_layer below)
            } else {
                layer_diff = false; // failed: skip copy_global_layer() below so a real diff
                                    // persists and the send re-fires next pass
                uprint("USER_SYNC_LAYER_DATA failed to send\n");
            }
        }
        if ( differ(get_local_last_latin(), get_global_last_latin(), sizeof(poly_last_t)) ) {
            if(sync_succeeded(send_to_bridge(USER_SYNC_LASTKEY_DATA, access_local_last_latin(), sizeof(poly_last_t), PERIODIC_SYNC_RETRIES))) {
                copy_global_last_latin(get_local_last_latin());
            } else {
                // if failed to sync, do not consider it a diff and try again later
                uprint("USER_SYNC_LASTKEY_DATA failed to send\n");
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

// Sets layer state variable tracking the active keyboard layer.
layer_state_t layer_state_set_user(layer_state_t state) {
    access_local_layer()->layer = state;
    return state;
}

// Small deterministic pseudo-random offset for a given seed and axis salt, mapped
// into [lo, hi]. Used per key, per dark-episode (see kdisp_idle): the seed is a
// rolling counter so each relocation lands somewhere new, the salt separates the X
// and Y axes. Cheap integer hash — no PRNG state to carry on either half.
static int8_t jitter_axis(uint32_t epoch, uint32_t salt, int8_t lo, int8_t hi) {
    uint32_t h = (epoch + salt) * 2654435761u;
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    uint8_t range = (uint8_t)(hi - lo + 1);
    return (int8_t)(lo + (int8_t)(h % range));
}

// Continuously monitors for idle timeout and dims/pulsates display accordingly.
// Drop to the base/default layer and render it once, just before a flash holds
// the main loop — so typed keys are plain characters and the keycaps show legible
// legends during the (mostly-blocking) transfer. Called from the HID BEGIN
// handlers BEFORE fw_up activates (which freezes display updates).
void poly_prepare_for_flash(void) {
    // Exit idle FIRST. If the keyboard had dimmed/idled (DISP_IDLE, or contrast
    // pulsed down to DISP_OFF) when the flash begins, the keycaps were dark and
    // update_displays() early-returns while DISP_IDLE is set — so the legible
    // base legends never get drawn and "some keys are not lit" during the flash.
    // Force a full wake (mirrors suspend_wakeup_init_kb / the cmd-15 stop-idle
    // path) so contrast is restored and the refresh below actually renders.
    poly_sync_t* local_state = access_local_state();
    if ((local_state->flags & DISP_IDLE) != 0 || local_state->contrast == DISP_OFF) {
        local_state->flags &= ~((uint8_t)DISP_IDLE) & ~((uint8_t)IDLE_TRANSITION);
        local_state->flags |= STATUS_DISP_ON;
        local_state->contrast = get_active_brightness();
        reset_idle_jitter();       // fresh centred legends, not jittered offsets
        update_performed();
    }
    // Momentary/toggle layers off, then back onto the PolyKybd default layout.
    // A bare layer_clear() falls through to QMK's *saved* default layer
    // (default_layer_state), NOT the Poly def_layer — which is a layer INDEX
    // driven through layer_on(), same as the KC_L*/KC_BASE selectors and the
    // boot path — so a Colemak/Neo base dropped to QWERTY here, and that
    // cleared layer state was bridged to the slave, leaving the slave on the
    // QMK default layer after the flash.
    layer_clear();
    layer_on(access_local_layer()->def_layer);
    request_disp_refresh();
    // Push the base layer + refresh to the SLAVE and render the master, before
    // fw_up freezes display sync — so BOTH halves show legible base legends and
    // typing produces plain characters during the flash. sync_and_refresh_displays
    // both bridges the layer/state to the slave and calls update_displays().
    sync_and_refresh_displays();
}

#ifdef POLYKYBD_LTR559_DRIVE
// --- LTR-559 auto-brightness + idle-inhibit (opt-in; needs hardware tuning) -----
//
// The sensor is just another SOURCE feeding the existing auto-brightness path:
// the 5 s-average lux is mapped to a contrast and pushed through
// set_auto_brightness_value() — exactly the volatile/host-auto channel the PC
// host uses — so the resulting contrast reaches the slave over the existing
// poly_sync_t brightness transport.
//
// Brightness and idle are master-authoritative, so the decision runs on the
// master. The sensor lives on the RIGHT half, so:
//   * master IS the right half  -> read the sensor locally
//   * master is the LEFT half   -> pull {avg lux, proximity} from the slave over
//     the generic USER_SYNC_SLAVE_DATA channel (kind = SLAVE_DATA_SENSOR) — the
//     split slot freed by the FW_UP transaction consolidation.
// Either way driving works regardless of which half USB is plugged into.
// All tunables below are first-cut guesses to be dialled in against the OLED.
// Proximity (0..2047) that counts as "close" -> wake / inhibit idle. Measured on
// hardware: hand at ~5 cm 400, ~1 cm 1000, hole fully covered ~2000 (saturated).
// The resting (nothing near) baseline depends on the housing: ~129 on the open
// bench, ~325 once mounted in the enclosure (its walls reflect IR back). 350 was
// kept as the wake point (wakes as a hand comes within ~5-6 cm). NOTE: with the
// housed ~325 baseline the margin is only ~25 counts — raise toward 400 if the
// mounted sensor ever self-triggers; lower toward the baseline for an earlier wake.
#    define LTR559_NEAR_THRESHOLD 350
#    define LTR559_DRIVE_MS 500         // how often the master samples + applies
#    define LTR559_LUX_FULL_REF 100     // avg lux mapped to FULL_BRIGHT (ceiling)
                                        // Tuned on hardware: a ~28 lux office reads
                                        // B≈26 (was B≈19 at 200), matching the level
                                        // the user set by hand. Curve (sqrt): ~4 in a
                                        // dark room, 26 @ 28 lux, 35 @ 50 lux, full @ 100+.
#    define LTR559_MIN_CONTRAST 4       // auto-brightness floor — the sensor never
                                        // drives below this. 4 = a dim but visible
                                        // night level; still clear of the near-off
                                        // B=1/DISP_OFF. (The power-on dark-screen was a
                                        // separate boot transient, fixed by the
                                        // don't-engage-until-first-reading guard below.)

// USER_SYNC_SLAVE_DATA is a GENERIC op-dispatched slave->master pull channel (see
// config.h): the master's request is a 1-byte `kind` selecting which slave-side
// payload to return. Append a new kind + a case below to carry other data over
// the same one split slot — no new transaction needed. Both halves run the same
// firmware image, so the per-kind payload structs can change freely (no split
// versioning); the handler just bounds every copy by out_len.
enum slave_data_kind {
    SLAVE_DATA_SENSOR = 0,  // ltr559_sync_t: {avg lux, proximity}
    // SLAVE_DATA_xxx = 1, ...  // future slave-side data reuses this slot
};

typedef struct {
    uint16_t lux;   // 5 s-average lux
    uint16_t prox;  // latest raw proximity (0..2047)
} ltr559_sync_t;

// Slave side: answer the master's pull for the requested `kind`. Registered on
// both halves; only ever runs on the slave (the master initiates the exec).
static void user_sync_slave_data_handler(uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data) {
    uint8_t kind = (in_len >= 1) ? ((const uint8_t*)in_data)[0] : SLAVE_DATA_SENSOR;
    switch (kind) {
        case SLAVE_DATA_SENSOR: {
            ltr559_sync_t s = { ltr559_avg_lux(), ltr559_prox() };
            if (out_len >= sizeof(s)) {
                memcpy(out_data, &s, sizeof(s));
            }
            break;
        }
        default:
            break;  // unknown kind: leave the reply buffer as-is
    }
}

// Master-side: mirror the HID cmd-15 stop-idle path to force the displays awake.
static void poly_force_wake(void) {
    poly_sync_t* local_state = access_local_state();
    if ((local_state->flags & (STATUS_DISP_ON | DISP_IDLE)) == 0) {
        suspend_wakeup_init_kb();   // fully suspended -> full wake
    } else if (local_state->flags & DISP_IDLE) {
        local_state->contrast = get_active_brightness();
        local_state->flags &= ~((uint8_t)DISP_IDLE);
        local_state->flags |= STATUS_DISP_ON;
        reset_idle_jitter();
        request_disp_refresh();
        update_performed();
    }
}

static uint32_t isqrt32(uint32_t x) {
    uint32_t r = 0, b = 1UL << 30;
    while (b > x) b >>= 2;
    while (b) {
        if (x >= r + b) { x -= r + b; r = (r >> 1) + b; }
        else            { r >>= 1; }
        b >>= 2;
    }
    return r;
}

static uint8_t lux_to_contrast(uint16_t lux) {
    // Perceptual (sqrt) curve: brightness rises quickly out of the dark and eases
    // toward the ceiling, so ordinary indoor light already gives a usable level
    // instead of the near-off B=2 a linear map produced. LTR559_LUX_FULL_REF is
    // the lux that reaches FULL_BRIGHT. (×100 before the sqrt for resolution.)
    uint32_t sref = isqrt32((uint32_t)LTR559_LUX_FULL_REF * 100u);
    uint32_t slux = isqrt32((uint32_t)lux * 100u);
    if (slux >= sref) {
        return FULL_BRIGHT;
    }
    uint32_t c = MIN_BRIGHT + ((uint32_t)(FULL_BRIGHT - MIN_BRIGHT) * slux) / sref;
    if (c < LTR559_MIN_CONTRAST) c = LTR559_MIN_CONTRAST;  // never near-off
    if (c > FULL_BRIGHT) c = FULL_BRIGHT;
    return (uint8_t)c;
}

static void poly_ltr559_drive(void) {
    static uint32_t last    = 0;
    static bool     engaged = false;
    if (!is_usb_host_side()) {
        return;   // decisions are master-only (the slave just serves reads)
    }
    if (timer_elapsed32(last) < LTR559_DRIVE_MS) {
        return;
    }
    last = timer_read32();

    // Brightness/idle decisions are master-only; the sensor is auto-detected on
    // whichever half it's soldered to.
    uint16_t lux, prox;
    if (ltr559_available()) {
        // The master itself has the sensor — read locally.
        lux  = ltr559_avg_lux();
        prox = ltr559_prox();
    } else {
        // Sensor is on the slave (right) half — pull its latest values up over the
        // generic slave->master channel (kind = SLAVE_DATA_SENSOR), so driving
        // works in either USB orientation.
        uint8_t       kind = SLAVE_DATA_SENSOR;
        ltr559_sync_t s;
        if (!transaction_rpc_exec(USER_SYNC_SLAVE_DATA, sizeof(kind), &kind, sizeof(s), &s)) {
            return;   // slave busy this round; try again next tick
        }
        lux  = s.lux;
        prox = s.prox;
    }

    // Auto-brightness from the 5 s average lux, via the same volatile/host-auto
    // path the host uses (keeps the manual brightness untouched).
    //
    // Don't engage until the sensor has produced a real reading: for the first
    // ~1 s after boot the 5 s average is still 0 (no samples), and engaging then
    // would yank the displays down to the floor. Hold at the manual/restored
    // brightness until the first non-zero average, then engage. Once engaged we
    // keep applying — a genuine dark-room 0 is floored by lux_to_contrast (never
    // off), so a momentary 0 can't blank the keys.
    if (!engaged) {
        if (lux == 0) {
            return;
        }
        set_brightness_auto_mode(true);
        engaged = true;
    }
    set_auto_brightness_value(lux_to_contrast(lux));

    // Proximity: something is close -> defer idle (and wake if already idle).
    if (prox > LTR559_NEAR_THRESHOLD) {
        poly_force_wake();
        update_performed();
    }
}
#endif  // POLYKYBD_LTR559_DRIVE

#ifdef POLYKYBD_LINK_DIAG
static void render_link_diag(void);   // defined near show_splash_screen()
#endif

void housekeeping_task_user(void) {
#ifdef RGB_MATRIX_ENABLE
    flash_rgb_tick();   // light the matrix while a font-pack/firmware flash runs
#endif
#ifdef POLYKYBD_LINK_DIAG
    // Bring-up: repaint the split-link status on the top keycap row ~1×/s so the
    // slave (no console) shows live whether it is receiving. Overwrites row-0
    // legends by design — this is a diagnostic build only.
    static uint32_t link_diag_timer = 0;
    if (link_diag_timer == 0 || timer_elapsed32(link_diag_timer) >= 750) {
        render_link_diag();
        link_diag_timer = timer_read32();
    }
#endif

    // fw_up state machine: apply on success path, advance deferred erase.
    // Both must run regardless of fw_up_active so the slave's erase actually
    // progresses and the master's apply-and-reboot fires after a successful
    // commit.
    if (fw_staging_commit_pending()) {
        save_all_dirty();   // persist MRU/settings before the firmware swap — this path resets via watchdog (never returns) and skips shutdown_quantum. Transfer is done by commit, so the blocking flash write is safe here.
        fw_staging_apply_and_reboot();
    }
    if (fw_staging_reboot_pending()) {
        save_all_dirty();   // persist before the full-chip reset — this path skips shutdown_quantum too
        mcu_reset();   // QK_REBOOT slave path — clean full-chip reset; never returns
    }
    fw_staging_process_deferred();

    // Drain the slave's deferred FONTPACK reload (the ~50 ms full-body verify was
    // moved off the COMMIT split-transaction callback so it can't overrun the
    // ~20 ms window). Refresh the keycaps once the new pack's fonts are live.
    if (fw_staging_process_fontpack_reload()) {
        request_disp_refresh();
    }

    // While a fw_up is in progress, skip EEPROM saves (wear-leveling consolidate
    // is ~100 ms IRQ-off) and the display refresh path (slave update_displays
    // can be ~50-100 ms over SPI, master state-push uses 10 retries × 80 ms).
    // Both would starve the split UART that the chunk transport relies on.
    if (!fw_staging_fw_up_active()) {
        // All user state is flushed at suspend/shutdown or via the store key
        // (KC_STORE_EE); the only housekeeping write is draining that one-shot
        // store request (on the master locally, on the slave via SAVE_EEPROM).
        save_all_if_requested();
        // Doom easter egg frame tick (no-op unless POLYKYBD_DOOM + game mode).
        // Runs before the display sync: it keeps last_update fresh so the
        // idle/fade pipeline below never fights the game blitter.
        doom_tick();
        sync_and_refresh_displays();
#ifdef POLYKYBD_LTR559
        // Poll the expansion-port light/proximity sensor. Run on BOTH halves —
        // the sensor is auto-detected on whichever half it's soldered to (left or
        // right). Internally throttled + non-blocking; on the half without it the
        // probe gives up after a bounded number of retries so it can't stall.
        ltr559_task();
        // Sensor telemetry heartbeat: only the half that actually has the sensor
        // logs (gated on ltr559_available()), and only every LTR559_LOG_MS (10 min)
        // so it's a periodic reading, not spam. This replaces the live status-OLED
        // readout used during bring-up. NOTE: this rolls its own timer; if a third
        // timed log ever appears, factor them into one shared timer — see
        // readme.md "Diagnostics" -> "Timed console logs".
        if (ltr559_available()) {
            static uint32_t s_ltr_log = 0;
            if (timer_elapsed32(s_ltr_log) >= LTR559_LOG_MS) {
                s_ltr_log = timer_read32();
                ltr559_reading_t r;
                ltr559_get_reading(&r);
                uprintf("LTR-559: lux=%u avg=%u prox=%u%s ch0=%u ch1=%u B=%u\n",
                        r.lux, ltr559_avg_lux(), r.prox, r.prox_sat ? " SAT" : "",
                        r.ch0, r.ch1, get_local_state()->contrast);
            }
        }
#    ifdef POLYKYBD_LTR559_DRIVE
        poly_ltr559_drive();   // master-side auto-brightness + idle-inhibit
#    endif
#endif
    }
    if(is_idle_tracking()) {
        //turn off displays
        // Full uint32 elapsed via timer_elapsed32 — no sign gate, so idle keeps
        // working past ~24.86 days of uptime (when timer_read32() sets bit 31).
        uint32_t elapsed_time_since_update = get_time_since_last_update();
        if (is_usb_host_side()) {
            poly_sync_t* local_state = access_local_state();
            uint8_t  contrast = local_state->contrast;
            uint8_t  flags = local_state->flags;

            flags |= STATUS_DISP_ON;
            flags &= ~((uint8_t)IDLE_TRANSITION);

            if(elapsed_time_since_update > FADE_OUT_TIME && contrast >= MIN_BRIGHT && (flags & DISP_IDLE)==0) {
                int32_t time_after = elapsed_time_since_update - FADE_OUT_TIME;
                int16_t brightness = ((FADE_TRANSITION_TIME - time_after) * get_active_brightness()) / FADE_TRANSITION_TIME;

                //transition to pulsing mode
                if(brightness<=MIN_BRIGHT) {
                    if (get_idle_style() == IDLE_STYLE_IDDQD && doom_screensaver_start()) {
                        // Doom attract screensaver instead of the pulse: the demo
                        // owns the keycaps at the user brightness — no DISP_IDLE,
                        // and IDLE_TRANSITION stays dropped (cleared above), which
                        // fires the back_from_idle_transition brightness restore.
                        // doom_tick() holds last_update while the demo runs and
                        // hands over to the normal TURN_OFF suspend at its own
                        // deadline. Falls through to the pulse whenever the demo
                        // can't start (non-doom build, fw staging active).
                        contrast = get_active_brightness();
                        uprint("Transition to doom screensaver\n");
                    } else {
                        contrast = DISP_OFF;
                        flags |= DISP_IDLE;
                        flags |= IDLE_TRANSITION;
                        uprint("Transition to pulsing\n");
                    }
                } else if(brightness>FULL_BRIGHT) {
                    contrast = FULL_BRIGHT;
                    flags |= IDLE_TRANSITION;
                    uprint("Limiting brightness\n");
                } else{
                    contrast = brightness;
                    flags |= IDLE_TRANSITION;
                }
            } else if(elapsed_time_since_update > TURN_OFF_TIME) {
                uprint("Turning off\n");
                poly_suspend();
                disable_idle_tracking();
                contrast = local_state->contrast;
                flags = local_state->flags;
            } else if((flags & DISP_IDLE)!=0) {
                int32_t time_after = PK_MAX(elapsed_time_since_update - FADE_OUT_TIME - FADE_TRANSITION_TIME, 0)/300;
                contrast = time_after%50;
                // In JITTER style each key relocates its own legend independently as
                // it pulses dark (kdisp_idle) — there is no shared per-cycle offset
                // to compute here; only the pulse `contrast` drives both halves.
            } else {
                flags &= ~((uint8_t)DISP_IDLE);
            }

            local_state->contrast = contrast;
            local_state->flags = flags;
            // Sync the active idle style so the slave jitters in lockstep with us.
            local_state->idle_style = get_idle_style();
        }
    }
    // Refresh the synced active-OS on the MASTER every pass — it is the single
    // source of truth (resolves manual pin / host push / USB detection); the slave
    // adopts it via copy_local_state for its legends/actions. Must be master-only:
    // get_active_os() reads this half's own state, which on the slave is empty and
    // would clobber the just-synced value. Cheap (a couple of branches) and the
    // diff-gated poly sync only crosses the UART when the value actually changes.
    if (is_usb_host_side()) {
        uint8_t os = (uint8_t)(get_active_os() | (get_os_auto_mode() ? POLY_OS_AUTO_FLAG : 0));
        if (access_local_state()->active_os != os) {
            access_local_state()->active_os = os;
            request_disp_refresh();   // OS or auto/pin mode changed -> re-render legends + icon
        }
        // Master-authoritative glyph-script override; the slave adopts it via
        // copy_local_state and re-renders its own legends on the synced diff.
        if (access_local_state()->glyph_script != get_glyph_script()) {
            access_local_state()->glyph_script = get_glyph_script();
            request_disp_refresh();   // script changed -> re-render letter/digit legends
        }
        // Doom game mode: synced so the SLAVE strips its legends down to the
        // game controls (the split_sync poly handler refreshes on the diff).
        // No master-side refresh — its keycaps are owned by the game blitter
        // while active, and doom_exit() restores them itself. The weapon-pad
        // state rides along; a change re-renders the slave's pad keys.
        // 2 = attract screensaver: chrome-free — the slave renders NO pad/ESC
        // legends, every keycap belongs to the mirror blitter (full-bleed demo).
        uint8_t doom_want = doom_mode_active() ? (doom_mode_screensaver() ? 2 : 1) : 0;
        if (access_local_state()->doom_ctl != doom_want) {
            // Breadcrumb: this is the value the next POLY sync carries to the
            // slave — "slave stayed in game mode" reports hinge on it.
            uprintf("doom ctl -> %u\n", doom_want);
        }
        access_local_state()->doom_ctl = doom_want;
        uint8_t wpn_owned = 0, wpn_ready = 0;
        if (access_local_state()->doom_ctl) {
            doom_weapon_state(&wpn_owned, &wpn_ready);
        }
        access_local_state()->doom_wpn_owned = wpn_owned;
        access_local_state()->doom_wpn_ready = wpn_ready;
    }
}



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


// Returns display text for special keys.
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
        // Doom easter-egg menu item: blank until typing IDDQD arms it
        // (doom_mode.c; always blank in non-doom builds via the stub).
        case KC_IDDQD:                      return doom_egg_armed() ? U"IDDQD" : U"";

        // Language selection keycodes: the tiny "xx-YY" code shown under the flag
        // (the flag + selection frame are drawn by render_lang_flag_key()). KCL_ENUS..
        // are contiguous (QK_USER_0-based), so index a cog-generated table by offset.
        case KCL_ENUS ... KCL_ENUS + NUM_LANG - 1: {
            static const uint32_t* const lang_code[NUM_LANG] = {
                /*[[[cog
                for lang in languages:
                    cog.outl(f'U"{lang[0:2]}-{lang[2:]}",')
                ]]]*/
                U"en-US",
                U"de-DE",
                U"fr-FR",
                U"es-ES",
                U"pt-PT",
                U"it-IT",
                U"tr-TR",
                U"ko-KR",
                U"ja-JP",
                U"ar-SA",
                U"el-GR",
                U"uk-UA",
                U"ru-RU",
                U"be-BY",
                U"kk-KZ",
                U"bg-BG",
                U"pl-PL",
                U"ro-RO",
                U"zh-CN",
                U"nl-NL",
                U"he-IL",
                U"sv-SE",
                U"fi-FI",
                U"nn-NO",
                U"da-DK",
                U"hu-HU",
                U"cs-CZ",
                U"hr-HR",
                U"sk-SK",
                U"lt-LT",
                U"lv-LV",
                U"et-EE",
                U"pt-BR",
                U"sr-RS",
                U"mk-MK",
                U"fa-IR",
                U"hi-IN",
                U"mr-IN",
                U"ne-NP",
                U"mn-MN",
                U"ur-PK",
                U"en-GB",
                U"es-MX",
                U"de-CH",
                U"fr-BE",
                U"fr-CA",
                U"th-TH",
                U"bn-IN",
                U"te-IN",
                U"ta-IN",
                U"zh-TW",
                U"ka-GE",
                U"hy-AM",
                U"id-ID",
                U"az-AZ",
                U"is-IS",
                U"vi-VN",
                U"zh-HK",
                U"en-AU",
                U"en-NZ",
                U"mi-NZ",
                U"sm-WS",
                U"fj-FJ",
                U"tl-PH",
                U"hw-US",
                U"en-ZA",
                U"af-ZA",
                U"ar-EG",
                U"sw-KE",
                U"am-ET",
                U"yo-NG",
                U"en-NG",
                U"ar-MA",
                U"ar-IQ",
                U"ku-IQ",
                U"ms-MY",
                U"uz-UZ",
                U"en-CA",
                U"es-AR",
                U"en-PG",
                U"ty-PF",
                U"es-CO",
                U"es-PE",
                U"es-VE",
                U"es-CL",
                U"es-EC",
                U"es-GT",
                U"es-DO",
                U"es-BO",
                U"es-PY",
                U"es-CR",
                U"es-SV",
                U"es-HN",
                U"es-PA",
                U"es-UY",
                U"es-NI",
                U"de-AT",
                U"nl-BE",
                U"ca-ES",
                U"en-IE",
                U"bs-BA",
                U"fr-CH",
                U"sl-SI",
                U"fo-FO",
                U"ar-AE",
                U"ar-SY",
                U"ar-JO",
                U"ar-LB",
                U"ar-YE",
                U"ar-KW",
                U"ar-OM",
                U"ar-PS",
                U"ar-QA",
                U"ar-BH",
                U"ar-DZ",
                U"ar-SD",
                U"ar-TN",
                U"ar-LY",
                U"fr-CD",
                U"fr-CI",
                U"fr-CM",
                U"fr-SN",
                U"fr-MG",
                U"en-GH",
                U"en-UG",
                U"en-ZM",
                U"sw-TZ",
                U"pt-AO",
                U"pt-MZ",
                U"bn-BD",
                U"en-IN",
                U"en-PK",
                U"en-PH",
                U"en-SG",
                U"en-LK",
                U"ky-KG",
                U"tg-TJ",
                U"en-GU",
                U"en-SB",
                U"en-VU",
                U"en-FM",
                U"fr-NC",
                U"to-TO",
                U"eu-ES",
                U"gl-ES",
                U"rm-CH",
                U"cy-GB",
                U"ga-IE",
                U"mt-MT",
                U"lb-LU",
                U"se-NO",
                U"gn-PY",
                U"qu-PE",
                U"ay-BO",
                U"nv-US",
                U"nh-MX",
                U"ps-AF",
                U"iu-CA",
                U"cr-CA",
                U"ck-US",
                //[[[end]]]
            };
            return lang_code[keycode - KCL_ENUS];
        }
        default:
            return NULL;
    }
}

// True if `s` is a single bare combining mark (ignoring positioning control codes) —
// e.g. the Devanagari/Bengali nukta used as an AltGr "+nukta" hint. Drawn on its own
// such a mark is invisible, so the AltGr-held view composes it onto the base glyph.
static bool altgr_is_bare_combining(const uint32_t* s) {
    uint32_t cp = 0;
    for (; *s; ++s) {
        if (*s < 0x20) continue;          // skip preview-positioning control codes
        if (cp) return false;             // more than one visible glyph -> not a bare mark
        cp = *s;
    }
    return cp == 0x093C || cp == 0x09BC;  // Devanagari / Bengali nukta (extend as needed)
}

// Renders key character to display using language translation, including modifiers etc.
// Glyph-script override codepoints. Each alternative script's glyphs are emitted
// into a private, dense, collision-free PUA range (fontconvert sequence -F remap),
// NOT the source font's native codepoints — the flags font-pack bundle already
// occupies 0xE000+, so a raw CSUR tengwa would render a language flag instead.
//
// Each glyph script's glyphs live in a dense private PUA block (disjoint from the
// flags bundle at 0xE000+ and from each other): letters a..z at base+0..25, and —
// for scripts that have their own numerals — digits 1..0 at base+26..35 (KC_1..KC_0
// are contiguous with 0 last). The per-key glyph choice lives in each font's
// generation sequence (fonts.yaml `-F<base>` relocation), so the firmware only
// needs the base + a dense index. Scripts with `digits=false` leave the digit keys
// as the normal numeral (e.g. Aurebesh/Cirth have no number system of their own).
// The table is indexed by `enum poly_glyph_script`; keep it in sync with state.h.
typedef struct { uint32_t base; bool digits; } glyph_script_block_t;
static const glyph_script_block_t glyph_script_blocks[GLYPH_SCRIPT_COUNT] = {
    [GLYPH_STD]      = { 0u,      false },
    [GLYPH_TENGWAR]  = { 0xE800u, true  },
    [GLYPH_RUNES]    = { 0xE840u, false },
    [GLYPH_AUREBESH] = { 0xE880u, false },
    [GLYPH_SGA]      = { 0xE8C0u, true  },
    [GLYPH_CIRTH]    = { 0xE900u, false },
    [GLYPH_IBMVGA]   = { 0xE940u, true  },
    [GLYPH_C64]      = { 0xE980u, true  },
    [GLYPH_AMIGA]    = { 0xE9C0u, true  },
    [GLYPH_APL]      = { 0xEA00u, true  },
    [GLYPH_BRAILLE]  = { 0xEA40u, true  },
};

// The dense mapping relies on the USB-HID keycodes being contiguous
// (KC_A..KC_Z, and KC_1..KC_0 with 0 last); guard that against any future change.
_Static_assert(KC_Z - KC_A == 25, "KC_A..KC_Z must be contiguous for the glyph-script map");
_Static_assert(KC_0 - KC_1 == 9,  "KC_1..KC_0 must be contiguous for the glyph-script map");

// Resolves the override codepoint for a key under the active glyph script, or 0
// when the key/script has no override (falls through to the normal legend). Only
// plain letters/digits are overridden; symbols, function keys, etc. are left alone.
static uint32_t glyph_script_codepoint(uint8_t script, uint16_t keycode) {
    if (script == GLYPH_STD || script >= GLYPH_SCRIPT_COUNT) return 0;
    const glyph_script_block_t blk = glyph_script_blocks[script];
    if (keycode >= KC_A && keycode <= KC_Z) return blk.base + (uint32_t)(keycode - KC_A);
    // KC_1..KC_0 are contiguous (1 first, 0 last) -> dense indices 26..35.
    if (blk.digits && keycode >= KC_1 && keycode <= KC_0) return blk.base + 26u + (uint32_t)(keycode - KC_1);
    return 0;
}

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
            kdisp_write_gfx_text(g_all_fonts, g_all_font_count, BUFFER_X, 23, latin_ex_map[offset+keycode-KC_A][variation]);
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
                kdisp_write_gfx_text(g_all_fonts, g_all_font_count, BUFFER_X, 23, variation);
                return true;
            }
        }
        return false;
    }

    const poly_sync_t* local_state = get_local_state();

    // Glyph-script override: on the normal language layer, replace a letter/digit's
    // legend with the selected alternative script (e.g. Tengwar), centered in the
    // keycap. Overlays and OS-hints are drawn elsewhere and are untouched. Falls
    // through to the normal legend when the script has no glyph for this key OR the
    // font-pack bundle providing it is not present (so a keyboard without the
    // "fantasy" bundle still shows Latin rather than blanks). While AltGr is held we
    // fall through to the real AltGr symbol (translate_keycode_only_altgr below) —
    // the override is only for the resting/base letter legend, and the AltGr output
    // is a genuinely different character, not a cased form of the same letter.
    if (local_state->glyph_script != GLYPH_STD && !add_lang && !(mods & MOD_RALT)) {
        uint32_t cp = glyph_script_codepoint(local_state->glyph_script, keycode);
        if (cp != 0 && kdisp_gfx_glyph(g_all_fonts, g_all_font_count, cp) != NULL) {
            const uint32_t s[2] = { cp, 0 };
            // Center the glyph in BOTH axes from its full pixel bbox, rather than
            // drawing at the base font's fixed y=23 baseline. The script fonts are
            // rendered taller (~30 px) than the 14 px Latin base, so at baseline 23
            // a tall glyph's top (23 + yOffset) lands above 0 and clips off the top
            // of the 40 px display (Amiga/C64/APL ran ~10 px off-screen). bbox gives
            // the y extent too, so we place the baseline to fit — same idea the idle
            // jitter uses (roll_idle_offset).
            int8_t xmin, xmax, ymin, ymax;
            kdisp_gfx_text_bbox(g_all_fonts, g_all_font_count, s, &xmin, &xmax, &ymin, &ymax);
            int8_t gx = (int8_t)(BUFFER_X + (SCREEN_WIDTH  - (xmax - xmin + 1)) / 2 - xmin);
            int8_t gy = (int8_t)(         (SCREEN_HEIGHT - (ymax - ymin + 1)) / 2 - ymin);
            kdisp_write_gfx_text(g_all_fonts, g_all_font_count, gx, gy, s);
            return true;
        }
    }

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
                // A bare combining mark (the nukta "+nukta" AltGr hint) is invisible on
                // its own when AltGr is actually held — compose it onto the base
                // consonant (क + ़ = क़) so the held view shows the real output. The
                // unshifted preview still draws the lone dot via the cell's own controls.
                if (altgr_is_bare_combining(letter)) {
                    const uint32_t* base = translate_keycode(local_state->lang, keycode, false, false);
                    if (base != NULL) {
                        uint32_t composed[10]; uint8_t ci = 0;
                        for (const uint32_t* p = base;   *p && ci < 8; ++p) composed[ci++] = *p;
                        for (const uint32_t* p = letter; *p && ci < 9; ++p) if (*p >= 0x20) composed[ci++] = *p;
                        composed[ci] = 0;
                        kdisp_write_gfx_text(g_all_fonts, g_all_font_count, 28+h_off, 23+v_off, composed);
                        return true;
                    }
                }
                kdisp_write_gfx_text(g_all_fonts, g_all_font_count, 28+h_off, 23+v_off, letter);
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
                    kdisp_gfx_text_bounds(g_all_fonts, g_all_font_count, letter, &bmin, &bmax);
                    kdisp_gfx_text_bounds(g_all_fonts, g_all_font_count, shift_letter, &pmin, &pmax);
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

        kdisp_write_gfx_text(g_all_fonts, g_all_font_count, base_x, 23+base_v, letter);
        if (shift_letter != NULL)
            kdisp_write_gfx_text(g_all_fonts, g_all_font_count, preview_x, 23+preview_v, shift_letter);

        //preview alt representation
        letter = translate_keycode_only_altgr(local_state->lang, keycode);
        if (letter != NULL) {
            int8_t v_off = get_setting(v_set, local_state->lang, VAR_ALTGR);
            int8_t h_off = get_setting(h_set, local_state->lang, VAR_ALTGR);
            if(v_off!=HIDE_KEY && h_off!=HIDE_KEY) {
                // Clamp to the right edge like the shift preview — wide glyphs
                // (e.g. @ on the French/Tahitian 0 key) otherwise clip off-screen.
                int8_t amin, amax;
                kdisp_gfx_text_bounds(g_all_fonts, g_all_font_count, letter, &amin, &amax);
                int8_t alt_x = 28+h_off;
                if (alt_x + amax > BUFFER_X + SCREEN_WIDTH - 1)
                    alt_x = (int8_t)((BUFFER_X + SCREEN_WIDTH - 1) - amax);
                kdisp_write_gfx_text(g_all_fonts, g_all_font_count, alt_x, 23+v_off, letter);
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
    // OS-aware shortcut-preview icons. The "editing" shortcuts (copy/paste/undo/…)
    // hang off the OS's primary command modifier: Cmd (GUI) on macOS, Ctrl
    // everywhere else — so on a Mac these show under Cmd, not Ctrl (where Ctrl+C
    // does not copy). The "window-management" shortcuts (lock/show-desktop/display/
    // maximize/minimize) hang off the GUI/Super key on Windows & Linux desktops; on
    // macOS Cmd is already the editing modifier (so e.g. Cmd+L is NOT lock and shows
    // nothing here), and on Android the Search key is not a window manager.
    const uint8_t active_os = get_local_state()->active_os & POLY_OS_VALUE_MASK;
    const bool apple = (active_os == POLY_OS_MACOS);
    // Collapse left/right modifier sides into one logical set (bit0 Ctrl, bit1 Shift,
    // bit2 Alt, bit3 GUI — the MOD_L* bit values) so every hint below matches the EXACT
    // set of modifiers held. Extra modifiers now disqualify a chord instead of leaking a
    // subset match (Win+Ctrl+Shift+X no longer shows the Win+Ctrl+X hint, Win+Ctrl+C no
    // longer falls through to plain Ctrl+C, etc.). Side (L/R) is intentionally ignored.
    const uint8_t mods_now = (uint8_t)((local_mods | (local_mods >> 4)) & 0x0F);
    if (apple) {
        // macOS: editing lives on Cmd (GUI). Each block is an exact modifier set.
        if (mods_now == (MOD_LGUI | MOD_LCTL)) {
            switch(keycode) {
                case KC_Q: return U"    " PRIVATE_LOCK;       // Ctrl+Cmd+Q = lock screen
                case KC_F: return U"     " PRIVATE_MAXIMIZE;  // Ctrl+Cmd+F = fullscreen
                default: break;
            }
        } else if (mods_now == MOD_LALT) {
            // Word nav on macOS is Option(Alt)+arrows (line nav is Cmd+arrows, below).
            switch(keycode) {
                case KC_LEFT:  return U"    " ICON_WORD_LEFT;
                case KC_RIGHT: return U"    " ICON_WORD_RIGHT;
                default: break;
            }
        } else if (mods_now == (MOD_LGUI | MOD_LSFT)) {
            // Cmd+Shift+Z = redo (mac has no Cmd+Y redo).
            switch(keycode) {
                case KC_Z: return U"      " ARROWS_REDO;
                default: break;
            }
        } else if (mods_now == MOD_LGUI) {
            switch(keycode) {
                case KC_A: return U"      " BOX_WITH_CHECK_MARK;
                case KC_C: return U"     " CLIPBOARD_COPY;
                case KC_F: return U"    " PRIVATE_FIND;
                case KC_X: return U"\t\b\b" CLIPBOARD_CUT;
                case KC_V: return U"     " CLIPBOARD_PASTE;
                case KC_S: return U"\t" PRIVATE_FLOPPY;
                case KC_O: return U"\t" FILE_OPEN;
                case KC_P: return U"\t" PRIVATE_PRINTER;
                case KC_M: return U"     " PRIVATE_WINDOW;    // Cmd+M = minimize
                case KC_Z: return U"      " ARROWS_UNDO;      // Cmd+Z = undo (Cmd+Shift+Z redo above)
                // OS-aware shortcut hints (wave B). Tab uses the narrow ARROWS_TAB
                // base legend, so 4 spaces clear it; Space gets 3.
                case KC_TAB:   return U"    " ICON_APP_SWITCH;    // Cmd+Tab app switcher
                case KC_SPACE: return U"   "  ICON_LAUNCHER;      // Cmd+Space (Spotlight)
                case KC_W:     return U"    " ICON_CLOSE;         // Cmd+W close
                case KC_Q:     return U"    " ICON_CLOSE;         // Cmd+Q quit
                case KC_GRV:   return U"    " ICON_WINDOW_SWITCH; // Cmd+` window switcher
                case KC_LEFT:  return U"    " ARROWS_LEFTSTOP;    // Cmd+Left  line start
                case KC_RIGHT: return U"    " ARROWS_RIGHTSTOP;   // Cmd+Right line end
                default: break;
            }
        }
    } else {
    // Windows / Linux / Android / undetected: editing on Ctrl, window-mgmt on GUI.
    // The two host-detected Linux desktops (GNOME/KDE) behave as Linux here, but a
    // few Super-key hints differ between them — see the Super (GUI) switch below.
    const bool gnome = (active_os == POLY_OS_LINUX_GNOME);
    const bool win_or_unknown = (active_os == POLY_OS_WINDOWS || active_os == POLY_OS_UNKNOWN);
    const bool linux_any = (active_os == POLY_OS_LINUX
                            || active_os == POLY_OS_LINUX_GNOME
                            || active_os == POLY_OS_LINUX_KDE);
    const bool wm = (win_or_unknown || linux_any);   // OSes whose window-mgmt hangs off GUI/Super
    // Windows multi-modifier Super chords (wave D), each on its EXACT modifier set. An
    // unmatched key returns nothing (no fall-through to the Ctrl/Alt editing hints) —
    // Win+Ctrl+C is a different chord from Ctrl+C, so it no longer previews "copy".
    if (win_or_unknown && mods_now == (MOD_LGUI | MOD_LCTL | MOD_LSFT)) {
        switch(keycode) {
            // Win+Ctrl+Shift+B restart graphics: monitor 🖵, then MOVE to the screen
            // cavity and HALF-draw the reload 🗘 into it.
            case KC_B: return U"    " ICON_GFX_RESTART HINT_MOVE(HINT_POS_SCREEN) HINT_HALF ICON_GFX_RELOAD;
            default: break;
        }
    } else if (win_or_unknown && mods_now == (MOD_LGUI | MOD_LCTL)) {
        switch(keycode) {
            // Virtual-desktop chords: a compact monitor glyph (ICON_DESKTOP_SMALL)
            // composed with +/←/→/x so the action reads next to the screen.
            case KC_D:     return U"  " PRIVATE_SCREEN U"+";             // Win+Ctrl+D new virtual desktop
            case KC_LEFT:  return U"  " ICON_LEFT PRIVATE_SCREEN;        // Win+Ctrl+Left  previous desktop
            case KC_RIGHT: return U"  " PRIVATE_SCREEN ICON_RIGHT;       // Win+Ctrl+Right next desktop
            case KC_F4:    return U"  " PRIVATE_SCREEN U"x";             // Win+Ctrl+F4 close desktop
            case KC_F:     return U"    " ICON_NET;                       // Win+Ctrl+F search network computers (🖧 pack glyph)
            case KC_V:     return U"   "  ICON_VOLUME_MIXER;              // Win+Ctrl+V volume mixer (🔊 pack glyph; mixer flyout on Win 11 24H2+)
            case KC_N:     return U"    "  ICON_NARRATOR;                 // Win+Ctrl+N Narrator settings (👂 pack glyph)
            case KC_Q:     return U"   "   ICON_QUICK_ASSIST;             // Win+Ctrl+Q Quick Assist (🤝 pack glyph)
            case KC_S:     return U"   "   ICON_SPEECH_REC;               // Win+Ctrl+S Speech Recognition (🎤 pack glyph)
            default: break;
        }
    } else if (win_or_unknown && mods_now == (MOD_LGUI | MOD_LALT)) {
        switch(keycode) {
            case KC_R: return U"   " ICON_SCREEN_RECORD;       // Win+Alt+R start/stop screen recording
            default: break;
        }
    }
    if (mods_now == (MOD_LCTL | MOD_LSFT)) {
        switch(keycode) {
            case KC_Z: return U"      " ARROWS_REDO;        // Ctrl+Shift+Z redo (Linux/cross-app)
            default: break;
        }
    } else if (mods_now == MOD_LCTL) {
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
            case KC_Y: return U"      " ARROWS_REDO;         // Ctrl+Y redo (Windows)
            case KC_Z: return U"      " ARROWS_UNDO;         // Ctrl+Z undo (Ctrl+Shift+Z redo above)
            // OS-aware shortcut hints (wave B): word nav + close on Ctrl.
            case KC_LEFT:  return U"    " ICON_WORD_LEFT;   // Ctrl+Left  word left
            case KC_RIGHT: return U"    " ICON_WORD_RIGHT;  // Ctrl+Right word right
            case KC_W:     return U"    " ICON_CLOSE;       // Ctrl+W close
            default: break;
        }
    } else if (mods_now == MOD_LALT) {
        switch(keycode) {
            case KC_TAB: return U"    " ICON_APP_SWITCH;    // Alt+Tab app switcher
            case KC_F4:  return U"    " ICON_CLOSE;         // Alt+F4 close
            default: break;
        }
    } else if (win_or_unknown && mods_now == (MOD_LGUI | MOD_LSFT)) {
        switch(keycode) {
            case KC_S: return U"   " ICON_SNIP;             // Win+Shift+S Snipping Tool (region capture)
            default: break;
        }
    } else if (wm && mods_now == MOD_LGUI) {
        switch(keycode) {
            case KC_D:
                // Show desktop: Win+D and KDE Super+D. GNOME has no default
                // show-desktop chord, so don't show it there.
                if (!gnome) return U"    " PRIVATE_PC;
                break;
            case KC_L:      return U"    " PRIVATE_LOCK;       // Win/Super+L lock
            case KC_P:      return U"    " PRIVATE_SCREEN;     // Win/Super+P display
            case KC_UP:     return U"     " PRIVATE_MAXIMIZE;  // Super+Up maximize
            case KC_DOWN:   return U"     " PRIVATE_WINDOW;    // Super+Down minimize
            // Super+Tab switches: Windows (Task View) and GNOME (switch apps). On
            // KDE / generic Linux the switcher is Alt+Tab (shown via the Alt branch),
            // and Super+Tab is unbound — so don't show it there.
            case KC_TAB:
                if (win_or_unknown || gnome) return U"    " ICON_WINDOW_SWITCH;
                break;
            // Launcher/search on a Super chord is Windows-only (Win+S). GNOME uses the
            // Super overview and KDE a Super-tap / Alt+Space — neither binds Super+S — so
            // show it only on Windows (and the unknown default). Win+Shift+S (Snipping
            // Tool) is handled in its own block above.
            case KC_S:
                if (win_or_unknown) return U"   " ICON_LAUNCHER;
                break;
            // Windows-only Super-chords (wave C). These have no standard GNOME/KDE
            // equivalent, so they are gated on win_or_unknown only. Dictation (Win+H)
            // is Windows-specific: macOS triggers it with a double-tap Fn/Ctrl (not a
            // GUI+letter chord the hint engine can preview), and Linux/Android bind no
            // standard dictation chord.
            // Leading-space counts tuned per glyph (oled_preview) so each wide emoji
            // glyph sits as far right as it fits without clipping the 72 px window —
            // matching the existing hints' placement. M reuses the 5-space minimize
            // legend (= Super+Down); X's narrower glyph takes 4.
            case KC_H:
                if (win_or_unknown) return U"   "   ICON_DICTATION;     // Win+H dictation
                break;
            case KC_I:
                if (win_or_unknown) return U"   "   ICON_SETTINGS;      // Win+I settings (⚙ pack glyph)
                break;
            case KC_M:
                if (win_or_unknown) return U"      " PRIVATE_MINIMIZE;  // Win+M minimize all (🗕)
                break;
            case KC_R:
                // Win+R run dialog: draw the run-dialog FRAME at its top-left, reset the
                // cursor to the origin, then draw the base-font ">_" (4 spaces,
                // right-of-centre) inside it.
                if (win_or_unknown) return HINT_MOVE(HINT_POS_RUNBOX) HINT_FRAME(HINT_SZ_RUNBOX) HINT_RESET U"    >_";
                break;
            case KC_T:
                if (win_or_unknown) return U"   "   ICON_TASK_CYCLE;    // Win+T cycle taskbar
                break;
            case KC_K:
                if (win_or_unknown) return U"   "   ICON_CAST;          // Win+K cast (📶 pack glyph)
                break;
            case KC_V:
                if (win_or_unknown) return U"   "   ICON_CLIP_HISTORY;  // Win+V clipboard history
                break;
            case KC_X:
                if (win_or_unknown) return U"    "  ICON_QUICK_MENU;    // Win+X quick-link menu
                break;
            case KC_COMMA:
                if (win_or_unknown) return U"   "   ICON_PEEK;          // Win+, peek desktop
                break;
            case KC_DOT:
                if (win_or_unknown) return U"   "   PRIVATE_EMOJI_1F600; // Win+. emoji panel
                break;
            // More Windows-only Super-chords (wave D). Leading-space counts tuned per
            // glyph (hint_preview) so each sits as far right as it fits without
            // clipping the 72 px window, matching the existing hints' placement.
            case KC_A:
                if (win_or_unknown) return U"      " ICON_LIGHTNING;    // Win+A Action Center/Quick Settings
                break;
            case KC_E:
                if (win_or_unknown) return U"    "  ICON_EXPLORER;      // Win+E File Explorer (folder pixmap)
                break;
            case KC_U:
                if (win_or_unknown) return U"      " ICON_ACCESSIBILITY;// Win+U Accessibility settings
                break;
            case KC_B:
                if (win_or_unknown) return U"   "   ICON_MAC_CONTROL;   // Win+B focus system tray (⌃ mac-control caret / show-hidden-icons chevron)
                break;
            case KC_HOME:
                if (win_or_unknown) return U"     " ICON_FOCUS_WINDOW;  // Win+Home minimize all but active
                break;
            case KC_LEFT:
                if (win_or_unknown) return U"     " ICON_SNAP_LEFT;     // Win+Left snap window left (⍇ pack glyph)
                break;
            case KC_RIGHT:
                if (win_or_unknown) return U"     " ICON_SNAP_RIGHT;    // Win+Right snap window right (⍈ pack glyph)
                break;
            case KC_SCLN:
                if (win_or_unknown) return U"   "   ICON_GIF;           // Win+; GIF / emoji panel
                break;
            case KC_PAUSE:
                if (win_or_unknown) return U"    " ICON_SLIDERS;        // Win+Pause System Properties (🎛 knobs, pack)
                break;
            case KC_PSCR:
                if (win_or_unknown) return U"   "   ICON_SCREENSHOT;    // Win+PrtScn full-screen screenshot
                break;
            // Magnifier zoom: '+' keys (= and numpad +) zoom in, '-' keys zoom out. Both
            // draw the pack magnifier 🔍, then MOVE the cursor so a plain base-font '+'/'-'
            // lands centred in the lens.
            case KC_EQL:
            case KC_KP_PLUS:
                if (win_or_unknown) return U"   " ICON_MAGNIFIER HINT_MOVE(HINT_POS_ZOOMIN) U"+";  // Win + '+' zoom in
                break;
            case KC_MINS:
            case KC_KP_MINUS:
                if (win_or_unknown) return U"   " ICON_MAGNIFIER HINT_MOVE(HINT_POS_ZOOMOUT) U"-"; // Win + '-' zoom out
                break;
            // Wave E — more Windows-only Super chords.
            case KC_Q:
                if (win_or_unknown) return U"   "   ICON_TEXT_RECOG;   // Win+Q Click to Do — text recognition (🔤 pack glyph)
                break;
            case KC_G:
                if (win_or_unknown) return U"   "   ICON_GAME_BAR;     // Win+G Xbox Game Bar (🎮 pack glyph)
                break;
            case KC_F:
                if (win_or_unknown) return U"   "   ICON_FEEDBACK;     // Win+F Feedback Hub (📣 pack glyph)
                break;
            case KC_C:
                if (win_or_unknown) return U"   "   ICON_COPILOT;      // Win+C Copilot (🤖 pack glyph)
                break;
            default: break;
        }
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

    kdisp_clear_bitmap_courtyard(28, 0, get_overlay(idx), 72, 40, KDISP_CY_DEFAULT);
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

// Draw one language key: oversized country flag on the left (vertically centred
// and clipped so the flag content fills the keycap height), language code running
// vertically up the right side (inverted bar when it is the active language).
//
// The flag glyphs (FLAG_CP_BASE + idx) live in the external-flash font pack, so
// they resolve through g_all_fonts only when a pack is present. With no pack the
// flag is simply omitted — the xx-YY code label below it still identifies the
// language (graceful fallback). The tiny label font stays resident.
static const GFXfont* const lang_label_fonts[] = { &NotoSans_Regular_Tiny_6pt7b };
// Mid (10px) utility font for the no-pack fallback code — between Tiny and Base,
// so a full "ll-CC" fits on one line (~52px) yet stays readable. Reuse this
// `mid_fonts` array for any misc utility-key text that wants a middle size.
static const GFXfont* const mid_fonts[]        = { &NotoSans_Regular_Mid_10pt7b };

static void render_lang_flag_key(uint8_t idx, const uint32_t* label, uint8_t current_lang) {
    const GFXfont* flag_font = NULL;
    const GFXglyph* g = kdisp_gfx_glyph_font(g_all_fonts, g_all_font_count,
                                             FLAG_CP_BASE + idx, &flag_font);
    if (g) {
        // The glyph is taller than the keycap, so centre it vertically — the
        // empty top/bottom margins clip off and the flag content fills the height.
        // Compensate BOTH the glyph's x and y bearing (kdisp_write_gfx_char draws
        // at x+xOffset, y+yOffset) so the flag's content lands flush at
        // FLAG_LEFT_X regardless of the glyph's left bearing (was x-shifted).
        //
        // Render through a SINGLE-font array holding just the flag font — NOT
        // g_all_fonts. kdisp_write_gfx_char baseline-aligns each glyph to fonts[0]
        // (`y += currentFont->yAdvance - fonts[0]->yAdvance`). With g_all_fonts,
        // fonts[0] is IconsFont (yAdvance 40) vs the flag's 54 → a spurious +14 px
        // downward shift (the gap-at-top regression introduced when flags moved
        // into the pack). Passing the owning font alone makes that adjustment 0,
        // as the old compiled-in { &flag_font } path did. (kdisp_gfx_glyph_font
        // returned flag_font from the same scan, so no second lookup.)
        const GFXfont* flag_only[1] = { flag_font };
        const int8_t fh  = (int8_t)pgm_read_byte(&g->height);
        const int8_t fyo = (int8_t)pgm_read_byte(&g->yOffset);
        const int8_t fxo = (int8_t)pgm_read_byte(&g->xOffset);
        kdisp_write_gfx_char(flag_only, 1, (int8_t)(FLAG_LEFT_X - fxo),
                             (int8_t)((SCREEN_HEIGHT - fh) / 2 - fyo),
                             FLAG_CP_BASE + idx, 1);   // flags: tight 1px courtyard
        // Language code: vertical, up the right side; inverted bar when selected.
        kdisp_write_gfx_vtext(&NotoSans_Regular_Tiny_6pt7b, LABEL_COL_X, label,
                              current_lang == idx);
    } else {
        // No font pack flashed: the flag glyphs are pack-only. Show the "ll-CC"
        // code on one centred line in the mid (10px) utility font — readable, and
        // a full code (~52px) fits the 72px keycap. The tiny vertical label is
        // dropped here; underline the active language.
        int8_t lo = 0, hi = 0;
        kdisp_gfx_text_bounds(mid_fonts, 1, label, &lo, &hi);
        int8_t w    = (int8_t)(hi - lo);
        int8_t left = (int8_t)(BUFFER_X + (SCREEN_WIDTH - w) / 2);
        kdisp_write_gfx_text(mid_fonts, 1, (int8_t)(left - lo), 24, label);
        if (current_lang == idx) {
            kdisp_fill_rect(left, 28, w, 2);   // underline = active language
        }
    }
}

// The "Preset" / "Clear" MRU control keys that bracket the top recents row.
// The label sits next to the recents (Preset right-aligned on the left corner,
// Clear left-aligned on the right corner) with an arrow pointing into the row.
// (lang_label_fonts is declared above render_lang_flag_key.)
static void render_mru_ctrl_key(bool preset) {
    if (preset) {
        kdisp_write_gfx_text(lang_label_fonts, 1, BUFFER_X + 14, 18, U"Preset");
        kdisp_write_gfx_text(g_all_fonts, g_all_font_count, BUFFER_X + 44, 23, ICON_RIGHT);
    } else {
        // Mirror of "Preset": back-arrow points left into the recents row, label in
        // the small keycap-label font (not the full-size font, which overflowed).
        kdisp_write_gfx_text(g_all_fonts, g_all_font_count, BUFFER_X + 10, 23, ICON_LEFT);
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

// MRU recents (emoji or language) get a full-width bar along the TOP edge —
// the mirror image of the category tabs' bottom bar — to set the row apart.
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

// Resolve the keycode whose legend a physical key is currently showing, honouring the
// active layer stack with a one-level transparent fallback. The single source of truth
// for both the awake render (update_displays) and the idle relocation (kdisp_idle), so
// a jittered legend always matches what was last drawn awake instead of snapping to the
// base layer. The effective state folds in the default layer (`def_layer`, tracked
// separately from the momentary `layer` stack — e.g. a Colemak/Neo base) so a key with
// no momentary layer active still shows its default-layer legend, not _BL.
static uint16_t display_keycode_at(const poly_layer_t* lyr, uint8_t row, uint8_t col) {
    layer_state_t eff = lyr->layer | ((layer_state_t)1 << lyr->def_layer);
    uint8_t layer = get_highest_layer(eff);
    uint16_t kc = poly_keycode_at(layer, row, col);
    if (kc == KC_TRNS) {
        kc = poly_keycode_at(get_highest_layer(eff & ~((layer_state_t)1 << layer)), row, col);
    }
    return kc;
}

// Roll a per-glyph idle jitter offset: a uniform random position within the legend's
// OWN on-screen slack, measured from its bounding box at the draw origin (ox/oy). The
// range is glyph-derived — never a global cap — so a slim "i" roams its full free
// width while a wide "w" stays within its small margin, each using all (and only) the
// space it actually has. A global ±N envelope would be counter-productive here: it
// would throttle the slim glyph (lots of slack, but capped) and edge-bias the wide one
// (most rolls clamp to the same boundary). A glyph with no slack in an axis simply
// doesn't move in it. The result is always fully on-screen for any script, so no
// separate clamp step is needed.
static void roll_idle_offset(const uint32_t* text, int8_t ox, int8_t oy, uint32_t seed,
                             int8_t* dx, int8_t* dy) {
    int8_t xmin, xmax, ymin, ymax;
    kdisp_gfx_text_bbox(g_all_fonts, g_all_font_count, text, &xmin, &xmax, &ymin, &ymax);
    int16_t axmin = ox + xmin, axmax = ox + xmax;   // glyph extent at the un-jittered origin
    int16_t aymin = oy + ymin, aymax = oy + ymax;
    int16_t xlo = (int16_t)BUFFER_X - axmin;                      // keep left edge >= BUFFER_X
    int16_t xhi = (int16_t)(BUFFER_X + SCREEN_WIDTH - 1) - axmax; // keep right edge on-screen
    int16_t ylo = -aymin;                                         // keep top >= 0
    int16_t yhi = (int16_t)(SCREEN_HEIGHT - 1) - aymax;           // keep bottom on-screen
    *dx = (xhi < xlo) ? 0 : jitter_axis(seed, 0x0000u, (int8_t)xlo, (int8_t)xhi);
    *dy = (yhi < ylo) ? 0 : jitter_axis(seed, 0x1000u, (int8_t)ylo, (int8_t)yhi);
}

// Idle (anti-burn-in) per-key relocation: redraws ONLY the resting normal legend — no
// shift/AltGr preview, no overlay image, no tab/MRU chrome — at a fresh random spot
// within THIS glyph's own slack (roll_idle_offset, seeded by `seed`), always fully
// visible. Renders into the currently selected display's buffer, so it works from
// inside kdisp_idle()'s shift-register walk (the caller selects the key, with the panel
// switched OFF, so the move is invisible). Returns false WITHOUT touching the buffer
// when the keycode has no plain-text legend (a language flag, emoji, region tab, MRU
// control, …): those can't be jittered (full-bleed images), so we leave their current
// frame in place instead of blanking it — e.g. the language-layer flags stay put and
// keep pulsing rather than disappearing on the first idle cycle.
static bool render_idle_key(uint16_t keycode, led_t state, uint32_t seed) {
    const poly_sync_t* local_state = get_local_state();
    uint32_t unimap[2] = {0, 0};
    const uint32_t* text = to_static_text(keycode, state);
    if (text == NULL) {
        // letter / symbol legends come from the language table (unshifted, no AltGr)
        text = translate_keycode(local_state->lang, keycode, false, state.caps_lock);
    }
    if (text == NULL && (keycode & QK_UNICODEMAP_PAIR) == QK_UNICODEMAP_PAIR) {
        uint16_t chr = state.caps_lock ? QK_UNICODEMAP_PAIR_GET_SHIFTED_INDEX(keycode)
                                       : QK_UNICODEMAP_PAIR_GET_UNSHIFTED_INDEX(keycode);
        unimap[0] = unicode_map[chr];
        text = unimap;
    }
    if (text == NULL || text[0] == 0) {
        return false;   // no text legend — keep the key's current frame
    }
    int8_t dx, dy;
    roll_idle_offset(text, BUFFER_X, 23, seed, &dx, &dy);
    kdisp_set_buffer(0x00);
    kdisp_set_draw_offset(dx, dy);
    kdisp_write_gfx_text(g_all_fonts, g_all_font_count, BUFFER_X, 23, text);
    kdisp_set_draw_offset(0, 0);
    kdisp_send_buffer();
    return true;
}

// Per-key "was dark on the previous kdisp_idle() pass" latch (this half only), so a
// key relocates at most once per pulse-dark episode rather than every pass while it
// is dark. Reset on every wake/suspend/stop-idle path (reset_idle_jitter) so a fresh
// idle session starts from the centred awake legend and re-relocates cleanly.
static uint8_t s_idle_was_dark[MATRIX_ROWS_PER_SIDE][MATRIX_COLS];
// Per-key dark-episode counter: the breathing curve dips dark ~twice per ~15 s pulse
// cycle, so relocating on every dark edge would move each key ~every 7.5 s. We only
// relocate every IDLE_JITTER_PERIOD-th dark episode to slow that down (the count is
// taken mod the period, so a fresh session relocates on the very first episode then
// every Nth after — it moves off-centre promptly, then drifts more slowly).
static uint8_t s_idle_episode[MATRIX_ROWS_PER_SIDE][MATRIX_COLS];
// Rolling seed for the per-key offset hash — bumped on every relocation so each lands
// somewhere new (no per-key PRNG state to keep).
static uint16_t s_idle_roll = 0;

void reset_idle_jitter(void) {
    memset(s_idle_was_dark, 0, sizeof(s_idle_was_dark));
    memset(s_idle_episode, 0, sizeof(s_idle_episode));
}

// Draw a legend horizontally CENTRED in the visible key window
// [BUFFER_X, BUFFER_X+SCREEN_WIDTH), baseline at y, instead of left-aligned at
// BUFFER_X. Used for the bottom (thumb) row so its mixed chrome/icon legends
// (arrows, Base, Make, toggles, world, …) sit centred.
//
// Leading spaces are skipped first: many legends carry manual left-padding (e.g.
// the arrows are U"  " + icon), and the Base font's space glyph is a degenerate
// 1×1 ink box, so kdisp_gfx_text_bounds would count those spaces as ink at x=0,
// inflate the measured width, and push the real glyph right of centre. Skipping
// them makes the measure+draw start at the first real glyph, truly centred.
static void draw_legend_cx(const uint32_t* text, int8_t y) {
    while (*text == U' ') text++;          // drop manual leading padding (skews bbox)
    int8_t lo = 0, hi = 0;
    kdisp_gfx_text_bounds(g_all_fonts, g_all_font_count, text, &lo, &hi);
    const int8_t w    = (int8_t)(hi - lo + 1);
    const int8_t left = (int8_t)(BUFFER_X + (SCREEN_WIDTH - w) / 2 - lo);
    kdisp_write_gfx_text_cy(g_all_fonts, g_all_font_count, left, y, text, KDISP_CY_DEFAULT);
}

void update_displays(enum refresh_mode mode) {
#ifdef POLYKYBD_LINK_DIAG
    // Diagnostic build: render_link_diag() owns the keycaps so the link status
    // stays readable. Suppress the normal legend re-render, which would otherwise
    // repaint over it (e.g. the slave redrawing legends when a sync lands).
    (void)mode;
    return;
#endif
    // Doom easter egg: while game mode owns the keycaps, the blitter is the
    // only writer — a legend re-render here would tear the game frame.
    if (doom_mode_active()) {
        return;
    }
    const poly_sync_t* local_state = get_local_state();
    const bool idle = (local_state->flags & DISP_IDLE) != 0;
    // While idle we never full-re-render here: kdisp_idle() pulses the existing
    // buffer and (in JITTER style) relocates each key's legend itself as that key
    // dims. A full update_displays() pass at idle would fight that and redraw the
    // awake chrome. The displays already hold the last centred awake render when we
    // enter idle, so there is nothing to do until we wake.
    if(idle || local_state->contrast<=DISP_OFF) {
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
                    keycode = display_keycode_at(local_layer, r + offset, c);
                    // Doom egg menu item: rewrites the EEPROM keymap's KC_NO at
                    // the armed utilities-layer position (see doom_mode.h;
                    // pass-through no-op in non-doom builds and while unarmed).
                    keycode = doom_egg_menu_keycode(keycode, (uint8_t)(r + offset), c);
                    kdisp_enable(true);
                    kdisp_set_contrast((uint8_t)(local_state->contrast-1));
                    // Doom control pad (this only ever renders on the SLAVE
                    // half — the master early-returns above while the game
                    // runs): the outer two columns become ESC + weapon slots,
                    // the game-control keys keep their legends, everything
                    // else goes dark.
                    bool doom_handled = false;
                    if (local_state->doom_ctl == 2) {
                        // Attract screensaver: chrome-free — no pad, no ESC face,
                        // no control legends. Every key belongs to the mirror
                        // blitter while its view is live (the full-viewport
                        // attract includes the bottom row); until the mirror is
                        // up, keys go dark so no legend flashes into the demo.
                        if (r < MATRIX_ROWS_PER_SIDE - 1 ? doom_slave_viewport_live()
                                                         : doom_slave_bottom_row_live()) {
                            doom_handled = true;
                        } else {
                            kdisp_set_buffer(0x00);
                            kdisp_send_buffer();
                            doom_handled = true;
                        }
                    } else if (local_state->doom_ctl) {
                        uint16_t pad = doom_pad_keycode((uint8_t)(r + offset), c);
                        // Fixed positional control layout (field round 23):
                        // the cursor cluster / use / enter legends render at
                        // their pad POSITIONS, whatever the active base
                        // layer holds there — matching what
                        // doom_process_record feeds the game.
                        {
                            uint16_t ctl = doom_ctl_keycode((uint8_t)(r + offset), c);
                            if (ctl != KC_NO) {
                                keycode = ctl;
                            }
                        }
                        if (pad == KC_NO &&
                            (r < MATRIX_ROWS_PER_SIDE - 1 ? doom_slave_viewport_live()
                                                          : doom_slave_bottom_row_live())) {
                            // The mirror blitter owns the non-pad keys while
                            // its view is live — leave their frames alone.
                            // The bottom (thumb) row belongs to the blitter
                            // only during the full-viewport ATTRACT; on the
                            // map it renders here so the cursor-key legends
                            // stay (field rounds 13+14).
                            doom_handled = true;
                        } else if (pad >= KC_1 && pad <= KC_7) {
                            uint8_t slot = (uint8_t)(pad - KC_1); // 0-based
                            kdisp_set_buffer(0x00);
                            if (local_state->doom_wpn_owned & (uint8_t)(1u << slot)) {
                                // Sprite silhouette + slot digit in the corner;
                                // a bottom bar marks the weapon in hand.
                                // Unowned slots stay dark.
                                uint8_t  iw = 0, ih = 0;
                                const uint8_t *icon = doom_weapon_icon((uint8_t)(slot + 1), &iw, &ih);
                                uint32_t digit[2] = {(uint32_t)('1' + slot), 0};
                                if (icon) {
                                    kdisp_write_gfx_text(mid_fonts, 1, BUFFER_X + 1, 12, digit);
                                    kdisp_draw_bitmap((int8_t)(BUFFER_X + (SCREEN_WIDTH - iw) / 2),
                                                      (int8_t)((SCREEN_HEIGHT - ih) / 2 + 3),
                                                      icon, (int8_t)iw, (int8_t)ih);
                                } else {
                                    draw_legend_cx(digit, 23);
                                }
                                if (local_state->doom_wpn_ready == slot + 1) {
                                    static const uint8_t ready_bar[18] = {
                                        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                    }; // 72x2 solid underline
                                    kdisp_draw_bitmap(BUFFER_X, SCREEN_HEIGHT - 2, ready_bar, 72, 2);
                                }
                            }
                            kdisp_send_buffer();
                            doom_handled = true;
                        } else if (pad == KC_ESC) {
                            // The shared exit-hint face — byte-identical to
                            // the master's HUD corner key (field rd 18).
                            doom_render_esc_key();
                            kdisp_send_buffer();
                            doom_handled = true;
                        } else if (keycode == KC_LCTL || keycode == KC_RCTL) {
                            // Ctrl fires in DOOM — show a crosshair reticle
                            // instead of the plain Ctrl legend (field rd 16).
                            kdisp_set_buffer(0x00);
                            doom_render_fire_key();
                            kdisp_send_buffer();
                            doom_handled = true;
                        } else if (keycode == KC_SPACE) {
                            // Space is DOOM's use/open — a door symbol
                            // instead of the space legend (field rd 17).
                            kdisp_set_buffer(0x00);
                            doom_render_use_key();
                            kdisp_send_buffer();
                            doom_handled = true;
                        } else if (!doom_key_is_control(keycode)) {
                            kdisp_set_buffer(0x00);
                            kdisp_send_buffer();
                            doom_handled = true;
                        }
                    }
                    if (doom_handled) {
                        // rendered above
                    } else if(keycode!=KC_TRNS) {
                        int16_t lang_idx = lang_index_for_keycode(keycode);
                        if (lang_idx >= 0) {
                            // Language layer: country flag + tiny language code
                            // (paged slots and the top-row MRU recents alike).
                            kdisp_set_buffer(0x00);
                            draw_mru_top_bar(keycode);
                            render_lang_flag_key((uint8_t)lang_idx, to_static_text((uint16_t)(KCL_ENUS + lang_idx), state), local_state->lang);
                            kdisp_send_buffer();
                        } else if (keycode == KC_EMJ_PRESET || keycode == KC_LANG_PRESET ||
                                   keycode == KC_EMJ_CLEAR  || keycode == KC_LANG_CLEAR) {
                            // Top-row MRU controls: "Preset" / "Clear".
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
                        // Draw the tab frame / row bar FIRST, then the emoji glyph with
                        // courtyard clearing so the icon punches a clean margin through it.
                        emj_draw_tab_indicator(keycode);
                        emj_draw_tab_bottom(keycode);
                        draw_mru_top_bar(keycode);
                        if(text==NULL) {
                            if(!render_key(keycode, state, mods) && (keycode&QK_UNICODEMAP_PAIR)==QK_UNICODEMAP_PAIR){
                                uint16_t chr = capital_case ? QK_UNICODEMAP_PAIR_GET_SHIFTED_INDEX(keycode) : QK_UNICODEMAP_PAIR_GET_UNSHIFTED_INDEX(keycode);
                                kdisp_write_gfx_char(g_all_fonts, g_all_font_count, BUFFER_X, 23, unicode_map[chr], 0);
                            }
                        } else if (r == MATRIX_ROWS_PER_SIDE - 1) {
                            // Bottom (thumb) row: centre the legend horizontally.
                            draw_legend_cx(text, 23);
                        } else {
                            kdisp_write_gfx_text_cy(g_all_fonts, g_all_font_count, BUFFER_X, 23, text, KDISP_CY_DEFAULT);
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
                            // The hint string is a self-contained display list: any
                            // frame / half-scale composite / +/- sign is encoded inline
                            // (see the \x0E-\x12 ops in kdisp_write_gfx_text_cy), so no
                            // per-keycode special-case is needed here.
                            kdisp_write_gfx_text_cy(g_all_fonts, g_all_font_count, BUFFER_X, 23, text, KDISP_CY_DEFAULT);
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
    kdisp_set_draw_offset(0, 0);   // clear the jitter offset so non-idle renders stay centred
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

// Updates all displays to show idle pulsating animation with varying brightness
// pattern. In JITTER style this is also where anti-burn-in relocation happens: the
// instant a key's out-of-phase pulse dims it to black, we redraw that one key's
// resting legend at a fresh random offset (clamped to its glyph). The move is
// invisible because the key is dark at that moment; it reappears at the new spot on
// its next bright cycle. Each half relocates only its own keys, so there is no
// cross-half offset to sync — the keys roam individually rather than in lockstep.
// The glyph is re-derived from the keycode each time, so nothing is stored in the
// OLED's own memory; only a 1-bit-per-key "was dark" latch (s_idle_was_dark) gates
// the once-per-episode redraw.
void kdisp_idle(uint8_t contrast) {
    uint8_t offset = is_left_side() ? 0 : MATRIX_ROWS_PER_SIDE;
    uint8_t skip = 0;
    const bool jitter = get_local_state()->idle_style == IDLE_STYLE_JITTER;
    const poly_layer_t* local_layer = get_local_layer();
    const led_t led_state = local_layer->led_state;
    sr_shift_out_buffer_latch(disp_row_0.bitmask, sizeof(struct display_info));

    //uint8_t idx = 0;
    for (uint8_t r = 0; r < MATRIX_ROWS_PER_SIDE; ++r) {
        for (uint8_t c = 0; c < MATRIX_COLS; ++c) {
            uint8_t  disp_idx = LAYOUT_TO_INDEX(r, c);

            //since MATRIX_COLS==8 we don't need to shift multiple times at the end of the row
            //except there was a leading and missing physical key (KC_NO on base layer)
            // base_kc drives the physical-layout skip and the per-key pulse phase (both
            // layout-, not layer-, dependent); the relocated legend itself is resolved
            // from the active layer below so it matches the awake render.
            uint16_t base_kc = keymaps[_BL][r + offset][c];
            if (base_kc == KC_NO) {
                skip++;
            } else {
                if (disp_idx != 255) {
                    uint8_t idle_brightness = to_brightness((contrast+(c%3+r)*base_kc+offset+r)%50);
                    if(idle_brightness==0) {
                        // Lit -> dark edge in JITTER style: relocate this key now. Turn
                        // the panel OFF *first*, THEN write the new frame while it is
                        // dark — so the move is never seen; the glyph reappears already
                        // at its new spot on the next bright cycle (writing before the
                        // off-switch made it flash at the old contrast — a visible jump
                        // just before the key dimmed out). The legend is resolved from
                        // the active layer (display_keycode_at) so it matches the awake
                        // render, not the base layer.
                        bool dark_edge = !s_idle_was_dark[r][c];
                        s_idle_was_dark[r][c] = 1;
                        kdisp_enable(false);
                        // Only relocate every IDLE_JITTER_PERIOD-th dark episode, so the
                        // legend drifts slowly rather than on every ~7.5 s dark valley.
                        if(jitter && dark_edge && (s_idle_episode[r][c]++ % IDLE_JITTER_PERIOD) == 0) {
                            uint16_t kc = display_keycode_at(local_layer, r + offset, c);
                            if(kc != KC_TRNS) {
                                render_idle_key(kc, led_state, s_idle_roll++);
                            }
                        }
                    } else {
                        s_idle_was_dark[r][c] = 0;
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
// Per-key latch (bit0=LGUI, 1=RGUI, 2=LALT, 3=RALT) recording that the Apple
// GUI/Alt swap was applied on press, so release unregisters the same modifier even
// if the active OS changed while the key was held. See the swap block below.
static uint8_t s_apple_swap_latch = 0;

bool process_record_user(uint16_t keycode, keyrecord_t* record) {

    // Doom easter egg: in game mode every key event is swallowed (fed to the
    // game, never the host); outside game mode this only advances the trigger
    // matcher. Inline no-op false unless built with POLYKYBD_DOOM.
    if (doom_process_record(keycode, record->event.pressed,
                            record->event.key.row, record->event.key.col)) {
        return false;
    }

    // SECURITY: the keycode of every keystroke (passwords included) must NOT be
    // streamed to the HID console in normal operation — any local process can open
    // that console interface and read it. Gate this logging on debug_enable (off by
    // default; toggle at runtime with the DB_TOGG key, which needs physical access).
    if (debug_enable) {
        uint32_t t = get_time_since_last_update();
        if(record->event.pressed) {
            uprintf("wait %ld.%03ld\n", t/1000, t%1000);
            uprintf("press 0x%04x\n", keycode);
        } else {
            uprintf("wait %ld.%03ld\n", t/1000, t%1000);
            uprintf("release 0x%04x\n", keycode);
        }
    }

    // macOS: swap the GUI and Alt keys (keycode here, legend in keycode_helper)
    // so the physical modifier row matches a Mac's Ctrl–Option–Command order — the
    // GUI key acts as Option/Alt and the Alt key as Command/GUI, on both halves.
    // Only the four basic modifier keycodes are remapped, and only on macOS.
    // The swap is LATCHED per key (s_apple_swap_latch) at press time and reused at
    // release, so an active_os change mid-hold can't unregister the opposite mod and
    // leave a stuck modifier — the key always releases whatever it pressed.
    {
        uint8_t  bit = 0;
        uint16_t swapped = 0;
        switch (keycode) {
            case KC_LGUI: swapped = KC_LALT; bit = 1u << 0; break;
            case KC_RGUI: swapped = KC_RALT; bit = 1u << 1; break;
            case KC_LALT: swapped = KC_LGUI; bit = 1u << 2; break;
            case KC_RALT: swapped = KC_RGUI; bit = 1u << 3; break;
        }
        if (bit) {
            const uint8_t os = get_local_state()->active_os & POLY_OS_VALUE_MASK;
            const bool swap_now = record->event.pressed
                ? (os == POLY_OS_MACOS)
                : (s_apple_swap_latch & bit) != 0;
            if (swap_now) {
                if (record->event.pressed) { s_apple_swap_latch |= bit;            register_code(swapped); }
                else                       { s_apple_swap_latch &= (uint8_t)~bit;  unregister_code(swapped); }
                return false;
            }
        }
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
        case KC_OS_ACTION_BASE ... KC_OS_ACTION_END - 1:
            // OS-semantic action key: emit the chord for the active OS. active_os
            // is synced from the master, so this resolves correctly on either half.
            if (record->event.pressed) {
                emit_os_action((uint16_t)(keycode - KC_OS_ACTION_BASE),
                               get_local_state()->active_os & POLY_OS_VALUE_MASK);
            }
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
                //
                // Hardened handoff — same rationale as CMD_FW_UP_APPLY (hid_fw_up.c,
                // field 2026-06-22).  This is the reset-key twin of that path and had
                // the identical flaw: a single dropped reboot frame at only 5 retries
                // left the slave alive on stale state, the master rebooted alone and
                // hung on the boot splash until the slave was replugged (field 2026-07
                // — plain reset key, no firmware apply).  Use 20 retries and re-fire the
                // whole round once if the slave still hasn't acked.  Safe: the slave
                // reset handler is idempotent (it only arms a deferred mcu_reset),
                // send_to_bridge is synchronous (returns only after the slave has
                // handled it), and we're about to reset anyway — the extra worst-case
                // ~1 s is free insurance on this critical step.
                poly_reset_sync_t reboot_msg = { .crc32 = 0, .magic = POLY_RESET_MAGIC,
                                                 .action = RESET_ACTION_REBOOT };
                uint8_t ack = send_to_bridge(USER_SYNC_RESET, &reboot_msg, sizeof(reboot_msg), 20);
                if (!sync_succeeded(ack)) {
                    ack = send_to_bridge(USER_SYNC_RESET, &reboot_msg, sizeof(reboot_msg), 20);
                }
                uprintf("Master: slave reboot ack=0x%02x\n", ack);
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
        // Default-layer selectors. `def_layer` holds a layer INDEX (_L0.._L4) — the
        // same value display_keycode_at() folds in as `1 << def_layer` to pick the
        // keycap legends. Drive QMK's resolved layer through the SAME momentary
        // mechanism the boot path (keyboard_post_init) and KC_BASE use —
        // layer_clear() + layer_on(index) — NOT default_layer_set(): that QMK API
        // takes a layer_state_t BITMASK, so passing it the index set the wrong base
        // (e.g. _L2=2 -> 0b10 = layer 1), making the keys type a different layer than
        // the keycaps showed. Persistence still round-trips the index via eeconfig
        // (defer_default_layer_save -> persistent_default_layer_get at boot).
        case KC_L0:
            local_layer->def_layer = _L0;
            defer_default_layer_save(local_layer->def_layer);
            layer_clear();
            layer_on(local_layer->def_layer);
            request_disp_refresh();
            break;
        case KC_L1:
            local_layer->def_layer = _L1;
            defer_default_layer_save(local_layer->def_layer);
            layer_clear();
            layer_on(local_layer->def_layer);
            request_disp_refresh();
            break;
        case KC_L2:
            local_layer->def_layer = _L2;
            defer_default_layer_save(local_layer->def_layer);
            layer_clear();
            layer_on(local_layer->def_layer);
            request_disp_refresh();
            break;
        case KC_L3:
            local_layer->def_layer = _L3;
            defer_default_layer_save(local_layer->def_layer);
            layer_clear();
            layer_on(local_layer->def_layer);
            request_disp_refresh();
            break;
        case KC_L4:
            local_layer->def_layer = _L4;
            defer_default_layer_save(local_layer->def_layer);
            layer_clear();
            layer_on(local_layer->def_layer);
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
        case KC_DAUTO:
            // Toggle host-driven (daylight/auto) brightness vs. manual control.
            // This switch is inside `if (!record->event.pressed)`, so it already
            // runs once per keypress (on release) — no extra guard needed.
            toggle_brightness_auto_mode();
            request_disp_refresh();
            break;
        case KC_OS_ICON:
            // Cycle the active-OS selection: auto -> pin Windows -> macOS -> Linux
            // -> Android -> auto. A manual pin overrides detection/host and survives
            // reboots (the only way to select Android). Runs once on release.
            if (get_os_auto_mode()) {
                set_user_os(POLY_OS_WINDOWS);                 // auto -> first pin
            } else if (get_active_os() >= POLY_OS_ANDROID) {
                set_os_auto_mode(true);                       // last pin (Android) -> back to auto
            } else {
                set_user_os((uint8_t)(get_active_os() + 1));  // next pin
            }
            request_disp_refresh();
            break;
        case KC_OS_SET_AUTO ... KC_OS_SET_END - 1: {
            // Direct OS selection (settings layer): KC_OS_SET_AUTO clears the pin
            // (back to host/USB detection); the others pin a specific OS. The
            // offset from KC_OS_SET_BASE is the poly_os value. We are already inside
            // the `if (!record->event.pressed)` block, so this runs once on release.
            uint8_t sel = (uint8_t)(keycode - KC_OS_SET_BASE);
            if (sel == POLY_OS_UNKNOWN) {
                set_os_auto_mode(true);   // auto: detection / host wins
            } else {
                set_user_os(sel);         // pin this OS (survives reboot)
            }
            request_disp_refresh();
            break;
        }
        case KC_STORE_EE:
            // Manual "commit everything to EEPROM" — for users who want to be
            // sure their changes survive a hard power-cut without suspending.
            // Defer our own write to housekeeping (save_all_if_requested), and
            // signal the slave to do the same via the SAVE_EEPROM sync flag,
            // mirroring the edge-triggered overlay action flags (hid_com case 11):
            // set the bit, push state, then clear it locally.
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
        // Direct per-language selectors on the language layer (KCL_ENUS..last).
        // The KCL_* and LANG_* enums are both generated from the same ordered
        // language list, so LANG_xx == (KCL_xx - KCL_ENUS) — the two _Static_asserts
        // above guard that — and the whole per-language block is one range case.
        case KCL_ENUS ... KCL_ENUS + NUM_LANG - 1:
            local_state->lang = (uint8_t)(keycode - KCL_ENUS);
            mark_settings_dirty();
            layer_off(_LL);
            break;
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

#ifdef POLYKYBD_LINK_DIAG
// Bring-up: paint the split-link state on THIS half's top keycap row, so the
// slave (which has no USB console) reveals whether it is receiving. Layout, one
// glyph per keycap on matrix row 0:
//   [role] [number...]
//   role   = 'M' master / 'S' slave
//   number = slave: frames RECEIVED from the master (get_split_rx_frames) — climbs
//            iff master->slave carries data; stays 0 if that direction is dead.
//            master: transport-fail count (get_link_transport_fail) — climbs while
//            the slave's reply never gets back.
// Read together across both halves: slave number climbing + master fails climbing
// => master->slave OK, slave->master (reply) dead; slave number stuck at 0
// => master->slave itself is dead. Enabled only with `-e POLYKYBD_LINK_DIAG=yes`.
static void render_link_diag(void) {
    const bool master = is_keyboard_master();
    uint32_t   val    = master ? get_link_transport_fail() : get_split_rx_frames();

    uint32_t buf[8];
    uint8_t  n = 0;
    buf[n++] = master ? U'M' : U'S';

    // Decimal-format val (max 5 digits) into the keycaps after the role glyph.
    char digits[5];
    int  d = 0;
    if (val == 0) {
        buf[n++] = U'0';
    } else {
        while (val && d < 5) { digits[d++] = (char)('0' + (val % 10)); val /= 10; }
        while (d) { buf[n++] = (uint32_t)digits[--d]; }
    }
    buf[n] = 0;
    display_message(0, 0, buf, &FreeSansBold24pt7b);
}
#endif

// Displays splash screen with polykybd/split72 logo and initializes displays with refresh.
void show_splash_screen(void) {
    clear_all_displays();
    if(is_left_side()) {
        display_message(1, 1, U"POLY", &FreeSansBold24pt7b);
        display_message(2, 1, U"KYBD", &FreeSansBold24pt7b);
    } else {
        display_message(1, 1, POLY_SPLASH_R1, &FreeSansBold24pt7b);
        display_message(POLY_SPLASH_R2_ROW, 1, POLY_SPLASH_R2, &FreeSansBold24pt7b);
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
        local_state->contrast = get_active_brightness();
        local_state->flags &= ~((uint8_t)DISP_IDLE);
        local_state->flags |= STATUS_DISP_ON;
        reset_idle_jitter();   // fresh, centred idle session next time
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

#ifdef OS_DETECTION_ENABLE
// QMK fires this (master only — detection is USB-enumeration based) once the
// detected OS stabilises. Feed it into the auto-mode resolver as the LOW-priority
// source: set_detected_os applies only while in auto mode and only until the host
// pushes an OS (host wins). The active_os refresh in housekeeping then syncs it to
// the slave. Note: QMK reports Android and ChromeOS as OS_LINUX (shared USB stack),
// so Android can only ever be selected by a manual pin (set_user_os), not here.
bool process_detected_host_os_kb(os_variant_t os) {
    if (!process_detected_host_os_user(os)) {
        return false;
    }
    switch (os) {
        case OS_WINDOWS: set_detected_os(POLY_OS_WINDOWS); break;
        case OS_MACOS:   set_detected_os(POLY_OS_MACOS);   break;
        case OS_IOS:     set_detected_os(POLY_OS_MACOS);   break;  // iOS folded into macOS
        case OS_LINUX:   set_detected_os(POLY_OS_LINUX);   break;
        case OS_UNSURE:                                    break;  // keep current
    }
    return true;
}
#endif

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
    // SECURITY: default OFF. debug_enable now gates keystroke logging to the HID
    // console (process_record_user) and the host-driven active-control HID commands
    // (cmd 14 key injection, cmd 23 bootloader entry). Enable at runtime with the
    // DB_TOGG key (physical access required) when you actually want demo/debug.
    debug_enable = false;
    debug_matrix = false;
    debug_keyboard = false;
    debug_mouse = false;

    //pointing_device_set_cpi(20000);
#if defined(POINTING_DEVICE_ENABLE)
    pointing_device_set_cpi(650);
#endif
    //pimoroni_trackball_set_rgbw(0,0,255,100);
    layer_state_t default_layer = persistent_default_layer_get();
    access_local_layer()->def_layer = default_layer;
    access_local_state()->unicode_mode = get_unicode_input_mode();
    layer_clear();
    layer_on(default_layer);
    g_force_layer_resync = true;   // push this boot's default layer to the slave
    g_force_resync_tries = FORCE_LAYER_RESYNC_TRIES;  // (re-arm the bounded budget)

    //set these values, they will never change
    set_com_state(is_keyboard_master() ? USB_HOST : BRIDGE);
    set_side(is_keyboard_left() ? LEFT_SIDE : RIGHT_SIDE);

#ifdef POLYKYBD_LTR559
    // Probe for the LTR-559 on BOTH halves — it can be soldered to either half's
    // expansion port (GP0/GP1 I2C exists on both). The half that finds it uses it;
    // the other half's probe fails and stays disabled (bounded retries in the
    // task, so no stall). ltr559_init() brings up I2C itself, so this works even
    // on a half with no pointing device.
    if (ltr559_init()) {
        uprint("LTR-559 sensor detected.\n");
    }
#endif


    //encoder pins
    gpio_set_pin_input_high(GP25);
    gpio_set_pin_input_high(GP29);

    //srand(halGetCounterValue());

    emj_init();
    lang_init();
    mru_init();

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
    transaction_register_rpc(USER_SYNC_FLASH_STAGE,         user_sync_flash_stage_handler);
    transaction_register_rpc(USER_SYNC_RESET,               user_sync_reset_handler);
#ifdef POLYKYBD_LTR559_DRIVE
    transaction_register_rpc(USER_SYNC_SLAVE_DATA,          user_sync_slave_data_handler);
#endif

    fw_staging_init();

    poly_eeconf_t ee = load_user_eeconf();
    poly_sync_t* local_state = access_local_state();
    local_state->lang = ee.lang;
    local_state->contrast = ee.brightness;
    note_user_brightness(ee.brightness);
    // If host-auto brightness was engaged before the reboot, come up in auto mode
    // at the last auto value instead of the deliberate manual brightness (which the
    // host never refreshes, so a stale low manual value would otherwise show until
    // the host re-engages). Overrides local_state->contrast when auto was on.
    load_auto_brightness(ee.auto_brightness);
    note_idle_style(ee.idle_style);
    note_glyph_script(ee.glyph_script);
    // Restore the active-OS state (auto/manual + last known OS). Auto by default, so
    // a fresh EEPROM re-resolves per host via detection / host push; a manual pin
    // (e.g. Android) sticks. Seed local_state->active_os so the first render before
    // housekeeping runs already reflects the restored OS.
    load_os_state(ee.os_state);
    // Seed with the same OS|auto-flag encoding housekeeping uses, so the first
    // render/sync before housekeeping runs shows the correct auto/pin badge.
    local_state->active_os = (uint8_t)(get_active_os() | (get_os_auto_mode() ? POLY_OS_AUTO_FLAG : 0));
    // Seed the glyph-script override so the first render reflects the persisted script.
    local_state->glyph_script = get_glyph_script();
#ifdef RGB_MATRIX_ENABLE
    local_state->flags = set_flag(STATUS_DISP_ON, RGB_ON, rgb_matrix_is_enabled());
#else
    local_state->flags = STATUS_DISP_ON;   // no RGB on this variant
#endif

    memcpy(access_global_latin_table()->ex, ee.latin_ex, sizeof(ee.latin_ex));

    // Restore the MRU recents and schedule a one-time push to the slave half.
    mru_load(ee.mru_emoji, ee.mru_lang);

    set_displays(local_state->contrast, false);   // active brightness (auto value if restored, else manual)
#ifdef FW_UP_BOOT_TRACE
    boot_trace(U"4");
#endif
}

// Pre-initialization setup: initializes display hardware, loads EEPROM config, shows splash screen.
void keyboard_pre_init_user(void) {
    // Load the external-flash font pack and assemble g_all_fonts = resident ++
    // pack BEFORE the first render (show_splash_screen() below draws keycaps).
    // No valid pack (erased/corrupt/ABI mismatch) -> resident-only fonts.
    fontpack_init(RESIDENT_FONTS, (uint8_t)RESIDENT_FONT_COUNT);

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
    ee.auto_brightness = 0;   // host-auto off on a fresh EEPROM
    memset(ee.latin_ex, 0, sizeof(ee.latin_ex));
    // Empty MRU recents: the serialised form uses 0 == empty for both lists, so
    // a zeroed block reads back as "no recent" (no stray category-0 / lang-0).
    memset(ee.mru_emoji, 0, sizeof(ee.mru_emoji));
    memset(ee.mru_lang, 0, sizeof(ee.mru_lang));
    eeconfig_update_user_datablock(&ee, 0, sizeof(ee));
}


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
    // A host-initiated sleep can land while the doom attract screensaver runs —
    // tear it down first (engine stopped, pool handed back) so the demo doesn't
    // keep blitting into panels this function is about to switch off. No-op for
    // a real game session and on the slave.
    doom_screensaver_stop();
    poly_sync_t* local_state = access_local_state();
    local_state->overlay_flags = flag_off(local_state->overlay_flags, DISPLAY_OVERLAYS);
    local_state->flags &= ~((uint8_t)STATUS_DISP_ON) & ~((uint8_t)DISP_IDLE) & ~((uint8_t)IDLE_TRANSITION);// & ~((uint8_t)RGB_ON);
    local_state->contrast = DISP_OFF;
    reset_idle_jitter();
}

// Suspends keyboard: suspends power down, disables RGB, calls housekeeping, resets update timer.
void suspend_power_down_kb(void) {
    // USB suspend fires on slave when master enters bootloader; skip to keep displays lit.
    if (get_local_state()->overlay_flags & BOOTLOADER_DISPLAY) {
        return;
    }
    poly_suspend();
#ifdef RGB_MATRIX_ENABLE
    rgb_matrix_disable_noeeprom();
#endif
    sync_and_refresh_displays();
    // Flush all dirty user state to EEPROM on real power suspension — on each
    // half independently. This is the single routine save point: settings, latin,
    // default layer and the MRU recents are otherwise only held in RAM during a
    // session. Every block is dirty-gated, and the write sits at the very end of
    // the suspend sequence (after the final sync), so a flash consolidation can't
    // corrupt a live split transaction.
    save_all_dirty();
    suspend_power_down_user();
    disable_idle_tracking();
}

// Called by QMK before every reset (QK_REBOOT, QK_BOOTLOADER, and the host-triggered
// HID bootloader command, all via shutdown_quantum). Flush dirty user state to EEPROM
// here too — without this, a reboot/bootloader jump that isn't preceded by a USB
// suspend would discard any MRU/settings/layer changes still held in RAM.
bool shutdown_user(bool jump_to_bootloader) {
    save_all_dirty();
    return true;
}


// Resumes keyboard on wakeup: restores display state, brightness, RGB settings, calls housekeeping.
void suspend_wakeup_init_kb(void) {
    poly_sync_t* local_state = access_local_state();
    local_state->flags |= STATUS_DISP_ON;
    local_state->flags &= ~((uint8_t)DISP_IDLE);
    local_state->contrast = get_active_brightness();
    reset_idle_jitter();
    set_last_update(0);

    //rgb_matrix_reload_from_eeprom();
#ifdef RGB_MATRIX_ENABLE
    if(test_flag(local_state->flags, RGB_ON)) {
        rgb_matrix_enable_noeeprom();
    }
#endif

    update_performed();
    housekeeping_task_user();
    suspend_wakeup_init_user();
}
