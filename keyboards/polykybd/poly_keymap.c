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
#include "hints/os_hints.h"
#include "poly_keymap.h"   // our own exports — declarations checked against the definitions below
#include "status_oled.h"
#include "bridge_helper.h"
#include "profiling/loop_profile.h"
#ifdef POLYKYBD_LOOP_PROFILE
#    include "hardware/structs/timer.h"   // timer_hw->timerawl — raw 1 MHz us counter
#endif
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
#include "base/legend_plan.h"            // the pure keycap legend-SIZE planner
// Country flags (NotoColorEmoji_Regular_LangFlags, codepoints FLAG_CP_BASE+idx)
// now ship in the external-flash font pack, resolved via g_all_fonts — they are
// NOT compiled in. The tiny label font stays resident (no-pack fallback label).
#include "base/fonts/nano_font.h"          // 10px label font under the flags
#include "base/fonts/util_font.h"         // mid (10px) utility-label font
#include "polymod_core1.h"
#include "boot_diag.h"                    // emit_boot_banner(), splash_progress(), SPLASH_DONE
#include "polymod_crc32.h"

// The LTR-559 driver is the polykybd/polymod_ltr559 community module; the build
// defines COMMUNITY_MODULE_POLYMOD_LTR559_ENABLE when a variant lists it, and the
// module probes + polls the part from its own hooks. Everything below is the
// PolyKybd-side POLICY on top of it (auto-brightness, idle-inhibit, telemetry).
#ifdef COMMUNITY_MODULE_POLYMOD_LTR559_ENABLE
#    include "polymod_ltr559.h"
#    define LTR559_LOG_MS 600000   // sensor telemetry log cadence: 10 min
#endif
#include "ltr559_policy.h"        // the POLICY on the module (POLYKYBD_LTR559_DRIVE)

#include "state.h"
#include "poly_macro.h"
#include "multicore_exec.h"
#include "split_sync.h"
#include "poly_util.h"

#include "lang/lang_lut.h"
#include "lang/lang_lut_ext.h"

#include "layers.h"
#include "keycode_helper.h"
#include "doom/doom_mode.h"   // Doom easter egg (inline no-ops unless POLYKYBD_DOOM)
#include "anim/startup_anim.h"   // one-time procedural boot animation (split72; no-op stubs on split42)
#include "polymod_os_actions.h"
#include "uni.h"
#include "emoji/emoji_layer.h"
#include "lang_layer.h"
#include "mru.h"

#include <stdint.h>
#include <string.h>
#include <stdbool.h>

// The KC_OS_* keycode range and enum poly_os bind POSITIONALLY to the
// polymod_os_actions module's own two index spaces (its rows and columns) — the
// module owns no keyboard enum, so these asserts are the whole contract. Every
// row is pinned (not just first/last/count): both enums are append-only, but a
// middle insertion on one side alone would silently shift every action after it.
_Static_assert(KC_OS_COPY       - KC_OS_ACTION_BASE == OSA_COPY,       "KC_OS_* order must match enum polymod_os_action");
_Static_assert(KC_OS_CUT        - KC_OS_ACTION_BASE == OSA_CUT,        "KC_OS_* order must match enum polymod_os_action");
_Static_assert(KC_OS_PASTE      - KC_OS_ACTION_BASE == OSA_PASTE,      "KC_OS_* order must match enum polymod_os_action");
_Static_assert(KC_OS_UNDO       - KC_OS_ACTION_BASE == OSA_UNDO,       "KC_OS_* order must match enum polymod_os_action");
_Static_assert(KC_OS_REDO       - KC_OS_ACTION_BASE == OSA_REDO,       "KC_OS_* order must match enum polymod_os_action");
_Static_assert(KC_OS_SELALL     - KC_OS_ACTION_BASE == OSA_SELALL,     "KC_OS_* order must match enum polymod_os_action");
_Static_assert(KC_OS_FIND       - KC_OS_ACTION_BASE == OSA_FIND,       "KC_OS_* order must match enum polymod_os_action");
_Static_assert(KC_OS_LOCK       - KC_OS_ACTION_BASE == OSA_LOCK,       "KC_OS_* order must match enum polymod_os_action");
_Static_assert(KC_OS_SCRSHOT    - KC_OS_ACTION_BASE == OSA_SCRSHOT,    "KC_OS_* order must match enum polymod_os_action");
_Static_assert(KC_OS_SEARCH     - KC_OS_ACTION_BASE == OSA_SEARCH,     "KC_OS_* order must match enum polymod_os_action");
_Static_assert(KC_OS_APP_SWITCH - KC_OS_ACTION_BASE == OSA_APP_SWITCH, "KC_OS_* order must match enum polymod_os_action");
_Static_assert(KC_OS_WIN_SWITCH - KC_OS_ACTION_BASE == OSA_WIN_SWITCH, "KC_OS_* order must match enum polymod_os_action");
_Static_assert(KC_OS_EMOJI      - KC_OS_ACTION_BASE == OSA_EMOJI,      "KC_OS_* order must match enum polymod_os_action");
_Static_assert(KC_OS_WORD_LEFT  - KC_OS_ACTION_BASE == OSA_WORD_LEFT,  "KC_OS_* order must match enum polymod_os_action");
_Static_assert(KC_OS_WORD_RIGHT - KC_OS_ACTION_BASE == OSA_WORD_RIGHT, "KC_OS_* order must match enum polymod_os_action");
_Static_assert(KC_OS_LINE_HOME  - KC_OS_ACTION_BASE == OSA_LINE_HOME,  "KC_OS_* order must match enum polymod_os_action");
_Static_assert(KC_OS_LINE_END   - KC_OS_ACTION_BASE == OSA_LINE_END,   "KC_OS_* order must match enum polymod_os_action");
_Static_assert(KC_OS_ACTION_END - KC_OS_ACTION_BASE == OSA_ACTION_COUNT, "the KC_OS_* range and the chord table must be the same length");
// …and the six shared OS values, which is what makes poly_os_action_column()'s
// pass-through half correct (the GNOME/KDE refinements are folded, not shared).
_Static_assert((int)POLY_OS_UNKNOWN == (int)OSA_OS_UNKNOWN, "enum poly_os must match enum polymod_os_action_os");
_Static_assert((int)POLY_OS_WINDOWS == (int)OSA_OS_WINDOWS, "enum poly_os must match enum polymod_os_action_os");
_Static_assert((int)POLY_OS_MACOS   == (int)OSA_OS_MACOS,   "enum poly_os must match enum polymod_os_action_os");
_Static_assert((int)POLY_OS_LINUX   == (int)OSA_OS_LINUX,   "enum poly_os must match enum polymod_os_action_os");
_Static_assert((int)POLY_OS_ANDROID == (int)OSA_OS_ANDROID, "enum poly_os must match enum polymod_os_action_os");
_Static_assert((int)POLY_OS_IOS     == (int)OSA_OS_IOS,     "enum poly_os must match enum polymod_os_action_os");

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
static void render_macro_key(uint8_t id);   // defined below, used from render_key()
void update_displays(enum refresh_mode mode);
void set_displays(uint8_t contrast, bool idle);
void set_selected_displays(int8_t old_value, int8_t new_value);
void toggle_stagger(bool new_state);
void oled_update_buffer(void);
void oled_fw_apply_screen(void);   // oled_helper.c — firmware-apply status screen
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

// Tracks whether the previous update_displays() pass reached the keycap render (vs
// early-returning for idle / Eden / DOOM). On the mode->render edge we invalidate
// the dirty-window bboxes so the first awake render erases whatever the mode drew.
static bool s_disp_render_active = false;

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
        if (fw_staging_commit_pending() || get_local_state()->fw_confirm) {
            // Applying the staged firmware → reboot imminent, you can't type → orange.
            // The FW-2 confirmation prompt joins it: while it is up every key except
            // A/R is swallowed, which is exactly the "can't type" state orange means.
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
    // ...and while the FW-2 confirmation prompt is up (synced, so both halves glow):
    // the board is modal there, so it belongs to the same "can't type" cue.
    if (fw_staging_fw_up_active() || fw_staging_commit_pending() ||
        get_local_state()->fw_confirm) {
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

#endif

// Force the orange "applying — you cannot type" cue onto the LEDs synchronously.
//
// rgb_matrix_indicators_kb() already picks orange while commit_pending, but that
// only runs from the next rgb_matrix_task() — and on the apply path there is no
// next task: the blocking self-flash / mcu_reset never returns. So the cue the
// code appears to implement was in practice never seen. Push it out here, the
// same way oled_fw_apply_screen() flushes the status OLED in one pass.
//
// No-op on a variant without an RGB matrix (split42), so callers stay unguarded.
static void poly_flash_rgb_now(void) {
#ifdef RGB_MATRIX_ENABLE
    if (!rgb_matrix_is_enabled()) rgb_matrix_enable_noeeprom();
    rgb_matrix_set_color_all(24, 6, 0);      // same orange as the bootloader cue
    rgb_matrix_update_pwm_buffers();
#endif
}

#ifdef RGB_MATRIX_ENABLE
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

    // Overlay-burst coalescing: while a program-switch overlay burst is still
    // arriving, DEFER starting a fresh render. Each report would otherwise redraw
    // half-staged overlays that the next report obsoletes (measured ~12 full renders
    // per switch). Render on whichever fires first: the burst going quiet
    // (OVERLAY_COALESCE_QUIET_MS), enough overlays piling up to stay reactive on a
    // long transfer (OVERLAY_COALESCE_FLUSH_COUNT), or the hard cap
    // (OVERLAY_COALESCE_MAX_MS). Only a FRESH render is held — a render already in
    // progress (START_SECOND_HALF) always finishes, and non-overlay refreshes fall
    // straight through (their overlay_activity_elapsed() is already large, pending 0).
    // The state/bridge sync above already ran, so deferring the render doesn't stall
    // the slave; the loop stays fast (no ~100 ms render) while held.
    static bool     s_holding    = false;
    static uint32_t s_hold_since = 0;
    if (refresh == ALL_AT_ONCE || refresh == START_FIRST_HALF) {
        bool settled = overlay_activity_elapsed() >= OVERLAY_COALESCE_QUIET_MS;
        bool enough  = overlay_pending_count()   >= OVERLAY_COALESCE_FLUSH_COUNT;
        if (!settled && !enough) {
            if (!s_holding) { s_holding = true; s_hold_since = timer_read32(); }
            if (timer_elapsed32(s_hold_since) < OVERLAY_COALESCE_MAX_MS) {
                return;   // hold the render; g_refresh stays pending for a later pass
            }
        }
        // Committing to a fresh render now: reset the hold + consume the pending
        // overlays so the next FLUSH_COUNT is counted from here (chunked progress).
        s_holding = false;
        clear_overlay_pending();
    }

    if (refresh == START_FIRST_HALF) {
#ifdef POLYKYBD_LOOP_PROFILE
        uint32_t _lp_r0 = timer_hw->timerawl;
#endif
        update_displays(START_FIRST_HALF);
#ifdef POLYKYBD_LOOP_PROFILE
        loop_profile_add_render_us(timer_hw->timerawl - _lp_r0);
#endif
        set_disp_refresh(START_SECOND_HALF);
    }
    else if (refresh == START_SECOND_HALF || refresh == ALL_AT_ONCE) {
#ifdef POLYKYBD_LOOP_PROFILE
        uint32_t _lp_r0 = timer_hw->timerawl;
#endif
        update_displays(refresh);
#ifdef POLYKYBD_LOOP_PROFILE
        loop_profile_add_render_us(timer_hw->timerawl - _lp_r0);
#endif
        set_disp_refresh(DONE_ALL);
    }
}

// The modifier that, together with Intl (_ADDLANG1), opens the variation picker
// on the number row.
//
// ⚠️ This was MOD_MASK_ALT and must not go back.  The picker swallows the keys it
// handles (process_record_user returns false), so the host only ever saw the
// modifier go down and back up — and a bare Alt tap is how Windows activates the
// menu bar.  Picking a variation therefore yanked focus out of the text field in
// a lot of programs, which is precisely when you are typing accented letters.  A
// bare Ctrl tap does nothing in the same apps.
#define LATIN_PICKER_MOD MOD_MASK_CTRL

// True only while the Ctrl the picker is riding on was registered BY the latch, so
// dropping it on layer exit can never release a Ctrl the user is really holding.
static bool s_picker_latched = false;

// Sets layer state variable tracking the active keyboard layer.
static void latin_picker_reset_page(void);   // defined with the picker helpers below
static void latin_remap_cancel(void);        // ditto

layer_state_t layer_state_set_user(layer_state_t state) {
    access_local_layer()->layer = state;
    if(get_highest_layer(state) != _SL) {
        // Leaving settings re-hides the advanced keys, so revealing them is a
        // deliberate act on every visit rather than a mode you can leave armed.
        // That is the whole safety argument for putting QK_BOOT behind the gate.
        access_local_state()->settings_more = 0;
    }
    if(get_highest_layer(state) != _ADDLANG1) {
        // Leaving Intl closes the picker. Without this the latched Ctrl would stay
        // registered with the layer gone, so every following keystroke reaches the
        // host as Ctrl+key.
        if(s_picker_latched) {
            unregister_mods(MOD_MASK_CTRL);
            s_picker_latched = false;
        }
        // Reset the page unconditionally, NOT only when we owned the latch: the
        // user can hold Ctrl themselves and page, in which case s_picker_latched is
        // false and the page would survive to the next visit to the layer.
        latin_picker_reset_page();
        // Same reason: an abandoned remap would otherwise still be showing its
        // "pick a key" prompt on the next visit to the layer.
        latin_remap_cancel();
    }
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
    // Exit idle FIRST, in EVERY flavour it comes in. If the keyboard had
    // dimmed/idled when the flash begins, the keycaps were dark and
    // update_displays() early-returns — so the legible base legends never get
    // drawn and "some keys are not lit" during the flash. The two animated idle
    // styles do NOT show up as "DISP_IDLE, or contrast pulsed to DISP_OFF", and
    // both own the keycaps through a tick that housekeeping FREEZES for the whole
    // flash (doom_tick / eden_idle_tick sit behind the !fw_up_active gate), so
    // neither can tear itself down once staging starts:
    //   * DOOM attract (IDLE_STYLE_IDDQD) runs with STATUS_DISP_ON SET and
    //     DISP_IDLE CLEARED at the user brightness — it matches no idle test at
    //     all, and left up it holds a half-drawn demo frame and swallows keys for
    //     the whole transfer. Screensaver only, never an active game — the same
    //     rule poly_force_wake()/poly_suspend() follow.
    //   * Eden (IDLE_STYLE_EDEN) does set DISP_IDLE, but clearing the flag is not
    //     enough: update_displays() also early-returns while startup_anim_active(),
    //     so the render below would draw nothing.
    // Mirrors display_wakeup() / poly_force_wake(); deliberately UNCONDITIONAL —
    // a partial fade (IDLE_TRANSITION, contrast somewhere in between) matches
    // neither of the old tests, and on an awake keyboard the assignments are a
    // no-op while the timestamp below must be stamped either way.
    doom_screensaver_stop();   // self-guards: only an active attract demo
    startup_anim_stop();       // looping Eden (and a one-shot mid-flight)
    poly_sync_t* local_state = access_local_state();
    local_state->flags &= ~((uint8_t)DISP_IDLE) & ~((uint8_t)IDLE_TRANSITION);
    local_state->flags |= STATUS_DISP_ON;
    local_state->contrast = get_active_brightness();
    reset_idle_jitter();       // fresh centred legends, not jittered offsets
    // Restart the idle countdown from the start of the flash — a deliberate host
    // command, so this is real activity by update.h's rule. Unconditional matters:
    // a keyboard 100 s into its 120 s FADE_OUT_TIME was awake (so the old gated
    // stamp never ran) and would fade out 20 s into the transfer.
    update_performed();
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

// The LTR-559 auto-brightness + idle-inhibit POLICY (the slave-data pull channel,
// lux -> contrast mapping and the proximity wake) lives in ltr559_policy.c —
// see poly_ltr559_register_split_handler() / poly_ltr559_drive().

#ifdef POLY_DUMMY_TXN_TEST
// Root-cause experiment: a no-op split-transaction handler. Registering three of these
// grows NUM_TOTAL_TRANSACTIONS by 3 (matching the working pointing build) without the
// pointing device, to test whether split42's dead split link is transaction-count
// dependent. Never actually invoked (the master never execs these ids).
static void user_sync_dummy_handler(uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data) {
    (void)in_len; (void)in_data; (void)out_len; (void)out_data;
}
#endif

// Eden idle screensaver runs DIM (anti-burn-in + it's a sleeping-keyboard ambience,
// not a legend you need to read). This is the OLED contrast register value, not a
// brightness level — a small number is a faint glow.
#define EDEN_IDLE_BRIGHTNESS 4

// Idle "Eden" screensaver driver (IDLE_STYLE_EDEN), run every housekeeping pass on
// BOTH halves (like doom_tick). It renders the looping boot animation while the
// synced idle state says we are idling in EDEN style — the signal is DISP_IDLE +
// idle_style, both already carried on the poly sync, so no extra UART field. Each
// half draws its own keycaps off its own local timer (a few ms of skew, invisible,
// exactly like the boot animation). The one-shot boot/KC_EDEN animation is never
// disturbed: `want` requires DISP_IDLE (never set during boot) and we skip while a
// one-shot is active. On wake/turn-off the flag clears (display_wakeup / poly_suspend)
// and the loop stops, handing the keycaps back to update_displays.
static void eden_idle_tick(void) {
    const poly_sync_t* ls = get_local_state();
    const bool one_shot = startup_anim_active() && !startup_anim_is_loop();
    const bool want = (ls->idle_style == IDLE_STYLE_EDEN) &&
                      ((ls->flags & DISP_IDLE) != 0) && !one_shot;
    if (want) {
        if (!startup_anim_is_loop()) {
            startup_anim_start_loop(EDEN_IDLE_BRIGHTNESS);   // dim glow, both halves
        }
        startup_anim_tick();
    } else if (startup_anim_is_loop()) {
        // Loop just ended (woke / turned off). Request a refresh so THIS half repaints
        // its real legends now that Eden released the keycaps — the slave has no
        // display_wakeup of its own (keys are processed on the master), so without
        // this it could keep the last Eden frame until the next unrelated diff
        // (the "slave didn't repaint on a plain key, only on Shift" symptom).
        startup_anim_stop();
        request_disp_refresh();
    }
}

void housekeeping_task_user(void) {
    // Optional loop-timing probe (no-op unless POLYKYBD_LOOP_PROFILE). At the very
    // top so it measures the FULL previous iteration — matrix scan, HID, bridge.
    loop_profile_tick();
#ifdef RGB_MATRIX_ENABLE
    flash_rgb_tick();   // light the matrix while a font-pack/firmware flash runs
#endif

    boot_banner_housekeeping_tick();   // re-emit the boot banner for a late console

    // Advance a playing macro by at most one step. Deliberately near the TOP of
    // housekeeping and outside every fw_up/idle gate: a macro is user-visible typing,
    // so it must not be starved by a flash or by the idle machinery, and a step that
    // took the long way round would show up as uneven keystroke timing on the host.
    poly_macro_tick();

    // Drain at most one queued macro label to the slave. Master-only (the slave is the
    // receiver), and one per pass because each bridge can spend its retries -- sixteen
    // of them back to back would be a visible stall on a link that is already unhappy.
    // Not while an apply is armed: the slave has already been told to reboot, so every
    // bridge here spends its full retry budget on a link that is going dark -- hundreds
    // of ms of blocked main loop immediately before the self-flash, for a label nobody
    // will read.
    if (is_keyboard_master() && !fw_staging_commit_pending()) {
        poly_macro_label_sync_tick();
    }

    // fw_up state machine: apply on success path, advance deferred erase.
    // Both must run regardless of fw_up_active so the slave's erase actually
    // progresses and the master's apply-and-reboot fires after a successful
    // commit.
    // Split across housekeeping passes ON PURPOSE, one stage per pass.
    //
    // Two reasons, and the second is why it is worth the three extra passes. (1) The
    // console only leaves this board from the main loop, so a marker printed
    // immediately before a call that never returns is simply lost -- which is how a
    // hang here has repeatedly been invisible. Returning between stages gives each
    // marker a main loop to go out on, so the log names the stage that wedged.
    // (2) EVERY stage below touches flash or the split link, and they must not be
    // interleaved with the rest of housekeeping while core1 is locked out.
    // ⚠️ FUNCTION-SCOPE, not inside the `if`: the sequence below only advances
    // while the apply is armed, and BOTH fw_staging_begin_target() and
    // fw_staging_begin_deferred_target() clear s_commit_pending. A host that
    // abandons an update and starts a fresh one mid-sequence would otherwise
    // leave this stuck at 1-3, and the NEXT apply would resume there -- skipping
    // clear_keyboard() (a key down at that moment auto-repeats on the host until
    // USB drops) and save_all_dirty(). The else-branch below puts it back.
    static uint8_t apply_step = 0;
    if (fw_staging_commit_pending()) {
        switch (apply_step) {
            case 0:
                // Release anything still held. From here the main loop never scans the
                // matrix again (the self-flash below does not return), so a key that is
                // physically down when the apply is armed would never have its release
                // reported — the host keeps it registered and auto-repeats until USB
                // drops at the reboot (field: hundreds of repetitions).
                clear_keyboard();
                poly_flash_rgb_now();
                // Paint the "⟳Applying / Firmware⟳" notice and flush it fully BEFORE the
                // blocking self-flash below (which never returns), so the status OLED is
                // completely refreshed — not torn mid-transition — as the apply begins.
                // Runs on both halves; each draws its own side's word. Its ~26 ms of I2C
                // also gives the clear_keyboard() report time to leave over USB.
                oled_fw_apply_screen();
                uprintf("APPLY 1/4: keys cleared, RGB + OLED flushed\n");
                apply_step = 1;
                return;
            case 1:
                // Persist MRU/settings before the firmware swap — this path resets via
                // watchdog (never returns) and skips shutdown_quantum. Transfer is done
                // by commit, so the blocking flash write is safe here.
                //
                // ⚠️ Under the core1 lockout: this can force a wear-levelling
                // CONSOLIDATION, i.e. a flash erase, and QMK's backing store does not
                // lock core1 out itself. Core1 was relaunched moments ago by the staging
                // erase, so it is free to be fetching from XIP exactly when the QSPI
                // leaves XIP mode — the bus then stalls and the erase never returns.
                //
                // ⚠️ The lockout is RELEASED before returning, not held across the
                // remaining stages. Each stage hands control back to the main loop,
                // which can service a raw-HID overlay -- and a compressed one is
                // dispatched to core1 through a BLOCKING FIFO push plus a spin on
                // core1's completion counter. With core1 in PSM reset nothing drains
                // it, so core0 would hang before the apply could even start. The
                // window is a pass or two; the failure is permanent. Holding it here
                // bought nothing either: fw_staging_do_apply() halts core1 itself.
                fw_staging_core1_lockout_begin();
                save_all_dirty();
                fw_staging_core1_lockout_end();
                uprintf("APPLY 2/4: EEPROM flushed with core1 held off\n");
                apply_step = 2;
                return;
            case 2: {
                // Verify the staged bytes IN FLASH and report them, then RETURN so the
                // line actually goes out. Everything below this point is a call that
                // does not come back, and the console only drains from the main loop --
                // a marker printed immediately before it is simply lost, which is how
                // "did it enter the copy?" stayed unanswerable for several rounds.
                uint32_t size = 0, want = 0, got = 0;
                const bool good = fw_staging_verify_staged_flash(&size, &want, &got);
                uprintf("APPLY 3/4: staged %lu B (%lu sectors) crc want=%08lx got=%08lx -> %s\n",
                        (unsigned long)size,
                        (unsigned long)((size + 4095u) / 4096u),
                        (unsigned long)want, (unsigned long)got, good ? "OK" : "MISMATCH");
                if (!good) {
                    // Refuse rather than erase a working firmware with an image we
                    // cannot vouch for. The board stays usable and the host can retry.
                    uprintf("APPLY: refusing to overwrite firmware from a bad staged image\n");
                    fw_staging_cancel_apply();
                    apply_step = 0;
                    return;
                }
                apply_step = 3;
                return;
            }
            default:
                // Nothing but the call: if the 3/4 line above is the last thing in the
                // log, the copy was entered and did not come back.
                apply_step = 0;
                fw_staging_apply_and_reboot();
                // Only reached when the apply was refused (no valid staged image /
                // failed flash CRC); it disarms itself there and the board keeps
                // running. Stage 2 already handed core1 back, and the refusal checks
                // sit above the PSM halt inside do_apply, so this is a no-op today --
                // kept because it is the one path where a future halt-then-refuse
                // would otherwise leave the RLE service down for good.
                fw_staging_core1_lockout_end();
                break;
        }
    } else if (apply_step != 0) {
        // Disarmed from another path (a new BEGIN) part-way through. Start the
        // next apply from stage 0 rather than resuming mid-sequence.
        uprintf("APPLY: disarmed at stage %u — sequence reset\n", (unsigned)apply_step);
        apply_step = 0;
    }
    if (fw_staging_reboot_pending()) {
        clear_keyboard();   // same as above: no further matrix scan before the reset
        poly_flash_rgb_now();
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

    // FW-2: drive the unsigned-image confirmation prompt. The master owns the
    // decision, but poly_sync_t.fw_confirm is synced so BOTH halves turn their
    // keycaps into the dialog (the slave's own render is driven by the sync
    // handler's refresh). Deliberately outside the !fw_up_active gate below: the
    // prompt only goes up at COMMIT, i.e. after finalize has already cleared
    // fw_up_active, and a prompt that could not be drawn would be unanswerable.
    // is_keyboard_master(), not is_usb_host_side(): the confirmation state only
    // ever exists on the half that verified the signature, and fw_staging gates
    // that on is_keyboard_master() (which the HIL images override per side).
    if (is_keyboard_master()) {
        fw_staging_confirm_tick();
        const uint8_t want = fw_staging_awaiting_confirm() ? 1 : 0;
        poly_sync_t *cfm_state = access_local_state();
        if (cfm_state->fw_confirm != want) {
            cfm_state->fw_confirm = want;
            if (want) {
                // Release anything still registered host-side BEFORE we start
                // swallowing events. A key held when the prompt goes up would
                // otherwise never have its release forwarded — the host keeps the
                // keycode down and auto-repeats it for the whole window (field:
                // "a few hundred repetitions until the keyboard rebooted"). Same
                // reasoning, same fix as doom_begin().
                clear_keyboard();
            }
            request_disp_refresh();
        }
        // Hold the idle timer off while we are asking: the fade/pulse would dim
        // the prompt out from under the user (update_displays early-returns once
        // DISP_IDLE is set, so it would never be redrawn either).
        if (want) {
            update_performed();
        }
    }

    // Hold the idle countdown off for the whole transfer, the same way the
    // confirmation prompt above does. The idle state machine below is NOT behind
    // the !fw_up_active gate (only the refresh that would act on it is), so a
    // flash long enough to cross FADE_OUT_TIME with nobody typing flips the state
    // to idle mid-transfer — the keycaps poly_prepare_for_flash() just made
    // legible then go dark the moment the flash releases the display path (a
    // font-pack flash, which does not reboot, shows this plainly). A flash is a
    // deliberate host command, so this is legitimate activity by update.h's rule.
    if (fw_staging_fw_up_active()) {
        update_performed();
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
        // Idle "Eden" screensaver frame tick (IDLE_STYLE_EDEN, both halves). Runs
        // before the boot-animation block below and owns the LOOPING variant; the
        // block below is for the ONE-SHOT boot/KC_EDEN animation only.
        eden_idle_tick();
        // One-time startup animation: render a frame while active (both halves
        // render their own keycaps). On the finishing edge, persist the "played"
        // marker and request a normal refresh so the base legends come back. Gated
        // to the ONE-SHOT animation — the looping idle screensaver is driven by
        // eden_idle_tick() above and must not run the finish edge / nonce sync here.
        if (startup_anim_active() && !startup_anim_is_loop()) {
            // Render THIS half's frame first — the keycap animation must never wait on
            // the split link (the boot-time link is flaky; blocking here left the master
            // stuck on the splash with dark keycaps).
            startup_anim_tick();
            // Then, ONCE and only when the transport is actually up, deliver the replay
            // nonce to the slave so a HID replay (cmd 31) or a KC_EDEN-rearmed boot plays
            // on both halves in lockstep. We skip the normal per-pass
            // sync_and_refresh_displays() while Eden owns the displays, so this is the
            // only carrier for the nonce. Gated on is_transport_connected() (non-blocking)
            // so a fresh boot — where both halves already animate from their own marker —
            // never stalls waiting on a slave that is still coming up; the send just
            // rides along once the link settles and the slave's !active guard makes it a
            // no-op there. Retried each pass until it lands (or the animation ends).
            static bool s_anim_synced = false;
            if (!s_anim_synced && is_usb_host_side() && is_transport_connected()) {
                // Classify the ack via sync_succeeded() — never bool-test send_to_bridge()
                // directly (every return is non-zero). Only latch on a genuine success so a
                // failed send stays eligible for retry on a later pass.
                uint8_t ack = send_to_bridge(USER_SYNC_POLY_DATA, (void *)access_local_state(), sizeof(poly_sync_t), 3);
                if (sync_succeeded(ack)) {
                    s_anim_synced = true;
                }
            }
            if (!startup_anim_active()) {   // just finished this pass
                s_anim_synced = false;      // re-arm for the next replay (KC_EDEN / HID)
                mark_boot_intro_done();
                // Eden ran at full brightness; restore the user's normal
                // brightness behaviour now that it has faded to black, then
                // redraw the real legends and resume normal split sync.
                set_displays(get_local_state()->contrast, false);
                request_disp_refresh();
                sync_and_refresh_displays();
            }
            // While Eden plays we OWN the displays: skip sync_and_refresh_displays
            // (its set_displays() would overwrite our full-bright contrast with the
            // user brightness — the "brightness changes mid-animation" bug) and skip
            // the boot forced-layer-resync's per-pass blocking UART, which also
            // stole frame time. Each half renders its own keycaps independently.
        } else {
            sync_and_refresh_displays();
        }
        // Drain a pending overlay-mapping repair (armed by enable_overlays when a
        // bridge dropped during an app switch). Master-only — it re-pushes OUR
        // tables to the slave — and bounded per tick, so it can never turn into
        // the multi-second main-loop stall an inline repair would be on a bad link.
        if (is_usb_host_side()) {
            overlay_map_repair_tick();
        }
#ifdef POLY_SPLIT_HEARTBEAT_EXPERIMENT
        // ROOT-CAUSE EXPERIMENT (split42, 2026-07-14). Reproduce the every-cycle
        // master->slave pull that SPLIT_POINTING_ENABLE provided, but with the
        // pointing device fully DISABLED, by pulling the slave every housekeeping
        // cycle over the existing generic USER_SYNC_SLAVE_DATA channel. (The LTR559
        // drive pull is only every LTR559_DRIVE_MS=500ms — present in the broken
        // build b25f2045 — so frequency is the suspected variable.) Self-contained:
        // references only the transaction id + a raw reply buffer (>= the 4-byte
        // ltr559_sync_t the handler writes), no pointing/LTR types. Requires
        // USER_SYNC_SLAVE_DATA registered (POLYKYBD_LTR559_DRIVE, which split42 has).
        //   split42 works -> the dependency is a frequent every-cycle slave pull;
        //     the proper fix is a heartbeat in the poly transport, not a borrowed feature.
        //   split42 breaks -> an every-cycle pull is NOT sufficient; the dependency is
        //     structural (transaction count / split_shmem layout / a pointing init path).
        if (is_usb_host_side()) {
            uint8_t kind     = 0;          // SLAVE_DATA_SENSOR
            uint8_t reply[4] = {0};        // >= sizeof(ltr559_sync_t)
            (void)transaction_rpc_exec(USER_SYNC_SLAVE_DATA, sizeof(kind), &kind, sizeof(reply), reply);
        }
#endif
#ifdef COMMUNITY_MODULE_POLYMOD_LTR559_ENABLE
        // The module's own housekeeping hook has already polled the sensor this
        // pass — quantum/keyboard.c runs housekeeping_task_modules() before
        // housekeeping_task_user(), so the reading below is this pass's, not the
        // previous one's. It polls on BOTH halves (the sensor is auto-detected on
        // whichever half it is soldered to); the half without it gives up after a
        // bounded number of retries so it can't stall the loop.
        //
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
    // The ONE-SHOT boot/KC_EDEN animation owns the whole idle pipeline; skip it.
    // The LOOPING Eden screensaver, however, still needs this block to run — it sets
    // DISP_IDLE, keeps contrast steady, and reaches the TURN_OFF suspend deadline.
    if(is_idle_tracking() && !(startup_anim_active() && !startup_anim_is_loop())) {
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
                    // Log the CONFIGURED style on every transition: three different
                    // styles land in the pulse branch below (pulse, jitter, and an
                    // iddqd that could not start its demo), so an unnamed
                    // "Transition to pulsing" cannot tell you which one is active.
                    const uint8_t style = get_idle_style();
                    if (style == IDLE_STYLE_IDDQD && doom_screensaver_start()) {
                        // Doom attract screensaver instead of the pulse: the demo
                        // owns the keycaps at the user brightness — no DISP_IDLE,
                        // and IDLE_TRANSITION stays dropped (cleared above), which
                        // fires the back_from_idle_transition brightness restore.
                        // doom_tick() holds last_update while the demo runs and
                        // hands over to the normal TURN_OFF suspend at its own
                        // deadline. Falls through to the pulse whenever the demo
                        // can't start (non-doom build, fw staging active).
                        contrast = get_active_brightness();
                        uprint("Transition to idle [style=iddqd] - doom screensaver\n");
                    } else if (style == IDLE_STYLE_EDEN) {
                        // Eden screensaver: enter DISP_IDLE like the pulse (so the
                        // wake-on-key and TURN_OFF suspend paths work unchanged and
                        // the flag+idle_style tell the slave to loop too), but hold
                        // contrast DIM and steady — eden_idle_tick() owns the pixels
                        // every pass and the loop migrates them itself, so there is no
                        // burn-in and no per-pass pulse contrast to fight.
                        contrast = EDEN_IDLE_BRIGHTNESS;
                        flags |= DISP_IDLE;
                        uprint("Transition to idle [style=eden] - eden screensaver\n");
                    } else {
                        contrast = DISP_OFF;
                        flags |= DISP_IDLE;
                        flags |= IDLE_TRANSITION;
                        // An IDDQD here means doom_screensaver_start() refused (no
                        // doom build / fw staging active) and we silently degraded
                        // to the legacy pulse — say so rather than reporting a plain
                        // "pulsing" the user never selected.
                        uprintf("Transition to idle [style=%s] - pulsing%s\n",
                                idle_style_name(style),
                                style == IDLE_STYLE_IDDQD ? " (doom unavailable)" : "");
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
                if (get_idle_style() == IDLE_STYLE_EDEN) {
                    // Eden owns the visuals via eden_idle_tick(); keep the panel DIM and
                    // steady and DON'T compute a pulse contrast (a per-pass contrast diff
                    // would call kdisp_idle() and fight the animation).
                    contrast = EDEN_IDLE_BRIGHTNESS;
                } else {
                    int32_t time_after = PK_MAX(elapsed_time_since_update - FADE_OUT_TIME - FADE_TRANSITION_TIME, 0)/300;
                    contrast = time_after%50;
                    // In JITTER style each key relocates its own legend independently as
                    // it pulses dark (kdisp_idle) — there is no shared per-cycle offset
                    // to compute here; only the pulse `contrast` drives both halves.
                }
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
        // Master-authoritative keycap legend size, adopted + re-rendered by the
        // slave the same way.
        if (access_local_state()->glyph_size != get_glyph_size()) {
            access_local_state()->glyph_size = get_glyph_size();
            request_disp_refresh();   // size changed -> re-render every main legend
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



// The function layer a base layout reaches. There is exactly ONE (`_FL`) — the split
// into _FL0/_FL1 existed only so the F-row lined up with each layout's number row, and
// that is now computed per layout instead (fl_aligned_keycode). Kept as a function
// rather than inlining `_FL` at the call site: `default: 0` still says "this base layer
// has no function layer" for anything outside the five.
// Global variables: (none - uses passed parameters only)
layer_state_t get_function_layer(layer_state_t def_layer) {
    switch (def_layer) {
        case _L0:
        case _L1:
        case _L2:
        case _L3:
        case _L4:
            return _FL;
        default:
            return 0;

    }
}


// Returns display text for special keys.

// "xx-YY" for a language index. The code table is cog-generated inside
// to_static_text() below, so route through it rather than keeping a second copy —
// the status OLED needs the same string the language layer prints under each flag.
const uint32_t* poly_lang_code(uint8_t lang) {
    if(lang >= NUM_LANG) return U"";
    const led_t none = {0};
    return to_static_text((uint16_t)(KCL_ENUS + lang), none);
}

// The settings layer opens with only its everyday keys; this names the rest.
//
// Two properties are load-bearing and easy to lose. It is keyed on the KEYCODE, not
// on a position, so moving a key around _SL cannot silently un-gate it. And it is
// consulted from BOTH the legend path (to_static_text) and the action path
// (process_record_user) — a key that is blank but still fires is worse than one that
// is merely visible, since QK_BOOT and QK_RBT are in here and neither is undoable
// from the board.
//
// Every keycode listed here is mapped exactly once, on _SL, in both keymaps —
// split42 carries its own KC_SETTINGS_MORE — so the list alone is a sufficient
// gate and settings_more_hidden() needs no layer test (see the note there).
// Re-check that if one of these is ever mapped somewhere else.
static bool settings_key_is_gated(uint16_t keycode) {
    switch (keycode) {
        case KC_IDLE_STYLE:
        case KC_GLYPH_SCRIPT:
        case LBL_TEXT:
        case KC_TOGMODS:
        case KC_TOGTEXT:
        case QK_BOOTLOADER:
        case QK_REBOOT:
        case QK_DEBUG_TOGGLE:
        case KC_DEADKEY:
        case KC_EDEN:
            return true;
        default:
            return false;
    }
}

// True while the gated keys must stay blank and inert.
//
// ⚠️ Deliberately NOT also gated on _SL being the active layer. Every gated
// keycode is mapped exactly once, on _SL, in BOTH keymaps (split42 has its own
// KC_SETTINGS_MORE), so the layer test could only ever be redundant — and it is
// read from get_local_layer(), the SYNCED snapshot, which lags a layer change by
// up to one housekeeping pass. A render that lands inside that window sees the
// old layer, decides the gate does not apply, and draws the advanced keys; only
// a later refresh would blank them, and _SL usually gets no later refresh. So the
// clause could not hide anything the keycode test does not, and could reveal what
// it is there to hide.
static bool settings_more_hidden(uint16_t keycode) {
    return settings_key_is_gated(keycode) && get_local_state()->settings_more == 0;
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

    // On the Intl layer Ctrl is not a modifier you send — it is LATIN_PICKER_MOD,
    // the key that turns the number row into the variation picker. Showing the
    // plain Ctrl symbol there gives no hint that it changes anything, so it gets
    // its own legend. Checked BEFORE keycode_to_static_text(), which would
    // otherwise return the normal Ctrl glyph first.
    if ((keycode == KC_LEFT_CTRL || keycode == KC_RIGHT_CTRL) &&
        get_highest_layer(get_local_layer()->layer) == _ADDLANG1) {
        return INTL_PICKER_LEGEND;
    }

    // Blank the advanced settings keys until KC_SETTINGS_MORE reveals them. Returning
    // an EMPTY string rather than NULL matters: NULL sends update_displays() on to
    // render_key(), which would draw the key's chrome around nothing — the "modifier
    // badge in an empty cell" shape this file already warns about.
    if (settings_more_hidden(keycode)) {
        return U"";
    }

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
        case KC_L0:                         return local_layer->def_layer == _L0 ? MID_WORD_OVER_ICON("Qwerty", ICON_SWITCH_ON)
                                                                                          : MID_WORD_OVER_ICON("Qwerty", ICON_SWITCH_OFF);
        case KC_L1:                         return local_layer->def_layer == _L1 ? MID_WORD_OVER_ICON("Stag!", ICON_SWITCH_ON)
                                                                                          : MID_WORD_OVER_ICON("Stag!", ICON_SWITCH_OFF);
        case KC_L2:                         return local_layer->def_layer == _L2 ? MID_WORD_OVER_ICON("Colemk", ICON_SWITCH_ON)
                                                                                          : MID_WORD_OVER_ICON("Colemk", ICON_SWITCH_OFF);
        case KC_L3:                         return local_layer->def_layer == _L3 ? MID_WORD_OVER_ICON("Neo", ICON_SWITCH_ON)
                                                                                          : MID_WORD_OVER_ICON("Neo", ICON_SWITCH_OFF);
        case KC_L4:                         return local_layer->def_layer == _L4 ? MID_WORD_OVER_ICON("Workm", ICON_SWITCH_ON)
                                                                                          : MID_WORD_OVER_ICON("Workm", ICON_SWITCH_OFF);
        // Doom easter-egg menu item: blank until typing IDDQD arms it
        // (doom_mode.c; always blank in non-doom builds via the stub).
        case KC_IDDQD:                      return doom_egg_armed() ? U"IDDQD" : U"";

        // The legend-size key states BOTH what it will do and where you are: the
        // increase/decrease icon plus the current tier as a digit in the top-right.
        // Shift swaps the icon and reverses the step (poly_custom_key_action reads
        // the same modifier), so one key replaces the old KC_GLYPH_SIZE_UP/_DOWN pair.
        //
        // ⚠️ It lives HERE and not in keycode_to_static_text() because both halves of
        // it are SYNCED state: the size comes from poly_sync_t and the modifiers from
        // poly_layer_t, and keycode_to_static_text() only receives `led_t` — so on the
        // slave it would draw the master's tier with its own (always clear) mods.
        case KC_GLYPH_SIZE_UP: {
            const bool     shifted = (local_layer->mods & MOD_MASK_SHIFT) != 0;
            const uint8_t  size    = local_state->glyph_size < GLYPH_SIZE_COUNT
                                         ? local_state->glyph_size : GLYPH_SIZE_S;
            static const uint32_t* const legend[2][GLYPH_SIZE_COUNT] = {
                { GLYPH_SIZE_LEGEND(ICON_FONT_BIGGER,  U"1"),
                  GLYPH_SIZE_LEGEND(ICON_FONT_BIGGER,  U"2"),
                  GLYPH_SIZE_LEGEND(ICON_FONT_BIGGER,  U"3") },
                { GLYPH_SIZE_LEGEND(ICON_FONT_SMALLER, U"1"),
                  GLYPH_SIZE_LEGEND(ICON_FONT_SMALLER, U"2"),
                  GLYPH_SIZE_LEGEND(ICON_FONT_SMALLER, U"3") },
            };
            return legend[shifted ? 1 : 0][size];
        }

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

// ── Keycap legend SIZE (enum poly_glyph_size, HID cmd 34) ────────────────────
//
// The relocation + placement arithmetic is the PURE planner in base/legend_plan.c
// (unit-tested: make test:polykybd_legend_plan); the wrappers below bind it to the
// assembled font set and this variant's visible window, keeping the signatures the
// render path has always called.
_Static_assert(LEGEND_PLAN_SIZE_COUNT == GLYPH_SIZE_COUNT,
               "legend_plan's size table no longer covers poly_glyph_size");
_Static_assert(LEGEND_PLAN_SIZE_S == GLYPH_SIZE_S && LEGEND_PLAN_SIZE_M == GLYPH_SIZE_M &&
               LEGEND_PLAN_SIZE_L == GLYPH_SIZE_L,
               "legend_plan's size indices drifted from poly_glyph_size");

static bool legend_has_glyph_cb(uint32_t cp, void* ctx) {
    (void)ctx;
    return kdisp_gfx_glyph(g_all_fonts, g_all_font_count, cp) != NULL;
}

static void legend_bbox_cb(const uint32_t* text, int8_t* xmin, int8_t* xmax, int8_t* ymin, int8_t* ymax, void* ctx) {
    (void)ctx;
    kdisp_gfx_text_bbox(g_all_fonts, g_all_font_count, text, xmin, xmax, ymin, ymax);
}

static const legend_plan_env_t legend_plan_env = {
    .has_glyph = legend_has_glyph_cb,
    .bbox      = legend_bbox_cb,
    .ctx       = NULL,
    .win_x0    = BUFFER_X,
    .win_x1    = BUFFER_X + SCREEN_WIDTH - 1,
    .win_y1    = SCREEN_HEIGHT - 1,
};

static bool glyph_size_remap(uint8_t size, const uint32_t* text, uint32_t* out, uint8_t out_cap) {
    return legend_plan_remap(&legend_plan_env, size, text, out, out_cap);
}

static void plan_main_legend(const uint32_t* text, int8_t small_x, int8_t small_y,
                             uint32_t* scratch, uint8_t scratch_cap, main_legend_t* out) {
    legend_plan_main(&legend_plan_env, get_local_state()->glyph_size, text, small_x, small_y,
                     scratch, scratch_cap, out);
}

// Slide a hint's origin so its measured ink lands on the panel. The main legend
// gets this inside legend_plan_main(); the Shift preview and the AltGr hint call it
// here, so all three elements share ONE definition of the panel edge.
static inline void clamp_legend(int8_t* x, int8_t* y,
                                int8_t xmin, int8_t xmax, int8_t ymin, int8_t ymax) {
    legend_plan_clamp(&legend_plan_env, x, y, xmin, xmax, ymin, ymax);
}

static inline void draw_main_legend(const main_legend_t* p) {
    kdisp_write_gfx_text(g_all_fonts, g_all_font_count, p->x, p->y, p->text);
}

// ── Intl latin-variation picker: slots and pages ─────────────────────────────
//
// The picker row shows LATIN_PICKER_SLOTS variations at a time (12 on split72, 10
// on split42 — the variant headers) and pages through any beyond that.  What is
// STORED is always the absolute variation index, so the two page sizes cost
// nothing and a pick made on one variant reads back correctly on the other.

// ⚠️ KC_LAT0..KC_LAT9 are contiguous but KC_LAT10/KC_LAT11 are NOT — they were
// appended further down the enum so widening the cog range could not renumber
// KC_DAUTO/KC_IDDQD out from under the dynamic-keymap EEPROM (keycode_helper.h).
// So resolve a picker keycode HERE; never write `keycode - KC_LAT0` or a
// `KC_LAT0 ... KC_LAT11` case range.
static int8_t latin_picker_slot(uint16_t keycode) {
    int8_t slot = -1;
    if (keycode >= KC_LAT0 && keycode <= KC_LAT9) slot = (int8_t)(keycode - KC_LAT0);
    else if (keycode == KC_LAT10)                 slot = 10;
    else if (keycode == KC_LAT11)                 slot = 11;
    // split42 wires only 10 slot keys, so 10/11 are unreachable there — but the
    // shared code is compiled for both, and a slot past the variant's row would
    // index the picker beyond what the keymap can ever show.
    return (slot < LATIN_PICKER_SLOTS) ? slot : -1;
}

// Rows are dense — the cog appends variations left to right and pads the tail with
// NULL — so the first NULL is the end.
static uint8_t latin_variation_count(uint8_t row) {
    uint8_t n = 0;
    while (n < LATIN_EX_VARIATIONS && latin_ex_map[row][n] != NULL) {
        n++;
    }
    return n;
}

static uint8_t latin_page_count(uint8_t row) {
    const uint8_t n = latin_variation_count(row);
    return (n <= LATIN_PICKER_SLOTS)
               ? 1
               : (uint8_t)((n + LATIN_PICKER_SLOTS - 1) / LATIN_PICKER_SLOTS);
}

// --- target slots and the assignment indirection ------------------------------
//
// A "target" is a key that can carry an Intl variation.  Today that is A..Z and
// slot == letter index, but the two are deliberately separate concepts: the slot
// addresses the STORAGE (which key), the letter addresses the TABLE (whose
// variations).  They diverge the moment a key is remapped -- which is the whole
// point, since French wants e, and two other keys, all showing forms of e.
static int8_t latin_target_slot(uint16_t keycode) {
    if(keycode >= KC_A && keycode <= KC_Z) {
        return (int8_t)(keycode - KC_A);
    }
    // KC_MINUS .. KC_SLASH is a contiguous run of the twelve printable punctuation
    // keycodes, so no table. Whether a given board can actually REACH one is the
    // keymap's business: a position masked with KC_NO on _ADDLANG1 (split42's ')
    // or absent entirely (split42 has no [ ] `) simply never produces the keycode.
    if(keycode >= KC_MINUS && keycode <= KC_SLASH) {
        return (int8_t)(LATIN_LETTER_TARGETS + (keycode - KC_MINUS));
    }
    return -1;
}

// The base letter a target key hosts, or -1 for none. A letter defaults to itself;
// a punctuation key defaults to nothing, so it keeps typing its own symbol until
// it is deliberately mapped.
static int8_t latin_slot_letter(int8_t slot) {
    if(slot < 0) {
        return -1;
    }
    return latin_assign_get(get_global_latin_table()->assign, (uint8_t)slot);
}

// True when this key currently HOSTS a variation row.  Every letter does by
// default; a punctuation key only once it has deliberately been mapped, so an
// unmapped one keeps typing and drawing its own symbol on the Intl layer.  This
// is the gate the picker and the emit path use -- NOT `keycode >= KC_A`, which
// was the same test only while the letters were the only targets.
static bool latin_has_row(uint16_t keycode) {
    return latin_slot_letter(latin_target_slot(keycode)) >= 0;
}

// The row the picker is currently showing, for the key last touched.  Both the
// render and the press path must agree on this, so it lives in one place.
// ⚠️ Precondition: latin_has_row(last_latin_keycode).  The clamp is only so a
// stray call can never index latin_ex_map out of bounds.
static uint8_t latin_picker_row(uint16_t last_latin_keycode, bool upper_case) {
    const int8_t letter = latin_slot_letter(latin_target_slot(last_latin_keycode));
    return (uint8_t)((upper_case ? 0 : 26) + (letter < 0 ? 0 : letter));
}

// Absolute variation index for a picker slot on the currently-shown page, or -1
// when that slot is past the end of the row.
static int8_t latin_picker_index(uint8_t row, uint8_t page, int8_t slot) {
    if (slot < 0) {
        return -1;
    }
    // ⚠️ Clamp, don't rely on the reset: the CASE selects a different row with its
    // own variation count and it can change while the picker is open (Shift/Caps),
    // which latin_picker_reset_page() does NOT cover — it only fires on a letter
    // change.  Shifting from a two-page row onto a one-page one then left the page
    // out of range, and every exit was blocked at once: every slot returned -1 (a
    // blank picker row), latin_page_count() said 1 so the arrows were blanked AND
    // the KC_LAT_PAGE_* handler's `pages > 1` guard did nothing.  Reachable today on
    // split42 (10 slots): lowercase y has 11 variations, uppercase Y has 10.
    // Clamping here rather than resetting on the case change keeps this the single
    // place the render path and the press path agree on — both go through it.
    const uint8_t pages = latin_page_count(row);
    if (page >= pages) {
        page = (uint8_t)(pages - 1);
    }
    const uint16_t idx = (uint16_t)page * LATIN_PICKER_SLOTS + (uint16_t)slot;
    return (idx < latin_variation_count(row)) ? (int8_t)idx : -1;
}

// Leave remap mode without changing anything.
static void latin_remap_cancel(void) {
    poly_layer_t* ll = access_local_layer();
    ll->remap_mode   = LATIN_REMAP_OFF;
    ll->remap_target = 0;
    request_disp_refresh();
}

// Point `slot` at `letter`'s variation row, or back at its own letter when the two
// are the same — so re-picking a key's own letter is the per-key reset, with no
// separate gesture to learn.
static void latin_remap_apply(uint8_t slot, uint8_t letter) {
    // ⚠️ The two bounds are deliberately DIFFERENT. Any target can be remapped, but
    // only a LETTER can be the source — a punctuation key hosts a letter's row,
    // never the reverse. The one exception is slot == letter, the "point this key
    // back at itself" clear, which is the only per-key way to unmap a punctuation
    // key (it has no own letter to re-pick).
    if(slot >= LATIN_TARGETS) {
        return;
    }
    if(letter >= LATIN_LETTER_TARGETS && letter != slot) {
        return;
    }
    latin_sync_t* table = access_global_latin_table();
    latin_assign_set(table->assign, slot, (slot == letter) ? LATIN_ASSIGN_NONE : letter);
    // The key now indexes a different row, so its stored pick is meaningless there
    // (and may be past the end of a shorter row). Start it at the first variation
    // rather than leaving a stale index for latin_variation() to fall back from.
    latin_pick_set(table->ex, latin_pick_field((int8_t)slot, true),  0);
    latin_pick_set(table->ex, latin_pick_field((int8_t)slot, false), 0);
    send_to_bridge(USER_SYNC_LATIN_EX_DATA, (void*)table, sizeof(*table), 10);
    mark_latin_dirty();
}

// Clear every assignment. Reached by tapping the remap key on the Intl layer
// WITHOUT opening the mode — see process_record_user.
//
// ⚠️ Clears the picks of the REMAPPED slots only, not the whole ex[] array. A key
// that was never remapped still hosts its own letter, so its pick is still valid
// and is the user's own choice — wiping it would make "clear the assignments" also
// silently discard every accent anyone had ever chosen (caught in review, #206).
// A remapped slot's pick, by contrast, indexes the row it is losing and may be past
// the end of its own, so that one must go back to 0.
static void latin_remap_reset_all(void) {
    latin_sync_t* table = access_global_latin_table();
    for(uint8_t slot = 0; slot < LATIN_TARGETS; slot++) {
        // The RAW field, not latin_assign_get(): the getter substitutes the key's
        // own letter for an unassigned slot, which is exactly the case to skip.
        if(latin_bits_get(table->assign, LATIN_ASSIGN_BYTES, slot) < LATIN_LETTER_TARGETS) {
            latin_pick_set(table->ex, latin_pick_field(slot, true),  0);
            latin_pick_set(table->ex, latin_pick_field(slot, false), 0);
        }
    }
    memset(table->assign, LATIN_ASSIGN_FILL, sizeof(table->assign));
    send_to_bridge(USER_SYNC_LATIN_EX_DATA, (void*)table, sizeof(*table), 10);
    mark_latin_dirty();
    request_disp_refresh();
}

// Back to page 0.  Anything that makes the current page meaningless calls this:
// picking a variation, changing the letter (a one-page letter would otherwise show
// a whole row of blanks while page 1 was still selected), and leaving the layer.
static void latin_picker_reset_page(void) {
    access_local_layer()->picker_page = 0;
}

// Resolve the latin variation a letter key should display/emit on the _ADDLANG1
// layer, honouring the persisted per-case pick in latin_sync_t.ex (high nibble =
// uppercase, low nibble = lowercase).
//
// Returns NULL when the letter has no variations at all.  A pick that lands on an
// empty slot falls back to variation 0 instead of handing back the NULL cell: the
// nibble can hold 0..15 while only LATIN_EX_VARIATIONS exist, and nothing
// re-validates a stored pick against a row that later shrank.  Feeding that NULL
// to kdisp_write_gfx_text() / register_unicode() reads address 0 — mapped boot ROM
// on RP2040, so no fault, just a garbage codepoint and junk on the keycap.  The
// picker now refuses to store an empty slot (see process_record_user), but a byte
// written before that guard — or a stale/garbage EEPROM — still arrives here.
static const uint32_t* latin_variation(uint16_t keycode, bool upper_case) {
    const int8_t slot = latin_target_slot(keycode);
    if (slot < 0) {
        return NULL;
    }
    // ⚠️ Two DIFFERENT indices. The row comes from the letter this key HOSTS (its
    // own, unless reassigned); the pick comes from the KEY's own storage slot. They
    // must not be collapsed back into one: two keys assigned to the same letter
    // share a row but need independent picks -- that is the entire feature.
    const int8_t letter = latin_slot_letter(slot);
    if (letter < 0) {
        return NULL;                        // punctuation key, not mapped to anything
    }
    const uint8_t row = (uint8_t)((upper_case ? 0 : 26) + letter);
    if (latin_ex_map[row][0] == NULL) {
        return NULL;                        // this letter has no variations
    }
    const uint8_t   idx    = latin_pick_get(get_global_latin_table()->ex, latin_pick_field(slot, upper_case));
    const uint32_t* chosen = (idx < LATIN_EX_VARIATIONS) ? latin_ex_map[row][idx] : NULL;
    // Fall back when the pick names a slot this build does not have (a stale
    // EEPROM), and ALSO when it names a real variation whose GLYPH is missing --
    // the table is static but the font set is not, so a variation drawn from a
    // font-pack bundle disappears if that bundle is absent or older. Without this
    // the keycap renders blank and the key emits a character with no legend.
    if (chosen != NULL && kdisp_gfx_glyph(g_all_fonts, g_all_font_count, chosen[0]) == NULL) {
        chosen = NULL;
    }
    return (chosen != NULL) ? chosen : latin_ex_map[row][0];
}

// The AltGr hint is drawn half-scale unless its ink is this tall or shorter. See
// the comment at the use site for why 7 is a measured gap and not a taste call.
#define ALTGR_HALF_MIN_INK_H 7
// Longest AltGr cell the halving scratch can hold. The longest in the LUT today is
// 9 codepoints (ps-AF KC_Z, five cursor nudges then the glyph); a longer one simply
// stays full size rather than being truncated.
#define ALTGR_HALF_MAX_LEN   12

bool render_key(uint16_t keycode, led_t state, uint8_t mods) {
    // ⚠️ A mod-tap's LEGEND is its tap keycode's legend. to_static_text() unwraps
    // this one function away, and update_displays() consults render_key() exactly
    // when to_static_text() returned NULL -- which is every letter, since the
    // language translation lives down here. So without the same unwrap a
    // RSFT_T(KC_A) fell through every branch below (is_letter is false for 0x3204,
    // and translate_keycode() has no row for it) and the keycap drew NO letter at
    // all: only the mod-tap hint badge, floating in an empty cell (field, 2026-08-18).
    // The two legend producers have to agree; keep the unwrap in both.
    if(IS_QK_MOD_TAP(keycode)) {
        keycode = QK_MOD_TAP_GET_TAP_KEYCODE(keycode);
    }

    const poly_layer_t* local_layer = get_local_layer();

    const bool shift = ((local_layer->mods & MOD_MASK_SHIFT) != 0);
    const bool add_lang = get_highest_layer(local_layer->layer)==_ADDLANG1;
    const bool picker_mod = ((local_layer->mods & LATIN_PICKER_MOD) != 0);
    const bool is_letter = keycode>=KC_A && keycode<=KC_Z;

    // --- letter-remap mode: the board becomes a "which key?" / "which letter?"
    // prompt.  Everything outside the set being chosen from blanks out, so the only
    // thing to look at is that set.
    if(add_lang && local_layer->remap_mode != LATIN_REMAP_OFF) {
        const int8_t  slot   = latin_target_slot(keycode);
        const bool    picked = (local_layer->remap_mode == LATIN_REMAP_PICKLTR) &&
                               (slot >= 0) && ((uint8_t)slot == local_layer->remap_target);
        // The two steps offer DIFFERENT sets. PICKKEY picks the key to change, so
        // every TARGET is live — the letters and the twelve punctuation keys alike.
        // PICKLTR picks the letter that key should host, and only a letter can be a
        // source, so the punctuation keys go dark again — EXCEPT the one that was
        // just picked, which has to stay visible (inverted) or the board stops
        // showing what is being changed.
        const bool selectable = (local_layer->remap_mode == LATIN_REMAP_PICKKEY) ? (slot >= 0)
                                                                                 : (is_letter || picked);
        if(!selectable) {
            return false;                   // blank — nothing else is pressable now
        }
        // ⚠️ Render the inversion, do NOT call kdisp_invert(): matrix_scan_kb toggles
        // that on every press/release independently of process_record, so a latched
        // indicator driven through it is undone by the next keypress.  Fill the
        // ground and erase the glyph out of it instead — and with cy_radius 0, or
        // the courtyard clear eats a dark halo and the key reads as outlined.
        if(picked) {
            kdisp_set_buffer(0xFF);
            kdisp_set_gfx_erase(true);
        }
        // In PICKKEY every target still shows what it currently hosts (so you can
        // see what you are about to change); in PICKLTR the letters are the SOURCE
        // menu, so each shows its own plain letter.
        bool drawn;
        if(local_layer->remap_mode == LATIN_REMAP_PICKKEY) {
            const uint32_t* variation = latin_variation(keycode, shift || state.caps_lock);
            if(variation == NULL) {
                // A punctuation key that has not been mapped yet hosts nothing —
                // draw its own symbol, so the set you are choosing from reads as the
                // actual keyboard rather than a row of holes.
                variation = translate_keycode(get_local_state()->lang, keycode,
                                              shift, state.caps_lock);
            }
            drawn = (variation != NULL);
            if(drawn) {
                kdisp_write_gfx_text_cy(g_all_fonts, g_all_font_count, BUFFER_X, 23, variation, 0);
            }
        } else if(is_letter) {
            const uint32_t letter[2] = { (uint32_t)('a' + (keycode - KC_A)), 0 };
            kdisp_write_gfx_text_cy(g_all_fonts, g_all_font_count, BUFFER_X, 23, letter, 0);
            drawn = true;
        } else {
            // Only reachable for a PICKED punctuation target (see `selectable`), so
            // it must draw its own symbol -- 'a' + (keycode - KC_A) would be a
            // nonsense codepoint here.
            const uint32_t* sym = translate_keycode(get_local_state()->lang, keycode,
                                                    shift, state.caps_lock);
            drawn = (sym != NULL);
            if(drawn) {
                kdisp_write_gfx_text_cy(g_all_fonts, g_all_font_count, BUFFER_X, 23, sym, 0);
            }
        }
        if(picked) {
            kdisp_set_gfx_erase(false);     // static flag — leaving it on blanks every later key
            drawn = true;                   // the filled ground IS the render
        }
        return drawn;
    }

    // ⚠️ Gated on "hosts a row", not on is_letter: a punctuation key that has been
    // remapped shows its host letter's variation here, and an unmapped one must
    // fall THROUGH to the normal translate_keycode path below so it keeps drawing
    // (and typing) its own symbol.
    if(add_lang && latin_has_row(keycode)) {
        //display the previously selected latin variation of the letter
        const uint32_t* variation = latin_variation(keycode, shift || state.caps_lock);
        if(variation!=NULL) {
            // The variation IS this key's main legend on this layer (there is no
            // preview to share the cell with), so it follows the legend size too.
            uint32_t      scratch[GLYPH_SIZE_MAX_LEN + 1];
            main_legend_t plan;
            plan_main_legend(variation, BUFFER_X, 23, scratch, (uint8_t)(GLYPH_SIZE_MAX_LEN + 1), &plan);
            draw_main_legend(&plan);
            return true;
        }
        return false;
    }

    //variation selection on the picker row
    uint16_t local_last_latin_keycode = get_local_last_latin_keycode();
    const bool picker_open = add_lang && picker_mod && latin_has_row(local_last_latin_keycode);
    const int8_t picker_slot = latin_picker_slot(keycode);
    if(picker_slot >= 0) {
        if(picker_open) {
            //show the alternatives on the current page for the selected latin letter
            const uint8_t row = latin_picker_row(local_last_latin_keycode, shift || state.caps_lock);
            const int8_t  idx = latin_picker_index(row, local_layer->picker_page, picker_slot);
            if(idx >= 0) {
                kdisp_write_gfx_text(g_all_fonts, g_all_font_count, BUFFER_X, 23, latin_ex_map[row][idx]);
                return true;
            }
        }
        return false;
    }
    if(keycode==KC_LAT_PAGE_PREV || keycode==KC_LAT_PAGE_NEXT) {
        // Blank unless this letter actually pages — same rule (and glyphs) as the
        // language layer's arrows, so a row that fits on one page shows nothing
        // rather than a control that does nothing.
        if(picker_open &&
           latin_page_count(latin_picker_row(local_last_latin_keycode, shift || state.caps_lock)) > 1) {
            kdisp_write_gfx_text(g_all_fonts, g_all_font_count, BUFFER_X, 23,
                                 keycode==KC_LAT_PAGE_PREV ? (const uint32_t*)(U"  " ICON_LEFT)
                                                           : (const uint32_t*)(U"  " ICON_RIGHT));
            return true;
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

    // Macro keys draw their own cell: the index, and the label along the bottom edge.
    // Reached because to_static_text() has no case for QK_MACRO_*, which is exactly the
    // seam update_displays() uses -- a key WITH a legend never gets here.
    if (keycode >= QK_MACRO && keycode <= QK_MACRO_MAX) {
        render_macro_key((uint8_t)(keycode - QK_MACRO));
        return true;
    }

    if (mods & MOD_RALT) {
        const uint32_t* letter = translate_keycode_only_altgr(local_state->lang, keycode);
        if (letter != NULL) {
            // Draw it WHERE THE RESTING VIEW DRAWS THE HINT: this key's own category
            // and its VAR_ALTGR offsets, so holding AltGr enlarges the glyph in place
            // instead of teleporting it across the keycap. Measured over the 1719
            // AltGr cells, 1148 (67%) land exactly on the hint's position and the
            // rest are pulled back a median 5 px by the clamp below — which is the
            // only reason this is safe to do at all (worst: the ar-* KC_F letters at
            // 34-38 px, where a wide script glyph cannot fit at x=+55).
            //
            // ⚠️ This replaced a defensive MIXTURE that predates the clamp: the
            // NUM-or-SYM category even on a LETTER key, H from VAR_SMALL, and V as
            // PK_MIN(VAR_SMALL, VAR_ALTGR). That kept the glyph near the base legend
            // where it could not fall off, at the cost of using offsets belonging to
            // a different category and a different variation than the glyph drawn.
            int8_t v_set;
            int8_t h_set;
            int8_t held_v_set;   // {<cat>.heldvoffset} — the delta from the hint position
            int8_t held_h_set;   // {<cat>.heldhoffset}
            if(is_letter) {
                v_set      = SETTING_LETTER_VOFFSET;
                h_set      = SETTING_LETTER_HOFFSET;
                held_v_set = SETTING_LETTER_HELDVOFFSET;
                held_h_set = SETTING_LETTER_HELDHOFFSET;
            } else {
                const bool is_num = keycode>=KC_1 && keycode<=KC_0; // yes the first is 1 and the last is 0
                v_set      = is_num ? SETTING_NUM_VOFFSET      : SETTING_SYM_VOFFSET;
                h_set      = is_num ? SETTING_NUM_HOFFSET      : SETTING_SYM_HOFFSET;
                held_v_set = is_num ? SETTING_NUM_HELDVOFFSET  : SETTING_SYM_HELDVOFFSET;
                held_h_set = is_num ? SETTING_NUM_HELDHOFFSET  : SETTING_SYM_HELDHOFFSET;
            }
            // HIDE on either axis falls through to the resting legend, as before —
            // hiding the hint says "do not show the AltGr" for this layout. Measured:
            // no layout hides its AltGr offsets today, and H/V never disagree, so
            // this is a semantic tidy-up rather than a behaviour change.
            int8_t v_off = get_setting(v_set, local_state->lang, VAR_ALTGR);
            int8_t h_off = get_setting(h_set, local_state->lang, VAR_ALTGR);
            if(v_off!=HIDE_KEY && h_off!=HIDE_KEY) {
                // A bare combining mark (the nukta "+nukta" AltGr hint) is invisible on
                // its own when AltGr is actually held — compose it onto the base
                // consonant (क + ़ = क़) so the held view shows the real output. The
                // unshifted preview still draws the lone dot via the cell's own controls.
                uint32_t composed[10];
                if (altgr_is_bare_combining(letter)) {
                    const uint32_t* base = translate_keycode(local_state->lang, keycode, false, false);
                    if (base != NULL) {
                        uint8_t ci = 0;
                        for (const uint32_t* p = base;   *p && ci < 8; ++p) composed[ci++] = *p;
                        for (const uint32_t* p = letter; *p && ci < 9; ++p) if (*p >= 0x20) composed[ci++] = *p;
                        composed[ci] = 0;
                        letter = composed;
                    }
                }
                // Clamp onto the panel, exactly as the resting view's three elements
                // do. This branch was the ONE element still drawn at a raw origin, and
                // it is the same defect the four-edge clamp closed for the small base
                // legend: the offsets here are the NUM/SYM ones, tuned against those
                // categories' own glyphs, while the glyph drawn is the AltGr cell's —
                // so a layout whose AltGr glyphs are wider or taller silently pushed
                // ink off an edge with nothing to report it. Measured over all 160
                // layouts: 191 keys / 1528 px across 69 layouts, down to the single
                // he-IL KC_BACKSLASH nikud that is 43 px tall in a 40 px panel.
                // {<cat>.heldhoffset} / {<cat>.heldvoffset}: a per-layout, per-category
                // DELTA from the hint position, defaulting to 0 so nothing moves until a
                // layout is tuned. It exists because the hint's own offsets place a glyph
                // that shares the keycap with the base legend and the Shift preview, while
                // the held view has the panel to itself — so the best spot is not
                // necessarily the same one, and that is a per-layout judgement.
                //
                // ⚠️ HIDE_KEY is treated as 0, NOT as -128. It is the tuner's "hide this
                // variation" sentinel and is meaningless for a delta; taken literally it
                // would fling the glyph a hundred pixels off the panel. Hiding the held
                // view is what the AltGr offsets above already do.
                int8_t held_h = get_setting(held_h_set, local_state->lang, VAR_ALTGR);
                int8_t held_v = get_setting(held_v_set, local_state->lang, VAR_ALTGR);
                if (held_h == HIDE_KEY) held_h = 0;
                if (held_v == HIDE_KEY) held_v = 0;

                int8_t axmin, axmax, aymin, aymax;
                int8_t hx = (int8_t)(28+h_off+held_h), hy = (int8_t)(23+v_off+held_v);
                kdisp_gfx_text_bbox(g_all_fonts, g_all_font_count, letter,
                                    &axmin, &axmax, &aymin, &aymax);
                clamp_legend(&hx, &hy, axmin, axmax, aymin, aymax);
                kdisp_write_gfx_text(g_all_fonts, g_all_font_count, hx, hy, letter);
                return true;
            }
        }
    }

    //translate to current language
    const uint32_t* letter = translate_keycode(local_state->lang, keycode, shift, state.caps_lock);
    if (letter != NULL) {
        int8_t v_set;
        int8_t h_set;
        int8_t half_set;   // {<cat>.altgrhalf} — the per-layout half-size AltGr opt-in
        if(is_letter) {
            v_set    = SETTING_LETTER_VOFFSET;
            h_set    = SETTING_LETTER_HOFFSET;
            half_set = SETTING_LETTER_ALTGRHALF;
        } else {
            const bool is_num = keycode>=KC_1 && keycode<=KC_0; // yes the first is 1 and the last is 0
            if(is_num){
                v_set    = SETTING_NUM_VOFFSET;
                h_set    = SETTING_NUM_HOFFSET;
                half_set = SETTING_NUM_ALTGRHALF;
            } else {
                v_set    = SETTING_SYM_VOFFSET;
                h_set    = SETTING_SYM_HOFFSET;
                half_set = SETTING_SYM_ALTGRHALF;
            }
        }
        int8_t v_small = get_setting(v_set, local_state->lang, VAR_SMALL);
        int8_t h_small = get_setting(h_set, local_state->lang, VAR_SMALL);
        int8_t base_x = 28+h_small;
        int8_t base_v = v_small;

        // Plan the base legend first: at a bigger legend size it is a different,
        // relocated glyph at a different origin, and everything below lays out
        // around its ink. At GLYPH_SIZE_S the plan is exactly the old placement.
        uint32_t      base_scratch[GLYPH_SIZE_MAX_LEN + 1];
        main_legend_t base_plan;
        plan_main_legend(letter, base_x, (int8_t)(23+base_v), base_scratch,
                         (uint8_t)(GLYPH_SIZE_MAX_LEN + 1), &base_plan);

        // Resolve the shift preview BEFORE drawing, so a wide base + wide preview
        // (e.g. the Arabic SAD/DAD key — both ~39 px) can be laid out as a pair:
        // the preview is placed clear of the base and clamped on screen; if it then
        // still has to overlap (two such glyphs cannot both fit a 72 px window) the
        // flat base is lifted and the preview dropped so the two read diagonally
        // instead of as one connected glyph.  Only this unshifted preview view is
        // affected — when shift is held there is no preview and the active glyph
        // keeps the normal VAR_SMALL baseline, so tall letters / high marks never
        // clip.  All of this is generic and glyph-width driven (no per-language code).
        //
        // ⚠️ The preview stays at the SMALL size by design: a keycap has room for one
        // big thing, and the whole point of the size setting is the main legend.
        const uint32_t* shift_letter = NULL;
        int8_t preview_x = 0, preview_y = 0;
        int8_t pmin = 0, pmax = 0, pymin = 0, pymax = 0;
        if(!shift && !state.caps_lock) {
            int8_t v_pv = get_setting(v_set, local_state->lang, VAR_SHIFT);
            int8_t h_pv = get_setting(h_set, local_state->lang, VAR_SHIFT);
            if(v_pv!=HIDE_KEY && h_pv!=HIDE_KEY) {
                shift_letter = translate_keycode_only_shift(local_state->lang, keycode);
                if (shift_letter != NULL) {
                    kdisp_gfx_text_bbox(g_all_fonts, g_all_font_count, shift_letter,
                                        &pmin, &pmax, &pymin, &pymax);
                    preview_x = 28+h_pv;
                    if (preview_x + pmin < base_plan.ink_max + 2)         // keep clear of the base
                        preview_x = base_plan.ink_max + 2 - pmin;
                    preview_y = (int8_t)(23 + v_pv);
                    clamp_legend(&preview_x, &preview_y, pmin, pmax, pymin, pymax);
                    if (preview_x + pmin <= base_plan.ink_max) {          // forced to overlap -> stagger
                        // Both moves are re-clamped: the panel edge wins over the
                        // stagger, which is a readability nicety and not worth a
                        // clipped glyph. (Before the clamp covered the small base
                        // too, this lift could push a tall flat legend off the top.)
                        if (!base_plan.big) {
                            base_plan.y -= 6;                             // lift the flat base
                            clamp_legend(&base_plan.x, &base_plan.y, base_plan.box_xmin,
                                         base_plan.box_xmax, base_plan.box_ymin, base_plan.box_ymax);
                            base_plan.ink_min = (int8_t)(base_plan.x + base_plan.box_xmin);
                            base_plan.ink_max = (int8_t)(base_plan.x + base_plan.box_xmax);
                        }
                        preview_y += 4;                                   // drop the preview
                        clamp_legend(&preview_x, &preview_y, pmin, pmax, pymin, pymax);
                    }
                }
            }
        }

        // Resolve the AltGr hint BEFORE anything is drawn, for the same reason the
        // shift preview above is resolved first: the two hints have to be laid out
        // as a pair (see the separation block below).
        const uint32_t* alt_letter = NULL;
        int8_t alt_x = 0, alt_y = 0, aymin = 0, aymax = 0;
        uint32_t alt_scratch[ALTGR_HALF_MAX_LEN + 2];   // HINT_SMALL + the cell + NUL
        letter = translate_keycode_only_altgr(local_state->lang, keycode);
        if (letter != NULL) {
            int8_t v_off = get_setting(v_set, local_state->lang, VAR_ALTGR);
            int8_t h_off = get_setting(h_set, local_state->lang, VAR_ALTGR);
            if(v_off!=HIDE_KEY && h_off!=HIDE_KEY) {
                int8_t amin, amax;
                kdisp_gfx_text_bbox(g_all_fonts, g_all_font_count, letter, &amin, &amax, &aymin, &aymax);

                // The AltGr glyph is a HINT — what this key would type under a
                // modifier nobody is holding — so on a script whose letters fill the
                // keycap it is drawn at HALF size: it reads as subordinate to the
                // base legend, and a full-size script glyph is most of what made the
                // two hints fight for the right-hand side.
                //
                // ⚠️ WHICH layouts is DATA, not a size test, and the measurement is
                // why. The intuition is "Arabic and Indic have very large glyphs",
                // but AltGr ink HEIGHT does not separate them at all — median 20 px
                // on Arabic letters against 21 px on Latin ones. What is actually
                // different is that on those layouts the base and the Shift hint are
                // wide too, so the row reads crowded. That is a per-LAYOUT judgement
                // no glyph measurement can make, so it lives where layout decisions
                // live: `{letter|num|sym.altgrhalf}` in lang_lut.xlsx, one cell per
                // language PER CATEGORY — the same three-way split the H/V offsets
                // already use, so a layout can halve its letters while its digit and
                // symbol rows keep full-size hints. Flip one by editing that cell,
                // not by tuning a threshold here. Only `{letter.…}` is set today.
                //
                // ⚠️ The size test that REMAINS is only the mark guard: halving a
                // glyph that is already tiny destroys it — a Hebrew nikud is 2×3 px
                // and comes out a dot. That threshold IS measured: over the 318
                // distinct AltGr cells the ink-height histogram has an EMPTY BIN at
                // 8 px, marks below it (44 cells — nikud, diaeresis, middle dot,
                // hyphen) and letterforms from 9 px up (274). It matters most on the
                // Indic layouts, whose letter AltGr hints are mostly bare combining
                // marks — median 4 px — so most of them stay full size even here.
                if (get_setting(half_set, local_state->lang, VAR_ALTGR) != 0
                    && aymax - aymin + 1 > ALTGR_HALF_MIN_INK_H && letter[0] != U'\x10') {
                    uint8_t n = 0;
                    while (n < ALTGR_HALF_MAX_LEN && letter[n] != 0) n++;
                    if (letter[n] == 0) {        // fits the scratch; else stay full size
                        alt_scratch[0] = U'\x10';    // HINT_SMALL
                        for (uint8_t i = 0; i < n; ++i) alt_scratch[i + 1] = letter[i];
                        alt_scratch[n + 1] = 0;
                        letter = alt_scratch;
                        kdisp_gfx_text_bbox(g_all_fonts, g_all_font_count, letter,
                                            &amin, &amax, &aymin, &aymax);
                    }
                }
                alt_x = 28+h_off;
                // At the small size this mark is kept off the legend by its VERTICAL
                // offset — it sits below the base glyph. A big legend fills that
                // height, so there the only separation left is horizontal: push it
                // clear of the base's ink, exactly as the shift preview does.
                if (base_plan.big && alt_x + amin < base_plan.ink_max + 2)
                    alt_x = (int8_t)(base_plan.ink_max + 2 - amin);
                alt_y = (int8_t)(23 + v_off);
                clamp_legend(&alt_x, &alt_y, amin, amax, aymin, aymax);
                alt_letter = letter;

                // Keep the two hints off EACH OTHER. Both sit right of the base —
                // shift upper, AltGr lower — and it is their VERTICAL offsets that
                // hold them apart. True for a narrow Latin pair, false for a tall
                // script: on every ar-* KC_F the shift tick lands inside the AltGr's
                // 29 px box, and bn-BD KC_D shares 57 px. A per-language offset
                // cannot fix that — the room left over is decided by the WIDTH of
                // this key's three glyphs, so one number per language would have to
                // satisfy the worst key and would crush the rest into the base.
                //
                // The base is bottom-left and narrow on exactly these keys, so the
                // free space is between it and the (right-clamped) AltGr: pull the
                // shift LEFT into that gap, never past the base's own 2 px margin,
                // and never to the right — which could only walk it into the clamp.
                // Where three wide glyphs genuinely do not fit on 72 px the pull
                // still shrinks the overlap rather than removing it.
                if (shift_letter != NULL) {
                    const int8_t sy0 = (int8_t)(preview_y + pymin);
                    const int8_t sy1 = (int8_t)(preview_y + pymax);
                    const int8_t ay0 = (int8_t)(alt_y + aymin);
                    const int8_t ay1 = (int8_t)(alt_y + aymax);
                    if (preview_x + pmin <= alt_x + amax && alt_x + amin <= preview_x + pmax
                        && sy0 <= ay1 && ay0 <= sy1) {
                        int16_t want  = (int16_t)(alt_x + amin - 2 - pmax);
                        const int16_t floor_x = (int16_t)(base_plan.ink_max + 2 - pmin);
                        if (want < floor_x) want = floor_x;
                        if (want < preview_x) preview_x = (int8_t)want;
                        // The pull only ever moves LEFT, so only the west edge can be
                        // violated — but re-clamp through the shared helper rather
                        // than open-coding that one test here.
                        clamp_legend(&preview_x, &preview_y, pmin, pmax, pymin, pymax);
                    }
                }
            }
        }

        draw_main_legend(&base_plan);
        if (shift_letter != NULL)
            kdisp_write_gfx_text(g_all_fonts, g_all_font_count, preview_x, preview_y, shift_letter);
        if (alt_letter != NULL)
            kdisp_write_gfx_text(g_all_fonts, g_all_font_count, alt_x, alt_y, alt_letter);
        return true;
    }
    return false;
}

// Returns builtin icon/symbol overlay text for keycode based on current modifiers and mod-tap states.
// The OS-aware shortcut-hint table itself lives in hints/os_hints.c as a PURE
// function. This wrapper is the only thing that knows where the two inputs come
// from, so the table can be exercised directly in a unit test.
const uint32_t* keycode_to_disp_overlay(uint16_t keycode) {
    return os_hint_for_keycode(keycode, get_local_layer()->mods, get_local_state()->active_os);
}

// Which of the 90 overlay keycode-slots are currently on screen, rebuilt as a side
// effect of every full update_displays() pass (set in copy_overlay_to_buffer below,
// the display's own per-key lookup). A slot is "displayed" iff some physical key on
// this half currently resolves to that keycode under the active layer stack — so it
// captures LAYER visibility (an F-key overlay's slot is absent while the Fn layer is
// inactive). The overlay-completion visibility gate (fill_overlay.c) ANDs this with the
// modifier-variant check to skip re-renders for overlays that aren't on screen. The set
// only changes on a layer/mods change, which forces its own (ungated) refresh, so an
// overlay burst — which never changes the layer — safely reads the last full render's set.
static uint8_t s_displayed_slots[(90 + 7) / 8];

static void clear_displayed_slots(void) {
    memset(s_displayed_slots, 0, sizeof(s_displayed_slots));
}

bool overlay_slot_displayed(uint16_t base_slot) {
    if (base_slot >= 90) {
        return false;
    }
    return (s_displayed_slots[base_slot >> 3] & (uint8_t)(1u << (base_slot & 7))) != 0;
}

bool copy_overlay_to_buffer(uint16_t keycode, uint8_t mods) {
    if(keycode>KC_RGUI || (keycode>KC_NUM_LOCK && keycode<KC_NUBS) || (keycode>KC_APP && keycode<KC_LEFT_CTRL)) {
        return false;
    }
    uint16_t idx = (keycode>KC_APP) ? (keycode - KC_LEFT_CTRL + 82) : (keycode>KC_NUM_LOCK ? keycode - KC_NUBS + 80 : keycode - KC_A);
    if(idx>=90) {
        return false;
    }
    // Record that this keycode-slot is on screen (LAYER visibility for the render gate).
    s_displayed_slots[idx >> 3] |= (uint8_t)(1u << (idx & 7));
    idx = adjust_overlay_idx_to_mod(idx, mods);
    // display_has_overlay_bits[] is from-indexed (see set_packed_overlay_mapping): check it
    // here on the display position, before resolving to the pool slot.
    if(!display_has_overlay(idx)) {
        return false;
    }
    idx = get_display_pool_slot(idx);

    // Overlay images are ROW-MAJOR MSB-first (host: np.packbits over the 40x72 mask),
    // which is what kdisp_draw_bitmap reads — so the courtyard must use the row-major
    // reader. The column-native variant (for font glyphs) reads the same 360 bytes
    // without complaint and dilates a scrambled mask, which punched a big garbage
    // rectangle through the legend underneath (field, 2026-08-01).
    kdisp_clear_rowmajor_courtyard(28, 0, get_overlay(idx), 72, 40, KDISP_CY_DEFAULT);
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
static const GFXfont* const lang_label_fonts[] = { &NotoSans_Regular_Nano_10px7b };
// Mid (19px) utility font for the no-pack fallback code — between Small and Base,
// so a full "ll-CC" fits on one line (~52px) yet stays readable. Reuse this
// `mid_fonts` array for any misc utility-key text that wants a middle size.
static const GFXfont* const mid_fonts[]        = { &NotoSans_Regular_Mid_19px7b };

// --- FW-2 unsigned-image confirmation prompt -------------------------------
//
// Which key on EACH half carries the prompt. The same LOCAL matrix position on
// both halves, so the two keycaps sit symmetrically: the home-row index finger,
// which is findable by touch without hunting across 36 keycaps. The keycap grid
// is not rectangular and the right half folds its display column, but this is a
// matrix position — update_displays' loop indexes the matrix directly, so no
// fold applies. Left half = ACCEPT, right half = REJECT (fixed by side, not by
// which half happens to be master, so the prompt never moves between flashes).
#if defined(KEYBOARD_polykybd_split42)
#  define FW_CONFIRM_ROW 1      // middle letter row (a s d f g)
#else
#  define FW_CONFIRM_ROW 2      // split72 home row (Fn a s d f g ')
#endif
#define FW_CONFIRM_COL 3

// Compose the prompt keycap into the scratch buffer: a big 2x letter above a tiny
// caption. Everything is measured from the font metrics rather than hardcoded —
// "REJECT" descends 2px below the baseline (the J) where "ACCEPT" does not, so a
// fixed bottom baseline would clip one of them.
static void render_fw_confirm_key(bool accept) {
    const uint32_t  letter    = accept ? (uint32_t)'A' : (uint32_t)'R';
    const uint32_t *caption   = accept ? U"ACCEPT" : U"REJECT";

    kdisp_set_buffer(0x00);

    int8_t cxmin, cxmax, cymin, cymax;
    kdisp_gfx_text_bbox(lang_label_fonts, 1, caption, &cxmin, &cxmax, &cymin, &cymax);
    // Pin the caption's lowest lit pixel to the last screen row.
    const int8_t cap_base = (int8_t)(SCREEN_HEIGHT - 1 - cymax);
    kdisp_write_gfx_text(lang_label_fonts, 1,
                         (int8_t)(BUFFER_X + (SCREEN_WIDTH - (cxmax - cxmin + 1)) / 2 - cxmin),
                         cap_base, caption);

    // The big letter, centred in what is left above the caption.
    const GFXfont  *lf = NULL;
    const GFXglyph *lg = kdisp_gfx_glyph_font(mid_fonts, 1, letter, &lf);
    if (lg == NULL) return;
    // _double_at takes the literal top-left of the INK (no baseline align, no
    // xOffset), so the glyph box is centred directly — nothing to compensate for.
    const int16_t lw = (int16_t)pgm_read_byte(&lg->width)  * 2;
    const int16_t lh = (int16_t)pgm_read_byte(&lg->height) * 2;
    const int8_t  free_rows = (int8_t)(cap_base + cymin);   // rows 0 .. caption top - 1
    kdisp_draw_glyph_double_at(mid_fonts, 1,
                               (int8_t)(BUFFER_X + (SCREEN_WIDTH - lw) / 2),
                               (int8_t)((free_rows - lh) / 2),
                               letter);
}

// Compose a macro keycap: the macro's index above, its label along the bottom edge.
//
// Same split as render_lang_flag_key -- identity on top, a tiny _Nano_ caption below --
// because it is the shape this board already uses for "what is this, and which one".
// The INDEX rather than a generic macro glyph: a generic glyph would be identical on
// all sixteen keys, so it says "this is a macro" and nothing else, while the index says
// which one and needs no font pack (the pack glyphs are the one thing a fresh keyboard
// might not have). When a label is set it carries the meaning and the index shrinks
// out of the way; when it is not, the index is all there is, so it takes the cell.
//
// The label is truncated by MEASURED WIDTH, never by character count: in this face a
// 'W' is about three times an 'i', so a fixed cut either clips a wide label off the
// panel or wastes half the band on a narrow one. The host runs the same measurement in
// its editor, so what you type is what the keycap shows.
// Draw `text` centred in the whole cell at the largest face that fits it, biggest
// first. Returns false when even the smallest face overflows, which the caller treats
// as "nothing to draw" rather than clipping.
//
// The ladder is the point: a caption alone has the entire 72x40 to spend, and the
// _Nano_ 10 px face the bottom band uses would waste it. The two big tiers live in the
// `latinbig` PACK bundle, so they are simply absent on a keyboard with no font pack --
// glyph_size_remap() is all-or-nothing and returns false there, and the ladder walks
// past them to the resident faces. That is why the resident 27 px face is on the list
// rather than being the assumed floor.
static bool draw_macro_caption_big(const uint32_t* text) {
    uint32_t scratch[POLY_MACRO_LABEL_LEN + 1];

    for (uint8_t tier = 0; tier < 4; tier++) {
        const GFXfont* const* fonts = NULL;
        uint8_t               count = 0;
        const uint32_t*       run   = text;

        switch (tier) {
            case 0:
            case 1:
                // GLYPH_SIZE_L then _M, relocated into the supplementary PUA planes the
                // latinbig entries are emitted at. Same helper the legend-size feature
                // uses, so a caption cannot end up at a size the legends cannot reach.
                if (!glyph_size_remap((uint8_t)(tier == 0 ? GLYPH_SIZE_L : GLYPH_SIZE_M),
                                      text, scratch,
                                      (uint8_t)(POLY_MACRO_LABEL_LEN + 1))) {
                    continue;
                }
                run   = scratch;
                fonts = g_all_fonts;
                count = g_all_font_count;
                break;
            case 2:
                // The resident latin face (~20 px caps). Always present.
                fonts = g_all_fonts;
                count = g_all_font_count;
                break;
            default:
                fonts = mid_fonts;
                count = 1;
                break;
        }

        int8_t xmin, xmax, ymin, ymax;
        kdisp_gfx_text_bbox(fonts, count, run, &xmin, &xmax, &ymin, &ymax);
        const int16_t w = (int16_t)(xmax - xmin + 1);
        const int16_t h = (int16_t)(ymax - ymin + 1);
        if (w > SCREEN_WIDTH || h > SCREEN_HEIGHT) continue;

        kdisp_write_gfx_text(fonts, count,
                             (int8_t)(BUFFER_X + (SCREEN_WIDTH - w) / 2 - xmin),
                             (int8_t)((SCREEN_HEIGHT - h) / 2 - ymin),
                             run);
        return true;
    }
    return false;
}

// Draws `mark` centred in the `free_rows` a caption left above it, and reports
// whether it landed.
//
// ⚠️ An ICON that overflows at its native size is HALVED rather than dropped, and
// that is the whole point of this function existing. A pack emoji is rendered at
// 40 px while a captioned keycap leaves about 29 rows, so the earlier
// native-size-or-nothing rule drew NOTHING for most icons a user could pick --
// measured over the picker's own set, four in five (field, 2026-08-27: "after
// selecting the icon I cannot see it ... also not on the keyboard"). The failure
// was silent on both ends because the host preview mirrors this placement.
//
// The 2x2-OR downsample keeps thin strokes that plain decimation loses, and half of
// even the tallest pack glyph is ~20 px, which fits any single-line caption.
static bool draw_macro_mark(const GFXfont* const* fonts, uint8_t count,
                            const uint32_t* mark, int8_t free_rows,
                            const GFXglyph* glyph, uint32_t cp) {
    int8_t xmin, xmax, ymin, ymax;
    kdisp_gfx_text_bbox(fonts, count, mark, &xmin, &xmax, &ymin, &ymax);
    const int8_t h = (int8_t)(ymax - ymin + 1);
    if (h < free_rows) {
        kdisp_write_gfx_text(fonts, count,
                             (int8_t)(BUFFER_X + (SCREEN_WIDTH - (xmax - xmin + 1)) / 2 - xmin),
                             (int8_t)((free_rows - h) / 2 - ymin), mark);
        return true;
    }
    if (glyph == NULL) return false;   // text marks (the index) are never rescaled
    // ⚠️ kdisp_draw_glyph_half_at takes the literal TOP-LEFT of the ink -- no baseline
    // align, no xOffset -- so the position comes from the glyph's own halved extents
    // and NOT from the bbox above. Halve rounding UP, or an odd-width glyph loses its
    // last column.
    const int8_t hw = (int8_t)((pgm_read_byte(&glyph->width) + 1) / 2);
    const int8_t hh = (int8_t)((pgm_read_byte(&glyph->height) + 1) / 2);
    if (hh >= free_rows) return false;
    kdisp_draw_glyph_half_at(fonts, count,
                             (int8_t)(BUFFER_X + (SCREEN_WIDTH - hw) / 2),
                             (int8_t)((free_rows - hh) / 2), cp);
    return true;
}

static void render_macro_key(uint8_t id) {
    poly_macro_look_t look;
    poly_macro_look_get(id, &look);
    const char *label = look.text;

    // Widen the caption to codepoints once -- both the big-text style and the captioned
    // styles below need it, and the display list wants uint32_t anyway.
    uint32_t text[POLY_MACRO_LABEL_LEN + 1];
    uint8_t  tlen = 0;
    for (; label[tlen] != '\0' && tlen < POLY_MACRO_LABEL_LEN; tlen++) {
        text[tlen] = (uint32_t)(uint8_t)label[tlen];
    }
    text[tlen] = 0;

    // Caption alone, filling the cell. Falls through to the captioned styles when the
    // caption is empty (there would be nothing to draw) or when even the smallest face
    // overflows -- an empty keycap is worse than the small one it replaced.
    if (look.style == POLY_MACRO_STYLE_TEXT && tlen > 0) {
        if (draw_macro_caption_big(text)) return;
    }

    // "M12" -- built by hand rather than snprintf, which is not worth linking for two
    // digits, and the display list wants uint32_t codepoints anyway.
    uint32_t index_text[4];
    uint8_t  n = 0;
    index_text[n++] = (uint32_t)'M';
    if (id >= 10) index_text[n++] = (uint32_t)('0' + id / 10);
    index_text[n++] = (uint32_t)('0' + id % 10);
    index_text[n]   = 0;

    // The mark above the caption: a chosen glyph, or the index. The icon is looked up
    // through its OWN single-font array once found, because kdisp_write_gfx_char
    // baseline-aligns every glyph to fonts[0] -- with g_all_fonts that is IconsFont
    // (yAdvance 40) and a taller pack glyph is shifted down by the difference, which is
    // exactly the language-flag gap-at-top regression. An icon this keyboard has no
    // glyph for falls back to the index rather than drawing nothing, so a caption
    // picked on a host with a richer font pack still names its macro here.
    const GFXfont*       icon_font  = NULL;
    const GFXglyph*      icon_glyph = NULL;
    const GFXfont*       icon_arr[1];
    const GFXfont* const* mark_fonts = mid_fonts;
    uint8_t              mark_count  = 1;
    uint32_t             mark_text[2];
    const uint32_t*      mark = index_text;

    if ((look.style == POLY_MACRO_STYLE_ICON || look.style == POLY_MACRO_STYLE_ICON_ONLY)
        && look.icon != 0) {
        icon_glyph = kdisp_gfx_glyph_font(g_all_fonts, g_all_font_count, look.icon,
                                          &icon_font);
        if (icon_glyph != NULL) {
            icon_arr[0]  = icon_font;
            mark_fonts   = (const GFXfont* const*)icon_arr;
            mark_count   = 1;
            mark_text[0] = look.icon;
            mark_text[1] = 0;
            mark         = mark_text;
        }
    }

    // ICON_ONLY draws the icon alone, centred in the whole cell, exactly as an
    // uncaptioned key does -- the caption is KEPT in storage so switching back does
    // not lose it, it is simply not drawn. An icon this keyboard has no glyph for
    // leaves icon_glyph NULL and falls through to the captioned index, which is the
    // same fallback every other icon path takes: a keycap is never left blank.
    const bool icon_only = (look.style == POLY_MACRO_STYLE_ICON_ONLY && icon_glyph != NULL);

    if (label[0] == '\0' || icon_only) {
        // No caption to place around: centre the mark in the whole cell. No fit check
        // and no halving here -- the tallest pack glyph is exactly SCREEN_HEIGHT, and
        // filling the cell is the point of this branch.
        int8_t ixmin, ixmax, iymin, iymax;
        kdisp_gfx_text_bbox(mark_fonts, mark_count, mark, &ixmin, &ixmax, &iymin, &iymax);
        kdisp_write_gfx_text(mark_fonts, mark_count,
                             (int8_t)(BUFFER_X + (SCREEN_WIDTH - (ixmax - ixmin + 1)) / 2 - ixmin),
                             (int8_t)((SCREEN_HEIGHT - (iymax - iymin + 1)) / 2 - iymin),
                             mark);
        return;
    }

    // Drop trailing characters until the caption fits the panel. Measuring after each
    // drop rather than estimating from a per-character width is the point -- the face
    // is proportional, so an estimate is wrong in both directions.
    uint8_t len = tlen;

    int8_t lxmin = 0, lxmax = 0, lymin = 0, lymax = 0;
    while (len > 0) {
        kdisp_gfx_text_bbox(lang_label_fonts, 1, text, &lxmin, &lxmax, &lymin, &lymax);
        if ((int16_t)(lxmax - lxmin + 1) <= SCREEN_WIDTH) break;
        text[--len] = 0;
    }
    if (len == 0) return;

    // Pin the caption's lowest lit pixel to the last screen row, exactly as the FW-2
    // prompt does -- the descender budget differs per string, so a fixed baseline
    // clips whichever label happens to carry a 'y'.
    const int8_t cap_base  = (int8_t)(SCREEN_HEIGHT - 1 - lymax);
    const int8_t free_rows = (int8_t)(cap_base + lymin);   // rows above the caption

    kdisp_write_gfx_text(lang_label_fonts, 1,
                         (int8_t)(BUFFER_X + (SCREEN_WIDTH - (lxmax - lxmin + 1)) / 2 - lxmin),
                         cap_base, text);

    // The mark, centred in whatever the caption left: native size, else halved.
    // An icon that fits at NO size falls back to the index, which always does --
    // the same fallback a missing glyph already takes, so an icon can never leave
    // the keycap without a mark.
    if (!draw_macro_mark(mark_fonts, mark_count, mark, free_rows, icon_glyph, look.icon)
        && icon_glyph != NULL) {
        draw_macro_mark(mid_fonts, 1, index_text, free_rows, NULL, 0);
    }
}

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
        kdisp_write_gfx_vtext(&NotoSans_Regular_Nano_10px7b, LABEL_COL_X, label,
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

#if defined(POLY_FL_ALIGN_FROW)
// ── Function-layer F-row alignment ───────────────────────────────────────────
// The F-row is DERIVED from the active base layout's own number row, so F5 always
// sits on the key that types 5 whichever layout is loaded. This replaces the old
// _FL0/_FL1 pair: two hand-maintained copies that drifted (Colemak and Workman were
// both wired to the 1..6 variant despite carrying an ordinary 1..5 row).
//
// The rule, which reproduces BOTH retired layers exactly: walk the base layer's top
// row across the two halves; every slot holding KC_1..KC_0 takes the matching
// F1..F10, the two slots after the last number take F11/F12, and whatever is left
// over takes the layer keys (OSL(_UL) at the far left, TO(_UL) for the other).
//
// ⚠️ It is ALL-OR-NOTHING and switches itself OFF the moment the row is customised.
// _FL sits below the dynamic-keymap write cap, so the host layout editor can remap
// it — and the editor reads keycodes through dynamic_keymap_get_buffer(), which does
// NOT come through here, so anything derived at runtime is invisible to it. Aligning
// a row the user had edited would therefore mean the editor showing one keycode
// while the board types another, with no way to see why. So: align only while all 14
// slots still hold exactly what the firmware compiled, and otherwise hand back the
// stored row untouched. Tweak one F-key and the magic stops for the whole row —
// editor and board agree again, which is the state you are in whenever you care.
#define FL_TOP_SLOTS 14

// The 14 top-row matrix positions in reading order: left half is row 0 cols 0..6,
// right half is row MATRIX_ROWS_PER_SIDE cols 1..7 (col 0 is not a key up there).
static bool fl_top_slot(uint8_t row, uint8_t col, uint8_t* slot) {
    if (row == 0 && col <= 6) {
        *slot = col;
        return true;
    }
    if (row == MATRIX_ROWS_PER_SIDE && col >= 1 && col <= 7) {
        *slot = (uint8_t)(6 + col);
        return true;
    }
    return false;
}

static void fl_slot_pos(uint8_t slot, uint8_t* row, uint8_t* col) {
    if (slot <= 6) {
        *row = 0;
        *col = slot;
    } else {
        *row = MATRIX_ROWS_PER_SIDE;
        *col = (uint8_t)(slot - 6);
    }
}

// What the F-row slot SHOULD hold for this base layout. Reads the base layer from the
// compiled keymap, never the dynamic one: the number row is the layout's identity, and
// a user who has remapped their digits has not thereby asked for a different F-row.
static uint16_t fl_aligned_keycode(uint8_t def_layer, uint8_t want) {
    uint8_t last_num = 0xFF;
    for (uint8_t i = 0; i < FL_TOP_SLOTS; i++) {
        uint8_t r, c;
        fl_slot_pos(i, &r, &c);
        const uint16_t base = keymaps[def_layer][r][c];
        if (base >= KC_1 && base <= KC_0) {
            // KC_1..KC_9 then KC_0 are contiguous, and so are KC_F1..KC_F10.
            if (i == want) return (uint16_t)(KC_F1 + (base - KC_1));
            last_num = i;
        }
    }
    // F11/F12 continue past the last digit (where a layout has "-" and "=").
    if (last_num != 0xFF) {
        if (want == last_num + 1) return KC_F11;
        if (want == last_num + 2) return KC_F12;
    }
    // Everything else is a layer key: the far-left slot sits over Esc and holds the
    // one-shot into _UL, the remaining leftover is the sticky TO(_UL).
    return (want == 0) ? OSL(_UL) : TO(_UL);
}

// Is the stored F-row still exactly what we compiled? Cached, because this runs on
// the render path (72 keycaps) as well as the key-event path, and each probe is 14
// dynamic-keymap reads. Invalidated by every write to the dynamic keymap.
static int8_t s_fl_row_pristine = -1;   // -1 unknown, 0 customised, 1 pristine

static bool fl_row_is_pristine(void) {
    if (s_fl_row_pristine < 0) {
        s_fl_row_pristine = 1;
        for (uint8_t i = 0; i < FL_TOP_SLOTS; i++) {
            uint8_t r, c;
            fl_slot_pos(i, &r, &c);
            if (keycode_at_keymap_location(_FL, r, c) != keymaps[_FL][r][c]) {
                s_fl_row_pristine = 0;
                break;
            }
        }
    }
    return s_fl_row_pristine == 1;
}
#endif // POLY_FL_ALIGN_FROW

// Drop the cached "is the F-row untouched?" answer. Called from every dynamic-keymap
// mutation (the three *_poly wrappers in split_sync.c), so a remap takes effect on the
// very next render rather than at the next reboot. A no-op on a board with no F-row to
// align, so the call sites need no #ifdef of their own.
void poly_fl_row_cache_invalidate(void) {
#if defined(POLY_FL_ALIGN_FROW)
    s_fl_row_pristine = -1;
#endif
}

// Layers below the host-write cap are remappable and live in the dynamic keymap
// (EEPROM); layers at/above it (the language/emoji function layers) are served straight
// from the compiled keymap in flash, so they never read the dynamic keymap and always
// reflect the flashed firmware. Used by both the display and the key-event path.
static uint16_t poly_keycode_at(uint8_t layer, uint8_t row, uint8_t col) {
    const uint16_t kc = (layer >= DYNAMIC_KEYMAP_UPDATE_MAX_LAYER_COUNT)
                            ? keymaps[layer][row][col]      // static, compiled-in (flash)
                            : keycode_at_keymap_location(layer, row, col);
#if defined(POLY_FL_ALIGN_FROW)
    // Both the render path (display_keycode_at) and the key-event path
    // (keymap_key_to_keycode) resolve through here, so the legend and the keycode can
    // never disagree about which F-key a slot holds — the render/action pairing rule.
    // The base layout comes from the SYNCED snapshot for the same reason: it is the
    // only source both halves share, and it is what the renderer already uses.
    uint8_t slot;
    if (layer == _FL && fl_top_slot(row, col, &slot) && fl_row_is_pristine()) {
        return fl_aligned_keycode((uint8_t)get_local_layer()->def_layer, slot);
    }
#endif
    return kc;
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
    // ⚠️ The ABSOLUTE box, not the relative one. The whole display list moves as a
    // unit under the jitter offset, so the slack has to be measured over ALL of it —
    // including art positioned by a MOVE and drawn by a composite op, which the
    // relative box cannot see (font_lookup.h). Measuring the relative box instead
    // reports only the cursor-laid-out part: for the context-menu keycap that is the
    // hamburger alone, granting travel that would carry its pointer off the panel,
    // and for a legend that is ONLY composite art (the media-stop badge) it reports
    // an empty box and would allow the lot.
    int8_t xmin, xmax, ymin, ymax;
    kdisp_gfx_text_bbox_abs(g_all_fonts, g_all_font_count, ox, oy, text, &xmin, &xmax, &ymin, &ymax);
    int16_t axmin = xmin, axmax = xmax;   // glyph extent at the un-jittered origin
    int16_t aymin = ymin, aymax = ymax;
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
    // Plan at the ACTIVE legend size, exactly as the awake render does. Drawing
    // `text` at the small face's fixed (BUFFER_X, 23) instead would shrink a
    // medium/large legend the moment its pulse dipped dark and this relocation
    // fired — and leave it small until wake, since update_displays() early-returns
    // while DISP_IDLE is set. roll_idle_offset() has to measure the SAME text it
    // will move, or the slack it computes is the wrong glyph's.
    uint32_t      scratch[GLYPH_SIZE_MAX_LEN + 1];
    main_legend_t plan;
    plan_main_legend(text, BUFFER_X, 23, scratch, (uint8_t)(GLYPH_SIZE_MAX_LEN + 1), &plan);
    int8_t dx, dy;
    roll_idle_offset(plan.text, plan.x, plan.y, seed, &dx, &dy);
    kdisp_set_buffer(0x00);
    kdisp_set_draw_offset(dx, dy);
    kdisp_write_gfx_text(g_all_fonts, g_all_font_count, plan.x, plan.y, plan.text);
    kdisp_set_draw_offset(0, 0);
    kdisp_send_window();   // idle jitter draws within the 72x40 window (roll_idle_offset clamps to it)
    return true;
}

// How often the Eden idle legend relocates to a fresh spot (anti-burn-in "ghosting").
#define EDEN_LEGEND_DRIFT_MS 7000u

// Eden idle screensaver: draw a key's resting legend LIT into the comet field the
// animation just built, so the letter is clearly visible as a faint "ghost" the
// comets drift around — and relocate it to a fresh in-glyph-slack spot every
// EDEN_LEGEND_DRIFT_MS so it slowly wanders (anti-burn-in), like the jitter style.
// (An earlier version ERASED the legend as a dark cutout, but at the dim idle
// brightness the sparse comet field had too few lit pixels for a cutout to read.)
// Called per panel from sa_render_idle_frame() (startup_anim.c) AFTER the comet
// field is drawn and BEFORE the send, so it does NOT clear the buffer and does NOT
// send. `disp_idx` is the display index == the anim's geom index == LAYOUT_TO_INDEX(r,c);
// invert it to (r,c) to resolve the keycode. Returns false without touching the
// buffer for keys with no plain-text legend (flags/emoji/tabs/overlays) — those faces
// just show the plain comet field. Mirrors render_idle_key's legend derivation.
bool eden_idle_erase_legend(uint8_t disp_idx) {
    if (disp_idx >= MATRIX_ROWS_PER_SIDE * MATRIX_COLS) return false;
    // disp_idx == the anim geom index == display row*8 + col. Invert to the matrix
    // (row,col), undoing the right-half `c--` display fold that invert_display()
    // applies to the upper display rows (mirrors the host sim's disp_mp): LEFT is a
    // straight (dr, dc); RIGHT is (dr+MATRIX_ROWS_PER_SIDE, dc+1) on rows 0..3 and
    // (dr+MATRIX_ROWS_PER_SIDE, dc) on the bottom row 4.
    uint8_t dr = disp_idx / MATRIX_COLS, dc = disp_idx % MATRIX_COLS;
    uint8_t mr, mc;
    if (is_left_side()) {
        mr = dr;
        mc = dc;
    } else {
        mr = dr + MATRIX_ROWS_PER_SIDE;
        mc = (dr < 4) ? (uint8_t)(dc + 1) : dc;
    }
    if (mc >= MATRIX_COLS) return false;   // phantom col — no OLED behind it
    const poly_layer_t* local_layer = get_local_layer();
    uint16_t keycode = display_keycode_at(local_layer, mr, mc);
    if (keycode == KC_NO || keycode == KC_TRNS) return false;

    const poly_sync_t* local_state = get_local_state();
    led_t state = local_layer->led_state;
    uint32_t unimap[2] = {0, 0};
    const uint32_t* text = to_static_text(keycode, state);
    if (text == NULL) {
        text = translate_keycode(local_state->lang, keycode, false, state.caps_lock);
    }
    if (text == NULL && (keycode & QK_UNICODEMAP_PAIR) == QK_UNICODEMAP_PAIR) {
        uint16_t chr = state.caps_lock ? QK_UNICODEMAP_PAIR_GET_SHIFTED_INDEX(keycode)
                                       : QK_UNICODEMAP_PAIR_GET_UNSHIFTED_INDEX(keycode);
        unimap[0] = unicode_map[chr];
        text = unimap;
    }
    if (text == NULL || text[0] == 0) {
        return false;   // no text legend — leave the plain comet field on this key
    }
    // Draw the legend LIT but with a scanline half-brightness effect (only even buffer
    // rows lit) so it reads lighter over the comet field, at a slowly-drifting position
    // within its own on-screen slack. roll_idle_offset() picks a uniform random offset
    // inside the glyph's free space (fully on-screen, per-glyph — the same helper the
    // jitter idle style uses); the seed changes once per EDEN_LEGEND_DRIFT_MS so every
    // ~7 s the letter jumps to a fresh spot. Per-key phase (disp_idx) so they don't all
    // move in lockstep.
    // Same glyph-size plan as render_idle_key() — this is the second idle draw path
    // and it had the identical fixed-origin bug, so fixing only the one a reviewer
    // pointed at would have left the Eden screensaver shrinking the legend instead.
    uint32_t      scratch[GLYPH_SIZE_MAX_LEN + 1];
    main_legend_t plan;
    plan_main_legend(text, BUFFER_X, 23, scratch, (uint8_t)(GLYPH_SIZE_MAX_LEN + 1), &plan);
    uint32_t epoch = timer_read32() / EDEN_LEGEND_DRIFT_MS;
    int8_t dx, dy;
    roll_idle_offset(plan.text, plan.x, plan.y, epoch * 2654435761u + disp_idx, &dx, &dy);
    kdisp_set_gfx_scanline(true);
    kdisp_set_draw_offset(dx, dy);
    kdisp_write_gfx_text(g_all_fonts, g_all_font_count, plan.x, plan.y, plan.text);
    kdisp_set_draw_offset(0, 0);
    kdisp_set_gfx_scanline(false);
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
// cy_radius is exposed because the courtyard clear is only wanted when something
// is drawn UNDERNEATH the legend for it to punch a margin through (a frame, a row
// bar, an overlay). On a deliberately filled ground — the inverted picker Ctrl —
// there is nothing to clear away from, so the clear just eats a dark halo out of
// the fill around every glyph. Pass 0 there.
static void draw_legend_cx_cy(const uint32_t* text, int8_t y, int8_t cy_radius) {
    while (*text == U' ') text++;          // drop manual leading padding (skews bbox)
    int8_t lo = 0, hi = 0;
    kdisp_gfx_text_bounds(g_all_fonts, g_all_font_count, text, &lo, &hi);
    const int8_t w    = (int8_t)(hi - lo + 1);
    const int8_t left = (int8_t)(BUFFER_X + (SCREEN_WIDTH - w) / 2 - lo);
    kdisp_write_gfx_text_cy(g_all_fonts, g_all_font_count, left, y, text, cy_radius);
}

static void draw_legend_cx(const uint32_t* text, int8_t y) {
    draw_legend_cx_cy(text, y, KDISP_CY_DEFAULT);
}

// ── Keycap-OLED selection strategy (update_displays / kdisp_idle) ─────────────
// Both scans walk every key on this half and must select that key's OLED before
// rendering. Two strategies, chosen per variant:
//   * DEFAULT (split72): a single 0-bit is walked through the shift-register
//     chain — latch a seed on the first key of the half, then shift once per
//     column. Valid ONLY when each matrix row fills a whole 8-bit shift register
//     (split72: MATRIX_COLS == 8), so a per-column shift lands the next key on the
//     next register with no gaps.
//   * split42 (POLY_DISP_SELECT_BY_TABLE): the 6-column matrix occupies only the
//     low 6 bits of each 8-bit register (the 2 high bits carry the thumb OLEDs), so
//     a per-column walk under-shifts by 2 every row and mis-aligns every row below
//     row 0 (the reported "home row shows S D F G shift" offset — and the thumb
//     seed would then collide with row 2). Instead select each key directly from
//     the authoritative key_display[] table — the SAME source keypress-invert uses,
//     so the legend render and press feedback can never drift, and any remaining
//     physical-wiring correction is a one-line edit to that one table.
#if defined(POLY_DISP_SELECT_BY_TABLE)
#  define POLY_DISP_SEED(bm)     ((void)(bm))
#  define POLY_DISP_SELECT(idx)  sr_shift_out_buffer_latch(get_key_disp_bitmask(idx), get_disp_bitmask_size())
#  define POLY_DISP_ADVANCE()    ((void)0)
#else
#  define POLY_DISP_SEED(bm)     sr_shift_out_buffer_latch((bm), sizeof(struct display_info))
#  define POLY_DISP_SELECT(idx)  ((void)(idx))
#  define POLY_DISP_ADVANCE()    sr_shift_once_latch()
#endif

void update_displays(enum refresh_mode mode) {
    // Doom easter egg: while game mode owns the keycaps, the blitter is the
    // only writer — a legend re-render here would tear the game frame.
    if (doom_mode_active()) {
        s_disp_render_active = false;
        return;
    }
    // Same for the one-time startup animation: while it owns the keycaps, its
    // procedural blitter is the only writer.
    if (startup_anim_active()) {
        s_disp_render_active = false;
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
        s_disp_render_active = false;
        return;
    }

    // We are about to render the keycaps. If the previous pass early-returned for a
    // mode (idle / Eden / DOOM), an untracked path drew the panels — invalidate every
    // dirty-window bbox so this first awake render erases the whole window per panel.
    if (!s_disp_render_active) {
        kdisp_invalidate_all_windows();
        s_disp_render_active = true;
    }

    //uint8_t layer = get_highest_layer(layer_state);
    const poly_layer_t* local_layer = get_local_layer();

    const led_t state = local_layer->led_state;
    const uint8_t mods = local_layer->mods;
    const bool capital_case = ((mods & MOD_MASK_SHIFT) != 0) || state.caps_lock;
    const bool display_overlays = test_flag(local_state->overlay_flags, DISPLAY_OVERLAYS);
    const bool add_lang = get_highest_layer(local_layer->layer) == _ADDLANG1;
    // The Intl picker latches, so nothing on the board would otherwise say it is
    // open — with a held key you could at least see your finger. Ctrl draws
    // inverted (white keycap, dark legend) for as long as it is armed.
    //
    // Rendered inverted rather than kdisp_invert()ed: that is a panel-level SSD1306
    // command which matrix_scan_kb already toggles on every press and release, so a
    // latched state driven through it would be undone by the next keypress. The
    // gate is the SYNCED mods bit (poly_layer_t), not the master-only latch static,
    // so the slave inverts its Ctrl too when Ctrl lives on that half.
    const bool picker_open = add_lang && ((mods & LATIN_PICKER_MOD) != 0);
    // The letter-remap prompt needs the same treatment for the same reason, and
    // more so: the remap key LATCHES on a tap, so without an inverted keycap
    // nothing on the board says the mode is open at all (field — "it does latch,
    // but I did not recognize it as it did not invert").
    const bool remap_open = add_lang && (local_layer->remap_mode != LATIN_REMAP_OFF);
    //the left side has an offset of 0, the right side an offset of MATRIX_ROWS_PER_SIDE
    const uint8_t offset = is_left_side() ? 0 : MATRIX_ROWS_PER_SIDE;
    uint8_t start_row = 0;

    //select first display (and later on shift that 0 till the end)
    if (mode == START_SECOND_HALF) {
        POLY_DISP_SEED(disp_row_3.bitmask);
        start_row = 3;
    }
    else {
        POLY_DISP_SEED(disp_row_0.bitmask);
    }

    const uint8_t max_rows = mode == START_FIRST_HALF ? 3 : MATRIX_ROWS_PER_SIDE;

#if !defined(POLY_DISP_SELECT_BY_TABLE)
    uint8_t skip = 0;
#endif
    // Start (or restart) the displayed-slot set for this full render; the two-pass
    // path (START_SECOND_HALF) keeps accumulating into the set the first pass began.
    if (mode != START_SECOND_HALF) {
        clear_displayed_slots();
    }
    for (uint8_t r = start_row; r < max_rows; ++r) {
        for (uint8_t c = 0; c < MATRIX_COLS; ++c) {
            uint8_t  disp_idx = LAYOUT_TO_INDEX(r, c);

            //since MATRIX_COLS==8 we don't need to shift multiple times at the end of the row
            //except there was a leading and missing physical key (KC_NO on base layer)
            uint16_t keycode = keymaps[_BL][r + offset][c];
            if (keycode == KC_NO) {
#if defined(POLY_DISP_SELECT_BY_TABLE)
                continue;   // no OLED behind an absent key; table-select needs no gap shift
#else
                skip++;
#endif
            }
            else {
                if (disp_idx != 255) {
                    POLY_DISP_SELECT(disp_idx);
                    kdisp_track_panel(disp_idx);   // dirty-window: send only this keycap's changed rect
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
                    // FW-2: an unsigned firmware image is waiting for a physical
                    // yes/no. The whole board becomes the dialog — every keycap
                    // goes dark except this half's prompt key — so it cannot be
                    // mistaken for normal typing, and it does not depend on which
                    // layer happens to be active. Checked first: this outranks
                    // every other per-key mode below.
                    if (local_state->fw_confirm) {
                        if (r == FW_CONFIRM_ROW && c == FW_CONFIRM_COL) {
                            render_fw_confirm_key(is_left_side());
                        } else {
                            kdisp_set_buffer(0x00);
                        }
                        kdisp_send_window();
                        doom_handled = true;
                    } else if (local_state->doom_ctl == 2) {
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
                            kdisp_send_window();
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
                            kdisp_send_window();
                            doom_handled = true;
                        } else if (pad == KC_ESC) {
                            // The shared exit-hint face — byte-identical to
                            // the master's HUD corner key (field rd 18).
                            doom_render_esc_key();
                            kdisp_send_window();
                            doom_handled = true;
                        } else if (keycode == KC_LCTL || keycode == KC_RCTL) {
                            // Ctrl fires in DOOM — show a crosshair reticle
                            // instead of the plain Ctrl legend (field rd 16).
                            kdisp_set_buffer(0x00);
                            doom_render_fire_key();
                            kdisp_send_window();
                            doom_handled = true;
                        } else if (keycode == KC_SPACE) {
                            // Space is DOOM's use/open — a door symbol
                            // instead of the space legend (field rd 17).
                            kdisp_set_buffer(0x00);
                            doom_render_use_key();
                            kdisp_send_window();
                            doom_handled = true;
                        } else if (!doom_key_is_control(keycode)) {
                            kdisp_set_buffer(0x00);
                            kdisp_send_window();
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
                            kdisp_send_window();
                        } else if (keycode == KC_EMJ_PRESET || keycode == KC_LANG_PRESET ||
                                   keycode == KC_EMJ_CLEAR  || keycode == KC_LANG_CLEAR) {
                            // Top-row MRU controls: "Preset" / "Clear".
                            kdisp_set_buffer(0x00);
                            render_mru_ctrl_key(keycode == KC_EMJ_PRESET || keycode == KC_LANG_PRESET);
                            kdisp_send_window();
                        } else if (keycode >= KC_LANG_CAT_BASE && keycode < KC_LANG_PAGE_PREV) {
                            // Language region tab — continent label + active frame.
                            kdisp_set_buffer(0x00);
                            lang_draw_tab_indicator(keycode);
                            lang_draw_tab_bottom(keycode);
                            render_lang_region_tab(keycode);
                            kdisp_send_window();
                        } else {
                        const uint32_t* text = to_static_text(keycode, state);
                        // ⚠️ The remap prompt blanks the board through render_key(),
                        // which is only consulted when there is NO static text — so a
                        // key that HAS one (the remap key itself, and any other legend
                        // on this layer) sailed straight past it and kept drawing.
                        // Drop the text here and the existing render_key() blanking
                        // takes over. The remap key is exempt: it stays visible so it
                        // can render inverted and offer the way out.
                        if(remap_open && keycode != KC_LAT_REMAP &&
                           !(keycode >= KC_A && keycode <= KC_Z)) {
                            text = NULL;
                        }
                        // Ctrl while the picker is armed: white ground, legend
                        // erased out of it. Paired reset below — the plotter flags
                        // are static, so leaving erase on would blank every
                        // following keycap on this pass.
                        const bool invert_key = (picker_open &&
                                                 (keycode == KC_LEFT_CTRL || keycode == KC_RIGHT_CTRL)) ||
                                                (remap_open && keycode == KC_LAT_REMAP);
                        kdisp_set_buffer(invert_key ? 0xFF : 0x00);
                        if(invert_key) {
                            kdisp_set_gfx_erase(true);
                        }
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
                            draw_legend_cx_cy(text, 23, invert_key ? 0 : KDISP_CY_DEFAULT);
                        } else {
                            kdisp_write_gfx_text_cy(g_all_fonts, g_all_font_count, BUFFER_X, 23, text,
                                                    invert_key ? 0 : KDISP_CY_DEFAULT);
                        }
                        text = NULL;
                        // ⚠️ Nothing overlays the Intl layer. Its letters ARE the
                        // payload — render_key() has just drawn the selected
                        // variation — and the picker modifier is Ctrl, so both
                        // sources below would paint the CTRL view over it:
                        // copy_overlay_to_buffer() the app's Ctrl-modifier overlay
                        // image, and keycode_to_disp_overlay() the built-in
                        // Ctrl-shortcut hints, on every key at once.
                        // ⚠️ The guard has to wrap BOTH arms. Folding it into the
                        // first condition instead (`!add_lang && display_overlays`)
                        // looks equivalent and is not: the else would then fire on
                        // the Intl layer and paint the hardcoded hint back over the
                        // variation, which is the bug this exists to stop.
                        if(!add_lang) {
                            if(display_overlays) {
                                if(!copy_overlay_to_buffer(keycode, mods)) {
                                    text = keycode_to_disp_overlay(keycode); //fallback to hardcoded
                                }
                            } else {
                                text = keycode_to_disp_overlay(keycode); //this should maybe go away - or setting?
                            }
                        }
                        if(text) {
                            // The hint string is a self-contained display list: any
                            // frame / half-scale composite / +/- sign is encoded inline
                            // (see the \x0E-\x12 ops in kdisp_write_gfx_text_cy), so no
                            // per-keycode special-case is needed here.
                            kdisp_write_gfx_text_cy(g_all_fonts, g_all_font_count, BUFFER_X, 23, text, KDISP_CY_DEFAULT);
                        }
                        if(invert_key) {
                            kdisp_set_gfx_erase(false);
                        }
                        kdisp_send_window();
                        }
                    }
                }
                POLY_DISP_ADVANCE();
            }

        }
#if !defined(POLY_DISP_SELECT_BY_TABLE)
        for (;skip > 0;skip--) {
            sr_shift_once_latch();
        }
#endif
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
    // Eden idle style: the looping screensaver (eden_idle_tick) owns the keycaps —
    // do NOT pulse/blank them here. Returning also leaves the panels enabled at the
    // brightness Eden's own start set, so the transition-pass set_displays(idle=true)
    // can't flash a black/pulse frame over the animation.
    if (get_local_state()->idle_style == IDLE_STYLE_EDEN) {
        return;
    }
    uint8_t offset = is_left_side() ? 0 : MATRIX_ROWS_PER_SIDE;
#if !defined(POLY_DISP_SELECT_BY_TABLE)
    uint8_t skip = 0;
#endif
    const bool jitter = get_local_state()->idle_style == IDLE_STYLE_JITTER;
    const poly_layer_t* local_layer = get_local_layer();
    const led_t led_state = local_layer->led_state;
    POLY_DISP_SEED(disp_row_0.bitmask);

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
#if defined(POLY_DISP_SELECT_BY_TABLE)
                continue;   // no OLED behind an absent key; table-select needs no gap shift
#else
                skip++;
#endif
            } else {
                if (disp_idx != 255) {
                    POLY_DISP_SELECT(disp_idx);
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
                POLY_DISP_ADVANCE();
            }

        }
#if !defined(POLY_DISP_SELECT_BY_TABLE)
        for (;skip > 0;skip--) {
            sr_shift_once_latch();
        }
#endif
    }
}

// Handles keypress events including unicode input, language modifications, and special commands.
// Per-key latch (bit0=LGUI, 1=RGUI, 2=LALT, 3=RALT) recording that the Apple
// GUI/Alt swap was applied on press, so release unregisters the same modifier even
// if the active OS changed while the key was held. See the swap block below.
static uint8_t s_apple_swap_latch = 0;

// Every PolyKybd settings/utility keycode is handled HERE, in process_record_user,
// and swallowed (`return false`) — not in post_process_record_user. That is the
// QMK-sanctioned shape for a custom keycode, and it is what keeps one physical
// press to one action on a ONE-SHOT layer:
//
//  * `process_record()` (quantum/action.c) returns BEFORE `process_action()` when
//    `process_record_user()` returns false, so the synthetic release
//    `process_action()` re-dispatches for a key pressed while a one-shot layer is
//    active (`record->event.pressed = false; process_record(record);`) is never
//    generated at all. Handled in post_process_record_user instead, that inner
//    dispatch ran the action once, the OUTER post_process ran it again (the same
//    record had been mutated), and the real release ran it a third time — one tap
//    of KC_GLYPH_SIZE_UP on the OSL-entered _UL stepped the legend size three
//    tiers (field, 2026-08-25; it reads as "twice" because the third step clamps
//    at the end tier). _SL is entered with TO(), which is why the same keys never
//    doubled there.
//  * QMK compensates for the swallow itself: that same early-return path still
//    runs `clear_oneshot_layer_state(ONESHOT_OTHER_KEY_PRESSED)`, so OSL(_UL)
//    resolves after one key exactly as before.
//
// These keycodes are display/state only and never travel to the host, so
// swallowing them costs nothing. Real keycodes that also want a repaint (the
// shifts, the F-keys, RM_NEXT/PREV) must NOT be swallowed and stay in
// post_process_record_user — all three of those actions are idempotent repaints,
// so the extra dispatches cost nothing there either.
//
// Returns true when the keycode was ours.
static bool poly_custom_key_action(uint16_t keycode, keyrecord_t* record) {
    poly_sync_t*  local_state = access_local_state();
    poly_layer_t* local_layer = access_local_layer();
    bool handled = true;
    // ⚠️ The release switch below is entered on BOTH edges; its actions run only when
    // `act`, i.e. on the release. Owning the PRESS — returning true without acting —
    // is the load-bearing half: process_record_user() then returns false, so
    // process_record() early-returns BEFORE process_action(), and the one-shot
    // re-dispatch at the end of process_action() never manufactures a synthetic
    // release. Leave the press unowned and that block fires, so the action runs once
    // on the synthetic release and AGAIN on the physical one — the physical release
    // still resolves to this keycode because store_or_get_action() caches the source
    // layer, and neither NO_ACTION_LAYER nor STRICT_LAYER_RELEASE is defined here.
    // (Caught by CodeRabbit on #229; the first cut of this refactor swallowed only
    // the release and still double-fired.) Owning both edges out of ONE switch is
    // deliberate: a second list of "keycodes to swallow on press" is a list that goes
    // stale silently — the failure mode this file warns about repeatedly.
    const bool act = !record->event.pressed;
    if (!act) {
        switch (keycode) {
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
            handled = false;
            break;
        }
        if (handled) {
            return true;      // a press-edge action (KC_LANG) owned it outright
        }
        handled = true;       // otherwise fall through — the release switch may own it
    }
    switch (keycode) {
        case KC_RGB_TOG:
            if (!act) break;
            local_state->flags = toggle_flag(local_state->flags, RGB_ON);
            break;
        case KC_DEADKEY:
            if (!act) break;
            local_state->flags = toggle_flag(local_state->flags, DEAD_KEY_ON_WAKEUP);
            request_disp_refresh();
            break;
        case KC_TOGMODS:
            if (!act) break;
            local_state->flags = toggle_flag(local_state->flags, MODS_AS_TEXT);
            request_disp_refresh();
            break;
        case KC_TOGTEXT:
            if (!act) break;
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
            if (!act) break;
            local_layer->def_layer = _L0;
            defer_default_layer_save(local_layer->def_layer);
            layer_clear();
            layer_on(local_layer->def_layer);
            request_disp_refresh();
            break;
        case KC_L1:
            if (!act) break;
            local_layer->def_layer = _L1;
            defer_default_layer_save(local_layer->def_layer);
            layer_clear();
            layer_on(local_layer->def_layer);
            request_disp_refresh();
            break;
        case KC_L2:
            if (!act) break;
            local_layer->def_layer = _L2;
            defer_default_layer_save(local_layer->def_layer);
            layer_clear();
            layer_on(local_layer->def_layer);
            request_disp_refresh();
            break;
        case KC_L3:
            if (!act) break;
            local_layer->def_layer = _L3;
            defer_default_layer_save(local_layer->def_layer);
            layer_clear();
            layer_on(local_layer->def_layer);
            request_disp_refresh();
            break;
        case KC_L4:
            if (!act) break;
            local_layer->def_layer = _L4;
            defer_default_layer_save(local_layer->def_layer);
            layer_clear();
            layer_on(local_layer->def_layer);
            request_disp_refresh();
            break;
        case KC_BASE:
            if (!act) break;
            layer_clear();
            layer_on(local_layer->def_layer);
            break;
        case KC_D1Q:
            if (!act) break;
            set_user_brightness(FULL_BRIGHT/4);
            break;
        case KC_D3Q:
            if (!act) break;
            set_user_brightness((FULL_BRIGHT/4)*3);
            break;
        case KC_DHLF:
            if (!act) break;
            set_user_brightness(FULL_BRIGHT/2);
            break;
        case KC_DMAX:
            if (!act) break;
            set_user_brightness(FULL_BRIGHT);
            break;
        case KC_DMIN:
            if (!act) break;
            set_user_brightness(2);
            break;
        case KC_DDIM:
            if (!act) break;
            dec_brightness();
            break;
        case KC_DBRI:
            if (!act) break;
            inc_brightness();
            break;
        case KC_DAUTO:
            if (!act) break;
            // Toggle host-driven (daylight/auto) brightness vs. manual control.
            // ⚠️ Once-per-press comes from poly_custom_key_action() being called
            // from process_record_user() and SWALLOWING the keycode, not from the
            // release edge on its own: on a one-shot layer a release-edge action
            // fires up to three times, and a toggle is the shape where that reads
            // as doing nothing at all (CLAUDE.md, "A release-edge action fires up
            // to THREE times on a ONE-SHOT layer").
            toggle_brightness_auto_mode();
            request_disp_refresh();
            break;
        case KC_OS_ICON:
            if (!act) break;
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
            if (!act) break;
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
            if (!act) break;
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
        case KC_SETTINGS_MORE:
            if (!act) break;
            // Toggle, so a mis-tap is undoable without leaving the layer. The value
            // is synced by the ordinary housekeeping diff — the slave draws its own
            // half of the revealed row and only ever sees poly_sync_t.
            local_state->settings_more = local_state->settings_more ? 0 : 1;
            request_disp_refresh();
            break;
        case KC_GLYPH_SIZE_UP:
        case KC_GLYPH_SIZE_DOWN: {
            if (!act) break;
            // Step the keycap legend size one tier, the same setting HID cmd 34
            // drives. Runs once on release (we are inside the `if
            // (!record->event.pressed)` block). No explicit sync: housekeeping picks
            // the new size up into local_state and the diff carries it to the slave,
            // which re-renders on receipt (split_sync.c). The step WRAPS at the ends
            // (3 -> 1, 1 -> 3), so every press changes the tier and redraws.
            //
            // Shift REVERSES the direction, which is what lets a single key on _UL
            // cover both — the keycap swaps its icon to match (to_static_text). Read
            // the live get_mods() rather than the synced poly_layer_t copy: this runs
            // on the master, at the moment of the release, and must follow the finger
            // rather than the last housekeeping snapshot the legend was drawn from.
            int8_t dir = (keycode == KC_GLYPH_SIZE_UP) ? 1 : -1;
            if (get_mods() & MOD_MASK_SHIFT) dir = (int8_t)-dir;
            step_glyph_size(dir);
            request_disp_refresh();
            break;
        }
        case KC_EDEN:
            if (!act) break;
            // Trigger the startup ("Eden") animation NOW on this (master) half and bump
            // the synced nonce so the slave plays in lockstep (the nonce is delivered by
            // the one-shot bridge send in housekeeping, once the transport is up — see
            // sync_and_refresh_displays gating). Input is swallowed while it plays.
            startup_anim_start();
            local_state->anim_nonce++;
            break;
        // Cycle the two display settings that were previously reachable only over HID
        // (cmds 28 / 30) — a keyboard with no host app could not change them at all.
        // Both go through the SAME setter the HID command uses, so the persist +
        // master-authoritative sync behaviour is identical however the change arrives.
        case KC_IDLE_STYLE: {
            if (!act) break;
            // Skip IDLE_STYLE_IDDQD: that one is the doom easter egg and has its own
            // way in (typing IDDQD arms the utilities-layer key, doom_mode.c). A
            // settings key that cycled into it would hand it to anyone who pressed
            // this key twice. A board already ON that style still cycles out of it.
            uint8_t style = get_idle_style();
            do {
                style = (uint8_t)((style + 1u) % IDLE_STYLE_COUNT);
            } while (style == IDLE_STYLE_IDDQD);
            set_idle_style(style);
            request_disp_refresh();
            break;
        }
        case KC_GLYPH_SCRIPT:
            if (!act) break;
            // Wrap on GLYPH_SCRIPT_COUNT (what THIS firmware can draw), not on 0xFF:
            // the wire accepts any index, but a key that walked past the known set
            // would spend most of its presses showing the plain Latin fallback.
            set_glyph_script((uint8_t)((get_glyph_script() + 1u) % GLYPH_SCRIPT_COUNT));
            request_disp_refresh();
            break;
        // ── Language layer: region tabs, paging, MRU controls, slot/MRU select ──
        case KC_LANG_CAT_BASE ... KC_LANG_PAGE_PREV - 1:
            if (!act) break;
            lang_select_region((uint8_t)(keycode - KC_LANG_CAT_BASE));
            break;
        case KC_LANG_PAGE_PREV:
            if (!act) break;
            lang_page_prev();
            request_disp_refresh();
            break;
        case KC_LANG_PAGE_NEXT:
            if (!act) break;
            lang_page_next();
            request_disp_refresh();
            break;
        case KC_LANG_PRESET:
            if (!act) break;
            mru_lang_preset();
            break;
        case KC_LANG_CLEAR:
            if (!act) break;
            mru_lang_clear();
            break;
        case KC_LANG_MRU_BASE ... KC_LANG_SLOT_BASE - 1:
        case KC_LANG_SLOT_BASE ... KC_LANG_END - 1: {
            if (!act) break;
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
            if (!act) break;
            local_state->lang = (uint8_t)(keycode - KCL_ENUS);
            mark_settings_dirty();
            layer_off(_LL);
            break;
        default:
            handled = false;
            break;
    }
    return handled;
}

bool process_record_user(uint16_t keycode, keyrecord_t* record) {

    // While the ONE-SHOT startup ("Eden") animation is playing, swallow every key
    // event — no typing is wanted (or needed) during the intro. Keys from both
    // halves funnel through the master's process_record before USB reporting, so
    // gating here stops all input from reaching the host until the intro ends.
    // The LOOPING idle Eden screensaver is deliberately NOT swallowed: like the
    // pulse, the first key press should dismiss it and pass through to the wake
    // (display_wakeup clears DISP_IDLE → eden_idle_tick stops the loop next pass).
    if (startup_anim_active() && !startup_anim_is_loop()) {
        return false;
    }

    // FW-2: while the unsigned-image prompt is up the whole board IS the dialog —
    // every key event is swallowed (nothing should reach the host mid-decision)
    // and only the two prompt keys mean anything. Only the master runs
    // process_record — the slave's matrix is pulled over the split link — so a
    // press on EITHER half arrives here, and the matrix row is what says which.
    if (fw_staging_awaiting_confirm()) {
        // Answer on the RELEASE, not the press. split72.c's matrix_scan_kb inverts a
        // keycap on press and un-inverts it on release, entirely independently of
        // process_record — so acting on the press tears the prompt down and redraws
        // the normal legend while that keycap is still inverted, and it stays
        // inverted until the finger lifts.
        if (!record->event.pressed && record->event.key.col == FW_CONFIRM_COL) {
            if (record->event.key.row == FW_CONFIRM_ROW) {
                fw_staging_confirm_answer(true);    // left half  -> A / ACCEPT
            } else if (record->event.key.row == FW_CONFIRM_ROW + MATRIX_ROWS_PER_SIDE) {
                fw_staging_confirm_answer(false);   // right half -> R / REJECT
            }
        }
        return false;
    }

    // A macro is interruptible: any PRESS while one is playing stops it. That is the
    // "make it stop" a user reaches for, and it is also what keeps a macro from
    // interleaving its own keystrokes with live typing. The aborting key then falls
    // through and behaves normally -- the abort is not a swallow.
    if (poly_macro_active() && record->event.pressed) {
        poly_macro_abort();
    }

    // Macro keycodes (QK_MACRO_0..QK_MACRO_MAX). Nothing else in the build consumes
    // them -- via.c is the only core dispatcher and VIA_ENABLE is unset -- so they are
    // ours, and they are SWALLOWED here rather than handled on the release edge: an
    // OSL() layer re-dispatches a release-edge action up to three times
    // (process_action's do_release_oneshot), which for a macro would mean playing it
    // two or three times over.
    if (keycode >= QK_MACRO && keycode <= QK_MACRO_MAX) {
        if (record->event.pressed) {
            poly_macro_start((uint8_t)(keycode - QK_MACRO));
        }
        display_wakeup(record);
        return false;
    }

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
            // poly_os_action_column() folds the Linux-DE refinements (GNOME/KDE)
            // to the LINUX column — without it they indexed columns the 6-wide
            // chord table never had, and every action key was dead on those
            // desktops (see the note on the helper in poly_os.h).
            if (record->event.pressed) {
                emit_os_action((uint16_t)(keycode - KC_OS_ACTION_BASE),
                               poly_os_action_column(get_local_state()->active_os & POLY_OS_VALUE_MASK));
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

    // --- Intl letter remap ----------------------------------------------------
    // Runs BEFORE the letter / picker handling below, because in remap mode a
    // letter press means "this one", not "emit your variation".
    if(addlang && access_local_layer()->remap_mode != LATIN_REMAP_OFF) {
        // ⚠️ Modifiers and LAYER keys must fall through — this block used to
        // `return false` for every release, which swallowed the release of
        // MO(_ADDLANG1) ITSELF.  QMK therefore never unregistered the layer: Intl
        // went down and never came back up, so releasing it did not leave the
        // prompt and there was no way out of the mode (field).  A held Shift was
        // stuck the same way.  This is the picker's documented "gate the swallow on
        // OWNERSHIP, not on the keycode" rule, which this block ignored.
        if(IS_MODIFIER_KEYCODE(keycode) || IS_QK_MOMENTARY(keycode) || IS_QK_TO(keycode)) {
            return true;
        }
        if(!record->event.pressed) {
            return false;                   // swallow the release of whatever we consumed
        }
        const int8_t slot = latin_target_slot(keycode);
        if(keycode == KC_LAT_REMAP) {
            latin_remap_cancel();           // tap the remap key again to back out
        } else if(slot >= 0) {
            poly_layer_t* ll = access_local_layer();
            if(ll->remap_mode == LATIN_REMAP_PICKKEY) {
                ll->remap_target = (uint8_t)slot;
                ll->remap_mode   = LATIN_REMAP_PICKLTR;
            } else if((uint8_t)slot == ll->remap_target || slot < LATIN_LETTER_TARGETS) {
                // Only a LETTER can be a source, so a punctuation key is inert as a
                // choice here (and renders blank) — with one exception: the TARGET
                // itself, which is how a key is cleared. For a letter that is
                // "re-pick your own letter"; for a punctuation key, which has no own
                // letter, it is the only per-key way back to plain typing. Both land
                // on LATIN_ASSIGN_NONE inside latin_remap_apply().
                latin_remap_apply(ll->remap_target, (uint8_t)slot);
                latin_remap_cancel();
            }
            request_disp_refresh();
        }
        // Everything else is inert while the prompt is up — the board is a dialog.
        return false;
    }
    if(keycode == KC_LAT_REMAP) {
        if(record->event.pressed && addlang) {
            // Shift+remap clears EVERY assignment. Once the board is remapped the
            // Intl legends no longer match the printed letters, so there has to be
            // one way back that does not depend on remembering what was changed.
            // (The per-key reset needs no gesture of its own: re-assigning a key to
            // its own letter is the same thing, and latin_remap_apply stores
            // LATIN_ASSIGN_NONE for it.)
            if((get_mods() & MOD_MASK_SHIFT) != 0) {
                latin_remap_reset_all();
                return false;
            }
            // Held with Intl: open remap mode. The Intl layer is the only place the
            // key exists, so there is no "outside the layer" case to guard.
            poly_layer_t* ll = access_local_layer();
            ll->remap_mode   = LATIN_REMAP_PICKKEY;
            ll->remap_target = 0;
            // ⚠️ The prompt swallows every non-modifier release from here on, so a key
            // QMK had ALREADY registered when the mode opened would never be
            // unregistered — the host would hold it down and auto-repeat until USB
            // dropped. Reachable: a letter with no variation falls through to QMK on
            // this layer, so it can genuinely be down at this moment. Same remedy the
            // firmware-confirm prompt and doom_begin() use for the same reason.
            clear_keyboard();
            // Drop the variation picker if it was open: the two prompts would
            // otherwise both claim the keycaps.
            if(s_picker_latched) {
                unregister_mods(MOD_MASK_CTRL);
                s_picker_latched = false;
            }
            latin_picker_reset_page();
            request_disp_refresh();
        }
        return false;
    }
    // The latch toggles on the PRESS, so the release of the tap that ARMED it must
    // be swallowed too — otherwise QMK unregisters the modifier we just registered
    // and the latch is over before the finger lifts.
    //
    // ⚠️ Gated on s_picker_latched, i.e. on us owning the modifier — NOT on the key
    // alone. A Ctrl held down before Intl was pressed is registered by QMK, and
    // swallowing that release would leave it registered forever, turning every
    // later keystroke into Ctrl+key. The other two cases fall through harmlessly:
    // the toggle-OFF press already unregistered, so QMK's release handler just
    // unregisters nothing.
    if(addlang && !record->event.pressed && s_picker_latched &&
       (keycode == KC_LEFT_CTRL || keycode == KC_RIGHT_CTRL)) {
        return false;
    }
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
            case KC_MINUS ... KC_SLASH:
                // A punctuation key is a target too, but ONLY once it has been
                // mapped. Until then it must behave exactly as it does everywhere
                // else — never become the picker's subject, never swallow its own
                // keystroke — so it leaves here before touching any of that. The
                // guard is a no-op for letters, which always host themselves.
                if(!latin_has_row(keycode)) {
                    break;
                }
                // A different letter has a different variation count, so the page
                // that was showing may not exist on it.  Reset here rather than in
                // the picker-open branch below: this case runs on EVERY letter press,
                // picker open or not, so it cannot be bypassed.
                if(keycode != get_local_last_latin_keycode()) {
                    latin_picker_reset_page();
                }
                set_local_last_latin_keycode(keycode);
                if((get_mods() & LATIN_PICKER_MOD) == 0 && addlang) {
                    const bool lshift = get_mods() == MOD_BIT(KC_LEFT_SHIFT);
                    const bool rshift = get_mods() == MOD_BIT(KC_RIGHT_SHIFT);
                    const bool upper_case = lshift || rshift || global_layer->led_state.caps_lock;
                    const uint32_t* variation = latin_variation(keycode, upper_case);
                    if(variation!=NULL) {
                        //this is a work-around (at least for I-Bus on Linux we need to remove the shift, otherwise the Unicode sequence will not be recognized!)
                        if(lshift) unregister_code16(KC_LEFT_SHIFT);
                        if(rshift) unregister_code16(KC_RIGHT_SHIFT);
                        register_unicode(variation[0]);
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

        // The picker LATCHES: tap Ctrl to open it, tap again to close, and it also
        // closes as soon as a variation is chosen. Holding Intl+Ctrl+Shift+digit is
        // four keys at once, which is what made it awkward in practice.
        //
        // It latches by registering the REAL modifier rather than a private flag,
        // because poly_layer_t.mods is already synced to the other half — the slave
        // draws the picker digits on ITS keys from that same bit, and both flag
        // bytes in base/com.h are full, so a private flag would have nowhere to
        // ride. s_picker_latched only records that WE registered it, so leaving the
        // layer never unregisters a Ctrl the user is genuinely holding.
        if(addlang && (keycode == KC_LEFT_CTRL || keycode == KC_RIGHT_CTRL)) {
            if(s_picker_latched) {
                unregister_mods(MOD_MASK_CTRL);
                s_picker_latched = false;
            } else if((get_mods() & LATIN_PICKER_MOD) == 0) {
                register_mods(MOD_BIT(KC_LEFT_CTRL));
                s_picker_latched = true;
            }
            request_disp_refresh();
            return false;   // swallow: the tap is the toggle, not a modifier press
        }

        if((get_mods() & LATIN_PICKER_MOD) != 0 && addlang) {
            // A modifier must still reach QMK while the picker is open.  This
            // block used to `return false` for EVERY press, so the Shift PRESS
            // was swallowed, get_mods() never gained the bit, and pick_upper
            // below could only ever be true if Shift had been held BEFORE the
            // picker was opened — "press Shift first or you cannot choose the
            // capital".  Letting it through also flips the keycaps to the
            // upper-case row for free: `mods` is part of poly_layer_t, so the
            // change shows up as a layer_diff and housekeeping redraws.
            if(IS_MODIFIER_KEYCODE(keycode)) {
                return true;
            }
            // ⚠️ NOT a `case KC_LAT0 ... KC_LAT11` range — KC_LAT10/11 are appended
            // elsewhere in the enum, so the picker slots are resolved by helper.
            const int8_t pick_slot = latin_picker_slot(keycode);
            if(pick_slot >= 0) {
                if( latin_has_row(last_latin_keycode)) {
                    const bool pick_upper = ((get_mods() & MOD_MASK_SHIFT) != 0) || global_layer->led_state.caps_lock;
                    const uint8_t pick_row = latin_picker_row(last_latin_keycode, pick_upper);
                    // Only the slots that exist are drawn on the picker keycaps
                    // (render_key returns false for the rest), so a press on an
                    // empty one is a press on a blank key.  Ignore it — storing
                    // the index would leave ex[] pointing at a NULL cell, which
                    // the letter then "emits" as whatever lies at address 0.
                    // On a paged row this also covers the tail slots of the last
                    // page, which are blank for exactly the same reason.
                    const int8_t pick_idx = latin_picker_index(pick_row, get_local_layer()->picker_page, pick_slot);
                    if(pick_idx >= 0) {
                        latin_sync_t* global_latin_table = access_global_latin_table();
                        // The ABSOLUTE variation index is what gets stored, not the
                        // slot — so a pick made on page 1 still reads back correctly,
                        // and on a variant with a different LATIN_PICKER_SLOTS.
                        // ⚠️ Stored against the KEY's field, read from the HOSTED
                        // row (pick_row above). Writing it at pick_row would work
                        // only while nothing is remapped, then silently overwrite
                        // the pick of whichever key owns that letter.
                        const int8_t pick_target = latin_target_slot(last_latin_keycode);
                        latin_pick_set(global_latin_table->ex, latin_pick_field(pick_target, pick_upper), (uint8_t)pick_idx);
                        send_to_bridge(USER_SYNC_LATIN_EX_DATA, (void*)global_latin_table, sizeof(*global_latin_table), 10);

                        // "or an alternative character has been selected"
                        if(s_picker_latched) {
                            unregister_mods(MOD_MASK_CTRL);
                            s_picker_latched = false;
                        }
                        latin_picker_reset_page();
                        mark_latin_dirty();
                        request_disp_refresh();
                    }
                }
                return false;
            }
            switch(keycode) {
                case KC_LAT_PAGE_PREV:
                case KC_LAT_PAGE_NEXT:
                    if( latin_has_row(last_latin_keycode)) {
                        const bool pg_upper = ((get_mods() & MOD_MASK_SHIFT) != 0) || global_layer->led_state.caps_lock;
                        const uint8_t pages = latin_page_count(latin_picker_row(last_latin_keycode, pg_upper));
                        if(pages > 1) {
                            // Wrap both ways, like the language/emoji layers: with at
                            // most two pages on the current table, "next" and "prev"
                            // are the same gesture and a dead end at either edge would
                            // just be a key that sometimes does nothing.
                            poly_layer_t* lyr = access_local_layer();
                            uint8_t page = lyr->picker_page;
                            // Same clamp as latin_picker_index(): the case may have
                            // changed since the page was set, so start the wrap from a
                            // page that exists on THIS row.
                            if(page >= pages) {
                                page = (uint8_t)(pages - 1);
                            }
                            page = (keycode==KC_LAT_PAGE_NEXT) ? (uint8_t)((page + 1) % pages)
                                                               : (uint8_t)((page + pages - 1) % pages);
                            lyr->picker_page = page;
                            request_disp_refresh();
                        }
                    }
                    break;
                case KC_A ... KC_Z:
                case KC_MINUS ... KC_SLASH:
                    // Switching the picker's subject: redraw so the slots show the
                    // new row. Punctuation is included because a mapped one is a
                    // subject like any letter; an unmapped one already left the
                    // outer switch without becoming one, so this is just a repaint.
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

    // A PolyKybd settings/utility keycode: act on it and swallow it here rather
    // than in post_process_record_user — see poly_custom_key_action for why that
    // is what makes one physical press produce exactly one action.
    //
    // ⚠️ Wake FIRST and honour the verdict, which also restores the ORDER this had
    // before the switch moved here (display_wakeup ran at the tail of
    // process_record_user, i.e. before process_action and therefore before
    // post_process_record_user). A dead wake press — displays off, DEAD_KEY_ON_WAKEUP
    // set, past TURN_OFF_TIME — is meant to be thrown away, and it used to be: a false
    // return made process_record_user() end the whole dispatch, so the action never
    // ran. Only a PRESS can be rejected, so every release-edge settings key is
    // unaffected; KC_LANG is the one press-edge case here and would otherwise open _LL
    // / advance the language on the very press that exists only to wake the board.
    // A gated settings key is BLANK, so it must also be INERT — swallow both edges
    // before anything can act on it. QK_BOOTLOADER / QK_REBOOT / QK_DEBUG_TOGGLE are
    // QMK's own keycodes, handled by process_action() rather than by
    // poly_custom_key_action(), so returning false here is the only thing that stops
    // them: a gate that only blanked the legend would leave a keycap that reboots the
    // board with nothing drawn on it.
    if (settings_more_hidden(keycode)) {
        return false;
    }

    const bool wake_accepted = display_wakeup(record);
    if (wake_accepted && poly_custom_key_action(keycode, record)) {
        return false;
    }

    return wake_accepted;
}

// Post-processes keystrokes to handle display and state changes for various special keycodes.
void post_process_record_user(uint16_t keycode, keyrecord_t* record) {
    if (keycode == KC_CAPS_LOCK) {
        request_disp_refresh();
    }
    // Only REAL keycodes are left here — ones that must reach process_action() and
    // the host, so they cannot be handled-and-swallowed the way the PolyKybd
    // settings keycodes now are (see poly_custom_key_action). All three actions are
    // idempotent repaints, so the extra dispatches a one-shot layer produces are
    // harmless; a modifier does not get them at all (process_action() excludes
    // IS_MODIFIER_KEYCODE from the re-dispatch).
    if (!record->event.pressed) {
        switch (keycode) {
        case KC_RIGHT_SHIFT:
        case KC_LEFT_SHIFT:
            request_disp_refresh();
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

// Progressive boot splash — an always-on boot-progress indicator (no compile
// flag). The splash fills in ONE glyph at a time as boot advances, so a boot that
// HANGS freezes the reveal at the exact glyph where it stalled — count the lit
// letters to read how far boot got. A healthy boot fills to the full "POLY KYBD" /
// "SPLIT 72" in well under a second and then hands the keycaps to the real key
// legends.
//
// splash_progress() and its SPLASH_DONE constant moved to boot_diag.c/.h.
// Displays the FIRST boot-splash glyph. Kept as the external symbol / pre-init
// call site; later reveal steps are driven from keyboard_post_init_user().
void show_splash_screen(void) {
    splash_progress(1);
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
        // Stop the looping Eden idle screensaver immediately so update_displays() (no
        // longer blocked by startup_anim_active()) can repaint the woken legends.
        startup_anim_stop();
        uprint("Wake by keypress\n");
        local_state->contrast = get_active_brightness();
        local_state->flags &= ~((uint8_t)DISP_IDLE);
        local_state->flags |= STATUS_DISP_ON;
        reset_idle_jitter();   // fresh, centred idle session next time
        update_performed();
        // Wake-from-idle is the single worst render stall (measured ~107 ms in one
        // pass — the user is pressing a key to wake it, so it is also the most likely
        // to swallow a keystroke). Split it across two housekeeping passes via the
        // existing two-pass path (rows 0-2, matrix scan, rows 3-4) instead of the
        // one-shot ALL_AT_ONCE request_disp_refresh(). A subsequent overlay burst
        // (program-switch wake) may still re-request ALL_AT_ONCE, but coalescing then
        // collapses that to one render; a plain wake keeps the two-pass split.
        set_disp_refresh(START_FIRST_HALF);
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
    // Labels live in RAM on both halves (the render path reads one per macro keycap per
    // refresh). Each half loads its own EEPROM copy, then the master overwrites the
    // slave's over the link -- so a role swap makes whichever half the host talks to
    // the authority, with no handedness bookkeeping.
    poly_macro_labels_load();
    // Queue them all. Nothing detects "the link is up" here and nothing needs to: the
    // sync tick only clears a label's bit on a real ACK, so the queue simply drains
    // once the slave starts answering. Same shape as the state diff being its own
    // retry queue.
    poly_macro_labels_mark_all_dirty();
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

    emit_boot_banner();   // one-shot at boot; housekeeping re-emits for a late console

    // Boot-splash progress: reaching post_init proves QMK's split/USB init got
    // past the point where the fw-apply "slave not rebooted" hang stalls. Reveal
    // the next glyph here (right after set_side, so is_left_side() is valid).
    splash_progress(2);

#ifdef COMMUNITY_MODULE_POLYMOD_LTR559_ENABLE
    // The module's keyboard_post_init hook has already probed the sensor — QMK
    // runs keyboard_post_init_modules() before keyboard_post_init_kb()/_user(),
    // so ltr559_available() is answerable here. It probes on BOTH halves (the
    // sensor can be soldered to either half's expansion port); the half that
    // finds it uses it, the other stays disabled after bounded retries.
    if (ltr559_available()) {
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
    splash_progress(3);                 // language/emoji/MRU init done

    reset_overlay_pool();
    clear_display_has_overlay();
    //standard mapping is 1:1
    reset_display_to_pool();

#ifdef FW_UP_BOOT_TRACE
    boot_trace(U"2");
#endif
    splash_progress(4);                 // before core1 launch
#ifdef USE_CORE1
    multicore_launch_core1();
#endif
#ifdef FW_UP_BOOT_TRACE
    boot_trace(U"3");
#endif
    splash_progress(5);                 // core1 up

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
    poly_ltr559_register_split_handler();
#endif
#ifdef POLY_DUMMY_TXN_TEST
    // Root-cause experiment: register 3 no-op transactions so NUM_TOTAL_TRANSACTIONS
    // grows by 3 (matching the working pointing build) without the pointing device.
    // They are never executed — only their existence changes the transaction count.
    transaction_register_rpc(USER_SYNC_DUMMY1, user_sync_dummy_handler);
    transaction_register_rpc(USER_SYNC_DUMMY2, user_sync_dummy_handler);
    transaction_register_rpc(USER_SYNC_DUMMY3, user_sync_dummy_handler);
#endif

    fw_staging_init();
    splash_progress(6);                 // split RPCs registered, fw-staging up

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
    emit_idle_config();   // the style is only known here — the banner tick re-emits it
    note_glyph_script(ee.glyph_script);
    note_glyph_size(ee.glyph_size);
    // The dynamic keymap is stored BY LAYER INDEX and QMK does not version it, so a
    // build whose layer enum has shifted would read the previous occupant of every
    // slot above the change. Discard it once, then stamp the revision so the next
    // boot is a no-op. Done here rather than in eeconfig_init_user() because the
    // user datablock and the keymap are separate blocks with separate lifetimes —
    // the keymap survives a user-data re-init, which is exactly the case that would
    // otherwise slip through.
    // Timed and reported on the banner tick (note_keymap_storage) as well as printed
    // here: this discard rewrites the capped keymap, the encoder map and the whole
    // macro region -- a few kB of wear-levelled EEPROM -- inside post_init, before USB
    // is up. The one-shot uprintf below is usually emitted before a console can see
    // it, so a board that spends a long time here (or never leaves) looks simply dead.
    const uint8_t  stored_fmt = ee.keymap_layers_fmt;
    const bool     need_reset = (stored_fmt != KEYMAP_STORAGE_CURRENT);
    const uint32_t reset_t0   = timer_read32();
    if (need_reset) {
        uprintf("Keymap layer enum changed (fmt %u) - resetting dynamic keymap\n",
                (unsigned)stored_fmt);
        dynamic_keymap_reset_poly();
        stamp_keymap_layers_fmt();
    }
    note_keymap_storage(stored_fmt, need_reset,
                        need_reset ? timer_elapsed32(reset_t0) : 0);
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
    // Same for the legend size, so the first render is already at the chosen size.
    local_state->glyph_size = get_glyph_size();
#ifdef RGB_MATRIX_ENABLE
    local_state->flags = set_flag(STATUS_DISP_ON, RGB_ON, rgb_matrix_is_enabled());
#else
    local_state->flags = STATUS_DISP_ON;   // no RGB on this variant
#endif

    // Normalise the persisted latin-variation picks.  Each nibble can hold 0..15
    // while only LATIN_EX_VARIATIONS slots exist, and a row may have lost entries
    // since the byte was written, so a stale EEPROM can name a slot that is empty
    // or off the end.  latin_variation() falls back to slot 0 at read time either
    // way; rewriting the byte here stops a dead index in one case's nibble being
    // carried forward every time the other case is re-picked.
    latin_sync_t* latin_table = access_global_latin_table();
    bool latin_normalised = false;
    // ⚠️ The assignment map MUST be gated on the version byte — do NOT infer "never
    // written" from the bytes themselves. An earlier version of this relied on
    // LATIN_ASSIGN_NONE being all-bits-set and an unwritten EEPROM reading 0xFF;
    // that is wrong for this backend. QMK's wear-levelling normalises its backing
    // store so cleared bytes arrive as ZERO (quantum/wear_leveling/wear_leveling.c
    // clears the cache with memset(...,0) and requires a 0xFF store to return the
    // complement), so the map read back as all 0x00 = "every key hosts letter 0",
    // and every keycap on the Intl layer showed a variation of 'a' (field, first
    // flash). LATIN_PICK_INTERLEAVED is only ever stamped by firmware that writes
    // this field in the same breath, so it is the one trustworthy witness that the
    // bytes mean anything.
    // ⚠️ The letter block is copied at LATIN_*_BASE_BYTES, not sizeof(): EEPROM
    // keeps the letters and the punctuation apart so the letter block can keep the
    // size and offset it shipped with (see state.h). The punctuation half of both
    // arrays is filled from the ext block further down — unconditionally, because
    // the ASSIGN split is not byte-aligned (26 x 6 = 156 bits), so the 20th base
    // byte carries four bits of the first punctuation field with it.
    if(ee.latin_pick_migrated == LATIN_PICK_ASSIGN_OK) {
        memcpy(latin_table->assign, ee.latin_assign, LATIN_ASSIGN_BASE_BYTES);
    } else {
        // Includes LATIN_PICK_INTERLEAVED, whose stored map is the persisted
        // all-zero garbage described in state.h — discarding it is the recovery.
        memset(latin_table->assign, LATIN_ASSIGN_FILL, sizeof(latin_table->assign));
        latin_normalised = true;           // re-stamp at the next flush
    }
    if(ee.latin_pick_migrated == LATIN_PICK_ASSIGN_OK ||
       ee.latin_pick_migrated == LATIN_PICK_INTERLEAVED) {
        memcpy(latin_table->ex, ee.latin_ex_wide, LATIN_PICK_BASE_BYTES);
    } else if(ee.latin_pick_migrated == LATIN_PICK_MIGRATED) {
        // Re-index case-BLOCKED (case*26 + letter) to case-INTERLEAVED (slot*2 +
        // case). Same 52 values, same width -- only the field numbering moves, so
        // every pick is preserved.
        memset(latin_table->ex, 0, sizeof(latin_table->ex));
        for(uint8_t i = 0; i < 26; i++) {
            latin_pick_set(latin_table->ex, latin_pick_field(i, true),  latin_bits_get(ee.latin_ex_wide, LATIN_PICK_BASE_BYTES, i));
            latin_pick_set(latin_table->ex, latin_pick_field(i, false), latin_bits_get(ee.latin_ex_wide, LATIN_PICK_BASE_BYTES, (uint8_t)(26 + i)));
        }
        latin_normalised = true;           // force the flush that stamps the new version
    } else {
        // One-time widening of the legacy nibble pairs (one byte per letter, high
        // nibble = uppercase) into the 6-bit fields. Every legacy value is < 16 so
        // it lands in the wider field unchanged and the user keeps their picks.
        memset(latin_table->ex, 0, sizeof(latin_table->ex));
        for(uint8_t i = 0; i < 26; i++) {
            latin_pick_set(latin_table->ex, latin_pick_field(i, true),  (uint8_t)(ee.latin_ex[i] >> 4));
            latin_pick_set(latin_table->ex, latin_pick_field(i, false), (uint8_t)(ee.latin_ex[i] & 0x0F));
        }
        latin_normalised = true;
    }
    // The punctuation targets, from their own appended block. An EEPROM written
    // before they existed reads this as zeros (wear-levelling clears to 0, NOT to
    // 0xFF — see state.h), which is why "never written" is decided by the format
    // byte and not by the bytes: zeroed assignments would mean "every punctuation
    // key hosts A", the exact failure the letter map already shipped once.
    if(ee.latin_ext_fmt == LATIN_EXT_OK) {
        memcpy(latin_table->ex + LATIN_PICK_BASE_BYTES, ee.latin_ex_ext, LATIN_PICK_EXT_BYTES);
        for(uint8_t i = 0; i < LATIN_PUNCT_TARGETS; i++) {
            latin_assign_set(latin_table->assign, (uint8_t)(LATIN_LETTER_TARGETS + i),
                             latin_bits_get(ee.latin_assign_ext, LATIN_ASSIGN_EXT_BYTES, i));
        }
    } else {
        memset(latin_table->ex + LATIN_PICK_BASE_BYTES, 0, LATIN_PICK_EXT_BYTES);
        for(uint8_t i = 0; i < LATIN_PUNCT_TARGETS; i++) {
            latin_assign_set(latin_table->assign, (uint8_t)(LATIN_LETTER_TARGETS + i), LATIN_ASSIGN_NONE);
        }
        latin_normalised = true;           // stamp the ext block at the next flush
    }
    // ⚠️ Validate each pick against the row its key actually HOSTS, not against the
    // field index. Those are the same number only while every key is unassigned;
    // once a key is remapped, checking latin_ex_map[field] would validate the pick
    // against the wrong letter and zero perfectly good choices.
    for(uint8_t slot = 0; slot < LATIN_TARGETS; slot++) {
        const int8_t letter = latin_assign_get(latin_table->assign, slot);
        for(uint8_t c = 0; c < 2; c++) {
            const bool    upper = (c == 0);
            const uint8_t field = latin_pick_field(slot, upper);
            const uint8_t idx   = latin_pick_get(latin_table->ex, field);
            // An unassigned punctuation key hosts no row at all, so there is nothing
            // to validate against — its pick must simply be 0, ready for whatever it
            // is later mapped to.
            const bool bad = (letter < 0)
                                 ? (idx != 0)
                                 : (idx >= LATIN_EX_VARIATIONS ||
                                    latin_ex_map[(uint8_t)((upper ? 0 : 26) + letter)][idx] == NULL);
            if(bad) {
                latin_pick_set(latin_table->ex, field, 0);
                latin_normalised = true;
            }
        }
    }
    if(latin_normalised) {
        // access_global_latin_table() returns &g_latin, i.e. the very buffer
        // save_user_latin() writes, so the normalised bytes are already in place --
        // they only need scheduling for the next flush point.  Gate it on an actual
        // change: marking latin dirty unconditionally would rewrite the 26-byte
        // block on EVERY boot->suspend cycle with nothing to correct, and EEPROM
        // write frequency is load-bearing here (the wear-levelling consolidation
        // erase blocking a split transaction is what made the slave go
        // unresponsive -- see the readme).  A corrupt byte thus converges once.
        mark_latin_dirty();
    }

    // Restore the MRU recents and schedule a one-time push to the slave half.
    mru_load(ee.mru_emoji, ee.mru_lang);
    splash_progress(7);                 // EEPROM config (brightness/lang/OS/MRU) loaded

    set_displays(local_state->contrast, false);   // active brightness (auto value if restored, else manual)

    // One-time startup animation: on the very first boot (fresh EEPROM), play the
    // procedural intro once, then persist BOOT_INTRO_DONE (in the housekeeping
    // finish edge). Each half reads its own flag and animates its own keycaps.
    note_boot_flags(ee.boot_flags);
    // Boot-time auto-play of the Eden animation is intentionally NOT started here:
    // running it during boot was wedging a half (see the startup logs in
    // startup_anim.c). The animation is triggered on demand by the KC_EDEN key
    // instead (process_record_user), when the board is fully up and the split link
    // is live. Re-enable a guarded boot auto-play once the startup hang is understood.
#ifdef FW_UP_BOOT_TRACE
    boot_trace(U"4");
#endif
    splash_progress(SPLASH_DONE);       // boot complete: full splash, dwell, then legends
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

// Runs immediately after QMK's own dynamic_keymap_reset() in eeconfig_init_quantum().
//
// That call is the ONE remaining reachable use of the unbounded reset, and with the
// encoder/macro regions rebased on DYNAMIC_KEYMAP_UPDATE_MAX_LAYER_COUNT (config.h) it
// writes layers 8..11 straight over both of them. It cannot be prevented from here --
// eeconfig.c is upstream and we deliberately do not patch it for this -- but it CAN be
// repaired, because eeconfig_init_quantum() calls us three lines later, unconditionally,
// in the same function. dynamic_keymap_reset_poly() rewrites layers 0..7 and the capped
// encoder map from flash and zeroes the macro buffer, which is exactly the state a fresh
// EEPROM should be in, so this is the normal initialisation rather than a fix-up that
// happens to also repair.
//
// Nearly free despite running second: eeprom_update_byte() only writes a byte that
// actually changed, and QMK's pass has already put the correct values in layers 0..7.
void eeconfig_init_kb(void) {
    // Replicate what the weak default does before adding the repair -- overriding it
    // replaces the whole body, and dropping either half here would be silent
    // (eeconfig.c, EECONFIG_KB_DATA_SIZE == 0 branch).
    eeconfig_update_kb(0);
    dynamic_keymap_reset_poly();
    eeconfig_init_user();
}

// Initializes EEPROM configuration with default language, brightness, and latin extension settings.
void eeconfig_init_user(void) {
    uprint("Init EE config\n");
    // Zero the WHOLE struct: the write below is a full-struct write, but not every
    // field is assigned here (os_state, glyph_script, boot_flags are not), so an
    // uninitialised local would persist stack garbage into a fresh EEPROM for those.
    poly_eeconf_t ee = {0};
    ee.lang = g_lang_init;
    ee.brightness = ~FULL_BRIGHT;
    ee.auto_brightness = 0;   // host-auto off on a fresh EEPROM
    // Born with the board default already recorded as a real choice, rather than
    // leaning on load_user_eeconf()'s pre-sentinel substitution. Same reasoning as
    // latin_ex_wide being born widened: a fresh EEPROM should never have to be
    // migrated, and the zero that {0} leaves here means PULSE, not "unset".
    ee.idle_style     = POLY_DEFAULT_IDLE_STYLE;
    ee.idle_style_fmt = IDLE_STYLE_FMT_OK;
    memset(ee.latin_ex, 0, sizeof(ee.latin_ex));
    // A fresh EEPROM is born already widened: zeroed picks (every letter on its
    // first variation) plus the sentinel, so it never runs the legacy conversion.
    memset(ee.latin_ex_wide, 0, sizeof(ee.latin_ex_wide));
    ee.latin_pick_migrated = LATIN_PICK_ASSIGN_OK;
    // ⚠️ LATIN_ASSIGN_FILL, NOT the struct-wide {0}: a zeroed map reads as "every
    // key assigned to A" and the whole Intl layer shows A's variations.
    memset(ee.latin_assign, LATIN_ASSIGN_FILL, sizeof(ee.latin_assign));
    // Same for the punctuation block, and for the same reason — plus its own format
    // byte, so a fresh EEPROM is born with the extension already valid.
    memset(ee.latin_ex_ext, 0, sizeof(ee.latin_ex_ext));
    memset(ee.latin_assign_ext, LATIN_ASSIGN_FILL, sizeof(ee.latin_assign_ext));
    ee.latin_ext_fmt = LATIN_EXT_OK;
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
    startup_anim_stop();   // tear down the looping Eden idle screensaver on suspend
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
