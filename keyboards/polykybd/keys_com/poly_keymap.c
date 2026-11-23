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
#include "features/hints/os_hints.h"
#include "poly_keymap.h"   // our own exports — declarations checked against the definitions below
#include "status_oled.h"
#include "lib/split_link/bridge_helper.h"
#include "dev/profiling/loop_profile.h"
#ifdef POLYKYBD_LOOP_PROFILE
#    include "hardware/structs/timer.h"   // timer_hw->timerawl — raw 1 MHz us counter
#endif
#include "lib/fw_update/split_fw_up.h"
#include "lib/fw_update/fw_apply.h"   // fw_apply_tick(), fw_apply_screen_tick()
#ifdef POLYKYBD_BULK_PIPE
#    include "lib/hid/poly_pipe.h"
#endif
#include "lib/drivers/fw_staging.h"
#include "polymod_core1_owner.h"
#include "uni.h"
#include "lib/split_link/side.h"
#include "lib/display/fill_overlay.h"
#include "lib/system/poly_util.h"

#include "base/com.h"
#include "lib/hid/mac_compose.h"   // mac_compose_set (protocol v38)
#include "polymod_rle.h"
#include "lib/drivers/e2prom.h"
#include "base/overlay.h"
#include "lib/drivers/disp_array.h"
#include "lib/drivers/helpers.h"
#include "base/update.h"
#include "lib/drivers/spi_helper.h"
#include "lib/drivers/shift_reg.h"
#include "base/text_helper.h"
#include "fonts/gfx_used_fonts.h"
#include "base/fontpack.h"                // g_all_fonts/g_all_font_count + loader
#include "features/modes/midi_legend.h"                 // QK_MIDI_* legends from the synced MIDI settings
#include "base/legend_plan.h"            // the pure keycap legend-SIZE planner
#include "base/ime_key_plan.h"           // what KC_IME sends, per language and OS
#include "base/ime_held_slots.h"         // which KC_IME key holds which usage
// Country flags (NotoColorEmoji_Regular_LangFlags, codepoints FLAG_CP_BASE+glyph)
// now ship in the external-flash font pack, resolved via g_all_fonts — they are
// NOT compiled in. The tiny label font stays resident (no-pack fallback label).
#include "fonts/nano_font.h"          // 10px label font under the flags
#include "fonts/util_font.h"         // mid (10px) utility-label font
#include "polymod_core1.h"
#include "features/anim/tutorial.h"
#include "features/anim/demo_mode.h"              // the showroom demo loop (KC_DEMO)
#include "features/modes/mines_mode.h"                   // the idle-screen mine sweeper (hold Hyper while idle)
#include "features/modes/ai_mode.h"                      // AI mode: agent cards on the number row (KC_AI)
#include "features/anim/key_attention.h"            // cmd 47: the focus ripple on a key
#include "features/anim/host_tutorial.h"            // cmd 49: the host tutorial (text, spotlight, events)
#include "features/anim/focus_ring.h"                 // the reusable "point at this key" ripple
#include "base/tutorial_plan.h"             // TUT_SLOT / TUT_SKIP_HOLD_MS
#include "features/anim/menu_cascade.h"             // menu_cascade_hidden() / _tick()
#include "features/anim/lang_sparkle.h"             // lang_sparkle_tick()
#include "features/anim/tutorial_rgb.h"             // the key LEDs during the show and the lesson
#include "lib/system/boot_diag.h"                    // emit_boot_banner(), splash_progress(), SPLASH_DONE
#include "lib/drivers/crash_record.h"            // crash_record_init(), the watchdog, the phase breadcrumb
#include "dev/diag/usb_stress.h"              // no-op unless -e POLYKYBD_USB_STRESS=yes
#include "base/hand_stamp.h"              // handedness that survives an EEPROM wipe
#include "lib/drivers/hw_uid.h"                  // flash unique ID, read once here (cmd 50)
#include "base/status_brightness.h"      // one brightness scale for keycaps + status OLED
#include "usb_util.h"                     // usb_vbus_state() — the suspend-time power check
#include "dev/diag/crash_test.h"              // POLYKYBD_CRASH_TEST: deliberate faults (no-op inlines otherwise)
#include "lib/split_link/slave_data.h"                   // slave_data_register(), slave_data_crash_pull_tick()
#include "polymod_crc32.h"

// The LTR-559 driver is the polykybd/polymod_ltr559 community module; the build
// defines COMMUNITY_MODULE_POLYMOD_LTR559_ENABLE when a variant lists it, and the
// module probes + polls the part from its own hooks. Everything below is the
// PolyKybd-side POLICY on top of it (auto-brightness, idle-inhibit, telemetry).
#ifdef COMMUNITY_MODULE_POLYMOD_LTR559_ENABLE
#    include "polymod_ltr559.h"
#    define LTR559_LOG_MS 600000   // sensor telemetry log cadence: 10 min
#endif
#include "lib/peripherals/ltr559_policy.h"        // the POLICY on the module (POLYKYBD_LTR559_DRIVE)

#include "lib/system/state.h"
#include "poly_macro.h"
#include "poly_macro_record.h"
#include "lib/display/multicore_exec.h"
#include "lib/split_link/split_sync.h"

#include "lang/lang_lut.h"
#include "base/caps_word_letter.h"
#include "poly_combos.h"
#include "lang/lang_lut_ext.h"

#include "keys_com/layers.h"
#include "keycode_helper.h"
#include "latin_picker.h"         // the Intl latin-variation picker and remap
#include "poly_keymap_internal.h"  // helpers shared with the files split out of this one
#include "doom/doom_mode.h"   // Doom easter egg (inline no-ops unless POLYKYBD_DOOM)
#include "features/anim/startup_anim.h"   // one-time procedural boot animation (split72; no-op stubs on split42)
#include "lib/display/status_idle.h"          // the status panel's idle screen (split72 only)
#include "polymod_os_actions.h"
#include "features/emoji/emoji_layer.h"
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

_Static_assert(KC_L4 - KC_L0 == 4 && _L4 - _L0 == 4, "KC_L0 ... KC_L4 maps onto _L0.._L4 by offset");

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

// The Intl latin table (picks + remap assignments) has no local/global diff
// behind it, so a lost USER_SYNC_LATIN_EX_DATA frame would leave the slave
// drawing stale variations until the next pick. Like the MRU push, it carries
// its own pending flag: latin_publish() tries at once, and
// sync_and_refresh_displays() re-sends until the slave acks.
static bool     s_latin_sync_pending = false;
static uint32_t s_latin_sync_last_try;

// A re-send costs up to `retries` blocking UART attempts on the main loop, so a link
// that answers but keeps failing must not be retried on every housekeeping pass.
#define LATIN_SYNC_RETRY_MS 500

static void latin_sync_send(uint8_t retries) {
    const latin_sync_t* table = get_global_latin_table();
    s_latin_sync_last_try = timer_read32();
    uint8_t ack = send_to_bridge(USER_SYNC_LATIN_EX_DATA, (void*)table, sizeof(*table), retries);
    if (sync_succeeded(ack)) {
        s_latin_sync_pending = false;
    } else {
        uprintf("USER_SYNC_LATIN_EX_DATA failed to send (ack=0x%02x), will retry\n", ack);
    }
}

void latin_publish(void) {
    s_latin_sync_pending = true;
    latin_sync_send(10);
}

/*[[[cog
import cog
import os
from textwrap import wrap
from openpyxl import load_workbook
wb = load_workbook(filename = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(cog.inFile))), "lang", "lang_lut.xlsx"))
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
// Languages past the frozen KCL_ block take their keycode from the tail block after
// KC_IME (keycode_helper.h), so the identity above holds for the first
// KCL_FROZEN_COUNT only. These two convert in both directions for every language.
static_assert((int)LANG_JAUS == KCL_FROZEN_COUNT + (int)(KCL_JAUS - KCL_TAIL_FIRST), "KCL_ tail / LANG_ order drift");
static_assert((int)(KCL_TAIL_LAST - KCL_TAIL_FIRST) == NUM_LANG - KCL_FROZEN_COUNT - 1, "KCL_ tail length != NUM_LANG");

uint16_t kcl_for_lang(uint8_t lang) {
    return lang < KCL_FROZEN_COUNT ? (uint16_t)(KCL_ENUS + lang)
                                   : (uint16_t)(KCL_TAIL_FIRST + (lang - KCL_FROZEN_COUNT));
}

// The LANG index of a KCL_ keycode, or -1.
static int16_t kcl_lang_index(uint16_t keycode) {
    if (keycode >= KCL_ENUS && keycode < KCL_ENUS + KCL_FROZEN_COUNT) {
        return (int16_t)(keycode - KCL_ENUS);
    }
    if (keycode >= KCL_TAIL_FIRST && keycode <= KCL_TAIL_LAST) {
        return (int16_t)(KCL_FROZEN_COUNT + (keycode - KCL_TAIL_FIRST));
    }
    return -1;
}

static enum lang_layer g_lang_init = INIT_LANG;

// keymaps[] / encoder_map[] / g_led_config are variant data, defined in each
// variant's keymaps/default/keymap.c. disp_row_* select the first display of
// row 0 / the second-half start row via macros from the variant header.
// The keymaps[] declaration is in poly_keymap_internal.h.
const struct display_info disp_row_0 = { POLY_DISP_ROW_0 };
const struct display_info disp_row_3 = { POLY_DISP_ROW_3 };


static bool restore_suspended_overlays(poly_sync_t* local_state);
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

// Tracks whether the previous update_displays() pass reached the keycap render (vs
// early-returning for idle / Eden / DOOM). On the mode->render edge we invalidate
// the dirty-window bboxes so the first awake render erases whatever the mode drew.
static bool s_disp_render_active = false;

// ---- the menu cascade's view of this file (features/anim/menu_cascade.h) ----------------
bool poly_render_live(void) { return s_disp_render_active; }

// What the language / emoji menu shows right now, or 0 when neither is up. Built from
// SYNCED state only (the layer, the region and page, the category and page), so the
// slave computes the same value from its own copy and cascades on the same change.
uint32_t poly_menu_signature(void) {
    const uint8_t top = get_highest_layer(get_local_layer()->layer);
    if (top == _LL) {
        return 0x01000000u | ((uint32_t)lang_active_region() << 8) | lang_active_page();
    }
    if (top == _EMJ) {
        return 0x02000000u | ((uint32_t)emj_active_category() << 8) | emj_active_page();
    }
    return 0u;
}

// The level update_displays() gives a keycap: the tutorial's one uniform level while it
// runs, the user's brightness otherwise, 0 while the panels are off.
uint8_t poly_panel_full_contrast(void) {
    const uint8_t c = get_local_state()->contrast;
    if (c == DISP_OFF) return 0u;
    return tutorial_active() ? (uint8_t)POLY_INTRO_CONTRAST : (uint8_t)(c - 1u);
}

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
    if (tutorial_rgb_paint()) return false;   // the first-run show and the lesson
    ai_rgb_paint();   // agent states on the number row; other LEDs keep the effect
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

// Status OLED contrast register for the current moment. Dark while idling (the
// panel is handed to the idle screen — status_idle.c's animation on split72, a blank
// panel on split42),
// otherwise the SAME contrast the
// keycaps are on, mapped onto this panel's range by base/status_brightness.h.
//
// ⚠️ The input is the SYNCED local_state->contrast — the one value both halves
// already agree on — NOT get_active_brightness(). That was the first attempt and
// it left the SLAVE's panel stuck at full: get_active_brightness() reads
// g_user_brightness, which on the slave is only a shadow maintained by one
// conditional in split_sync.c's sync handler. That conditional is an EDGE
// (`incoming->contrast != current->contrast`) that is skipped whenever the sync
// also carries DISP_IDLE or IDLE_TRANSITION — and copy_local_state() advances
// current->contrast on that very same sync, so the skipped change can never be
// seen again. Unlike the periodic state syncs, where the diff IS the retry queue,
// a missed note is missed for good, and the slave keeps the FULL_BRIGHT the
// static initialiser gave it.
//
// The strobe this was guarding against is already handled by `idle`: the pulse
// style cycles contrast 0..49 with DISP_IDLE SET for the whole time, and Eden
// holds EDEN_IDLE_BRIGHTNESS with DISP_IDLE set too, so both return the idle
// level and never see the cycling value. What the synced contrast does add is the
// fade-out: the panel now dims WITH the keycaps over FADE_TRANSITION_TIME instead
// of holding full until the pulse starts, identically on both halves.
static uint8_t status_oled_level(bool idle) {
    // ⚠️ The tutorial owns the status panel's level. Without this, any contrast change
    // during the lesson (the tutorial forces the keycaps' own level on its start edge)
    // ran the branch in sync_and_refresh_displays() and dropped the panel back to the
    // user's mapped level, which tops out at OLED_BRIGHTNESS (60).
    if (tutorial_active() || startup_anim_welcome()) return (uint8_t)POLY_INTRO_STATUS_BRIGHT;
    return idle ? POLY_STATUS_IDLE_BRIGHT : poly_status_brightness(get_local_state()->contrast);
}

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
    uint8_t global_flags = get_global_state()->flags;

    if (is_usb_host_side()) {

        local_flags = set_flag(get_local_state()->flags, DBG_ON, debug_enable);
        access_local_state()->flags = local_flags;

        const bool back_from_idle_transition = flag_turned_on(local_flags, global_flags, IDLE_TRANSITION);
        if (back_from_idle_transition) {
            access_local_state()->contrast = get_active_brightness();
        }

        access_local_state()->emj_category = emj_active_category();
        access_local_state()->emj_page     = emj_active_page();
        {
            poly_sync_t *ls = access_local_state();
            emj_search_export(&ls->emj_search[0], &ls->emj_search[1], &ls->emj_search[2], ls->emj_query);
        }
        access_local_state()->lang_page    = lang_pack_state();
#ifdef MIDI_ENABLE
        access_local_state()->midi_cfg     = midi_cfg_pack(midi_config.octave, midi_config.transpose,
                                                           midi_config.velocity, midi_config.channel);
#endif
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

        if (s_latin_sync_pending && timer_elapsed32(s_latin_sync_last_try) >= LATIN_SYNC_RETRY_MS) {
            latin_sync_send(PERIODIC_SYNC_RETRIES);
        }

        access_local_layer()->led_state = host_keyboard_led_state();
        // ⚠️ The tutorial holds caps INTERNALLY for chapter 1, and this is the only place
        // it can: the line above re-reads the host's real lock state every pass, so an
        // override set anywhere else is erased before update_displays() runs (which it
        // does from further down this same function). It was previously set in the
        // tutorial's own housekeeping branch and never took effect — first because that
        // branch runs before this one, and now because the branch does not run at all.
        //
        // The host's real caps lock is untouched; only the LOCAL snapshot the renderer
        // reads is forced, and only while chapter 1 is up.
        if (tutorial_caps_hold()) access_local_layer()->led_state.caps_lock = true;
#ifdef CAPS_WORD_ENABLE
        // Caps Word shifts the letters, as Caps Lock does, so the legends show capitals
        // while it is on. Display only: the host's Caps Lock is untouched. The slave
        // gets it through the same synced led_state.
        if (is_caps_word_on()) access_local_layer()->led_state.caps_lock = true;
#endif
        // The same for Num Lock on the Num layer: with the host's Num Lock off the keypad
        // keys draw their navigation legends (arrows, Ins, Del), so "hold Num" showed a
        // layer without a single number on it (hardware). Display only, lesson only; the
        // lesson swallows the keypad presses, so nothing typed disagrees with the legend.
        if (tutorial_active() && get_highest_layer(access_local_layer()->layer) == _NL) {
            access_local_layer()->led_state.num_lock = true;
        }
        // The demo's Shift is display-only: OR'd into the snapshot the renderer and the
        // slave read, never registered, so nothing reaches the host.
        access_local_layer()->mods = get_mods() | demo_display_mods();
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

        // ⚠️ This used to be an idle-ENTRY-only oled_set_brightness(0) with no
        // restore on idle EXIT. The only code that raised the panel again was the
        // status_disp_changed branch below, and the idle block in housekeeping sets
        // STATUS_DISP_ON on every pass — so that flag never flips across an
        // idle->wake cycle and the status OLED stayed at contrast register 0 (on an
        // SSD1306 that is "barely visible", not off) until the status display was
        // toggled or the board rebooted. A LONGER absence hid the bug, because
        // poly_suspend() does clear STATUS_DISP_ON and the resume path did restore
        // the brightness: hence "dim for no reason", but only sometimes.
        // Keying the restore off contrast_changed as well as idle_changed fixes it —
        // waking moves contrast from the pulse value back to the active brightness.
        if(idle_changed || contrast_changed) {
            if(status_disp_on) {
                oled_set_brightness(status_oled_level(in_idle_mode));
            }
        }

        if(status_disp_changed) {
            if(status_disp_on) {
                oled_set_brightness(status_oled_level(in_idle_mode));
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
        // A request from here on (core1, or the split thread on the slave) must
        // survive the end of this render. See begin_disp_refresh() in update.h.
        begin_disp_refresh();
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
        finish_disp_refresh();
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
// bare Ctrl tap does nothing in the same apps. LATIN_PICKER_MOD is defined in
// poly_keymap_internal.h, which render_key.c shares.


// Sets layer state variable tracking the active keyboard layer.

layer_state_t layer_state_set_user(layer_state_t state) {
    access_local_layer()->layer = state;
    if(get_highest_layer(state) != _SL) {
        // Leaving settings re-hides the advanced keys, so revealing them is a
        // deliberate act on every visit rather than a mode you can leave armed.
        // That is the whole safety argument for putting QK_BOOT behind the gate.
        access_local_state()->settings_more = 0;
    }
    if(get_highest_layer(state) != _EMJ) {
        // Leaving the emoji layer drops the search query, so the next visit starts
        // with an empty search (the chosen mode stays).
        emj_search_on_layer_exit();
    }
    if(get_highest_layer(state) != _ADDLANG1) {
        // Leaving Intl closes the picker. Without this the latched Ctrl would stay
        // registered with the layer gone, so every following keystroke reaches the
        // host as Ctrl+key.
        latin_picker_latch_release();
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
    // The showroom demo goes first: a flash takes the board, and the demo would otherwise
    // keep moving layers and previews under a screen that housekeeping has frozen.
    demo_stop();               // no-op unless it runs (master only)
    doom_screensaver_stop();   // self-guards: only an active attract demo
    startup_anim_stop();       // looping Eden (and a one-shot mid-flight)
    poly_sync_t* local_state = access_local_state();
    // A flash the host starts while the board is suspended wakes the panels
    // here, not through poly_set_awake_state(): bring the overlays back too.
    restore_suspended_overlays(local_state);
    local_state->flags &= ~((uint8_t)DISP_IDLE) & ~((uint8_t)IDLE_TRANSITION);
    local_state->flags |= STATUS_DISP_ON;
    local_state->contrast = get_active_brightness();
    reset_idle_jitter();       // fresh centred legends, not jittered offsets
    // Restart the idle countdown from the start of the flash — a deliberate host
    // command, so this is real activity by update.h's rule. Unconditional matters:
    // a keyboard 100 s into a 120 s idle timeout was awake (so the old gated
    // stamp never ran) and would fade out 20 s into the transfer. Shorter presets
    // (cmd 40 goes down to 15 s) make that window tighter, not wider.
    update_performed();
    // Momentary/toggle layers off, then back onto the PolyKybd default layout.
    // A bare layer_clear() falls through to QMK's *saved* default layer
    // (default_layer_state), NOT the Poly def_layer — which is a layer INDEX
    // driven through layer_on(), same as the KC_L*/KC_BASE selectors and the
    // boot path — so a Colemak/Neo base dropped to QWERTY here, and that
    // cleared layer state was bridged to the slave, leaving the slave on the
    // QMK default layer after the flash.
    layer_reset_to(access_local_layer()->def_layer);
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

// EDEN_IDLE_BRIGHTNESS lives in base/idle_style.h with the rest of the style table.

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
#if defined(KEYBOARD_polykybd_split72)
        // On the core0 fallback path, take turns with the status panel's idle screen:
        // start no new keycap frame while its frame is still going out over I2C (see
        // status_idle.c). A frame already under way keeps rendering its slices. When
        // core1 computes the keycaps, core0's share is small and there is no turn to take.
        if (!startup_anim_idle_on_core1() && !startup_anim_frame_busy() && status_idle_holds_bus()) return;
#endif
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

// Armed when the first-run experience starts, so Eden's finishing edge knows to hand
// off to the tutorial rather than stamping the marker itself.
static bool     s_tutorial_armed  = false;

// Arm the post-intro hand-off on THIS half, and (on the master) publish the intent on
// the sync so the slave can arm its own.
//
// ⚠️ This is the shape the Eden IDLE loop already has and the tutorial did not, which
// is why one works on both halves and the other did not. eden_idle_tick() never
// receives a "start" message: every pass, each half re-derives `want` from state it
// already holds (`idle_style` + DISP_IDLE, ordinary poly_sync_t fields carried by the
// normal diff-retry sync) and starts or stops its own animation. The boot intro is the
// same idea one level down — keyboard_post_init_user() runs on BOTH halves and each
// reads its OWN EEPROM marker, so a cold boot needs no cross-half message at all.
// Eden's only push is the replay nonce, for the one case with no local trigger.
//
// The tutorial was the opposite: the slave's entry was a single 0->1 edge on tut[0],
// from the master, over a bespoke one-retry push, on a path where the normal state
// sync is skipped. On the KC_EDEN path it was worse than fragile — s_tutorial_armed is
// set in process_record_user(), which only ever runs on the master, so the slave had
// NO local trigger and that one message was the entire mechanism. Losing it left the
// slave dark for the whole session, with the master happily running the lesson.
static void arm_tutorial_after_intro(void) {
    s_tutorial_armed = true;
    poly_tutorial_skip_reset();
    poly_tutorial_forget_passes();
    // Level, not edge: the bit rides every sync for the whole of Eden (seconds), so it
    // has many chances to land rather than one. ⚠️ tutorial_sync_fill() PRESERVES it
    // (the two writers of tut[0] must not fight), so it is retired with the rest of
    // the word at the teardown in poly_tutorial_finish_if_done() — not by the start.
    access_local_state()->tut[0] |= TUT_SYNC_ARMED;
    // Eden says the welcome in its tail and keeps the stars falling until the first
    // letter (startup_anim.c, SA_TAIL_MS). A plain flag: this also runs on the slave's
    // split-protocol thread (poly_arm_tutorial_after_intro), where nothing may touch
    // the panels.
    startup_anim_set_tail(true);
}

// Called from the split handler on the slave when the master's sync says the first-run
// experience is armed. Idempotent, and deliberately NOT tutorial_start(): the slave
// starts the tutorial from its own Eden finish edge, exactly as a cold boot does.
void poly_arm_tutorial_after_intro(void) {
    if (!s_tutorial_armed) arm_tutorial_after_intro();
}

void housekeeping_task_user(void) {
    // Optional loop-timing probe (no-op unless POLYKYBD_LOOP_PROFILE). At the very
    // top so it measures the FULL previous iteration — matrix scan, HID, bridge.
    loop_profile_tick();
    // The watchdog is fed HERE and in suspend_power_down_kb() and nowhere else: a
    // main loop that stops reaching housekeeping for CRASH_WATCHDOG_MS is exactly
    // the hang the crash record exists to catch. The LOOP tag closes whatever
    // phase the previous pass left open (a HID handler tags on entry only).
    crash_watchdog_feed();
    (void)crash_phase_enter(CRASH_PHASE_LOOP, 0);
    usb_stress_task();   // test builds only (POLYKYBD_USB_STRESS)
#ifdef RGB_MATRIX_ENABLE
    flash_rgb_tick();   // light the matrix while a font-pack/firmware flash runs
    tutorial_rgb_tick(); // …and own it through the first-run show and the lesson
#endif
    fw_apply_screen_tick();   // ...and keep the status OLED on the matching firmware screen

    boot_banner_housekeeping_tick();   // re-emit the boot banner for a late console
#ifdef POLYKYBD_BULK_PIPE
    poly_pipe_task();                  // drop a pipe frame the host abandoned mid-way
#endif
    slave_data_crash_pull_tick();      // master: fetch + print the slave's crash record once per link-up

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

    // The firmware apply step machine, the reboot path, deferred staging and the
    // slave's font-pack reload (lib/fw_update/fw_apply.c). True = a stage ran and
    // this pass ends here, one stage per pass, as when the code was inline.
    if (fw_apply_tick()) {
        return;
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
    // On-keyboard macro recording. The master owns the gesture (only it runs
    // process_record -- the slave's matrix is pulled over the link), and the state is
    // mirrored into the synced struct so the SLAVE draws its half of the slot picker.
    // Outside the !fw_up_active gate below for the same reason the FW-2 prompt is: a
    // dialog that could not be drawn or advanced would be unanswerable.
    if (is_keyboard_master()) {
        poly_macro_rec_tick();
        poly_sync_t *rec_state = access_local_state();
        const uint8_t rs = (uint8_t)poly_macro_rec_state();
        const uint8_t rl = poly_macro_rec_slot();
        if (rec_state->rec_state != rs || rec_state->rec_slot != rl) {
            rec_state->rec_state = rs;
            rec_state->rec_slot  = rl;
            request_disp_refresh();
        }
        // Hold the idle countdown off for the whole gesture, the same way the FW-2
        // prompt below does. The panel IS the only indicator a recording has, and
        // update_displays() early-returns once DISP_IDLE is set — so a slow-typed
        // macro that crossed the idle timeout would dim the picker and the REC readout
        // out from under the user, with no way back short of a keypress that the
        // recorder would then capture.
        if (rs != (uint8_t)POLY_REC_IDLE) {
            update_performed();
        }
    }

    if (is_keyboard_master()) {
        fw_staging_confirm_tick();
        // Which dialog the board IS right now. FW-2's unsigned image outranks
        // FW-9's unsigned pack: a flash in flight is the more consequential
        // question, and doom_session_start() refuses to take the pool while the
        // stager owns the link anyway, so the two cannot really coincide.
        const uint8_t want = fw_staging_awaiting_confirm() ? POLY_CONFIRM_FW_IMAGE
                           : doom_pack_confirm_pending()   ? POLY_CONFIRM_DOOM_PACK
                                                           : POLY_CONFIRM_NONE;
        // The slave never sees the keypress, so the master's answer travels with
        // the rest of the synced state (state.h explains why delegating it is
        // sound, and what stays local).
        access_local_state()->doom_pack_auth_crc = doom_pack_auth_crc();
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
    // flash long enough to cross the idle timeout with nobody typing flips the state
    // to idle mid-transfer — the keycaps poly_prepare_for_flash() just made
    // legible then go dark the moment the flash releases the display path (a
    // font-pack flash, which does not reboot, shows this plainly). A flash is a
    // deliberate host command, so this is legitimate activity by update.h's rule.
    // ⚠️ EVERY firmware phase, not just the streaming one. fw_up_active() covers the
    // chunk transfer and nothing else, so staging, the apply and the signing prompt were
    // all free to idle out from under their own screen — seen on hardware. poly_fw_screen()
    // is the one selector that knows about all four.
    //
    // …and the tutorial. It used to call update_performed() from its own housekeeping
    // branch; once it moved onto the normal render path that branch stopped running and
    // the lesson could fade away mid-step. Suppressing idle belongs with "is something
    // on screen that the user did not put there", which is exactly these two.
    if (fw_staging_fw_up_active() || poly_fw_screen() != POLY_FW_SCREEN_NONE ||
        tutorial_active()) {
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
        // Showroom demo (KC_DEMO): the master advances the playlist, both halves move
        // their press highlights. Before the display sync so a new segment's layer and
        // preview go out this pass, and before the idle block so a non-idle segment's
        // update_performed() lands first.
        demo_tick();
        // The idle-screen mine sweeper: holds the idle countdown off while it runs and
        // hands the board back to the idle animation on hide or after 2 min untouched.
        // Before the idle block for the same reason as the demo.
        if (is_keyboard_master()) mines_tick();
        // AI mode's latch timeout (ai_mode.h).
        if (is_keyboard_master()) ai_mode_tick();
        // The segment just entered may preview another language or script: write it
        // BEFORE the render below, or each board is first drawn in the previous
        // segment's language and repainted a pass later (the tutorial's lesson, see
        // poly_apply_draw_script()). Idempotent; the master block repeats it later.
        if (is_usb_host_side() && demo_active()) poly_apply_draw_script();
        // Idle "Eden" screensaver frame tick (IDLE_STYLE_EDEN, both halves). Runs
        // before the boot-animation block below and owns the LOOPING variant; the
        // block below is for the ONE-SHOT boot/KC_EDEN animation only.
#if defined(KEYBOARD_polykybd_split72)
        // The status panel's idle frame, when one is due. BEFORE eden_idle_tick(), so
        // on the pass between two Eden frames the panel composes first and Eden then
        // waits for the flush instead of the two interleaving.
        status_idle_task();
#endif
        eden_idle_tick();
        // An Eden replay the split handler recorded (it may not start one itself).
        split_sync_drain_anim_replay();
        // A keymap reset the split handler recorded (too slow to run there).
        split_sync_drain_keymap_reset();
        // Combos / tap dances the master sent: this half's flash write (cmd 48).
        poly_combos_drain_pending();
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
                // Hand off to the tutorial rather than stamping the marker here. The
                // marker must record "the TUTORIAL was seen", not "Eden played":
                // stamped here, RESET Eden would clear it, replay the animation, and
                // this line would re-stamp it a second later — so the tutorial could
                // never appear, with nothing in any log to say why.
                if (s_tutorial_armed) {
                    s_tutorial_armed = false;
                    poly_tutorial_skip_reset();
                    // ⚠️ RETIRE THE SYNCED LEVEL HERE, not only at the teardown. It has
                    // done its job — the slave armed itself from it during Eden — and
                    // tutorial_sync_fill() PRESERVES it, so left set it rides every sync
                    // for the rest of the session. Two paths then go wrong: the branch
                    // below that finds nothing to teach never reaches the teardown that
                    // clears it, and a later Eden replay re-arms the slave, which starts
                    // a lesson alone and is torn down by the next !ACTIVE packet.
                    // Both halves run this, and both own their own copy of the bit.
                    access_local_state()->tut[0] &= (uint8_t)~TUT_SYNC_ARMED;
                    // The layer stack AND the default layout are parked inside
                    // tutorial_start() -> tutorial_enter_base_layout(), so both halves
                    // do it rather than only the one that owns this arm site.
                    tutorial_start(timer_read32());
                    poly_tutorial_publish_active();
                    if (!tutorial_active()) {                        // nothing to teach
                        core1_hold();            // see the teardown
                        mark_boot_intro_done();
                        core1_release();
                    }
                }
                // Eden ran at full brightness; restore the user's normal
                // brightness behaviour now that it has faded to black, then
                // redraw the real legends and resume normal split sync. Skipped when
                // the tutorial just took the keycaps over — it owns them now.
                if (!tutorial_active()) {
                    set_displays(get_local_state()->contrast, false);
                    request_disp_refresh();
                    sync_and_refresh_displays();
                }
            }
            // While Eden plays we OWN the displays: skip sync_and_refresh_displays
            // (its set_displays() would overwrite our full-bright contrast with the
            // user brightness — the "brightness changes mid-animation" bug) and skip
            // the boot forced-layer-resync's per-pass blocking UART, which also
            // stole frame time. Each half renders its own keycaps independently.
        } else if (tutorial_exclusive()) {
            // ⚠️ CHAPTER 1 ONLY. From chapter 2 the tutorial runs in INTRO mode and
            // falls through to the normal `else` below — the stock mods snapshot, the
            // stock layer sync, the stock render, on both halves. This branch used to
            // carry hand-rolled copies of all three, and each one was a bug: the mods
            // were never refreshed, the layer never reached the slave, and the repaint
            // front covered 12 % of the board. The fix was to stop re-implementing
            // rather than to fix the copies. See base/tutorial_plan.h.
            //
            // The renderer is SLICED (a few ms per pass) precisely so the matrix keeps
            // being scanned mid-frame — it has to notice a tap, which the unsliced Eden
            // path provably cannot.
            tutorial_tick();
            poly_tutorial_push_sync();
            poly_tutorial_finish_if_done();
        } else {
            // INTRO mode rides the normal path: the board renders itself, and the
            // tutorial only advances its own phase machine and keeps the other half in
            // step. tutorial_tick() does NOT touch a panel outside chapter 1.
            if (tutorial_active()) {
                tutorial_tick();
                // The preview for the phase just entered, before anything draws or is
                // pushed (see poly_apply_draw_script()).
                if (is_usb_host_side()) poly_apply_draw_script();
                poly_tutorial_skip_if_held();
                poly_tutorial_push_sync();
                poly_tutorial_finish_if_done();
            }
            sync_and_refresh_displays();
            // After the render, so a change it just drew has already started the cascade.
            menu_cascade_tick();
            lang_sparkle_tick();   // the language preview's twinkles; self-gating
        }
        // The host tutorial (cmd 49): a staged report on the slave, the relay, the
        // watchdog and the layer conditions on the master, the end of a reveal.
        host_tutorial_tick();
        // The host's key attention request (cmd 47) first, then an agent that waits
        // (ai_mode.h): both run the focus ripple, one at a time.
        key_attention_tick();
        ai_attention_tick();
        // Advance the focus ripple, if one is live. Self-gating and bounded to
        // POLY_FOCUS_SLICE_MS, so an idle keyboard pays a single boolean test.
        poly_focus_tick();
        // Drain a pending overlay-mapping repair (armed by enable_overlays when a
        // bridge dropped during an app switch). Master-only — it re-pushes OUR
        // tables to the slave — and bounded per tick, so it can never turn into
        // the multi-second main-loop stall an inline repair would be on a bad link.
        if (is_usb_host_side()) {
            overlay_map_repair_tick();
            // Read the slave's bundle versions for the GET_ID 'V' block (v20).
            // Self-gating: one RPC at boot and after a font-pack COMMIT.
            fw_up_slave_versions_tick();
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
        // Not while Eden or the tutorial owns the displays: the sensor pushes a fresh
        // contrast about twice a second and would dim the intro out from under itself.
        if (!startup_anim_active() && !tutorial_active()) {
            poly_ltr559_drive();   // master-side auto-brightness + idle-inhibit
        }
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

            // The idle delay is a per-board SETTING now (enum poly_idle_timeout, HID
            // cmd 40), read fresh every pass rather than latched: a host that shortens
            // it below the time already elapsed must drop the board into idle on the
            // very next pass, not at the next key press.
            const uint32_t idle_after_ms = get_idle_timeout_ms();
            if(elapsed_time_since_update > idle_after_ms && contrast >= MIN_BRIGHT && (flags & DISP_IDLE)==0) {
                int32_t time_after = elapsed_time_since_update - idle_after_ms;
                int16_t brightness = ((FADE_TRANSITION_TIME - time_after) * get_active_brightness()) / FADE_TRANSITION_TIME;

                //transition to pulsing mode
                if(brightness<=MIN_BRIGHT) {
                    // Log the CONFIGURED style on every transition: three different
                    // styles land in the pulse branch below (pulse, jitter, and an
                    // iddqd that could not start its demo), so an unnamed
                    // "Transition to pulsing" cannot tell you which one is active.
                    const uint8_t            style = get_idle_style();
                    const idle_style_desc_t *desc  = idle_style_desc(style);
                    if (desc->enter == IDLE_ENTER_DOOM && doom_screensaver_start()) {
                        // Doom attract screensaver instead of the pulse: the demo
                        // owns the keycaps at the user brightness — no DISP_IDLE,
                        // and IDLE_TRANSITION stays dropped (cleared above), which
                        // fires the back_from_idle_transition brightness restore.
                        // doom_tick() holds last_update while the demo runs and
                        // hands over to the normal TURN_OFF suspend at its own
                        // deadline.
                        contrast = get_active_brightness();
                        uprintf("Transition to idle [style=%s] - doom screensaver\n", desc->name);
                    } else if (desc->enter == IDLE_ENTER_STEADY) {
                        // A style that draws the keycaps itself (Eden): enter DISP_IDLE
                        // like the pulse, so wake-on-key, the TURN_OFF suspend and the
                        // slave's copy of the flag work unchanged, but hold contrast DIM
                        // and steady. Its renderer owns the pixels every pass and
                        // migrates them itself, so there is no burn-in and no per-pass
                        // pulse contrast to fight.
                        contrast = desc->steady_contrast;
                        flags |= DISP_IDLE;
                        uprintf("Transition to idle [style=%s] - %s screensaver\n", desc->name, desc->name);
                    } else {
                        contrast = DISP_OFF;
                        flags |= DISP_IDLE;
                        flags |= IDLE_TRANSITION;
                        // A DOOM style here means doom_screensaver_start() refused (no
                        // doom build / fw staging active) and we silently degraded to
                        // the legacy pulse — say so rather than reporting a plain
                        // "pulsing" the user never selected.
                        uprintf("Transition to idle [style=%s] - pulsing%s\n", desc->name,
                                desc->enter == IDLE_ENTER_DOOM ? " (doom unavailable)" : "");
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
                const idle_style_desc_t *desc = idle_style_desc(get_idle_style());
                if (desc->enter == IDLE_ENTER_STEADY) {
                    // The style's renderer owns the visuals (Eden: eden_idle_tick());
                    // keep the panel DIM and steady and DON'T compute a pulse contrast
                    // (a per-pass contrast diff would call kdisp_idle() and fight it).
                    contrast = desc->steady_contrast;
                } else {
                    int32_t time_after = PK_MAX(elapsed_time_since_update - idle_after_ms - FADE_TRANSITION_TIME, 0)/300;
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
        // The tutorial's chapter 3 may be previewing a language or script: it writes
        // that over the board's state for a couple of seconds at a time and hands back
        // the script to draw. Outside the tutorial this is get_glyph_script().
        poly_apply_draw_script();
        // Master-authoritative keycap legend size, adopted + re-rendered by the
        // slave the same way.
        if (access_local_state()->glyph_size != get_glyph_size()) {
            access_local_state()->glyph_size = get_glyph_size();
            request_disp_refresh();   // size changed -> re-render every main legend
        }
        // Glyph-script placement (cmd 30 v26), adopted + re-rendered the same way.
        if (access_local_state()->glyph_place != get_glyph_place()) {
            access_local_state()->glyph_place = get_glyph_place();
            request_disp_refresh();
        }
        // Idle style and timeout, every pass, so the KC_IDLE_STYLE / KC_IDLE_TIMEOUT
        // legends on the SLAVE half follow a change made here. The idle block below
        // also writes idle_style, but only while idle tracking runs.
        if (access_local_state()->idle_style != get_idle_style() ||
            access_local_state()->idle_timeout != get_idle_timeout()) {
            access_local_state()->idle_style   = get_idle_style();
            access_local_state()->idle_timeout = get_idle_timeout();
            request_disp_refresh();   // re-render the two setting legends
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
    return to_static_text(kcl_for_lang(lang), none);
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
        case KC_IDLE_TIMEOUT:
        case KC_GLYPH_SCRIPT:
        case LBL_TEXT:
        case KC_TOGMODS:
        case KC_TOGTEXT:
        case QK_BOOTLOADER:
        case QK_REBOOT:
        case QK_DEBUG_TOGGLE:
        case KC_DEADKEY:
        case KC_EDEN:
        case KC_DEMO:
        case KC_DEMO_KEYS:
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

    // MIDI keys show live values (the note an octave change moves, the current
    // velocity), so they read the SYNCED settings, like KC_GLYPH_SIZE_UP below:
    // only the master runs process_midi, and the slave knows them from poly_sync_t.
#ifdef MIDI_ENABLE
    const uint32_t *midi = midi_legend(keycode, get_local_state()->midi_cfg);
    if (midi != NULL) return midi;
#endif

    const uint32_t *lng = lang_display_text(keycode);
    if (lng != NULL) return lng;

    const uint32_t* emoji = keycode_to_emoji(keycode);
    if(emoji!=NULL) {
        return emoji;
    }

    keycode = poly_mt_tap(keycode);

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

        // The input-method key, in the wording a Korean / Japanese keyboard prints.
        // Only reached on ko-KR / ja-JP: elsewhere display_keycode_at() hands the
        // renderer the key KC_IME stands in for (ime_key_stand_in): KC_NUBS, or on
        // the ANSI English layouts Right Alt (KC_RGUI on macOS, which the board draws
        // as Option). On ja-JP it follows Shift, because Shift+tap selects
        // katakana (base/ime_key_plan.c) and the key should say so while it is held.
        // HERE rather than in keycode_to_static_text() for the KC_GLYPH_SIZE_UP
        // reason below: Shift must come from the SYNCED poly_layer_t.mods, or the
        // slave half would never show カナ.
        case KC_IME:
            // Only reached when ime_key_stand_in() gave no plain key: ja-US, and
            // ja-JP on macOS. Elsewhere ja-JP draws as Caps Lock.
            if (poly_ime_family(local_state->lang) == IME_FAMILY_JAPANESE ||
                poly_ime_family(local_state->lang) == IME_FAMILY_JAPANESE_JIS) {
                return (local_layer->mods & MOD_MASK_SHIFT) != 0 ? ICON_KATAKANA : ICON_EISU_KANA;
            }
            return ICON_HAN_YEONG;

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

        // The macro-record key states WHAT IT WILL DO next, which is the whole reason
        // it is here and not in keycode_to_static_text(): the gesture's state is
        // poly_sync_t.rec_state, and that function only receives `led_t`, so on the
        // slave the key would draw "REC" through a recording the master is already
        // taking. Same seam, same reason, as KC_GLYPH_SIZE_UP above.
        //
        // The mid face is ASCII-only and RESIDENT, so this legend renders on a
        // keyboard with no font pack flashed -- which matters more here than
        // anywhere: its neighbours on this row are the macro keys, and a REC key
        // nobody can find is a gesture nobody can start.
        case KC_MACRO_REC:
            return local_state->rec_state == POLY_REC_RECORDING ? MID_TWO_LINE("STOP", "macro")
                                                                : MID_TWO_LINE("REC", "macro");

        // Language selection keycodes: the tiny "xx-YY" code shown under the flag
        // (the flag + selection frame are drawn by render_lang_flag_key()). The KCL_
        // keycodes sit in two blocks (frozen + tail); kcl_lang_index() joins them.
        case KCL_ENUS ... KCL_ENUS + KCL_FROZEN_COUNT - 1:
        case KCL_TAIL_FIRST ... KCL_TAIL_LAST: {
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
                U"ja-US",
                //[[[end]]]
            };
            return lang_code[kcl_lang_index(keycode)];
        }
        default:
            return NULL;
    }
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
    [GLYPH_C64KEYS]  = { 0xEA80u, true  },
};

// The dense mapping relies on the USB-HID keycodes being contiguous
// (KC_A..KC_Z, and KC_1..KC_0 with 0 last); guard that against any future change.
_Static_assert(KC_Z - KC_A == 25, "KC_A..KC_Z must be contiguous for the glyph-script map");
_Static_assert(KC_0 - KC_1 == 9,  "KC_1..KC_0 must be contiguous for the glyph-script map");

// Resolves the override codepoint for a key under the active glyph script, or 0
// when the key/script has no override (falls through to the normal legend). Only
// plain letters/digits are overridden; symbols, function keys, etc. are left alone.
uint32_t glyph_script_codepoint(uint8_t script, uint16_t keycode) {
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

// How far an idle legend with no free space of its own may hang OFF the visible
// window to win itself some anti-burn-in travel (legend_plan_idle_travel). The
// scratch buffer really extends there and kdisp_send_window() streams the window
// only, so the ink is clipped by the panel edge, not by an array bound — but that
// is a property of the geometry, so assert it rather than trusting the number.
#define IDLE_TRAVEL_OVERHANG_PX 3
_Static_assert(IDLE_TRAVEL_OVERHANG_PX <= BUFFER_SLACK_W, "idle travel would run off the west of the buffer");
_Static_assert(IDLE_TRAVEL_OVERHANG_PX <= BUFFER_SLACK_E, "idle travel would run off the east of the buffer");
_Static_assert(IDLE_TRAVEL_OVERHANG_PX <= BUFFER_SLACK_S, "idle travel would run off the south of the buffer");

static bool legend_has_glyph_cb(uint32_t cp, void* ctx) {
    (void)ctx;
    return kdisp_gfx_glyph(g_all_fonts, g_all_font_count, cp) != NULL;
}

static void legend_bbox_cb(const uint32_t* text, int8_t* xmin, int8_t* xmax, int8_t* ymin, int8_t* ymax, void* ctx) {
    (void)ctx;
    kdisp_gfx_text_bbox(g_all_fonts, g_all_font_count, text, xmin, xmax, ymin, ymax);
}

const legend_plan_env_t legend_plan_env = {
    .has_glyph = legend_has_glyph_cb,
    .bbox      = legend_bbox_cb,
    .ctx       = NULL,
    .win_x0    = BUFFER_X,
    .win_x1    = BUFFER_X + SCREEN_WIDTH - 1,
    .win_y1    = SCREEN_HEIGHT - 1,
};


void plan_main_legend(const uint32_t* text, int8_t small_x, int8_t small_y,
                             uint32_t* scratch, uint8_t scratch_cap, main_legend_t* out) {
    legend_plan_main(&legend_plan_env, get_local_state()->glyph_size, text, small_x, small_y,
                     scratch, scratch_cap, out);
}





// Returns builtin icon/symbol overlay text for keycode based on current modifiers and mod-tap states.
// The OS-aware shortcut-hint table itself lives in features/hints/os_hints.c as a PURE
// function. This wrapper is the only thing that knows where the two inputs come
// from, so the table can be exercised directly in a unit test.
const uint32_t* keycode_to_disp_overlay(uint16_t keycode) {
    const uint32_t* hint = os_hint_for_keycode(keycode, get_local_layer()->mods, get_local_state()->active_os);
    // The hint icons are a font-pack font (dev/tools/hint_icons.py, symbol bundle). With
    // no pack flashed their glyphs are absent, and drawing one would show the
    // missing-glyph '!' on every key the modifier touches: show no hint instead, so
    // the key keeps its own legend. A leading control code is a display-list op
    // (the mod-tap badge), whose glyphs are resident.
    if (hint != NULL && hint[0] >= 0x20 && kdisp_gfx_glyph(g_all_fonts, g_all_font_count, hint[0]) == NULL) {
        return NULL;
    }
    return hint;
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

// The keycode's overlay slot (0..89, before the modifier variant), or -1 for a keycode
// that has none. One mapping for copy_overlay_to_buffer() and overlay_present().
static int16_t overlay_key_slot(uint16_t keycode) {
    if(keycode>KC_RGUI || (keycode>KC_NUM_LOCK && keycode<KC_NUBS) || (keycode>KC_APP && keycode<KC_LEFT_CTRL)) {
        return -1;
    }
    uint16_t idx = (keycode>KC_APP) ? (keycode - KC_LEFT_CTRL + 82) : (keycode>KC_NUM_LOCK ? keycode - KC_NUBS + 80 : keycode - KC_A);
    return idx < 90 ? (int16_t)idx : -1;
}

// Whether copy_overlay_to_buffer() would draw an icon on this key. Draws nothing and
// records nothing, so a legend can be placed for the icon before it is drawn.
bool overlay_present(uint16_t keycode, uint8_t mods) {
    const int16_t slot = overlay_key_slot(keycode);
    return slot >= 0 && display_has_overlay(adjust_overlay_idx_to_mod((uint16_t)slot, mods));
}

bool copy_overlay_to_buffer(uint16_t keycode, uint8_t mods) {
    const int16_t slot = overlay_key_slot(keycode);
    if(slot < 0) {
        return false;
    }
    uint16_t idx = (uint16_t)slot;
    // Record that this keycode-slot is on screen (LAYER visibility for the render gate).
    s_displayed_slots[idx >> 3] |= (uint8_t)(1u << (idx & 7));
    idx = adjust_overlay_idx_to_mod(idx, mods);
    // display_has_overlay_bits[] is from-indexed (see set_packed_overlay_mapping): check it
    // here on the display position, before resolving to the pool slot.
    if(!display_has_overlay(idx)) {
        return false;
    }
    const bool dim = display_is_dim(idx);
    const bool no_legend = display_is_no_legend(idx);
    idx = get_display_pool_slot(idx);

    // Overlay images are ROW-MAJOR MSB-first (host: np.packbits over the 40x72 mask),
    // which is what kdisp_draw_bitmap reads — so the courtyard must use the row-major
    // reader. The column-native variant (for font glyphs) reads the same 360 bytes
    // without complaint and dilates a scrambled mask, which punched a big garbage
    // rectangle through the legend underneath (field, 2026-08-01).
    // The courtyard is cut from the FULL icon even when it is drawn dimmed, so the
    // legend keeps the same clearance either way.
    if (no_legend) {
        // v25 (a mapping `to` offset by OVERLAY_MAP_NO_LEGEND_OFFSET): the icon replaces the keycap, so
        // nothing of the legend drawn before this call may remain.
        kdisp_clear_rect(BUFFER_X, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    } else {
        kdisp_clear_rowmajor_courtyard(28, 0, get_overlay(idx), 72, 40, KDISP_CY_DEFAULT);
    }
    if (dim) {
        // v23: a browser's icon under a website's overlay (cmd 33 OVERLAY_MAP_W_DIM).
        kdisp_draw_bitmap_dimmed(28, 0, get_overlay(idx), 72, 40);
    } else {
        kdisp_draw_bitmap(28, 0, get_overlay(idx), 72, 40); //don't understnad why we start at offset 28... need to think about it
    }
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
// FLAG_CP_BASE + LANG_FLAG_GLYPH[LANG_*] (see fonts/gen-lang-fonts.sh); the label uses the tiny
// NotoSans font.
#define FLAG_CP_BASE   0xE000u
#define FLAG_LEFT_X    (BUFFER_X - 2)    // flag glyph left (keeps the left border on-screen)
#define LABEL_COL_X    (BUFFER_X + 66)   // baseline column of the vertical label

// Draw one language key: oversized country flag on the left (vertically centred
// and clipped so the flag content fills the keycap height), language code running
// vertically up the right side (inverted bar when it is the active language).
//
// The flag glyphs (FLAG_CP_BASE + glyph) live in the external-flash font pack, so
// they resolve through g_all_fonts only when a pack is present. With no pack the
// flag is simply omitted — the xx-YY code label below it still identifies the
// language (graceful fallback). The tiny label font stays resident.
const GFXfont* const lang_label_fonts[] = { &NotoSans_Regular_Nano_10px7b };

// Mid (19px) utility font for the no-pack fallback code — between Small and Base,
// so a full "ll-CC" fits on one line (~52px) yet stays readable. Reuse this
// `mid_fonts` array for any misc utility-key text that wants a middle size.
const GFXfont* const mid_fonts[]        = { &NotoSans_Regular_Mid_19px7b };

// --- FW-2 unsigned-image confirmation prompt -------------------------------
//
// Which key on EACH half carries the prompt. The same LOCAL matrix position on
// both halves, so the two keycaps sit symmetrically: the home-row index finger,
// which is findable by touch without hunting across 36 keycaps. The keycap grid
// is not rectangular and the right half folds its display column, but this is a
// matrix position — update_displays' loop indexes the matrix directly, so no
// fold applies. Left half = ACCEPT, right half = REJECT (fixed by side, not by
// which half happens to be master, so the prompt never moves between flashes).
// FW_CONFIRM_ROW / FW_CONFIRM_COL are defined in poly_keymap_internal.h.


// The REC key while the picker is open. Everything else on the board goes dark, so
// without this the one key that can back OUT of the picker is invisible -- the OLED
// says "REC = cancel", but the board is the dialog and the dialog should say it too.
static void render_macro_rec_cancel_key(void) {
    const uint32_t *word = U"cancel";
    int8_t xmin, xmax, ymin, ymax;
    kdisp_gfx_text_bbox(mid_fonts, 1, word, &xmin, &xmax, &ymin, &ymax);
    kdisp_write_gfx_text(mid_fonts, 1,
                         (int8_t)(BUFFER_X + (SCREEN_WIDTH - (xmax - xmin + 1)) / 2 - xmin),
                         (int8_t)((SCREEN_HEIGHT - (ymax - ymin + 1)) / 2 - ymin), word);
}

void render_fw_confirm_key(bool accept) {
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




// One flag glyph per COUNTRY (fonts/gen-lang-fonts.sh): the six Indian languages,
// the four US ones and so on share theirs, so a language's glyph is looked up rather
// than being its enum index.
#include "fonts/generated/lang_flag_glyph.h"
#include "fonts/generated/fontpack_layout.h"
_Static_assert(sizeof(LANG_FLAG_GLYPH) == NUM_LANG,
               "lang_flag_glyph.h is stale: re-run fonts/gen-lang-fonts.sh");

// The flags bundle's slot index, from the generated layout table.
enum {
#define X(id, index, offset, size) LANG_FLAG_SLOT_##id = (index),
    FONTPACK_BUNDLE_LIST
#undef X
};
// The flags bundle that first carries one glyph per country. An older one is still
// one glyph per LANGUAGE, and a keyboard keeps it until the host re-flashes the slot
// on its next connect, so until then the enum index is still the right glyph, except
// for ja-US: it was added after the per-language bundles and has no glyph of its own
// there, so it borrows ja-JP's. (In the table it shares Japan's glyph already, because
// FLAG_ALIAS in gen-lang-fonts.sh files it under JP, its home country.)
#define LANG_FLAG_PER_COUNTRY_VERSION 6u

static uint8_t lang_flag_glyph(uint8_t idx) {
    if (idx >= NUM_LANG) {
        return 0;
    }
    return fontpack_bundle_version(LANG_FLAG_SLOT_flags) >= LANG_FLAG_PER_COUNTRY_VERSION
               ? LANG_FLAG_GLYPH[idx]
               : (idx == LANG_JAUS ? (uint8_t)LANG_JAJP : idx);
}

void render_lang_flag_key(uint8_t idx, const uint32_t* label, uint8_t current_lang) {
    const uint32_t flag_cp = FLAG_CP_BASE + lang_flag_glyph(idx);
    const GFXfont* flag_font = NULL;
    const GFXglyph* g = kdisp_gfx_glyph_font(g_all_fonts, g_all_font_count,
                                             flag_cp, &flag_font);
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
                             flag_cp, 1);   // flags: tight 1px courtyard
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



// MRU recents (emoji or language) get a full-width bar along the TOP edge —
// the mirror image of the category tabs' bottom bar — to set the row apart.
void draw_mru_top_bar(uint16_t keycode) {
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
uint16_t poly_keycode_at(uint8_t layer, uint8_t row, uint8_t col) {
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
uint16_t display_keycode_at(const poly_layer_t* lyr, uint8_t row, uint8_t col) {
    layer_state_t eff = lyr->layer | ((layer_state_t)1 << lyr->def_layer);
    uint8_t layer = get_highest_layer(eff);
    uint16_t kc = poly_keycode_at(layer, row, col);
    if (kc == KC_TRNS) {
        kc = poly_keycode_at(get_highest_layer(eff & ~((layer_state_t)1 << layer)), row, col);
    }
    // KC_IME that stands in for a plain key (NUBS, or Right Alt on the ANSI English
    // layouts) draws as that key: its legend, overlay slot and macOS swap and all.
    // DISPLAY ONLY — the key event keeps KC_IME, and ime_key_stroke() sends exactly
    // ime_key_stand_in()'s key, so the legend and the action cannot disagree.
    if (kc == KC_IME) {
        const poly_sync_t* st = get_local_state();
        const uint8_t stand_in = ime_key_stand_in(poly_ime_family(st->lang),
                                                  st->active_os & POLY_OS_VALUE_MASK);
        if (stand_in) {
            kc = stand_in;
        }
    }
    return kc;
}

// Resolve what a physical position holds ON THE UTILITY LAYER, whatever layer is
// active. The macro slot picker is a modal dialog over _UL's macro row, and it cannot
// use the live stack: KC_MACRO_REC is SWALLOWED on the press, and QMK clears the
// one-shot layer whenever process_record_user() returns false on a press
// (quantum/action.c process_record), so the very tap that opens the picker drops an
// OSL(_UL). Resolving live then finds the BASE layer -- no macro key, no REC key -- so
// every keycap went dark and neither picking a slot nor cancelling was reachable
// (field, 2026-09-08). Both the render and the key-event path go through here, which is
// what keeps them from disagreeing about which key is which.
// The press the open picker is waiting to see released, so an answer is only ever
// taken from a press/release PAIR the picker itself observed.
#define POLY_PICK_NO_PRESS 0xFFu
static uint8_t s_pick_press_row = POLY_PICK_NO_PRESS;
static uint8_t s_pick_press_col = POLY_PICK_NO_PRESS;

static uint16_t macro_picker_keycode_at(uint8_t row, uint8_t col) {
    // ⚠️ Bounds-checked for the same reason keymap_key_to_keycode() is: not every
    // record carries a MATRIX position. An encoder event is row KEYLOC_ENCODER_CW/CCW
    // (253/252) with the encoder index as the column, so while the picker is open a
    // turn of the knob would index the keymap far out of range and answer with
    // whatever that read produced -- which can match a macro key or the REC key.
    if (row >= MATRIX_ROWS || col >= MATRIX_COLS) return KC_NO;
    return poly_keycode_at(_UL, row, col);
}

// Roll a per-glyph idle jitter offset: a uniform random position within the legend's
// OWN on-screen slack, measured from its bounding box at the draw origin (ox/oy). The
// range is glyph-derived — never a global cap — so a slim "i" roams its full free
// width while a wide "w" stays within its small margin, each using all (and only) the
// space it actually has. A global ±N envelope would be counter-productive here: it
// would throttle the slim glyph (lots of slack, but capped) and edge-bias the wide one
// (most rolls clamp to the same boundary).
//
// A glyph with NO slack in an axis used to simply not move in it — which is silent
// and complete failure for exactly the legends that need an idle style most: a 40 px
// tall icon fills the window, so it held the same pixels for the whole idle session.
// Those now borrow up to IDLE_TRAVEL_OVERHANG_PX px of travel by hanging off the
// bottom/left/right edges into the scratch buffer's own slack, which is never sent;
// legend_plan_idle_travel() holds the rule and the reasons. Everything that already
// had room is unaffected, so a legend only ever clips because it had no alternative.
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
    int8_t xlo, xhi, ylo, yhi;
    legend_plan_idle_travel(&legend_plan_env, xmin, xmax, ymin, ymax, IDLE_TRAVEL_OVERHANG_PX,
                            &xlo, &xhi, &ylo, &yhi);
    *dx = (xhi < xlo) ? 0 : jitter_axis(seed, 0x0000u, xlo, xhi);
    *dy = (yhi < ylo) ? 0 : jitter_axis(seed, 0x1000u, ylo, yhi);
}

// Idle (anti-burn-in) per-key relocation: redraws ONLY the resting normal legend — no
// shift/AltGr preview, no overlay image, no tab/MRU chrome — at a fresh random spot
// within THIS glyph's own slack (roll_idle_offset, seeded by `seed`) — fully visible
// unless the legend had no slack at all, in which case it borrows a few px off the
// window's bottom/left/right edges. Renders into the currently selected display's
// buffer, so it works from inside kdisp_idle()'s shift-register walk (the caller selects the key, with the panel
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
    // The draw may land up to IDLE_TRAVEL_OVERHANG_PX px outside the 72x40 window
    // (roll_idle_offset); that ink is inside the scratch buffer and this send ignores it.
    kdisp_send_window();
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
    // (row,col), undoing the right-half display fold (mirrors the host sim's disp_mp).
    uint8_t mr, mc;
    if (!display_index_to_matrix(!is_left_side(), disp_idx, &mr, &mc)) return false;   // phantom col — no OLED behind it
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
    // Draw the legend LIT but with a scanline half-brightness effect (every other buffer
    // row lit) so it reads lighter over the comet field, at a slowly-drifting position
    // within its own on-screen slack. roll_idle_offset() picks a uniform random offset
    // inside the glyph's free space (per-glyph — the same helper the jitter idle style
    // uses, including the few px of off-window travel it lends a legend that has no free
    // space of its own); the seed changes once per EDEN_LEGEND_DRIFT_MS so every
    // ~7 s the letter jumps to a fresh spot. Per-key phase (disp_idx) so they don't all
    // move in lockstep.
    // Same glyph-size plan as render_idle_key() — this is the second idle draw path
    // and it had the identical fixed-origin bug, so fixing only the one a reviewer
    // pointed at would have left the Eden screensaver shrinking the legend instead.
    uint32_t      scratch[GLYPH_SIZE_MAX_LEN + 1];
    main_legend_t plan;
    plan_main_legend(text, BUFFER_X, 23, scratch, (uint8_t)(GLYPH_SIZE_MAX_LEN + 1), &plan);
    uint32_t epoch = timer_read32() / EDEN_LEGEND_DRIFT_MS;
    uint32_t seed  = epoch * 2654435761u + disp_idx;
    int8_t dx, dy;
    roll_idle_offset(plan.text, plan.x, plan.y, seed, &dx, &dy);
    // ⚠️ The scanline phase has to roll TOO, and the drift offset above does NOT do
    // it: scanline_skip_row() gates on absolute buffer y, so the drift slides the
    // glyph past a stationary stripe pattern — it changes which rows OF THE GLYPH are
    // dropped while the lit PANEL rows never move. Left at a fixed phase this lights
    // the even panel rows and only ever the even panel rows, for every idle session
    // the board will ever have: half the panel takes the legend's entire share of the
    // wear and the other half takes none, which is the fixed-pixel burn an idle style
    // exists to prevent. A third salt on the same jitter hash (so uncorrelated with
    // dx/dy) flips it, and rolling it on the same `epoch` puts the flip on the frame
    // the letter jumps anyway — invisible, rather than a shimmer.
    uint8_t sl_phase = (uint8_t)jitter_axis(seed, 0x2000u, 0, 1);
    kdisp_set_gfx_scanline(true, sl_phase);
    kdisp_set_draw_offset(dx, dy);
    kdisp_write_gfx_text(g_all_fonts, g_all_font_count, plan.x, plan.y, plan.text);
    kdisp_set_draw_offset(0, 0);
    kdisp_set_gfx_scanline(false, 0);
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
void draw_legend_cx_cy(const uint32_t* text, int8_t y, int8_t cy_radius) {
    // ⚠️ A legend that MOVEs (\x0E) places its art at ABSOLUTE buffer positions, laid
    // out against the whole cell already, so it is drawn unshifted. Centring shifts the
    // origin, which moves the relatively placed glyphs and not the MOVE'd ones: the
    // context-menu lines slid left under their own pointer on the bottom row (hardware
    // round 34). The scan skips each op's ARGUMENTS, since a coordinate or a size can
    // be the byte 0x0E too (the same trap the bbox walker documents for MOVE).
    for (const uint32_t* p = text; *p; ++p) {
        uint8_t args = 0;
        switch (*p) {
            case U'\x0E':
                kdisp_write_gfx_text_cy(g_all_fonts, g_all_font_count, BUFFER_X, y, text, cy_radius);
                return;
            case U'\x0F': case U'\x11': args = 1; break;   // HALF / THIN glyph
            case U'\x12': case U'\x15': args = 2; break;   // FRAME (w,h) / ROT (step,glyph)
            case U'\x13': args = 3; break;                  // BADGE (w,h,style)
            default: break;
        }
        for (; args > 0 && p[1]; --args) ++p;
    }
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
    // ⚠️ And for the apply / imminent-reset phase: the keycaps were deliberately
    // blanked by poly_board_unusable_cue() because the matrix is never scanned again,
    // and a re-render here paints the full legend set straight back over that blank.
    // It is reachable because the apply sequence RETURNS between stages, and because
    // clear_keyboard() on the way in moves the mods, which makes the very next
    // sync_and_refresh_displays() see a layer diff and request a refresh.
    // s_disp_render_active = false is what makes a REFUSED apply repaint correctly:
    // the blank was an untracked write, so the next awake pass must invalidate every
    // dirty-window bbox rather than diff against a stale one.
    if (fw_staging_board_is_flashing()) {
        s_disp_render_active = false;
        return;
    }
    // Same for chapter 1 of the first-run tutorial — its sliced blitter is the only
    // writer while it is up, and a repaint here would erase the lit letter mid-step.
    // ⚠️ EXCLUSIVE, not active. From chapter 2 the board renders NORMALLY and this
    // function is exactly what draws it; standing down there is what forced the
    // tutorial to re-implement the renderer, which is where four rounds of bugs came
    // from. See the architectural note in base/tutorial_plan.h.
    if (tutorial_exclusive()) {
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
            // Boot only: breadcrumb + watchdog feed for the final handoff render
            // (see boot_diag.h). A bool test per key at every other time.
            boot_render_mark(r, c);
            // ⚠️ DISPLAY index, not LAYOUT_TO_INDEX(r, c). On split72's right half the
            // upper four rows carry no col-0 key, so matrix col c sits behind display
            // col c-1 — and every panel-addressed thing (the chip-select table, the
            // animation geometry, the per-panel dirty-window bbox) is in DISPLAY space.
            // This tracked each right-half panel's bbox under its NEIGHBOUR's index
            // until 2026-09; legends are similar centred boxes so the union happened to
            // cover, and only the focus ripple's thin off-centre arc exposed it as
            // "parts of the ring are not cleared, on the slave". key_display_index() is
            // the one fold — see split72.h.
            uint8_t  disp_idx = key_display_index((uint8_t)(r + offset), c);

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
                    // The tutorial's one uniform level (see set_displays()).
                    kdisp_set_contrast(tutorial_active() ? (uint8_t)POLY_INTRO_CONTRAST
                                                         : (uint8_t)(local_state->contrast - 1));
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
                    } else if (mines_active()) {
                        // The idle-screen mine sweeper: every key is a cell, a
                        // control or dark. Drawn from the SYNCED board, so the slave
                        // draws its half too.
                        mines_render_key((uint8_t)(r + offset), c);
                        kdisp_send_window();
                        doom_handled = true;
                    } else if (ai_render_key((uint8_t)(r + offset), c)) {
                        // AI mode: the number row shows the host's agent cards and
                        // ESC the pager. Every other key renders normally below.
                        // From the SYNCED state, so the slave draws its half too.
                        // The focus ripple composites on top, as on any other key.
                        if (poly_focus_active()) {
                            const sa_geom_t fg = startup_anim_key_geom(!is_left_side(), disp_idx);
                            poly_focus_overlay(disp_idx, &fg);
                        }
                        kdisp_send_window();
                        doom_handled = true;
                    } else if (tutorial_intro_mode() &&
                               tutorial_is_chrome_key((uint8_t)(r + offset), c)) {
                        // The tutorial's chrome: "Hold to / skip..." on Esc, the chapter
                        // on its mirror at the top-right outer edge. A preview name's
                        // letters are chrome too, and cascade in (features/anim/menu_cascade.h):
                        // until its turn such a key stays dark.
                        kdisp_set_buffer(0x00);
                        if (!menu_cascade_hidden((uint8_t)(r + offset), c)) {
                            tutorial_draw_chrome((uint8_t)(r + offset), c);
                        }
                        kdisp_send_window();
                        doom_handled = true;
                    } else if (tutorial_intro_mode() &&
                               !tutorial_key_visible((uint8_t)(r + offset), c)) {
                        // The tutorial's intro mode: the board is drawing NORMALLY, and
                        // the only thing the lesson does is darken what it is not asking
                        // you to look at. A visible key falls straight through to the
                        // ordinary legend path below — real mods, real layer, real
                        // language — which is the whole point of this mode.
                        //
                        // ⚠️ Both halves reach this: update_displays() runs on each, and
                        // each answers tutorial_key_visible() from its own keymap and
                        // the phase it already has from the ordinary tutorial sync. No
                        // extra state crosses the link.
                        kdisp_set_buffer(0x00);
                        kdisp_send_window();
                        doom_handled = true;
                    } else if (host_tutorial_key_hidden((uint8_t)(r + offset), c)) {
                        // The host tutorial's spotlight (cmd 49): every key outside the
                        // host's mask is dark, and during a reveal each key lights as the
                        // front passes it. Like the first-run lesson's intro mode, a dark
                        // key still acts when pressed. Each half answers from its own copy
                        // of the forwarded report.
                        kdisp_set_buffer(0x00);
                        kdisp_send_window();
                        doom_handled = true;
                    } else if (menu_cascade_hidden((uint8_t)(r + offset), c)) {
                        // A menu's content rows appear key by key (features/anim/menu_cascade.h);
                        // until its turn a key stays dark. Asked on both halves, from
                        // each one's own synced menu state.
                        kdisp_set_buffer(0x00);
                        kdisp_send_window();
                        doom_handled = true;
                    } else if (local_state->rec_state == POLY_REC_PICKING) {
                        // The macro slot picker, the same "the board is the dialog"
                        // shape as the prompt above: every keycap goes dark except the
                        // macro keys, which show what they already hold so you pick a
                        // caption rather than a number. Driven off the SYNCED state, so
                        // the slave draws its half of the row too.
                        // Resolved against _UL, NOT the live `keycode` above: opening
                        // the picker drops an OSL(_UL), so the live stack is the base
                        // layer by now. macro_picker_keycode_at() is the same resolver
                        // the key-event path uses, so the two cannot disagree.
                        const uint16_t ul = macro_picker_keycode_at((uint8_t)(r + offset), c);
                        kdisp_set_buffer(0x00);
                        if (ul >= QK_MACRO && ul <= QK_MACRO_MAX) {
                            const uint8_t id = poly_macro_banked_id(
                                (uint8_t)(ul - QK_MACRO),
                                (local_layer->mods & MOD_MASK_SHIFT) != 0);
                            if (id != POLY_MACRO_NONE) {
                                render_macro_picker_key(id, id == local_state->rec_slot);
                            }
                        } else if (ul == KC_MACRO_REC) {
                            render_macro_rec_cancel_key();
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
                        // The language menu's bespoke keys; see render_menu_key().
                        kdisp_set_buffer(0x00);
                        if (render_menu_key(keycode, state, local_state->lang)) {
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
                        } else if (r == MATRIX_ROWS_PER_SIDE - 1 &&
                                   !(display_overlays && !add_lang && !tutorial_active() &&
                                     overlay_present(keycode, mods))) {
                            // Bottom (thumb) row: centre the legend horizontally, unless
                            // an app overlay is drawn on the key below. Then the legend
                            // goes top-left like every other row's, clear of the icon in
                            // the bottom-right (the conditions are the overlay's own).
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
                        // ⚠️ The tutorial joins `add_lang` in the OUTER condition, for
                        // exactly the reason the note above gives: folding it into the
                        // first arm (`display_overlays && !tutorial_active()`) would let
                        // the ELSE fire and paint the hardcoded Ctrl-shortcut hints over
                        // the lesson instead. A lesson's keycaps are the payload — an
                        // app overlay or a hint over them teaches the wrong thing.
                        if(!add_lang && !tutorial_active()) {
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
                        // ⚠️ The focus ripple composites LAST, on top of everything this
                        // key drew — it points AT the legend, so it must not be painted
                        // under it. One boolean test per keycap when no ripple is live,
                        // which is every keypress on a keyboard nobody is pointing at.
                        if (poly_focus_active()) {
                            const sa_geom_t fg = startup_anim_key_geom(!is_left_side(), disp_idx);
                            poly_focus_overlay(disp_idx, &fg);
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
    const idle_style_desc_t *style = idle_style_desc(get_local_state()->idle_style);
    if (style->owns_keycaps) {
        return;
    }
    uint8_t offset = is_left_side() ? 0 : MATRIX_ROWS_PER_SIDE;
#if !defined(POLY_DISP_SELECT_BY_TABLE)
    uint8_t skip = 0;
#endif
    const bool jitter = style->jitter;
    const poly_layer_t* local_layer = get_local_layer();
    const led_t led_state = local_layer->led_state;
    POLY_DISP_SEED(disp_row_0.bitmask);

    //uint8_t idx = 0;
    for (uint8_t r = 0; r < MATRIX_ROWS_PER_SIDE; ++r) {
        for (uint8_t c = 0; c < MATRIX_COLS; ++c) {
            // Display space, as in update_displays() above (see the note there).
            uint8_t  disp_idx = key_display_index((uint8_t)(r + offset), c);

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

// ---- KC_IME (base/ime_key_plan.h) ---------------------------------------------------
// The planner's usages are QMK basic keycodes; pin each name so a wrong hex value in
// the pure header cannot hide behind the tests, which only compare it with itself.
_Static_assert(IME_HID_K     == KC_K,               "IME_HID_K");
_Static_assert(IME_HID_CAPS  == KC_CAPS_LOCK,       "IME_HID_CAPS");
_Static_assert(IME_HID_SPACE == KC_SPACE,           "IME_HID_SPACE");
_Static_assert(IME_HID_NUBS  == KC_NONUS_BACKSLASH, "IME_HID_NUBS");
_Static_assert(IME_HID_INT2  == KC_INTERNATIONAL_2, "IME_HID_INT2");
_Static_assert(IME_HID_INT5  == KC_INTERNATIONAL_5, "IME_HID_INT5");
_Static_assert(IME_HID_LANG1 == KC_LANGUAGE_1,      "IME_HID_LANG1");
_Static_assert(IME_HID_LANG2 == KC_LANGUAGE_2,      "IME_HID_LANG2");
_Static_assert(IME_HID_RALT  == KC_RIGHT_ALT,       "IME_HID_RALT");
_Static_assert(IME_HID_RGUI  == KC_RIGHT_GUI,       "IME_HID_RGUI");
_Static_assert(IME_MOD_LCTL  == MOD_BIT(KC_LCTL),   "IME_MOD_LCTL");
_Static_assert(IME_MOD_LSFT  == MOD_BIT(KC_LSFT),   "IME_MOD_LSFT");
_Static_assert(IME_MOD_LALT  == MOD_BIT(KC_LALT),   "IME_MOD_LALT");
_Static_assert((int)IME_OS_WINDOWS == (int)POLY_OS_WINDOWS && (int)IME_OS_MACOS == (int)POLY_OS_MACOS &&
               (int)IME_OS_LINUX == (int)POLY_OS_LINUX && (int)IME_OS_ANDROID == (int)POLY_OS_ANDROID &&
               (int)IME_OS_LINUX_GNOME == (int)POLY_OS_LINUX_GNOME &&
               (int)IME_OS_LINUX_KDE == (int)POLY_OS_LINUX_KDE,
               "enum ime_os must equal enum poly_os");

uint8_t poly_ime_family(uint8_t lang) {
    switch (lang) {
        case LANG_KOKR: return IME_FAMILY_KOREAN;
        // ja-JP assumes the JIS OS layout, whose Caps key is 英数; ja-US, the same
        // keycaps on the 101-key layout, has no such key and keeps the IME cycle.
        case LANG_JAJP: return IME_FAMILY_JAPANESE_JIS;
        case LANG_JAUS: return IME_FAMILY_JAPANESE;
        // ANSI English layouts: the OS layout is plain US, where NUBS only repeats
        // the Backslash key, so the key earns more as a right-hand Alt. An explicit
        // list on purpose: a NULL NUBS cell in lang_lut is NOT the signal, because
        // ISO layouts with a real <> key (es-ES, it-IT, pl-PL, ...) are NULL too.
        case LANG_ENUS:
        case LANG_ENCA:
        case LANG_ENAU:
        case LANG_ENNZ:
        case LANG_ENIN:
        case LANG_ENPH:
        case LANG_ENSG: return IME_FAMILY_RALT;
        default:        return IME_FAMILY_NONE;
    }
}

// Held KC_IME strokes, per key position (base/ime_held_slots.h).
static ime_slots_t s_ime_held;
// The Japanese mode the key last selected (enum ime_ja_mode). RAM only: after a
// reboot the first press assumes "off" and goes to hiragana, which is right for a
// fresh login and costs one extra press otherwise.
static uint8_t s_ime_ja_mode = IME_JA_OFF;

// Whether the host report still carries `usage`. A modifier lives in the mods byte,
// every other key in the key array (is_key_pressed covers both 6KRO and NKRO).
static bool ime_usage_in_report(uint8_t usage) {
    if (usage >= KC_LEFT_CTRL && usage <= KC_RIGHT_GUI) {
        return (get_mods() & MOD_BIT(usage)) != 0;
    }
    return is_key_pressed(usage);
}
static void ime_press(uint8_t usage) { register_code(usage); }
static void ime_release(uint8_t usage) { unregister_code(usage); }
static void ime_tap(uint8_t usage) { tap_code(usage); }

static const ime_slot_io_t s_ime_io = {
    .key_down  = matrix_is_on,
    .in_report = ime_usage_in_report,
    .press     = ime_press,
    .release   = ime_release,
    .tap       = ime_tap,
};

static void ime_key_record(keyrecord_t* record) {
    if (!record->event.pressed) {
        ime_slots_release(&s_ime_held, record->event.key.row, record->event.key.col, &s_ime_io);
        return;
    }
    // One-shot Shift counts as Shift: QMK's send_keyboard_report() adds pending
    // one-shot mods to the very report this stroke sends (and clears them there),
    // so ignoring it sent Shift+カタカナひらがな — katakana — while recording
    // hiragana, and Shift+LANG1 instead of Ctrl+Shift+K on macOS (Greptile, #355).
    const uint8_t shift_mods = (uint8_t)((get_mods() | get_oneshot_mods()) & MOD_MASK_SHIFT);
    const poly_sync_t* st = get_local_state();
    const ime_stroke_t s = ime_key_stroke(poly_ime_family(st->lang),
                                          st->active_os & POLY_OS_VALUE_MASK,
                                          shift_mods != 0,
                                          &s_ime_ja_mode);
    if (s.usage == 0) {
        return;
    }
    if (s.hold) {
        ime_slots_press(&s_ime_held, record->event.key.row, record->event.key.col, s.usage, &s_ime_io);
        return;
    }
    // A tap with a chord. Add only the modifiers that are not already down, and
    // release only those: Shift+tap for katakana arrives with the user's own Shift
    // held, and releasing it here would drop a key the finger is still holding.
    // drop_shift: lift the user's Shift (real and pending one-shot) for this one
    // stroke and put the real one back after, so the host sees exactly s.mods.
    const uint8_t lifted = s.drop_shift ? (uint8_t)(get_mods() & MOD_MASK_SHIFT) : 0;
    if (s.drop_shift) {
        del_oneshot_mods(MOD_MASK_SHIFT);
        if (lifted) del_mods(lifted);
    }
    const uint8_t add = (uint8_t)(s.mods & ~get_mods());
    if (add) register_mods(add);
    tap_code(s.usage);
    if (add) unregister_mods(add);
    if (lifted) {
        add_mods(lifted);
        send_keyboard_report();
    }
}

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
// Pick language `li` from the language layer and close it. `push_mru` puts it on the
// recents row; the cycle key and the direct per-language selectors do not.
static void poly_select_lang(poly_sync_t* local_state, uint8_t li, bool push_mru) {
    local_state->lang = li;
    if (push_mru) mru_lang_push(li);
    mark_settings_dirty();
    layer_off(_LL);
}

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
                poly_select_lang(local_state, (uint8_t)((local_state->lang + 1) % NUM_LANG), false);
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
        case KC_L0 ... KC_L4:   // KC_L0..KC_L4 and _L0.._L4 are both contiguous
            if (!act) break;
            local_layer->def_layer = (uint8_t)(_L0 + (keycode - KC_L0));
            defer_default_layer_save(local_layer->def_layer);
            layer_reset_to(local_layer->def_layer);
            request_disp_refresh();
            break;
        case KC_BASE:
            if (!act) break;
            layer_reset_to(local_layer->def_layer);
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
            // RESET Eden: clear the boot marker, then play Eden AND the tutorial now.
            //
            // ⚠️ docs/features/tutorial.md §3 asks for "clear the marker, replay only the animation" as
            // the shipping behaviour, with the tutorial waiting for the next boot. That
            // was tried with a Shift modifier for the run-now path, and SHIFT CANNOT BE
            // HELD HERE: KC_EDEN lives on _SL, whose two Shift positions are
            // KC_SETTINGS_MORE and KC_NO, so the tutorial became unreachable from the
            // keyboard ("I only see the animation", hardware). Until the shipping split is
            // decided, the key does both.
            //
            // The marker clear still matters for a cold-boot test: the tutorial re-stamps
            // it only at done/skip, so unplugging mid-lesson leaves it cleared and the
            // next power-up plays the first-run experience.
            //
            // ⚠️ Clearing only THIS half's marker is enough: at boot the master bumps
            // anim_nonce when it starts the intro, and that replays Eden on a slave whose
            // own marker is already consumed (split_sync.c, the anim_replay guard).
            // ⚠️ EEPROM writes run with the QSPI out of XIP; halt core1 across it.
            core1_hold();
            rearm_boot_intro();
            core1_release();
            arm_tutorial_after_intro();
            // Trigger the startup ("Eden") animation NOW on this (master) half and bump
            // the synced nonce so the slave plays in lockstep (the nonce is delivered by
            // the one-shot bridge send in housekeeping, once the transport is up — see
            // sync_and_refresh_displays gating). Input is swallowed while it plays.
            startup_anim_start();
            local_state->anim_nonce++;
            break;
        // Start the showroom demo. On the RELEASE, like every key here, so the
        // demo's swallow never sees this key's own release and leave it half-handled.
        case KC_DEMO:
            if (!act) break;
            demo_start(false);
            break;
        // The key demo: the same loop, and its typing reaches the host (no modifiers).
        case KC_DEMO_KEYS:
            if (!act) break;
            demo_start(true);
            break;
        // Cycle the two display settings that were previously reachable only over HID
        // (cmds 28 / 30) — a keyboard with no host app could not change them at all.
        // Both go through the SAME setter the HID command uses, so the persist +
        // master-authoritative sync behaviour is identical however the change arrives.
        case KC_IDLE_STYLE: {
            if (!act) break;
            // IDDQD is not in the cycle: it is the doom easter egg and has its own
            // way in (typing IDDQD arms the utilities-layer key, doom_mode.c). A
            // board already ON that style still cycles out of it. idle_style.c's
            // in_key_cycle column decides; the unit test pins it.
            set_idle_style(idle_style_next_in_cycle(get_idle_style()));
            request_disp_refresh();
            break;
        }
        // Cycle the idle TIMEOUT. No skipped value, unlike the style above: every
        // preset is a legitimate choice and none of them hides an easter egg. The
        // activity timestamp is deliberately NOT reset — the new delay is measured
        // against the one already running, so cycling down to a timeout shorter than
        // the time since the last keypress idles the board on the next housekeeping
        // pass. ⚠️ That means walking past 15s while the board has been quiet can
        // idle it under your finger; the press itself is activity, so the very next
        // pass restarts the countdown and it wakes again — which is the behaviour a
        // person testing "how short can I make this" expects to see.
        case KC_IDLE_TIMEOUT:
            if (!act) break;
            set_idle_timeout((uint8_t)((get_idle_timeout() + 1u) % IDLE_TIMEOUT_COUNT));
            request_disp_refresh();
            break;
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
                poly_select_lang(local_state, (uint8_t)li, true);
            }
            break;
        }
        // Direct per-language selectors on the language layer: the frozen KCL_ block
        // and its tail, mapped to a LANG index by kcl_lang_index().
        case KCL_ENUS ... KCL_ENUS + KCL_FROZEN_COUNT - 1:
        case KCL_TAIL_FIRST ... KCL_TAIL_LAST:
            if (!act) break;
            poly_select_lang(local_state, (uint8_t)kcl_lang_index(keycode), false);
            break;
        default:
            handled = false;
            break;
    }
    return handled;
}

bool process_record_user(uint16_t keycode, keyrecord_t* record) {
    // Before anything below can swallow this event: drop KC_IME holds that a
    // clear_keyboard() has already released (see ime_slots_forget_cleared).
    ime_slots_forget_cleared(&s_ime_held, &s_ime_io);

    // TEST BUILDS ONLY (-e POLYKYBD_CRASH_TEST=yes): the deliberate-crash chord.
    // FIRST, so it works even while another mode below would swallow the event --
    // the whole point is to be able to fault the board on demand. Compiles to an
    // inline `return false` in every normal build.
    if (crash_test_process_record(keycode, record->event.pressed)) {
        return false;
    }

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

    // The host tutorial's conditions (cmd 49, master only: only the master runs
    // process_record). It observes and never swallows.
    host_tutorial_note_key(record->event.key.row, record->event.key.col, keycode,
                           record->event.pressed);

    // ⚠️ ORDER: the confirm prompt outranks the tutorial. Both are "the board IS the
    // dialog" modes and both swallow every key, so whichever runs first wins — and
    // with the tutorial first its swallow ate the ACCEPT / REJECT presses while the
    // prompt was rendered on the keycaps and the status panel, leaving a dialog
    // nobody could answer. The prompt is the one that must never be unanswerable.
    // FW-2: while the unsigned-image prompt is up the whole board IS the dialog —
    // every key event is swallowed (nothing should reach the host mid-decision)
    // and only the two prompt keys mean anything. Only the master runs
    // process_record — the slave's matrix is pulled over the split link — so a
    // press on EITHER half arrives here, and the matrix row is what says which.
    if (fw_staging_awaiting_confirm() || doom_pack_confirm_pending()) {
        // Answer on the RELEASE, not the press. split72.c's matrix_scan_kb inverts a
        // keycap on press and un-inverts it on release, entirely independently of
        // process_record — so acting on the press tears the prompt down and redraws
        // the normal legend while that keycap is still inverted, and it stays
        // inverted until the finger lifts.
        if (!record->event.pressed && record->event.key.col == FW_CONFIRM_COL) {
            // Route to whichever dialog is up. Both answer functions ignore a
            // call while they are not pending, so the pair is safe even in the
            // window where one has just resolved.
            if (record->event.key.row == FW_CONFIRM_ROW) {
                fw_staging_confirm_answer(true);    // left half  -> A / ACCEPT
                doom_pack_confirm_answer(true);
            } else if (record->event.key.row == FW_CONFIRM_ROW + MATRIX_ROWS_PER_SIDE) {
                fw_staging_confirm_answer(false);   // right half -> R / REJECT
                doom_pack_confirm_answer(false);
            }
        }
        return false;
    }

    // The showroom demo IS the board while it runs: every event is swallowed — nothing
    // reaches the host, not even a modifier, because the demo draws its own Shift and
    // owns the layer stack (clear_keyboard() ran at its start, and it restores the
    // layers on exit). Holding Esc for DEMO_EXIT_HOLD_MS ends it; the exit fires while
    // Esc is still down, and this also swallows that release. Below the confirm prompt,
    // which outranks it (the demo stops itself when a firmware screen comes up).
    if (demo_process_record(keycode, record)) {
        return false;
    }

    // The first-run tutorial IS the board while it runs: every key event is swallowed
    // (nothing should reach the host mid-lesson) and consumed here instead. Swallowed
    // in process_record_user rather than on the release edge — an OSL layer
    // re-dispatches a release-edge action up to three times.
    if (tutorial_active() && !poly_tutorial_tour_passes(record)) {
        return poly_tutorial_record(record);
    }

    // The idle-screen mine sweeper IS the board while it runs: every key event is
    // swallowed and answered there, so nothing reaches the host. Holding Hyper or the AI key for 1 s
    // while an idle animation runs starts it, or resumes the board hidden last
    // (mines_mode.h); a shorter tap only wakes the board.
    if (mines_active()) {
        return !mines_process_record(keycode, record);
    }
    // The mine sweeper's start gesture goes BEFORE AI mode: on the idle screen the
    // AI key (which replaced Hyper on the split72 layouts) starts the game, as Hyper
    // does, instead of turning AI mode on. Away from the idle screen it passes.
    if (mines_try_start(keycode, record)) {
        return false;
    }
    // AI mode (ai_mode.h): KC_AI turns the number row and ESC into the host's agent
    // cards. While it is on every key event is swallowed and answered there, except
    // the releases a held key or a modifier / layer key needs.
    if (ai_process_record(keycode, record)) {
        return false;
    }

    // A macro is interruptible: any PRESS while one is playing stops it. That is the
    // "make it stop" a user reaches for, and it is also what keeps a macro from
    // interleaving its own keystrokes with live typing. The aborting key then falls
    // through and behaves normally -- the abort is not a swallow.
    if (poly_macro_active() && record->event.pressed) {
        poly_macro_abort();
    }

    // While the slot picker is open the board IS the dialog: every key event is
    // swallowed, and only a macro key or the REC key means anything. It sits AHEAD of
    // the KC_MACRO_REC and macro-keycode blocks below so picking a slot arms the
    // recording instead of PLAYING that macro, and so the cancel is answered here --
    // by this point `keycode` is the BASE layer's, because opening the picker dropped
    // the OSL(_UL) that reached the REC key, so neither of those blocks can match.
    //
    // Answered on the RELEASE for the same reason the FW-2 prompt is: matrix_scan_kb
    // inverts a keycap on press and un-inverts on release independently of
    // process_record, so acting on the press tears the picker down while the keycap is
    // still inverted and it stays that way until the finger lifts.
    if (poly_macro_rec_state() == POLY_REC_PICKING) {
        // ⚠️ A MODIFIER falls THROUGH to QMK instead of being swallowed. Shift is what
        // banks the row to M12..M15, and poly_macro_banked_id() reads it from
        // get_mods() -- which only ever moves if process_action() sees the event, so
        // swallowing Shift makes the top four slots unpickable and leaves the render's
        // synced copy of the mods clear as well. A bare Shift tap does nothing on the
        // host, the same reasoning the Intl picker's Ctrl latch rests on.
        // ⚠️ The clear_keyboard() that opened the picker drops a Shift that was ALREADY
        // held, so it has to be pressed again once the picker is up.
        if (IS_MODIFIER_KEYCODE(keycode)) return true;

        // ⚠️ Act only on a release whose PRESS this picker saw. The tap that opens the
        // picker is handled one block down while the state is still IDLE, so its
        // release arrives here and resolves to KC_MACRO_REC -- without this the opening
        // tap would open the picker and immediately cancel it again. Owning the whole
        // press/release pair closes that for every key, not just REC.
        if (record->event.pressed) {
            s_pick_press_row = record->event.key.row;
            s_pick_press_col = record->event.key.col;
        } else if (record->event.key.row == s_pick_press_row &&
                   record->event.key.col == s_pick_press_col) {
            s_pick_press_row = POLY_PICK_NO_PRESS;
            s_pick_press_col = POLY_PICK_NO_PRESS;
            const uint16_t ul = macro_picker_keycode_at(record->event.key.row,
                                                        record->event.key.col);
            if (ul >= QK_MACRO && ul <= QK_MACRO_MAX) {
                const uint8_t id = poly_macro_banked_id((uint8_t)(ul - QK_MACRO),
                                                        (get_mods() & MOD_MASK_SHIFT) != 0);
                if (id != POLY_MACRO_NONE) poly_macro_rec_pick(id);
            } else if (ul == KC_MACRO_REC) {
                poly_macro_rec_toggle();   // the picker's own way out
            }
        }
        return false;
    }

    // The record gesture. SWALLOWED on the press, never left to the release edge:
    // _UL is entered with OSL(), where a release-edge action fires up to three times
    // (process_action's do_release_oneshot) -- for a start/stop toggle that is start,
    // stop, start, i.e. the recording you just made is thrown away and a new one
    // begins. Same rule the settings keys follow, and the reason they were moved.
    if (keycode == KC_MACRO_REC) {
        if (record->event.pressed) {
            // Anything the finger is holding is released BEFORE the mode changes, for
            // the reason doom_begin() and the FW-2 prompt do it: the picker swallows
            // the RELEASE of a key that was already down, so the host keeps it
            // registered and auto-repeats it. It also stops the modifier that reached
            // this key from being the first thing a recording captures.
            clear_keyboard();
            // This press is NOT seen by the picker block above (the state is still
            // IDLE here), so start the session with no pending press -- its release
            // must not be read as an answer.
            s_pick_press_row = POLY_PICK_NO_PRESS;
            s_pick_press_col = POLY_PICK_NO_PRESS;
            poly_macro_rec_toggle();
        }
        display_wakeup(record);
        return false;
    }

    // Macro keycodes (QK_MACRO_0..QK_MACRO_MAX). Nothing else in the build consumes
    // them -- via.c is the only core dispatcher and VIA_ENABLE is unset -- so they are
    // ours, and they are SWALLOWED here rather than handled on the release edge: an
    // OSL() layer re-dispatches a release-edge action up to three times
    // (process_action's do_release_oneshot), which for a macro would mean playing it
    // two or three times over.
    if (keycode >= QK_MACRO && keycode <= QK_MACRO_MAX) {
        if (record->event.pressed) {
            // The LIVE modifier state here, not the synced snapshot the legend uses:
            // this runs on the master at the instant of the press and must follow the
            // finger, while the legend must render identically on a half that only sees
            // the housekeeping snapshot. Same deliberate asymmetry as KC_GLYPH_SIZE_UP.
            const uint8_t id = poly_macro_banked_id((uint8_t)(keycode - QK_MACRO),
                                                    (get_mods() & MOD_MASK_SHIFT) != 0);
            if (id != POLY_MACRO_NONE) {
                // ⚠️ Release everything the FINGER is holding before the macro types.
                // Reaching the second bank means holding Shift, and playback replays
                // through register_code/tap_code -- so without this the host applies
                // that Shift to every keystroke the macro sends and M12..M15 type in
                // caps. The physical release afterwards unregisters an already-clear
                // modifier, which is harmless.
                clear_keyboard();
                poly_macro_start(id);
            }
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

    // The input-method key. Swallowed on BOTH edges: the press sends (or holds) the
    // stroke, the release lets go of a held one. Never on the release edge alone — on
    // an OSL() layer that edge fires up to three times, and a toggle fired three
    // times is a toggle fired once with two stray switches. AFTER the Doom hook, so a
    // game swallows it like any other key instead of sending Right Alt or an IME
    // shortcut to the host (Greptile on #355).
    if (keycode == KC_IME) {
        ime_key_record(record);
        display_wakeup(record);
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
        // The settings layer's four effect presets. These are the legacy UNDERGLOW
        // mode keycodes (RGB_MODE_PLAIN/BREATHE/RAINBOW/SWIRL, 0x782B..0x782E), and
        // QMK does route that range through process_underglow() even on an
        // RGB-matrix-only board — but its switch only covers toggle / next / prev /
        // hue / sat / val / speed. The four mode presets have no case there, none in
        // process_rgb_matrix() (which owns the RM_* range only), and IS_RGB_KEYCODE /
        // RGB_KEYCODE_RANGE are defined in keycodes.h and dispatched nowhere. So
        // these keycaps drew a legend (keycode_helper.c: "Plan", "Brth", "Rnbw",
        // "Swrl") and did nothing at all. Map each onto the closest effect that is
        // enabled on BOTH variants — CYCLE_SPIRAL is split72-only, so Swirl takes
        // CYCLE_PINWHEEL. rgb_matrix_mode() persists the pick the way RM_NEXT does
        // (a deferred eeconfig flag, not a blocking write), and like RM_NEXT it is a
        // no-op while RGB is off.
        case RGB_M_P:  case RGB_M_B:
        case RGB_M_R:  case RGB_M_SW:
            if (record->event.pressed) {
                uint8_t mode = RGB_MATRIX_SOLID_COLOR;              // RGB_M_P  "Plan"
                if      (keycode == RGB_M_B)  mode = RGB_MATRIX_BREATHING;              // "Brth"
                else if (keycode == RGB_M_R)  mode = RGB_MATRIX_RAINBOW_MOVING_CHEVRON; // "Rnbw"
                else if (keycode == RGB_M_SW) mode = RGB_MATRIX_CYCLE_PINWHEEL;         // "Swrl"
                rgb_matrix_mode(mode);
                request_disp_refresh();
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

    // The Intl remap prompt, the remap key and the picker latch's release
    // (latin_picker.c). Before the settings gate, as when this code was inline here.
    bool intl_ret;
    if (latin_picker_record_early(keycode, record, addlang, &intl_ret)) {
        return intl_ret;
    }
    // A gated settings key is BLANK, so it must also be INERT — swallow both edges
    // before anything can act on it.
    //
    // ⚠️ MUST stay above the QK_BOOTLOADER / QK_REBOOT cases below. This file
    // intercepts both of those and returns true from inside that switch (the
    // bootloader announce; the reboot's bridged handoff so the slave restarts too),
    // so a gate placed after them never ran for the only two keys on the row that
    // cannot be undone — the blank Restart keycap rebooted the board (field
    // 2026-09-08). QK_DEBUG_TOGGLE really is left to process_action(), which is why
    // it alone was gated. Every other gated keycode is handled further down in
    // poly_custom_key_action(), already below this point.
    if (settings_more_hidden(keycode)) {
        return false;
    }

#ifdef AUTO_SHIFT_ENABLE
    // The Auto Shift keycodes change the persisted setting (cmd 44), not only QMK's
    // live state, so the host and the next boot see the same value. Swallowed on
    // both edges, acted on at the press, before process_auto_shift() sees them.
    // AS_UP/AS_DOWN step 10 ms, not QMK's 5, to stay on the setting's grid.
    // AS_RPT (types the timeout) is left to QMK.
    switch (keycode) {
        case AS_TOGG:
        case AS_ON:
        case AS_OFF:
        case AS_UP:
        case AS_DOWN:
            if (record->event.pressed) {
                if (keycode == AS_UP || keycode == AS_DOWN) {
                    set_poly_auto_shift_ms(auto_shift_ms_step(get_poly_auto_shift_ms(), keycode == AS_UP));
                } else {
                    set_poly_auto_shift(keycode == AS_ON || (keycode == AS_TOGG && !get_poly_auto_shift()));
                }
                request_disp_refresh();
            }
            return false;
        default:
            break;
    }
#endif

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
                // — plain reset key, no firmware apply).  fw_up_send_slave_reset()
                // carries the hardened retry policy for every reboot handoff.
                poly_reset_sync_t reboot_msg = { .crc32 = 0, .magic = POLY_RESET_MAGIC,
                                                 .action = RESET_ACTION_REBOOT };
                uint8_t ack = fw_up_send_slave_reset(&reboot_msg);
                uprintf("Master: slave reboot ack=0x%02x\n", ack);
                return true;   // let QMK's QK_REBOOT handler reset the master
            }
            default:
                break;
        }
        // The Intl letters, the picker latch and the open picker (latin_picker.c).
        if (latin_picker_record_pressed(keycode, addlang, &intl_ret)) {
            return intl_ret;
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
    // The tutorial runs every keycap at the one first-run level (POLY_INTRO_CONTRAST),
    // whatever brightness is set or pushed meanwhile; OFF (suspend) still turns them off.
    // The finish edge calls this again once the tutorial is no longer active, which is
    // what hands the user's own level back.
    if (!idle && contrast != DISP_OFF && tutorial_active()) {
        contrast = (uint8_t)(POLY_INTRO_CONTRAST + 1u);   // this function stores level+1
    }
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

void poly_set_awake_state(poly_sync_t* local_state) {
    local_state->contrast = get_active_brightness();
    local_state->flags &= ~((uint8_t)DISP_IDLE);
    local_state->flags |= STATUS_DISP_ON;
    reset_idle_jitter();   // fresh, centred idle session next time
    restore_suspended_overlays(local_state);
}

// Wake the board out of idle (or out of a suspend that left the status display off)
// without a keypress: the host's "stop idle" (HID cmd 15) and the demo's end of an idle
// segment. ONE copy, because a wake that forgets one of these leaves a dark half: the
// DOOM attract screensaver runs with DISP_IDLE clear, so only the explicit stop reaches
// it; the pulsing contrast must be put back or the woken legends stay dark.
void poly_wake_from_idle(void) {
    poly_sync_t* local_state = access_local_state();
    doom_screensaver_stop();
    if ((local_state->flags & (STATUS_DISP_ON | DISP_IDLE)) == 0) {
        suspend_wakeup_init_kb();
        return;
    }
    if (local_state->flags & DISP_IDLE) {
        // Contrast is cycling 0-49 during pulsing; restore the active brightness
        // (host-auto value or user brightness) so display_wakeup() conditions don't
        // leave the display dark.
        local_state->contrast = get_active_brightness();
    }
    local_state->flags &= ~((uint8_t)DISP_IDLE);
    local_state->flags |= STATUS_DISP_ON;
    reset_idle_jitter();   // fresh, centred idle session next time
    request_disp_refresh();
    update_performed();
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
        poly_set_awake_state(local_state);
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

// QMK's notification CALLBACK, fired from set_unicode_input_mode() /
// unicode_input_mode_init() / the cycle keys. It mirrors the mode into the synced
// state so the language layer's Mac/Lnx/Win/WinC/BSD keycaps can draw their ON/OFF
// switch, and nothing else.
// WARNING: never call this to CHANGE the mode — it does not touch
// unicode_config.input_mode, so the keycaps would advertise a mode the keyboard
// does not type in. Use set_unicode_input_mode() (see hid_com.c case 20).
void unicode_input_mode_set_user(uint8_t unicode_mode) {
    // Every mode change ends MacCompose; cmd 20 value 6 sets it again AFTER
    // applying the macOS mode (hid_com.c).
    mac_compose_set(false);
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
    display_message(1, 1, digit, poly_heavy_font());
}
#endif

// ---- keyboard_post_init_user(), in the order it runs -----------------------------
// Each step is called once, from keyboard_post_init_user() below. The order is the
// boot order; the splash and sub-step numbers in the steps are what a hang report names.

// The tables that come from flash and EEPROM before anything draws: MIDI octave,
// handedness repair, combos, macro labels.
static void post_init_tables(void) {
#ifdef MIDI_ENABLE
    // QMK starts at octave index 4 (OCTAVE_2), where MI_C plays MIDI note 48, C3. One
    // index higher puts MI_C on middle C (60, C4), which is what the MIDI layer's home
    // row is drawn as. setup_midi() already ran from protocol_pre_init().
    midi_config.octave = QK_MIDI_OCTAVE_3 - MIDI_OCTAVE_MIN;
#endif
    // (The previous run's crash record was captured and archived at the top of
    // keyboard_pre_init_user(); the boot banner reports it.)
    // Put the EEPROM handedness byte back when the flash stamp outvoted it.
    // Nothing in this build READS that byte (config.h drops EE_HANDS), so this is
    // purely so a downgrade to firmware predating the stamp still comes up on the
    // right side. Deliberately not in pre_init: quantum_init() runs between the
    // two and erases the whole store when the magic reads invalid, which is
    // exactly the state a wipe leaves behind. Ahead of emit_boot_banner() below,
    // so the banner can report the repair.
    poly_hand_post_init();
    poly_combos_init();   // cmd 48: this half's flash sector into QMK's tables
    // Labels live in RAM on both halves (the render path reads one per macro keycap per
    // refresh). Each half loads its own EEPROM copy, then the master overwrites the
    // slave's over the link -- so a role swap makes whichever half the host talks to
    // the authority, with no handedness bookkeeping.
    poly_macro_labels_load();
    // Give the slots nothing has claimed their stock look, so a keyboard that has never
    // met the host app still shows sixteen tellable-apart macro keycaps rather than
    // sixteen "M<n>" ones. Writes EEPROM only on the boot that finds a slot empty.
    poly_macro_seed_defaults();
    // Queue them all. Nothing detects "the link is up" here and nothing needs to: the
    // sync tick only clears a label's bit on a real ACK, so the queue simply drains
    // once the slave starts answering. Same shape as the state diff being its own
    // retry queue.
    poly_macro_labels_mark_all_dirty();
#ifdef FW_UP_BOOT_TRACE
    boot_trace(U"1");
#endif
}

// QMK debug flags (debug_enable: POLYKYBD_DEBUG builds start with it on) and the
// pointing device CPI.
static void post_init_debug_flags(void) {
    // Customise these values to desired behaviour
    // SECURITY: default OFF. debug_enable now gates keystroke logging to the HID
    // console (process_record_user) and the host-driven active-control HID commands
    // (cmd 14 key injection, cmd 23 bootloader entry). Enable at runtime with the
    // DB_TOGG key (physical access required) when you actually want demo/debug.
    // A POLYKYBD_DEBUG build (rules.mk, test builds only) starts with it on.
#ifdef POLYKYBD_DEBUG
    debug_enable = true;
#else
    debug_enable = false;
#endif
    debug_matrix = false;
    debug_keyboard = false;
    debug_mouse = false;

    //pointing_device_set_cpi(20000);
#if defined(POINTING_DEVICE_ENABLE)
    pointing_device_set_cpi(650);
#endif
    //pimoroni_trackball_set_rgbw(0,0,255,100);
}

// The default layer, this half's role and side, the boot banner (splash step 2) and
// the LTR-559 report.
static void post_init_layers_and_side(void) {
    layer_state_t default_layer = persistent_default_layer_get();
    access_local_layer()->def_layer = default_layer;
    access_local_state()->unicode_mode = get_unicode_input_mode();
    layer_reset_to(default_layer);
    g_force_layer_resync = true;   // push this boot's default layer to the slave
    g_force_resync_tries = FORCE_LAYER_RESYNC_TRIES;  // (re-arm the bounded budget)

    //set these values, they will never change
    set_com_state(is_keyboard_master() ? USB_HOST : BRIDGE);
    set_side(is_keyboard_left() ? LEFT_SIDE : RIGHT_SIDE);

    emit_boot_banner();   // one-shot at boot; housekeeping re-emits for a late console
    crash_record_emit_lines();   // `crash: side=master ...` when this boot followed a crash
    crash_test_announce();       // TEST BUILDS ONLY: list the deliberate-crash chords

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
}

// Encoder pins, the emoji/language/MRU tables, the overlay pool, and core1 (splash
// steps 3 to 5).
static void post_init_peripherals_and_core1(void) {
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
    g_core1_entered = 0u;
    multicore_launch_core1();
#endif
#ifdef FW_UP_BOOT_TRACE
    boot_trace(U"3");
#endif
    splash_progress(5);                 // core1 up
}

// The split-link RPCs and fw-staging (boot sub-steps 1 to 4, splash step 6).
static void post_init_split_rpcs(void) {
    // ⚠️ Sub-steps, because 63% -> 75% is the gap a boot hang has repeatedly landed
    // in and "somewhere in these four calls" is as far as the milestone can narrow
    // it. Everything here is cheap on paper — array writes and XIP reads — which is
    // exactly why the hang is interesting: it points at core1, launched immediately
    // above, rather than at the calls themselves. The sub-step is what turns the
    // next occurrence into a name instead of a photograph of a percentage.
    boot_substep(1, 4);                    // core1 launched, about to register RPCs
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
    boot_substep(2, 4);         // the 11 poly RPCs are registered
    slave_data_register();   // USER_SYNC_SLAVE_DATA: LTR-559 sensor pull + the slave crash record
#ifdef POLY_DUMMY_TXN_TEST
    // Root-cause experiment: register 3 no-op transactions so NUM_TOTAL_TRANSACTIONS
    // grows by 3 (matching the working pointing build) without the pointing device.
    // They are never executed — only their existence changes the transaction count.
    transaction_register_rpc(USER_SYNC_DUMMY1, user_sync_dummy_handler);
    transaction_register_rpc(USER_SYNC_DUMMY2, user_sync_dummy_handler);
    transaction_register_rpc(USER_SYNC_DUMMY3, user_sync_dummy_handler);
#endif

    boot_substep(3, 4);                    // slave_data_register() returned
    fw_staging_init();
    boot_substep(4, 4);                    // fw_staging_init() returned (apply-log + done-record read)
    splash_progress(6);                 // split RPCs registered, fw-staging up
}

// The persisted settings from the user EEPROM block: language, brightness, idle,
// tap terms, Auto Shift, glyph script and size, emoji search mode.
static void post_init_load_settings(poly_eeconf_t* ee) {
    poly_sync_t* local_state = access_local_state();
    local_state->lang = ee->lang;
    local_state->contrast = ee->brightness;
    note_user_brightness(ee->brightness);
    // If host-auto brightness was engaged before the reboot, come up in auto mode
    // at the last auto value instead of the deliberate manual brightness (which the
    // host never refreshes, so a stale low manual value would otherwise show until
    // the host re-engages). Overrides local_state->contrast when auto was on.
    load_auto_brightness(ee->auto_brightness);
    note_idle_style(ee->idle_style);
    note_idle_timeout(ee->idle_timeout);   // BEFORE the banner below, which prints it
    // The tail bytes (tapping_term..emj_search_mode) may hold old keymap data on a
    // board older than 2026-08-13 (state.h, tail_fmt): use their "never chosen"
    // value and stamp the marker, once.
    if (ee->tail_fmt != TAIL_FMT_OK) {
        uprintf("Settings tail unmarked (tail_fmt 0x%02X): tap terms and emoji search mode reset to defaults\n",
                (unsigned)ee->tail_fmt);
        ee->tapping_term    = 0;
        ee->flow_tap_term   = 0;
        ee->emj_search_mode = 0;
        stamp_tail_fmt();
    }
    note_poly_tap_terms(tapping_term_unpack(ee->tapping_term), flow_tap_term_unpack(ee->flow_tap_term));
    // auto_shift is after tail_fmt, so it has its own marker (state.h, tail2_fmt).
    if (ee->tail2_fmt != TAIL2_FMT_OK) {
        ee->auto_shift = 0;
        stamp_tail2_fmt();
    }
    note_poly_auto_shift(auto_shift_unpack_on(ee->auto_shift), auto_shift_unpack_ms(ee->auto_shift));
    emit_idle_config();   // the style is only known here — the banner tick re-emits it
    note_glyph_script(ee->glyph_script);
    note_glyph_size(ee->glyph_size);
    note_glyph_place(ee->glyph_place);
    note_emj_search_mode(ee->emj_search_mode);
}

// The stored keymap's format check and its one-time migrations.
static void post_init_keymap_storage(poly_eeconf_t* ee) {
    // The dynamic keymap is stored BY LAYER INDEX and QMK does not version it, so a
    // build whose layer enum has shifted would read the previous occupant of every
    // slot above the change. Discard it once, then stamp the revision so the next
    // boot is a no-op. Done here rather than in eeconfig_init_user() because the
    // user datablock and the keymap are separate blocks with separate lifetimes —
    // the keymap survives a user-data re-init, which is exactly the case that would
    // otherwise slip through.
    // Timed and reported on the banner tick (note_keymap_storage) as well as printed
    // here: this discard rewrites the capped keymap, the encoder map and the whole
    // macro region -- a few kB of wear-levelled EEPROM -- inside post_init. USB is
    // already connected by then (protocol_pre_init() runs before keyboard_init()), so
    // the host is enumerating while this writes; a wear-levelling erase here masks
    // the USB interrupt for tens of ms (docs/build/upstream-patches.md -> "ChibiOS-Contrib RP2040
    // USB driver"). No console is attached yet either: the one-shot uprintf below is
    // usually lost, so a board that spends a long time here (or never leaves) looks
    // simply dead.
    const uint8_t  stored_fmt = ee->keymap_layers_fmt;
    const bool     need_reset = (stored_fmt != KEYMAP_STORAGE_CURRENT);
    const uint32_t reset_t0   = timer_read32();
    if (need_reset) {
        uprintf("Keymap layer enum changed (fmt %u) - resetting dynamic keymap\n",
                (unsigned)stored_fmt);
        // The late-boot watchdog guard is armed here (boot_diag.c, from step 5): give
        // this one-time rewrite of a few kB of EEPROM the full CRASH_WATCHDOG_MS.
        crash_watchdog_feed();
        dynamic_keymap_reset_poly();
        stamp_keymap_layers_fmt();
    }
    note_keymap_storage(stored_fmt, need_reset,
                        need_reset ? timer_elapsed32(reset_t0) : 0);
    // The split72 default layouts have the AI key where Hyper was. A stored keymap
    // takes priority over the defaults, so a board that stored one gets the change
    // once here: only keys that still hold Hyper at those positions change, the rest
    // of the keymap and the macros stay (state.h, keymap_mig).
    if (ee->keymap_mig < KEYMAP_MIG_AI_KEY) {
        const uint8_t moved = dynamic_keymap_migrate_poly(KC_HYPR, KC_AI);
        stamp_keymap_mig(KEYMAP_MIG_AI_KEY);
        note_keymap_migration(KEYMAP_MIG_AI_KEY, moved);   // printed on the banner tick
    }
    // Fn + the key above the right Shift opens the MIDI layer (split72 default, the
    // key the host's MIDI guide points at). A stored Fn layer still has it
    // transparent: change it there once, and only where it is still transparent.
    // Only on _FL: the utility layer has TO(_ML) too, and a transparent key a user
    // put there stays.
    if (ee->keymap_mig < KEYMAP_MIG_FN_MIDI) {
        const uint8_t moved = dynamic_keymap_migrate_layers_poly(_FL, _FL + 1, KC_TRNS, TO(_ML));
        stamp_keymap_mig(KEYMAP_MIG_FN_MIDI);
        note_keymap_migration(KEYMAP_MIG_FN_MIDI, moved);
    }
}

// The OS state and the synced fields the first render reads.
static void post_init_seed_state(poly_eeconf_t* ee) {
    poly_sync_t* local_state = access_local_state();
    // Restore the active-OS state (auto/manual + last known OS). Auto by default, so
    // a fresh EEPROM re-resolves per host via detection / host push; a manual pin
    // (e.g. Android) sticks. Seed local_state->active_os so the first render before
    // housekeeping runs already reflects the restored OS.
    load_os_state(ee->os_state);
    // Seed with the same OS|auto-flag encoding housekeeping uses, so the first
    // render/sync before housekeeping runs shows the correct auto/pin badge.
    local_state->active_os = (uint8_t)(get_active_os() | (get_os_auto_mode() ? POLY_OS_AUTO_FLAG : 0));
    // Seed the glyph-script override so the first render reflects the persisted script.
    local_state->glyph_script = get_glyph_script();
    local_state->idle_style   = get_idle_style();
    local_state->idle_timeout = get_idle_timeout();
    // Same for the legend size, so the first render is already at the chosen size.
    local_state->glyph_size = get_glyph_size();
    local_state->glyph_place = get_glyph_place();
#ifdef RGB_MATRIX_ENABLE
    local_state->flags = set_flag(STATUS_DISP_ON, RGB_ON, rgb_matrix_is_enabled());
#else
    local_state->flags = STATUS_DISP_ON;   // no RGB on this variant
#endif
}

// The persisted Intl latin picks and assignments: migrate and normalise.
static void post_init_latin_table(poly_eeconf_t* ee) {
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
    if(ee->latin_pick_migrated == LATIN_PICK_ASSIGN_OK) {
        memcpy(latin_table->assign, ee->latin_assign, LATIN_ASSIGN_BASE_BYTES);
    } else {
        // Includes LATIN_PICK_INTERLEAVED, whose stored map is the persisted
        // all-zero garbage described in state.h — discarding it is the recovery.
        memset(latin_table->assign, LATIN_ASSIGN_FILL, sizeof(latin_table->assign));
        latin_normalised = true;           // re-stamp at the next flush
    }
    if(ee->latin_pick_migrated == LATIN_PICK_ASSIGN_OK ||
       ee->latin_pick_migrated == LATIN_PICK_INTERLEAVED) {
        memcpy(latin_table->ex, ee->latin_ex_wide, LATIN_PICK_BASE_BYTES);
    } else if(ee->latin_pick_migrated == LATIN_PICK_MIGRATED) {
        // Re-index case-BLOCKED (case*26 + letter) to case-INTERLEAVED (slot*2 +
        // case). Same 52 values, same width -- only the field numbering moves, so
        // every pick is preserved.
        memset(latin_table->ex, 0, sizeof(latin_table->ex));
        for(uint8_t i = 0; i < 26; i++) {
            latin_pick_set(latin_table->ex, latin_pick_field(i, true),  latin_bits_get(ee->latin_ex_wide, LATIN_PICK_BASE_BYTES, i));
            latin_pick_set(latin_table->ex, latin_pick_field(i, false), latin_bits_get(ee->latin_ex_wide, LATIN_PICK_BASE_BYTES, (uint8_t)(26 + i)));
        }
        latin_normalised = true;           // force the flush that stamps the new version
    } else {
        // One-time widening of the legacy nibble pairs (one byte per letter, high
        // nibble = uppercase) into the 6-bit fields. Every legacy value is < 16 so
        // it lands in the wider field unchanged and the user keeps their picks.
        memset(latin_table->ex, 0, sizeof(latin_table->ex));
        for(uint8_t i = 0; i < 26; i++) {
            latin_pick_set(latin_table->ex, latin_pick_field(i, true),  (uint8_t)(ee->latin_ex[i] >> 4));
            latin_pick_set(latin_table->ex, latin_pick_field(i, false), (uint8_t)(ee->latin_ex[i] & 0x0F));
        }
        latin_normalised = true;
    }
    // The punctuation targets, from their own appended block. An EEPROM written
    // before they existed reads this as zeros (wear-levelling clears to 0, NOT to
    // 0xFF — see state.h), which is why "never written" is decided by the format
    // byte and not by the bytes: zeroed assignments would mean "every punctuation
    // key hosts A", the exact failure the letter map already shipped once.
    if(ee->latin_ext_fmt == LATIN_EXT_OK) {
        memcpy(latin_table->ex + LATIN_PICK_BASE_BYTES, ee->latin_ex_ext, LATIN_PICK_EXT_BYTES);
        for(uint8_t i = 0; i < LATIN_PUNCT_TARGETS; i++) {
            latin_assign_set(latin_table->assign, (uint8_t)(LATIN_LETTER_TARGETS + i),
                             latin_bits_get(ee->latin_assign_ext, LATIN_ASSIGN_EXT_BYTES, i));
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
}

// QMK calls this once after reset. It runs the post_init_*() steps above in boot
// order, then the displays, the first-run intro and the watchdog.
void keyboard_post_init_user(void) {
    post_init_tables();
    post_init_debug_flags();
    post_init_layers_and_side();
    post_init_peripherals_and_core1();
    post_init_split_rpcs();
    poly_eeconf_t ee = load_user_eeconf();
    post_init_load_settings(&ee);
    post_init_keymap_storage(&ee);
    post_init_seed_state(&ee);
    post_init_latin_table(&ee);
    poly_sync_t* local_state = access_local_state();
    // Restore the MRU recents and schedule a one-time push to the slave half.
    mru_load(ee.mru_emoji, ee.mru_lang);
    splash_progress(7);                 // EEPROM config (brightness/lang/OS/MRU) loaded

    set_displays(local_state->contrast, false);   // active brightness (auto value if restored, else manual)
    // …and bring the status panel up on the same scale (local_state->contrast was
    // just loaded from EEPROM above). QMK's oled_init() programs the contrast
    // register to OLED_BRIGHTNESS unconditionally, so without this a board that
    // booted with a stored brightness of 2 would light its status OLED at full
    // until the first contrast change moved it.
    oled_set_brightness(status_oled_level(false));

    // One-time startup animation: on the very first boot (fresh EEPROM), play the
    // procedural intro once, then persist BOOT_INTRO_DONE (in the housekeeping
    // finish edge). Each half reads its own flag and animates its own keycaps.
    note_boot_flags(ee.boot_flags);
    // The boot-time auto-play of Eden that used to wedge a half is the first-run
    // trigger below; see the note there for what the hang was and why it is fixed.
#ifdef FW_UP_BOOT_TRACE
    boot_trace(U"4");
#endif
    splash_progress(SPLASH_DONE);       // boot complete: full splash, dwell, then legends
    // First-run experience: Eden, then the tutorial. boot_intro_pending() reads the
    // EEPROM marker — which is stamped only once the TUTORIAL has been seen or skipped,
    // so an interrupted first run replays both and a completed one never returns. The
    // marker survives a firmware flash: EEPROM is the last 8 KB of the chip
    // (0x7FE000..0x800000), outside every region any flash path writes.
    //
    // ⚠️ boot_intro_pending() had NO callers before this — the marker, the pending check
    // and the finish edge all existed, but nothing ever started the animation at boot.
    //
    // ON BY DEFAULT since 1.0.0 (`-e POLYKYBD_BOOT_INTRO=no` opts out; HIL images
    // default it off, see rules.mk). It was off for a long time because boot auto-play
    // had wedged a half and the hang was never understood. It is understood now: the
    // master bumps anim_nonce at boot, and the SLAVE's poly-sync handler started Eden
    // RIGHT THERE — on the split-protocol thread (serial_protocol.c's SlaveThread,
    // HIGHPRIO), concurrently with the slave's own post_init. Both wrote the keycap SPI
    // bus at once and the slave parked forever in a spiSend() with no timeout, status
    // panel frozen at "100%" with no render sub-steps (hardware, 2026-09-25). The
    // handler now only records the request and housekeeping starts it
    // (split_sync_drain_anim_replay()). The rounds of tutorial testing since have run
    // this exact path on every reset (POLYKYBD_TUTORIAL_TEST) without a recurrence.
    // What runs HERE is cheap — arming, and startup_anim_start()'s contrast write; the
    // frames render from housekeeping, after crash_watchdog_start() below.
#if defined(POLYKYBD_BOOT_INTRO) || defined(POLYKYBD_TUTORIAL_TEST)
    // ⚠️ The TEST build respects the marker too. It used to force this true on every
    // reset, and a tester who had FINISHED the lesson saw it again after a restart. The
    // test build's marker is keyed to the build instead (boot_done_value() in state.c),
    // so each newly flashed image plays once and a finished lesson stays finished; RESET
    // Eden replays it by hand. It still uses the Eden hand-off (ARMED is set right here,
    // before the first sync), never a direct start from each half's own timer, which
    // raced and left the slave on the default layer.
    const bool first_run = boot_intro_pending();
#    ifdef POLYKYBD_TUTORIAL_TEST
    uprintf("Tutorial TEST build: first-run %s\n", first_run ? "pending" : "already played");
#    endif
    if (first_run) {
        arm_tutorial_after_intro();
        startup_anim_start();
        // ⚠️ The master's marker decides for BOTH halves. RESET Eden runs on the
        // master only and so clears only the master's marker; a slave that already
        // saw the tutorial reads DONE and would sit out the first run. The nonce
        // replays Eden there exactly as KC_EDEN does (split_sync.c's anim_replay), and
        // is a no-op on a slave already animating from its own pending marker.
        // is_keyboard_master(), not is_usb_host_side(): the bridge role is not known
        // yet this early, while QMK's split role is.
        if (is_keyboard_master()) local_state->anim_nonce++;
    }
#endif
    // LAST: arm the hardware watchdog. Everything above may block for seconds
    // (the keymap discard, the splash dwell); from here on housekeeping feeds it
    // every pass and a hang becomes a reset with a `kind=watchdog` crash record.
    crash_watchdog_start();
}

// Pre-initialization setup: initializes display hardware, loads EEPROM config, shows splash screen.
void keyboard_pre_init_user(void) {
    // FIRST, before any of our own init and before QMK's (matrix, split link and
    // status OLED all run between here and post_init): did the previous run end
    // in a crash? Captures the NOLOAD record + reset cause into RAM and stamps
    // the phase BOOT, so a fault anywhere in this boot is recorded as one and
    // the previous record is already archived if it recurs. The flash write
    // runs here without the core1 lockout, which is only safe because core1 has
    // not been launched yet (see crash_record.h) -- keep this ahead of
    // post_init's multicore_launch_core1().
    crash_record_init();

    // Resolve handedness while the EEPROM's own verdict is still readable. It has
    // to be HERE, in keyboard_setup(), because keyboard_init() later runs
    // quantum_init()'s `if (!eeconfig_is_enabled()) eeconfig_init()` -- an erase of
    // the whole store -- and after that point "the store was wiped" and "the store
    // is fine" look identical, which is the one input poly_hand_decide() needs.
    // Same core1 rule as crash_record_init() above: the migration write takes no
    // lockout because core1 has not been launched yet.
    // Provisioning build (-e POLYKYBD_FORCE_HAND=left|right): stamp the side from
    // the image, BEFORE the resolve below, so poly_hand_boot_init() reads it back
    // and reports SRC_STAMP and drives the EEPROM repair through the ordinary path.
    // Written as two plain #ifs rather than IS_DEFINED(), which needs the macro to
    // expand to 0/1 -- OPT_DEFS passes these with no value at all.
#if defined(POLYKYBD_FORCE_HAND_LEFT)
    poly_hand_force_stamp(true);
#elif defined(POLYKYBD_FORCE_HAND_RIGHT)
    poly_hand_force_stamp(false);
#endif
    poly_hand_boot_init();

    // The flash unique ID for cmd 50. A flash command, so it runs here, before
    // core1 exists (lib/drivers/hw_uid.h).
    poly_hw_uid_boot_init();

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
    // Resolve through poly_hand_is_left(), which poly_hand_boot_init() above has
    // already settled from the flash stamp — so this is a cached read that touches
    // no EEPROM at all.  That matters here specifically: any route through
    // eeconfig this early — right after eeprom_driver_init() in keyboard_setup,
    // before the wear-leveling store is validated — can see eeconfig as "not
    // enabled" and run eeconfig_init() → nvm_eeconfig_erase() →
    // eeprom_driver_format(), wiping the *entire* emulated EEPROM.  That is what
    // the old eeconfig_read_handedness() call here had to be careful about, and
    // what dropping EE_HANDS removed from split_pre_init() as well.
    set_side(poly_hand_is_left() ? LEFT_SIDE : RIGHT_SIDE);
    show_splash_screen();
#ifdef FW_UP_BOOT_TRACE
    boot_trace(U"0");
#endif

    gpio_set_pin_input_high(I2C1_SDA_PIN);

    // Test builds only (-e POLYKYBD_USB_STRESS=yes): last in pre_init, so its
    // interrupt-masked windows start just before protocol_pre_init() connects USB
    // and never overlap the flash writes above.
    usb_stress_start();
}

// Runs immediately after QMK's own dynamic_keymap_reset() in eeconfig_init_quantum().
//
// That call is the ONE remaining reachable use of the unbounded reset, and with the
// encoder/macro regions rebased on DYNAMIC_KEYMAP_UPDATE_MAX_LAYER_COUNT (config.h) it
// writes layers 9..12 straight over both of them. It cannot be prevented from here --
// eeconfig.c is upstream and we deliberately do not patch it for this -- but it CAN be
// repaired, because eeconfig_init_quantum() calls us three lines later, unconditionally,
// in the same function. dynamic_keymap_reset_poly() rewrites layers 0..8 and the capped
// encoder map from flash and zeroes the macro buffer, which is exactly the state a fresh
// EEPROM should be in, so this is the normal initialisation rather than a fix-up that
// happens to also repair.
//
// Nearly free despite running second: eeprom_update_byte() only writes a byte that
// actually changed, and QMK's pass has already put the correct values in layers 0..8.
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
    // Likewise born with a real choice recorded. ⚠️ Through idle_timeout_pack(), not
    // raw: the stored form is biased by one, and the zero {0} leaves here is the
    // "never written" value — writing the enum directly would store IDLE_TIMEOUT_15S
    // as a 0 that the loader then reads back as unset. Same class of trap as the
    // latin_assign memset two blocks down, opposite direction.
    ee.idle_timeout   = idle_timeout_pack(POLY_DEFAULT_IDLE_TIMEOUT);
    // Biased like idle_timeout, for the same reason (base/tap_terms.h).
    ee.tapping_term   = tap_term_pack(POLY_DEFAULT_TAPPING_TERM_MS);
    ee.flow_tap_term  = tap_term_pack(POLY_DEFAULT_FLOW_TAP_TERM_MS);
    ee.tail_fmt       = TAIL_FMT_OK;
    ee.auto_shift     = auto_shift_pack(false, POLY_DEFAULT_AUTO_SHIFT_MS);
    ee.tail2_fmt      = TAIL2_FMT_OK;
    ee.keymap_mig     = KEYMAP_MIG_FN_MIDI;  // the keymap was just written from the defaults
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


// Initializes OLED display: turns off, clears buffer, sets the scroll speed (the DOOM
// attract logo's hardware scroll), then enables. The scrolling Poly/Kybd logos once
// drawn here were removed: this runs before the driver is initialised, so they only
// ever filled RAM and were never seen at boot.
oled_rotation_t oled_init_user(oled_rotation_t rotation){
    oled_off();
    oled_clear();
    oled_render();
    oled_scroll_set_speed(0);
    oled_on();
    return rotation;
}

// Clears overlay display flags, disables overlays and status display, sets contrast to OFF.
// Whether poly_suspend() switched the app overlays off. The suspend clears
// DISPLAY_OVERLAYS so the dark panels draw nothing, but the overlay images,
// usage bits and mapping stay in RAM; the wake turns the flag back on, so a
// game that kept the focus during the turn-off (TURN_OFF_TIME) gets its
// overlays back without the host noticing anything. The host only re-sends
// overlays on a focus change, and the keyboard tells it nothing on turn-off
// or wake.
static bool overlays_off_by_suspend = false;

// The host turned the overlays off (cmd 12) while the board slept: the wake
// must not switch them back on.
void poly_forget_suspended_overlays(void) {
    overlays_off_by_suspend = false;
}

// Turns the overlays back on that poly_suspend() turned off; true when it did.
// Called from both wake paths (poly_set_awake_state(), suspend_wakeup_init_kb()).
// It requests no refresh: the key wake splits its render across two passes on
// purpose (display_wakeup()). The slave gets the flag through the periodic
// diff sync (OVERLAY_SYNCED_STATE_FLAGS).
static bool restore_suspended_overlays(poly_sync_t* local_state) {
    if (!overlays_off_by_suspend) {
        return false;
    }
    overlays_off_by_suspend = false;
    local_state->overlay_flags = flag_on(local_state->overlay_flags, DISPLAY_OVERLAYS);
    // Ungated, like "Wake by keypress": the rig's only way to see the restore
    // (polykybd-ctnd station/hil_tests.py, test_overlays_back_after_suspend).
    uprint("Wake: overlays back on\n");
    return true;
}

void poly_suspend(void) {
    // A host-initiated sleep can land while the doom attract screensaver runs —
    // tear it down first (engine stopped, pool handed back) so the demo doesn't
    // keep blitting into panels this function is about to switch off. No-op for
    // a real game session and on the slave.
    doom_screensaver_stop();
    startup_anim_stop();   // tear down the looping Eden idle screensaver on suspend
    poly_sync_t* local_state = access_local_state();
    if (test_flag(local_state->overlay_flags, DISPLAY_OVERLAYS)) {
        overlays_off_by_suspend = true;
    }
    local_state->overlay_flags = flag_off(local_state->overlay_flags, DISPLAY_OVERLAYS);
    local_state->flags &= ~((uint8_t)STATUS_DISP_ON) & ~((uint8_t)DISP_IDLE) & ~((uint8_t)IDLE_TRANSITION);// & ~((uint8_t)RGB_ON);
    local_state->contrast = DISP_OFF;
    reset_idle_jitter();
}

// Suspends keyboard: suspends power down, disables RGB, calls housekeeping, resets update timer.
void suspend_power_down_kb(void) {
    // QMK's suspend loop (protocol_pre_task) calls this every ~17 ms INSTEAD of
    // running housekeeping, so the watchdog has to be fed here too or a long
    // suspend would read as a hang.
    crash_watchdog_feed();
    (void)crash_phase_enter(CRASH_PHASE_SUSPEND, 0);
    // USB suspend fires on slave when master enters bootloader; skip to keep displays lit.
    if (get_local_state()->overlay_flags & BOOTLOADER_DISPLAY) {
        return;
    }
    // The host that set the agent keys is asleep or gone: no stale lights on wake.
    ai_reset_keys("usb suspend");
    poly_suspend();
    // After poly_suspend() stopped Eden: no new jobs reach core1, so what it still owes
    // either finishes within a few ms or is a stall (multicore_exec.h).
    core1_suspend_tick();
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
    //
    // ⚠️ …but ONLY while USB is still supplying power. This hook fires ~3 ms after
    // the bus goes idle, and a hub or cable being cut looks exactly like a host
    // sleeping: the board then runs a few more ms on the regulator's output
    // capacitance while this starts a flash write that takes tens of ms. The cost
    // of losing that race is not one setting — QMK's wear-levelling recovery
    // clears the WHOLE store on any inconsistency it cannot replay, so a torn
    // write takes the brightness, the language, the Intl map, the dynamic keymap
    // and (before the flash stamp) the handedness with it. That is the
    // 2026-09-07 field report: hub interrupted mid-flush, and the half came back
    // as `right master` with the RGB matrix on and the keymap reset.
    //
    // Nothing is lost by skipping: the dirty flags stay set, so the next suspend
    // with power present, KC_STORE_EE, or the host's shutdown signal writes them.
    // usb_vbus_state() is QMK's own accessor for USB_VBUS_PIN (GP24, set in each
    // keymap's config.h), and this is not a new dependency on it: with that pin
    // defined, chibios_config.h stops force-defining SPLIT_USB_DETECT, so the SAME
    // read already decides which half is master. A GP24 this gate could not trust
    // is a board whose split link never comes up, so it fails loudly long before
    // it reaches here.
    //
    // What makes the reading useful in time is where the divider is tapped. The
    // hardware runs USB1 -> [R8 5.6k taps here] -> D2 (1N5819WS) -> U1 (TLV62569
    // buck), with R15 10k to ground: BEFORE the VSYS Schottky. So the node is
    // pulled down within microseconds of the host cutting power, while the MCU
    // runs on for milliseconds on the regulated side's capacitance. A tap after
    // D2 would be held up by that same capacitance and could never report a loss
    // this hook could act on.
    // ⚠️ Gated on being the USB half as well, because on the other one the reading
    // carries no information: a non-USB half has no VBUS of its own, so its GP24
    // reads low always. It is powered over the split cable, so its rail really
    // does die with the master's — it simply cannot see that coming from this pin.
    // Without this clause the slave would silently stop flushing at suspend
    // forever, which is a bigger behaviour change than the one being fixed.
    if (!core1_suspend_flush_ready()) {
        // core1 may still be running an Eden job from flash; a flash write now wedges
        // it or the bus (multicore_exec.h). The master's hold comes within a few ms,
        // or after 500 ms as a recorded stall; the slave's jobs finish within a few
        // ms. A later tick of this suspend flushes.
    } else if (is_keyboard_master() && !usb_vbus_state()) {
        // Seen on resume when a host cuts VBUS to sleep — there the flush was
        // skipped for a rail that was never in danger, and the next one takes it.
        // Unseen when the power really was going, because the console goes with
        // it; that silence IS the feature working.
        uprintf("suspend: VBUS low — EEPROM flush deferred (a torn write clears the whole store)\n");
    } else {
        save_all_dirty();
    }
    suspend_power_down_user();
    disable_idle_tracking();
}

// Called by QMK before every reset (QK_REBOOT, QK_BOOTLOADER, and the host-triggered
// HID bootloader command, all via shutdown_quantum). Flush dirty user state to EEPROM
// here too — without this, a reboot/bootloader jump that isn't preceded by a USB
// suspend would discard any MRU/settings/layer changes still held in RAM.
bool shutdown_user(bool jump_to_bootloader) {
    // ⚠️ Do NOT paint a "Restart" screen here, though shutdown_quantum makes this the
    // one hook that catches every deliberate reset. It was tried (2026-09-16) and
    // backed out, for two reasons that compound:
    //
    //  * it is not visible. mcu_reset() follows within microseconds and the board is
    //    back in keyboard_pre_init_user()'s splash — which opens with
    //    clear_all_displays() — inside a couple of hundred ms. The user reported
    //    seeing nothing, which is what the timing predicts.
    //  * the cost is a 72-panel SPI broadcast plus a blocking ~26 ms I2C flush added
    //    to the reset path, and the reset path is the ONE place where a stall leaves
    //    a dead board: the watchdog is deliberately not armed until the END of
    //    post_init (crash_watchdog_start()), so nothing recovers a board that does
    //    not make it back through boot.
    //
    // A restart genuinely worth announcing announces itself from a path that DWELLS —
    // the slave's reboot_pending branch in housekeeping still does, because it holds
    // the screen while it flushes handedness and EEPROM before mcu_reset().
    save_all_dirty();
    // Disarm the watchdog before any deliberate reset / bootloader jump. Left
    // armed across a BOOTSEL entry it would reset the bootrom out from under a
    // UF2 copy, and a reboot it fired would be archived as a crash.
    crash_watchdog_stop();
    return true;
}


// Resumes keyboard on wakeup: restores display state, brightness, RGB settings, calls housekeeping.
void suspend_wakeup_init_kb(void) {
    // First, so the woken board decodes overlays on a fresh core1. A no-op unless the
    // suspend took the hold (multicore_exec.h).
    core1_resume();
    poly_sync_t* local_state = access_local_state();
    local_state->flags |= STATUS_DISP_ON;
    local_state->flags &= ~((uint8_t)DISP_IDLE);
    local_state->contrast = get_active_brightness();
    reset_idle_jitter();
    if (restore_suspended_overlays(local_state)) {
        request_disp_refresh();   // nothing else on this path repaints the keycaps
    }

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
