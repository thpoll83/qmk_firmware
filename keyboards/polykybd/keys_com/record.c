// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The key event path: process_record_user() with the handlers it calls, in that
// order, and post_process_record_user(). It also has the state that only these use:
// the macOS GUI/Alt swap latch, the macro picker's pending press, the RGB adjust
// repeat and the held KC_IME strokes.

#include QMK_KEYBOARD_H

#include "print.h"
#include "debug.h"

#include "lib/system/state.h"
#include "lib/system/poly_util.h"         // poly_announce_bootloader()
#include "lib/fw_update/split_fw_up.h"    // fw_up_send_slave_reset()
#include "lib/drivers/fw_staging.h"       // the FW-2 confirm prompt
#include "base/update.h"                  // request_disp_refresh(), update_performed()
#include "base/tap_terms.h"               // auto_shift_ms_step()
#include "base/ime_key_plan.h"            // what KC_IME sends, per language and OS
#include "base/ime_held_slots.h"          // which KC_IME key holds which usage
#include "dev/diag/crash_test.h"
#include "doom/doom_mode.h"
#include "features/anim/startup_anim.h"
#include "features/anim/host_tutorial.h"
#include "features/anim/demo_mode.h"
#include "features/anim/tutorial.h"
#include "features/modes/mines_mode.h"
#include "features/modes/ai_mode.h"
#include "features/emoji/emoji_layer.h"
#include "polymod_os_actions.h"           // emit_os_action()
#include "poly_keymap.h"                  // display_wakeup()
#include "poly_macro.h"
#include "poly_macro_record.h"
#include "keys_com/layers.h"
#include "keycode_helper.h"
#include "latin_picker.h"
#include "poly_keymap_internal.h"

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

// The press the open picker is waiting to see released, so an answer is only ever
// taken from a press/release PAIR the picker itself observed.
#define POLY_PICK_NO_PRESS 0xFFu
static uint8_t s_pick_press_row = POLY_PICK_NO_PRESS;
static uint8_t s_pick_press_col = POLY_PICK_NO_PRESS;

// Per-key latch (bit0=LGUI, 1=RGUI, 2=LALT, 3=RALT) recording that the Apple
// GUI/Alt swap was applied on press, so release unregisters the same modifier even
// if the active OS changed while the key was held. See the swap block below.
static uint8_t s_apple_swap_latch = 0;

// ---- process_record_user() handlers ----------------------------------------------
// Each handler covers one step of process_record_user(), in the order it calls them.
// A handler that returns true has decided the event, and process_record_user() returns
// at once: *ret where the handler has one, false for a handler that swallows, and true
// for record_reset_keys(). A handler that returns false lets the event go on.

// The modes in which the board IS the dialog or the game: the crash-test chord, the
// startup animation, the confirm prompt, the demo, the first-run tutorial, the mine
// sweeper and AI mode. Also feeds the host tutorial's key conditions, which never swallow.
static bool record_board_modes(uint16_t keycode, keyrecord_t* record, bool* ret) {
    // TEST BUILDS ONLY (-e POLYKYBD_CRASH_TEST=yes): the deliberate-crash chord.
    // FIRST, so it works even while another mode below would swallow the event --
    // the whole point is to be able to fault the board on demand. Compiles to an
    // inline `return false` in every normal build.
    if (crash_test_process_record(keycode, record->event.pressed)) {
        *ret = false;
        return true;
    }

    // While the ONE-SHOT startup ("Eden") animation is playing, swallow every key
    // event — no typing is wanted (or needed) during the intro. Keys from both
    // halves funnel through the master's process_record before USB reporting, so
    // gating here stops all input from reaching the host until the intro ends.
    // The LOOPING idle Eden screensaver is deliberately NOT swallowed: like the
    // pulse, the first key press should dismiss it and pass through to the wake
    // (display_wakeup clears DISP_IDLE → eden_idle_tick stops the loop next pass).
    if (startup_anim_active() && !startup_anim_is_loop()) {
        *ret = false;
        return true;
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
        *ret = false;
        return true;
    }

    // The showroom demo IS the board while it runs: every event is swallowed — nothing
    // reaches the host, not even a modifier, because the demo draws its own Shift and
    // owns the layer stack (clear_keyboard() ran at its start, and it restores the
    // layers on exit). Holding Esc for DEMO_EXIT_HOLD_MS ends it; the exit fires while
    // Esc is still down, and this also swallows that release. Below the confirm prompt,
    // which outranks it (the demo stops itself when a firmware screen comes up).
    if (demo_process_record(keycode, record)) {
        *ret = false;
        return true;
    }

    // The first-run tutorial IS the board while it runs: every key event is swallowed
    // (nothing should reach the host mid-lesson) and consumed here instead. Swallowed
    // in process_record_user rather than on the release edge — an OSL layer
    // re-dispatches a release-edge action up to three times.
    if (tutorial_active() && !poly_tutorial_tour_passes(record)) {
        *ret = poly_tutorial_record(record);
        return true;
    }

    // The idle-screen mine sweeper IS the board while it runs: every key event is
    // swallowed and answered there, so nothing reaches the host. Holding Hyper or the AI key for 1 s
    // while an idle animation runs starts it, or resumes the board hidden last
    // (mines_mode.h); a shorter tap only wakes the board.
    if (mines_active()) {
        *ret = !mines_process_record(keycode, record);
        return true;
    }
    // The mine sweeper's start gesture goes BEFORE AI mode: on the idle screen the
    // AI key (which replaced Hyper on the split72 layouts) starts the game, as Hyper
    // does, instead of turning AI mode on. Away from the idle screen it passes.
    if (mines_try_start(keycode, record)) {
        *ret = false;
        return true;
    }
    // AI mode (ai_mode.h): KC_AI turns the number row and ESC into the host's agent
    // cards. While it is on every key event is swallowed and answered there, except
    // the releases a held key or a modifier / layer key needs.
    if (ai_process_record(keycode, record)) {
        *ret = false;
        return true;
    }
    return false;
}

// Macro playback abort, the slot picker, the record key and the macro keycodes.
static bool record_macros(uint16_t keycode, keyrecord_t* record, bool* ret) {
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
        if (IS_MODIFIER_KEYCODE(keycode)) { *ret = true; return true; }

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
        *ret = false;
        return true;
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
        *ret = false;
        return true;
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
        *ret = false;
        return true;
    }
    return false;
}

static void record_debug_log(uint16_t keycode, keyrecord_t* record) {
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
}

// macOS: swap the GUI and Alt keys (keycode here, legend in keycode_helper)
// so the physical modifier row matches a Mac's Ctrl–Option–Command order — the
// GUI key acts as Option/Alt and the Alt key as Command/GUI, on both halves.
// Only the four basic modifier keycodes are remapped, and only on macOS.
// The swap is LATCHED per key (s_apple_swap_latch) at press time and reused at
// release, so an active_os change mid-hold can't unregister the opposite mod and
// leave a stuck modifier — the key always releases whatever it pressed.
// Returns true when it swallowed the event.
static bool record_apple_swap(uint16_t keycode, keyrecord_t* record) {
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
            return true;
        }
    }
    return false;
}

// Keys acted on here and swallowed: the RGB adjust and preset keys, the line and word
// editing keys and the OS action keys. Returns true when it swallowed the event.
static bool record_action_keys(uint16_t keycode, keyrecord_t* record) {
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
            return true;
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
            return true;
#endif
        case KC_CRSEL:
            if (record->event.pressed) { SEND_STRING(SS_TAP(X_HOME) SS_TAP(X_HOME) SS_LSFT(SS_TAP(X_END)) SS_TAP(X_BACKSPACE) SS_TAP(X_BACKSPACE) SS_TAP(X_DOWN)); }
            uprint("Delete Line.\n");
            return true;
        case KC_SELECT:
            if (record->event.pressed) { SEND_STRING(SS_LCTL(SS_TAP(X_LEFT) SS_LSFT(SS_TAP(X_RGHT)))); }
            uprint("Select Word.\n");
            return true;
        case KC_EXSEL:
            if (record->event.pressed) { SEND_STRING(SS_TAP(X_HOME) SS_LSFT(SS_TAP(X_END))); }
            uprint("Select Line.\n");
            return true;
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
            return true;
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
            return true;
        default:
            break;
    }
    return false;
}

#ifdef AUTO_SHIFT_ENABLE
// The Auto Shift keycodes change the persisted setting (cmd 44), not only QMK's
// live state, so the host and the next boot see the same value. Swallowed on
// both edges, acted on at the press, before process_auto_shift() sees them.
// AS_UP/AS_DOWN step 10 ms, not QMK's 5, to stay on the setting's grid.
// AS_RPT (types the timeout) is left to QMK.
// Returns true when it swallowed the event.
static bool record_auto_shift(uint16_t keycode, keyrecord_t* record) {
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
            return true;
        default:
            break;
    }
    return false;
}
#endif

// QK_BOOTLOADER and QK_REBOOT, on the press. Returns true when process_record_user()
// must return true, so that QMK's own handler resets the board.
static bool record_reset_keys(uint16_t keycode) {
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
    return false;
}

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

// Handles keypress events including unicode input, language modifications, and special commands.
bool process_record_user(uint16_t keycode, keyrecord_t* record) {
    // Before anything below can swallow this event: drop KC_IME holds that a
    // clear_keyboard() has already released (see ime_slots_forget_cleared).
    ime_slots_forget_cleared(&s_ime_held, &s_ime_io);

    bool ret;
    if (record_board_modes(keycode, record, &ret)) {
        return ret;
    }
    if (record_macros(keycode, record, &ret)) {
        return ret;
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

    record_debug_log(keycode, record);

    if (record_apple_swap(keycode, record)) {
        return false;
    }

    if (emj_process_keycode(keycode, record->event.pressed)) return false;

    if(process_unicodemap_poly(keycode, record)) {
        return  false;
    }

    if (record_action_keys(keycode, record)) {
        return false;
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
    // ⚠️ MUST stay above the record_reset_keys() call below. That handler
    // intercepts QK_BOOTLOADER and QK_REBOOT and returns true for both (the
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
    // Before process_auto_shift() sees them (see record_auto_shift).
    if (record_auto_shift(keycode, record)) {
        return false;
    }
#endif

    if (record->event.pressed) {
        if (record_reset_keys(keycode)) {
            return true;
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
