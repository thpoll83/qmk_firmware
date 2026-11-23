// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The PolyKybd settings and utility keycodes: poly_custom_key_action(), called from
// process_record_user(), and one handler per keycode family. Split out of
// poly_keymap.c.

#include QMK_KEYBOARD_H

#include <transactions.h>   // USER_SYNC_POLY_DATA

#include "lib/system/state.h"
#include "lib/split_link/bridge_helper.h"
#include "polymod_core1_owner.h"
#include "base/com.h"
#include "base/update.h"
#include "base/idle_style.h"
#include "features/anim/demo_mode.h"
#include "features/anim/startup_anim.h"
#include "keys_com/layers.h"
#include "lang/lang_lut.h"
#include "keycode_helper.h"
#include "lang_layer.h"
#include "mru.h"
#include "poly_keymap_internal.h"

// Pick language `li` from the language layer and close it. `push_mru` puts it on the
// recents row; the cycle key and the direct per-language selectors do not.
static void poly_select_lang(poly_sync_t* local_state, uint8_t li, bool push_mru) {
    local_state->lang = li;
    if (push_mru) mru_lang_push(li);
    mark_settings_dirty();
    layer_off(_LL);
}

// The press-edge action: KC_LANG opens the language layer, or steps to the next
// language while the layer is open. Returns true when it owned the press.
static bool lang_press_key(uint16_t keycode) {
    switch (keycode) {
        case KC_LANG: {
            poly_sync_t* local_state = access_local_state();
            if (IS_LAYER_ON(_LL)) {
                poly_select_lang(local_state, (uint8_t)((local_state->lang + 1) % NUM_LANG), false);
            }
            else {
                layer_on(_LL);
            }
            return true;
        }
        default:
            return false;
    }
}

// Settings toggles and the manual EEPROM commit.
static bool settings_flag_key(uint16_t keycode, bool act) {
    poly_sync_t* local_state = access_local_state();
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
        default:
            return false;
    }
    return true;
}

// The base-layer selectors.
static bool base_layer_key(uint16_t keycode, bool act) {
    poly_layer_t* local_layer = access_local_layer();
    switch (keycode) {
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
        default:
            return false;
    }
    return true;
}

// Keycap brightness: fixed steps, up/down, and the host-driven auto mode.
static bool brightness_key(uint16_t keycode, bool act) {
    switch (keycode) {
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
        default:
            return false;
    }
    return true;
}

// The active-OS selection: the cycle key and the direct selectors.
static bool os_key(uint16_t keycode, bool act) {
    switch (keycode) {
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
        default:
            return false;
    }
    return true;
}

// Legend size, glyph script, idle style and idle timeout.
static bool display_style_key(uint16_t keycode, bool act) {
    switch (keycode) {
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
        default:
            return false;
    }
    return true;
}

// Eden with the tutorial, and the two showroom demos.
static bool show_key(uint16_t keycode, bool act) {
    poly_sync_t* local_state = access_local_state();
    switch (keycode) {
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
        default:
            return false;
    }
    return true;
}

// Language layer: region tabs, paging, MRU controls, slot/MRU select, and the
// direct per-language selectors.
static bool lang_key(uint16_t keycode, bool act) {
    poly_sync_t* local_state = access_local_state();
    switch (keycode) {
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
            return false;
    }
    return true;
}

// Every PolyKybd settings/utility keycode is handled HERE, from process_record_user,
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
bool poly_custom_key_action(uint16_t keycode, keyrecord_t* record) {
    // ⚠️ Every handler below is entered on BOTH edges; its actions run only when
    // `act`, i.e. on the release. Owning the PRESS — returning true without acting —
    // is the load-bearing half: process_record_user() then returns false, so
    // process_record() early-returns BEFORE process_action(), and the one-shot
    // re-dispatch at the end of process_action() never manufactures a synthetic
    // release. Leave the press unowned and that block fires, so the action runs once
    // on the synthetic release and AGAIN on the physical one — the physical release
    // still resolves to this keycode because store_or_get_action() caches the source
    // layer, and neither NO_ACTION_LAYER nor STRICT_LAYER_RELEASE is defined here.
    // (Caught by CodeRabbit on #229; the first cut of this refactor swallowed only
    // the release and still double-fired.) Each handler owns both edges out of ONE
    // switch on purpose: a second list of "keycodes to swallow on press" is a list
    // that goes stale silently — the failure mode poly_keymap.c warns about repeatedly.
    //
    // ⚠️ The families must not share a keycode. One switch used to make the compiler
    // reject a duplicate case; now a keycode listed in two handlers runs only the
    // first one, with no warning.
    const bool act = !record->event.pressed;
    if (!act && lang_press_key(keycode)) {
        return true;      // a press-edge action (KC_LANG) owned it outright
    }
    return settings_flag_key(keycode, act) || base_layer_key(keycode, act) ||
           brightness_key(keycode, act) || os_key(keycode, act) ||
           display_style_key(keycode, act) || show_key(keycode, act) ||
           lang_key(keycode, act);
}
