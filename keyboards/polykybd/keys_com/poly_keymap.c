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

#include <hardware/flash.h>
#include "poly_keymap.h"   // our own exports — declarations checked against the definitions below
#include "lib/split_link/bridge_helper.h"
#include "dev/profiling/loop_profile.h"
#include "lib/fw_update/split_fw_up.h"
#include "lib/fw_update/fw_apply.h"   // fw_apply_tick(), fw_apply_screen_tick()
#ifdef POLYKYBD_BULK_PIPE
#    include "lib/hid/poly_pipe.h"
#endif
#include "uni.h"
#include "lib/split_link/side.h"
#include "lib/display/fill_overlay.h"
#include "lib/hid/mac_compose.h"   // mac_compose_set (protocol v38)
#include "lib/drivers/disp_array.h"
#include "lib/drivers/spi_helper.h"
#include "lib/drivers/shift_reg.h"
#include "fonts/gfx_used_fonts.h"
#include "base/fontpack.h"                // g_all_fonts/g_all_font_count + loader
#include "features/modes/midi_legend.h"                 // QK_MIDI_* legends from the synced MIDI settings
#include "base/ime_key_plan.h"           // what KC_IME sends, per language and OS

// Country flags (NotoColorEmoji_Regular_LangFlags, codepoints FLAG_CP_BASE+glyph)
// now ship in the external-flash font pack, resolved via g_all_fonts — they are
// NOT compiled in. The tiny label font stays resident (no-pack fallback label).
#include "fonts/nano_font.h"          // 10px label font under the flags
#include "fonts/util_font.h"         // mid (10px) utility-label font
#include "features/anim/demo_mode.h"              // the showroom demo loop (KC_DEMO)
#include "features/modes/mines_mode.h"                   // the idle-screen mine sweeper (hold Hyper while idle)
#include "features/modes/ai_mode.h"                      // AI mode: agent cards on the number row (KC_AI)
#include "features/anim/key_attention.h"            // cmd 47: the focus ripple on a key
#include "features/anim/host_tutorial.h"            // cmd 49: the host tutorial (text, spotlight, events)
#include "features/anim/focus_ring.h"                 // the reusable "point at this key" ripple
#include "features/anim/menu_cascade.h"             // menu_cascade_hidden()
#include "features/anim/tutorial_rgb.h"             // the key LEDs during the show and the lesson
#include "lib/system/boot_diag.h"                    // emit_boot_banner(), splash_progress(), SPLASH_DONE
#include "lib/drivers/crash_record.h"            // crash_record_init(), the watchdog, the phase breadcrumb
#include "dev/diag/usb_stress.h"              // no-op unless -e POLYKYBD_USB_STRESS=yes
#include "base/status_brightness.h"      // one brightness scale for keycaps + status OLED
#include "lib/split_link/slave_data.h"                   // slave_data_register(), slave_data_crash_pull_tick()

// The LTR-559 driver is the polykybd/polymod_ltr559 community module; the build
// defines COMMUNITY_MODULE_POLYMOD_LTR559_ENABLE when a variant lists it, and the
// module probes + polls the part from its own hooks. Everything below is the
// PolyKybd-side POLICY on top of it (auto-brightness, idle-inhibit, telemetry).
#ifdef COMMUNITY_MODULE_POLYMOD_LTR559_ENABLE
#    include "polymod_ltr559.h"
#endif
#include "poly_macro_record.h"
#include "lang/lang_lut.h"
#include "poly_combos.h"
#include "keycode_helper.h"
#include "latin_picker.h"         // the Intl latin-variation picker and remap
#include "poly_keymap_internal.h"  // helpers shared with the files split out of this one
#include "doom/doom_mode.h"   // Doom easter egg (inline no-ops unless POLYKYBD_DOOM)
#include "lib/display/status_idle.h"          // the status panel's idle screen (split72 only)
#include "polymod_os_actions.h"
#include "features/emoji/emoji_layer.h"
#include "lang_layer.h"
#include <string.h>


// For fontpack_init() in board_init.c, which cannot include fonts/gfx_used_fonts.h.
const uint8_t resident_font_count = (uint8_t)RESIDENT_FONT_COUNT;

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
int16_t kcl_lang_index(uint16_t keycode) {
    if (keycode >= KCL_ENUS && keycode < KCL_ENUS + KCL_FROZEN_COUNT) {
        return (int16_t)(keycode - KCL_ENUS);
    }
    if (keycode >= KCL_TAIL_FIRST && keycode <= KCL_TAIL_LAST) {
        return (int16_t)(KCL_FROZEN_COUNT + (keycode - KCL_TAIL_FIRST));
    }
    return -1;
}

// keymaps[] / encoder_map[] / g_led_config are variant data, defined in each
// variant's keymaps/default/keymap.c. disp_row_* select the first display of
// row 0 / the second-half start row via macros from the variant header.
// The keymaps[] declaration is in poly_keymap_internal.h.
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
uint8_t status_oled_level(bool idle) {
    // ⚠️ The tutorial owns the status panel's level. Without this, any contrast change
    // during the lesson (the tutorial forces the keycaps' own level on its start edge)
    // ran the branch in sync_and_refresh_displays() and dropped the panel back to the
    // user's mapped level, which tops out at OLED_BRIGHTNESS (60).
    if (tutorial_active() || startup_anim_welcome()) return (uint8_t)POLY_INTRO_STATUS_BRIGHT;
    return idle ? POLY_STATUS_IDLE_BRIGHT : poly_status_brightness(get_local_state()->contrast);
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

    // On-keyboard macro recording, then the FW-2 / FW-9 confirmation prompt
    // (keys_com/housekeeping.c). Both are outside the !fw_up_active gate below: a
    // dialog that could not be drawn or advanced would be unanswerable.
    macro_rec_sync_tick();
    fw_confirm_sync_tick();

    // Hold the idle countdown off for the whole transfer, the same way the
    // confirmation prompt does. The idle state machine (idle_fade_tick()) is NOT behind
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
        // before the boot-animation block in housekeeping_display_tick() and owns the LOOPING variant; the
        // block there is for the ONE-SHOT boot/KC_EDEN animation only.
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
        // Eden, the chapter 1 tutorial, or the normal sync and render.
        housekeeping_display_tick();
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
        ltr559_housekeeping_tick();   // LTR-559 telemetry and auto-brightness (housekeeping.c)
    }
    idle_fade_tick();           // the idle fade state machine (housekeeping.c)
    master_state_sync_tick();   // the master-authoritative synced fields (housekeeping.c)
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
bool settings_more_hidden(uint16_t keycode) {
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

// Updates all display based on current layer and modifiers.
// Global variables: keymaps
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

// ---- KC_IME (base/ime_key_plan.h) ---------------------------------------------------
// KC_IME's language family. ime_key_record() in record.c calls it. It stays in this
// file because PolyKybdHost parses it here.
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

