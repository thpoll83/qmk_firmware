// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The first-run tutorial's keyboard side: the callbacks that features/anim/tutorial.c
// calls (tutorial.h), the key tour, chapter 3's language and glyph-script preview, the
// name spelled on the keys, and the two halves of the tutorial's housekeeping that
// housekeeping_task_user() and process_record_user() in poly_keymap.c run. Split out of
// poly_keymap.c; the functions the two files share are declared in
// poly_keymap_internal.h.

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
#ifdef POLYKYBD_BULK_PIPE
#    include "lib/hid/poly_pipe.h"
#endif
#include "lib/drivers/fw_staging.h"
#include "polymod_core1_owner.h"
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
#include "base/fontpack.h"                // g_all_fonts/g_all_font_count + loader
#include "features/modes/midi_legend.h"                 // QK_MIDI_* legends from the synced MIDI settings
#include "base/legend_plan.h"            // the pure keycap legend-SIZE planner
#include "base/ime_key_plan.h"           // what KC_IME sends, per language and OS
#include "base/ime_held_slots.h"         // which KC_IME key holds which usage
// Country flags (NotoColorEmoji_Regular_LangFlags, codepoints FLAG_CP_BASE+glyph)
// now ship in the external-flash font pack, resolved via g_all_fonts — they are
// NOT compiled in. The tiny label font stays resident (no-pack fallback label).
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


#include "latin_picker.h"
#include "poly_keymap_internal.h"
#include "features/anim/tutorial_names_gen.h"   // pre-rendered native language names

static uint32_t s_tut_skip_since  = 0;   // 0 = the skip key is not being held
// ONE buffer for the status prose built at run time (a preview name + "?", "letter X").
// The two builders never speak in the same phase, and every caller draws the line
// before asking for another, so they share it rather than holding 64 B each.
static uint32_t s_tut_prose[16];
// The key-tour presses that were let through, so their RELEASES go through too: LCAT
// and KC_BASE act on the release (poly_custom_key_action), and a swallowed release would
// leave them doing nothing. SEVERAL, not one: the Intl chapter has Intl held while Ctrl,
// a letter and an accent are pressed, and each needs its own release. 0xFF = free.
#define TUT_PASS_MAX 4u
static uint8_t  s_tut_pass_row[TUT_PASS_MAX] = {0xFFu, 0xFFu, 0xFFu, 0xFFu};
static uint8_t  s_tut_pass_col[TUT_PASS_MAX] = {0xFFu, 0xFFu, 0xFFu, 0xFFu};
// Chapter 3's language/script preview; defined beside the tutorial's keymap helpers.
static uint8_t poly_tutorial_apply_preview(void);
// Master only: write the preview (or the user's own glyph script) into the synced state.
// ⚠️ Called from the tutorial branch BEFORE its slave push and its render, and again
// from the master block of housekeeping. It used to run only there — AFTER the tutorial
// branch had already pushed the new phase to the slave and rendered — so each board of
// glyphs was first drawn in the PREVIOUS item's language and repainted a pass later
// ("I can still see the previous script and then it changes", hardware). Idempotent.
void poly_apply_draw_script(void) {
    const uint8_t draw_script = poly_tutorial_apply_preview();
    if (access_local_state()->glyph_script != draw_script) {
        access_local_state()->glyph_script = draw_script;
        request_disp_refresh();   // script changed -> re-render letter/digit legends
    }
}
static uint32_t tut_name_letter(uint8_t row, uint8_t col);   // the spelled preview name
static uint8_t  tut_name_key(uint8_t row, uint8_t col, uint32_t *cp, const uint8_t **tile);
static void     tut_draw_name_tile(const uint8_t *tile);
static void     tut_draw_heavy(uint32_t cp);
// The user's own default LAYOUT, parked while the tutorial runs. 0xFF = nothing parked.
// ⚠️ This is poly's def_layer (a layer INDEX _L0.._L4, the Qwerty/Colemak/Neo choice),
// NOT the momentary layer stack — layer_clear() does not touch it, which is why the
// board kept coming back on the user's layout mid-lesson.
static uint8_t  s_tut_saved_def_layer = 0xFF;

// Park the layout on _L0 for the lesson. ⚠️ Both halves call this, from tutorial_start():
// the SLAVE resolves its own keycaps through its own def_layer, so forcing it only on the
// master would letter one half from Qwerty and the other from whatever the user runs.
//
// ⚠️ NO defer_default_layer_save() — this is a temporary override, and persisting it
// would mean a tutorial silently changed the user's layout.
void tutorial_enter_base_layout(void) {
    poly_layer_t *ll = access_local_layer();
    if (s_tut_saved_def_layer == 0xFF) s_tut_saved_def_layer = ll->def_layer;
    ll->def_layer = _L0;
    layer_reset_to(_L0);
    ll->layer = layer_state;
}

// Hand the board back exactly as it was found: the parked layout, and no momentary or
// toggled layer left over from a chapter that asked the user to hold one (the skip
// gesture can land mid-hold).
void tutorial_restore_layout(void) {
    poly_layer_t *ll = access_local_layer();
    if (s_tut_saved_def_layer != 0xFF) {
        ll->def_layer         = s_tut_saved_def_layer;
        s_tut_saved_def_layer = 0xFF;
    }
    layer_reset_to(ll->def_layer);
    ll->layer = layer_state;
}

// ---- the two halves of the tutorial's housekeeping -------------------------
// Shared by the EXCLUSIVE branch (chapter 1) and the NORMAL branch (intro mode), so the
// skip gesture, the slave push and the teardown exist once rather than once per mode.

static uint8_t tutorial_slot_of(uint8_t row, uint8_t col);

// The key tour: does this event ACT rather than being swallowed? Only the press of the
// key being asked for, and later that same key's release. Everything else falls to the
// swallow below it, so an emoji or language slot stays inert mid-lesson.
static bool tut_tour_step_ready(void);
static bool tut_tour_step_inert(void);
bool poly_tutorial_tour_passes(keyrecord_t *record) {
    const uint8_t row = record->event.key.row, col = record->event.key.col;
    if (!record->event.pressed) {
        for (uint8_t i = 0; i < TUT_PASS_MAX; ++i) {
            if (s_tut_pass_row[i] == row && s_tut_pass_col[i] == col) {
                s_tut_pass_row[i] = s_tut_pass_col[i] = 0xFFu;
                return true;
            }
        }
        return false;
    }
    // Not yet: the Intl chapter's Ctrl, letter and accent only mean what the lesson says
    // with Intl held and the picker in the right state. Swallowed until then.
    if (!tut_tour_step_ready()) return false;
    const bool inert = tut_tour_step_inert();   // read BEFORE the press moves the step
    if (!tutorial_tour_press(tutorial_slot_of(row, col))) return false;
    // Accepted but held back from the board: the lesson moves on, nothing is typed.
    if (inert) return false;
    for (uint8_t i = 0; i < TUT_PASS_MAX; ++i) {
        if (s_tut_pass_row[i] == 0xFFu) {
            s_tut_pass_row[i] = row;
            s_tut_pass_col[i] = col;
            break;
        }
    }
    return true;
}

// The documented skip: hold either outer-edge top key. A held key emits no events, so
// the duration is measured here rather than in process_record.
void poly_tutorial_skip_if_held(void) {
    if (s_tut_skip_since != 0 && timer_elapsed32(s_tut_skip_since) >= TUT_SKIP_HOLD_MS) {
        s_tut_skip_since = 0;
        tutorial_skip();
    }
}

// ⚠️ The master's tut[0] must say ACTIVE for as long as a lesson runs, not only from the
// first successful tutorial push. tut[] rides EVERY poly_sync_t send, and a zero word is
// "stop" to a slave already in the tutorial (tut_sync_word_stops). Before this, any
// ordinary state sync the master sent between starting and its first push — a host
// language or brightness change at connect — could end the slave's lesson. Only the
// flag byte is written here: the rest of the word changes per pass during a wave, and
// writing it every pass would make every pass a state diff and a full repaint.
void poly_tutorial_publish_active(void) {
    if (is_usb_host_side() && tutorial_active()) {
        access_local_state()->tut[0] |= TUT_SYNC_ACTIVE;
    }
}

// ⚠️ Belt to the layer-key swallow's braces: outside the layer chapter the lesson runs on
// _L0 and nothing else, so if the layer stack is ever found elsewhere — an emoji layer
// latched by a TO() press, or anything not yet thought of — put it back. Master only:
// the slave renders from the synced poly_layer_t, which follows. Cheap: one compare per
// pass, and a repark only on a real drift.
static uint8_t tut_tour_layer(void);
static uint8_t tut_tour_allowed_layer(void);
static void    poly_tutorial_tour_rewind_if_let_go(void);
static void poly_tutorial_hold_lesson_layer(void) {
    if (!is_usb_host_side() || !tutorial_active() || tutorial_in_layer_chapter()) return;
    // The key tour opens the language and emoji menus for real, so there the lesson's
    // layer is _L0 plus whatever the current step says. ⚠️ ENFORCED, not just allowed: the
    // press that opens a menu is let through to act, and if it did not (a keymap whose
    // key does something else), the guard still puts the menu the lesson is talking
    // about on screen rather than leaving the prose describing a layer nobody sees.
    const uint8_t       extra = tut_tour_layer();
    const layer_state_t base  = (layer_state_t)1 << _L0;
    const layer_state_t want  = base | (extra != 0xFFu ? (layer_state_t)1 << extra : 0);
    poly_layer_t       *ll    = access_local_layer();
    // def_layer too: display_keycode_at() ORs it into the stack, so a stray one would
    // letter the keys from another layer just as surely.
    // `| base`: TO(_EMJ) turns off every other layer, _L0 included. The emoji layer
    // covers the board either way, so that is not a drift worth a repaint.
    // A layer the step lets the user HOLD (Fn, Num, Intl) is allowed on top, never
    // forced: forcing it would leave it on after the finger lifts.
    const uint8_t       hold  = tut_tour_allowed_layer();
    const layer_state_t held  = hold != 0xFFu ? want | ((layer_state_t)1 << hold) : want;
    if (((layer_state | base) == want || (layer_state | base) == held) && ll->def_layer == _L0) {
        return;
    }
    uprintf("Tutorial: layer drifted (state 0x%08lX, def %u), back to _L0\n",
            (unsigned long)layer_state, (unsigned)ll->def_layer);
    ll->def_layer = _L0;
    layer_reset_to(_L0);
    if (extra != 0xFFu) layer_on(extra);
    ll->layer = layer_state;
    request_disp_refresh();
}

void poly_tutorial_push_sync(void) {
    if (is_usb_host_side() && tutorial_active()) poly_tutorial_tour_rewind_if_let_go();
    poly_tutorial_hold_lesson_layer();
    poly_tutorial_publish_active();
    poly_tutorial_skip_if_held();
        // Push the step/ripple to the slave: it draws the keys that land on its own
        // half. Gated on the transport being up (non-blocking) for the same reason
        // Eden's nonce is — a fresh boot must never stall on a slave still coming up.
        if (is_usb_host_side() && tutorial_sync_pending() && is_transport_connected()) {
            poly_sync_t* ls = access_local_state();
            tutorial_sync_fill(ls->tut);
            // ONE attempt, not three. The push is re-armed on a timer, so a
            // failure costs 400 ms rather than a retry; three blocking retries
            // every 400 ms is main-loop time taken from the matrix scan and the
            // slave pull — which presents as a sluggish keypress.
            const uint8_t ack = send_to_bridge(USER_SYNC_POLY_DATA, (void*)ls,
                                               sizeof(poly_sync_t), 1);
            if (sync_succeeded(ack)) {
                tutorial_sync_sent();
            } else {
                // Never bool-test the ack (every return is non-zero). Logged
                // because a slave that never joins the tutorial shows dark
                // keycaps and its ordinary status screen — which reads as "the
                // tutorial is broken" rather than "the link dropped one frame".
                static uint8_t s_tut_sync_fail;
                if (++s_tut_sync_fail <= 5) {
                    uprintf("Tutorial sync to slave failed (ack=0x%02X)\n", ack);
                }
            }
        }
}

void poly_tutorial_finish_if_done(void) {
        if (tutorial_finished()) {
            const bool was_skipped = tutorial_was_skipped();
            poly_tutorial_forget_passes();
            // Order matters. tutorial_stop() restores the parked layout, drops any
            // layer a chapter left held, blanks every panel and invalidates the
            // dirty-window boxes; THEN the brightness is restored, THEN the legends are
            // redrawn — so the panels are never repainted against stale state, and the
            // legends drawn are the ones the board will actually type.
            tutorial_stop();
            // Hand back the user's language and glyph script BEFORE the teardown
            // repaint below. tutorial_stop() retires the preview, but the state only
            // follows at the master block's poly_apply_draw_script(), later in the same
            // pass: a skip mid-preview repainted one frame, and synced it to the slave,
            // in the previewed language (review of #313). Idempotent on the normal end.
            if (is_usb_host_side()) poly_apply_draw_script();
            // ⚠️ RETIRE THE SYNC WORD FIRST — BEFORE the teardown's own
            // sync_and_refresh_displays() below. tut[0] still holds whatever the last
            // push wrote (ACTIVE, and the ARMED level Eden handed over), and both are
            // LEVELS the slave acts on. Clearing it afterwards left a real race: the
            // slave finishes off the TUT_DONE phase in the final push, so by the time
            // that teardown sync arrives it is INACTIVE — and an ACTIVE word reaching
            // an inactive slave arms s_start_pending, which tutorial_tick() then
            // drains into a lesson running on the slave ALONE. (The zeroed word that
            // follows cannot undo it; see the cancel in tutorial_sync_apply().)
            if (is_usb_host_side()) {
                poly_sync_t *ls = access_local_state();
                for (uint8_t i = 0; i < TUTORIAL_SYNC_BYTES; ++i) ls->tut[i] = 0;
            }
            set_displays(get_local_state()->contrast, false);
            // Two-pass (rows 0-2, matrix scan, rows 3-4) rather than a one-shot
            // ALL_AT_ONCE, for the reason display_wakeup() gives: a full render is
            // ~107 ms in one pass and would swallow the keystroke right after.
            set_disp_refresh(START_FIRST_HALF);
            sync_and_refresh_displays();
            // ⚠️ EEPROM LAST. The straight-through write (see mark_boot_intro_done)
            // can trigger a wear-levelling consolidation — ~50-100 ms with
            // interrupts off — and that must not land between the SPI writes that
            // are handing the displays back.
            // ⚠️ EEPROM writes run with the QSPI out of XIP; halt core1 across it.
            // The overlay decompressor is free to be anywhere in flash, and the
            // refresh just requested above is exactly what puts it there. Same
            // lockout the fw-apply flush takes, and for the same hazard.
            core1_hold();
            mark_boot_intro_done();
            core1_release();
            uprintf("Tutorial %s; displays handed back\n", was_skipped ? "skipped" : "done");
        }
}

// ---- first-run tutorial callbacks (see features/anim/tutorial.h) -------------------
// Matrix (row,col) -> packed display slot, or TUT_SLOT_NONE when no OLED sits behind
// that key. The INVERSE of eden_idle_erase_legend's mapping, right-half `c--` display
// fold included. That fold is why the mirrored skip key at the outer edge is NOT the
// same local column on both halves: right display col 0 is the INNER edge.
static uint8_t tutorial_slot_of(uint8_t row, uint8_t col) {
    if (row >= MATRIX_ROWS || col >= MATRIX_COLS) return TUT_SLOT_NONE;
    if (!key_has_display(row, col)) return TUT_SLOT_NONE;
    // ⚠️ The right-half column fold used to be re-derived here. It is key_display_index()'s
    // job — the same fold the chip-select table and the dirty-window bboxes use — and a
    // second copy is how the two drift. It returns 255 for a position with no panel of
    // its own (the right half's absent col 0), which is the case this used to spell out.
    const uint8_t idx = key_display_index(row, col);
    if (idx == 255) return TUT_SLOT_NONE;
    return TUT_SLOT(row >= MATRIX_ROWS_PER_SIDE ? 1 : 0, idx);
}

// The display slot (TUT_SLOT packing) of the key at (row, col), TUT_SLOT_NONE for a
// key without a panel. For poly_focus_start() outside the tutorial (ai_mode.c).
uint8_t poly_slot_of(uint8_t row, uint8_t col) { return tutorial_slot_of(row, col); }

uint8_t tutorial_collect_candidates(uint8_t* out, uint8_t max) {
    uint8_t n = 0;
    for (uint8_t r = 0; r < MATRIX_ROWS && n < max; ++r) {
        for (uint8_t c = 0; c < MATRIX_COLS && n < max; ++c) {
            const uint16_t kc = keymaps[_BL][r][c];
            if (kc < KC_A || kc > KC_Z) continue;          // plain letters only
            const uint8_t slot = tutorial_slot_of(r, c);
            if (slot != TUT_SLOT_NONE) out[n++] = slot;
        }
    }
    return n;
}

// The two Shift keys, left hand first. ⚠️ SCANNED FROM THE KEYMAP, not hardcoded to
// (3,0) and (8,7): chapter 2 points a ring at one of them and refuses the other, so a
// position that disagrees with the keymap would leave the lesson unclearable. The scan
// is the same shape as tutorial_collect_candidates() and asks the same questions —
// key_has_display() (a shift with no OLED cannot be pointed at) and the mod-tap unwrap
// (a home-row Shift is a mod-tap, and its TAP keycode is what its legend says).
void tutorial_shift_slots(uint8_t out[TUT_SHIFT_STAGES]) {
    for (uint8_t i = 0; i < TUT_SHIFT_STAGES; ++i) out[i] = TUT_SLOT_NONE;
    for (uint8_t r = 0; r < MATRIX_ROWS; ++r) {
        for (uint8_t c = 0; c < MATRIX_COLS; ++c) {
            uint16_t kc = keymaps[_BL][r][c];
            kc = poly_mt_tap(kc);
            if (kc != KC_LEFT_SHIFT && kc != KC_RIGHT_SHIFT) continue;
            const uint8_t slot = tutorial_slot_of(r, c);
            if (slot == TUT_SLOT_NONE) continue;
            // By HAND, from the slot's own side bit — not from the keycode. KC_RSFT on
            // the left half is a perfectly ordinary keymap, and chapter 2 is teaching
            // "either hand", not "either keycode".
            const uint8_t hand = TUT_SLOT_RIGHT(slot) ? 1u : 0u;
            if (out[hand] == TUT_SLOT_NONE) out[hand] = slot;
        }
    }
}

uint32_t tutorial_slot_letter(uint8_t slot) {
    if (slot == TUT_SLOT_NONE) return 0;
    uint8_t mr, mc;
    if (!display_index_to_matrix(TUT_SLOT_RIGHT(slot), TUT_SLOT_IDX(slot), &mr, &mc)) return 0;
    const uint16_t kc = keymaps[_BL][mr][mc];
    if (kc < KC_A || kc > KC_Z) return 0;
    // Upper case: at 2x the 19px face this fills the keycap, and a lone capital reads
    // as "this one" rather than as a letter you are being asked to type.
    return (uint32_t)('A' + (kc - KC_A));
}

// Any key whose job is to change layer. ⚠️ Derived from QMK's keycode RANGES, not from
// a list of the layer keycodes this keymap happens to use — a hand-kept list is the
// guard shape that goes stale the moment a keymap gains a layer key, and here going
// stale means the tutorial silently leaves that key dark.
bool tutorial_is_layer_key(uint16_t kc) {
    return IS_QK_MOMENTARY(kc) || IS_QK_ONE_SHOT_LAYER(kc) || IS_QK_TO(kc) ||
           IS_QK_LAYER_TAP_TOGGLE(kc) || IS_QK_DEF_LAYER(kc) || IS_QK_TOGGLE_LAYER(kc) ||
           kc == KC_BASE;
}

// The key chapter 3 asks for: the MOMENTARY layer key, since holding is the gesture
// chapter 2 just taught. Returns TUT_SLOT_NONE when this half has none — the master
// asks both halves' keymaps, so a board with Fn on either side works.
static bool tutorial_is_chapter3_key(uint16_t kc) {
    return IS_QK_MOMENTARY(kc);
}

// Is this key in a chapter's lit set? ⚠️ A direct per-key question now, not a
// precomputed bitmap over display slots: update_displays() walks the MATRIX and already
// knows (row, col), so the slot round-trip bought nothing. The letters are in BOTH sets
// — they are the board the chapter transforms. Only the key the chapter asks you to
// HOLD differs.
// Does this matrix position resolve to `slot`? The inverse question to
// tutorial_slot_of(), asked per key by the intro-mode visibility gate.
bool tutorial_slot_matches(uint8_t slot, uint8_t row, uint8_t col) {
    return slot != TUT_SLOT_NONE && tutorial_slot_of(row, col) == slot;
}

bool tutorial_key_in_chapter_set(uint8_t row, uint8_t col, bool layer_chapter) {
    if (row >= MATRIX_ROWS || col >= MATRIX_COLS) return false;
    uint16_t kc = keymaps[_BL][row][col];
    // A mod-tap's legend is its TAP keycode's legend, so unwrap before asking what this
    // key is — the same unwrap render_key() opens with, and for the same reason.
    kc = poly_mt_tap(kc);
    if (kc >= KC_A && kc <= KC_Z) return true;
    return layer_chapter ? tutorial_is_layer_key(kc)
                         : (kc == KC_LEFT_SHIFT || kc == KC_RIGHT_SHIFT);
}


// Matrix (row,col) for a slot on THIS half, or false when it does not map back.
static bool tutorial_matrix_of(uint8_t slot, uint8_t *row, uint8_t *col) {
    if (slot == TUT_SLOT_NONE) return false;
    return display_index_to_matrix(TUT_SLOT_RIGHT(slot), TUT_SLOT_IDX(slot), row, col);
}

// draw_legend_cx_cy() and display_keycode_at() are in poly_keymap.c
// (poly_keymap_internal.h), beside update_displays()' own use of them.

// The focus ripple repaints keys outside update_displays()' own pass and needs their
// ordinary legend back under the arc — the same draw, so the same function.
//
// ⚠️ Draws NOTHING for a key that is not being rendered. Without this, the wavefront
// crossing a hidden key drew that key's real legend, un-hiding exactly the keys the
// lesson had darkened. The ARC is not gated on this — see the note in focus_ring.h; the
// ring has to cross the whole board. The visibility question has ONE answer and both the
// normal render path and the ripple ask it here.
// Does the tutorial leave this key lit? Always true outside the lesson.
bool poly_slot_visible(uint8_t slot) {
    uint8_t r, c;
    return tutorial_matrix_of(slot, &r, &c) && tutorial_key_visible(r, c) &&
           !host_tutorial_key_hidden(r, c);
}

bool poly_focus_draw_legend(uint8_t slot) {
    uint8_t r, c;
    if (!tutorial_matrix_of(slot, &r, &c)) return false;
    // The firmware prompt owns every panel: a ripple still running when it opened
    // repaints the prompt's state (one key or dark), as update_displays() does.
    if (get_local_state()->fw_confirm) {
        // r is the board row (the right half starts at MATRIX_ROWS_PER_SIDE);
        // update_displays() compares the half-local one.
        if (r % MATRIX_ROWS_PER_SIDE == FW_CONFIRM_ROW && c == FW_CONFIRM_COL) {
            render_fw_confirm_key(is_left_side());
        } else {
            kdisp_set_buffer(0x00);
        }
        return true;
    }
    // An agent card (AI mode) under the ripple, not the key's ordinary legend.
    if (ai_render_key(r, c)) return true;
    if (!tutorial_key_visible(r, c)) return false;
    if (host_tutorial_key_hidden(r, c)) return false;   // cmd 49 spotlight / reveal front
    if (menu_cascade_hidden(r, c)) return false;   // not its turn in the cascade yet
    // The chrome keys show the lesson's labels, here as in update_displays(), or a ring
    // crossing one would paint "Esc" back for a frame.
    if (tutorial_is_chrome_key(r, c)) {
        tutorial_draw_chrome(r, c);
        return true;
    }
    tutorial_draw_board_legend(slot);
    return true;
}


void tutorial_draw_board_legend(uint8_t slot) {
    uint8_t r, c;
    if (!tutorial_matrix_of(slot, &r, &c)) return;

    const poly_layer_t *local_layer = get_local_layer();
    const poly_sync_t  *local_state = get_local_state();
    const led_t   state = local_layer->led_state;
    const uint8_t mods  = local_layer->mods;
    const uint16_t keycode = display_keycode_at(local_layer, r, c);
    // The same three inputs update_displays() decides the overlay with.
    const bool display_overlays = test_flag(local_state->overlay_flags, DISPLAY_OVERLAYS);
    const bool add_lang = get_highest_layer(local_layer->layer) == _ADDLANG1;
    const bool capital_case = ((mods & MOD_MASK_SHIFT) != 0) || state.caps_lock;

    // The menus' bespoke keys, then the emoji tab frames and the MRU bar — the same
    // order update_displays() draws them in.
    if (render_menu_key(keycode, state, local_state->lang)) return;
    emj_draw_tab_indicator(keycode);
    emj_draw_tab_bottom(keycode);
    draw_mru_top_bar(keycode);

    // ⚠️ The PAIR, in update_displays()' own order: to_static_text() first, render_key()
    // only when it returned NULL. render_key() alone draws nothing at all for a key
    // whose legend is static text.
    const uint32_t *text = to_static_text(keycode, state);
    if (text == NULL) {
        if (!render_key(keycode, state, mods) &&
            (keycode & QK_UNICODEMAP_PAIR) == QK_UNICODEMAP_PAIR) {
            const uint16_t chr = capital_case ? QK_UNICODEMAP_PAIR_GET_SHIFTED_INDEX(keycode)
                                              : QK_UNICODEMAP_PAIR_GET_UNSHIFTED_INDEX(keycode);
            kdisp_write_gfx_char(g_all_fonts, g_all_font_count, BUFFER_X, 23, unicode_map[chr], 0);
        }
    } else if (r % MATRIX_ROWS_PER_SIDE == MATRIX_ROWS_PER_SIDE - 1 &&
               !(display_overlays && !add_lang && !tutorial_active() &&
                 overlay_present(keycode, mods))) {
        draw_legend_cx_cy(text, 23, KDISP_CY_DEFAULT);   // thumb row: centred
    } else {
        kdisp_write_gfx_text_cy(g_all_fonts, g_all_font_count, BUFFER_X, 23, text,
                                KDISP_CY_DEFAULT);
    }

    // ⚠️ The app overlay (or the built-in Ctrl hint), as update_displays() draws it.
    // Without it every key the ring crossed lost its icon on the ring's frames and got
    // it back on the next full refresh: the keys around the ring flickered between the
    // two (hardware, 2026-11-18). Same guards: no overlay on the Intl layer or in the
    // first-run lesson.
    if (!add_lang && !tutorial_active()) {
        const uint32_t *hint = NULL;
        if (display_overlays) {
            if (!copy_overlay_to_buffer(keycode, mods)) {
                hint = keycode_to_disp_overlay(keycode);
            }
        } else {
            hint = keycode_to_disp_overlay(keycode);
        }
        if (hint) {
            kdisp_write_gfx_text_cy(g_all_fonts, g_all_font_count, BUFFER_X, 23, hint,
                                    KDISP_CY_DEFAULT);
        }
    }
}

// The two outer-edge top keys — the documented (not displayed) skip gesture. LEFT is
// display (0,0); RIGHT is display (0,6), the outer edge at board x=223, NOT (0,0)
// which the `c--` fold puts on the inner edge at x=143.
static bool tutorial_is_skip_key(uint8_t row, uint8_t col) {
    const uint8_t slot = tutorial_slot_of(row, col);
    return slot == TUT_SLOT(0, 0) || slot == TUT_SLOT(1, 6);
}

uint8_t tutorial_slot_at(uint8_t row, uint8_t col) { return tutorial_slot_of(row, col); }

// The menus' recents (the bottom row of _LL and _EMJ, and its Preset/Clear controls)
// stay dark for the whole lesson: on a new board they are empty or the factory's, and a
// lit row reads as something the lesson points at (hardware round 33). Base, beside
// them, keeps its legend; the tour asks for it next.
bool tutorial_hides_recent(uint8_t row, uint8_t col) {
    const uint16_t kc = display_keycode_at(get_local_layer(), row, col);
    return (kc >= KC_EMJ_MRU_BASE && kc < KC_EMJ_MRU_BASE + MRU_CAP) ||
           (kc >= KC_LANG_MRU_BASE && kc < KC_LANG_MRU_BASE + MRU_CAP) ||
           kc == KC_EMJ_PRESET || kc == KC_EMJ_CLEAR || kc == KC_LANG_PRESET ||
           kc == KC_LANG_CLEAR;
}

// The two chrome keys are the two halves of the skip gesture (tutorial_is_skip_key):
// Esc at left display (0,0) says how to leave, and its mirror — right display (0,6),
// the OUTER edge — shows the chapter. Either still skips when held.
static const uint32_t *tutorial_chrome_label(uint8_t row, uint8_t col) {
    const uint8_t slot = tutorial_slot_of(row, col);
    // The key the tour is asking for shows its OWN legend, even where the chrome sits:
    // the emoji page arrow is the progress key's position, and a key labelled "9/10"
    // cannot be asked for as "the next page".
    if (slot != TUT_SLOT_NONE && slot == tutorial_tour_target()) return NULL;
    if (slot == TUT_SLOT(0, 0)) return tutorial_skip_label();
    if (slot == TUT_SLOT(1, 6)) return tutorial_progress_label();
    return NULL;
}

bool tutorial_is_name_key(uint8_t row, uint8_t col) { return tut_name_letter(row, col) != 0; }

bool tutorial_is_chrome_key(uint8_t row, uint8_t col) {
    return tutorial_chrome_label(row, col) != NULL || tut_name_letter(row, col) != 0;
}

// Esc's label is a HINT_MID two-line stack drawn like any static legend on a non-thumb
// row; the progress is one HINT_MID run, centred both ways.
void tutorial_draw_chrome(uint8_t row, uint8_t col) {
    uint32_t       name_cp   = 0;
    const uint8_t *name_tile = NULL;
    switch (tut_name_key(row, col, &name_cp, &name_tile)) {
        case 1:
            (void)tutorial_draw_key_letter(name_cp);   // latin capitals one tier up
            kdisp_set_gfx_erase(false);
            return;
        case 2:
            tut_draw_name_tile(name_tile);
            return;
        case 3:
            tut_draw_heavy(name_cp);
            kdisp_set_gfx_erase(false);
            return;
        default:
            break;
    }
    const uint32_t *t = tutorial_chrome_label(row, col);
    if (t == NULL) return;
    if (tutorial_slot_of(row, col) == TUT_SLOT(0, 0)) {
        kdisp_write_gfx_text_cy(g_all_fonts, g_all_font_count, BUFFER_X, 23, t,
                                KDISP_CY_DEFAULT);
    } else {
        // The progress: one HINT_MID run, centred both ways (the face's digits sit 13 px
        // above the baseline, so baseline 26 centres them on the 40 px panel).
        draw_legend_cx_cy(t, 26, KDISP_CY_DEFAULT);
    }
    kdisp_set_gfx_erase(false);
}

// ---- chapter 3: languages and glyph scripts, previewed on the board only ----------
//
// ⚠️ BOARD-ONLY. The host polls GET_LANG every second and SWITCHES THE OS LAYOUT to
// match (PolyHost's language-changed flow), so a preview written into
// local_state->lang would retype the user's OS in Greek. poly_reported_lang() is what
// GET_LANG and the EEPROM save read instead, and it answers with the REAL language for
// as long as a preview is on screen. The glyph script needs no such guard: cmd 30 and
// the save both read get_glyph_script(), which the preview never touches.
typedef struct {
    bool            script;   // false = a keyboard language (LANG_*), true = GLYPH_*
    uint8_t         value;
    const uint32_t *name;     // Latin name: status panel + the LEFT half's keys. ASCII.
    // The language's own name for itself, for the RIGHT half's keys, one character per
    // keycap. NULL for a glyph script (its keys spell `name` through the script) and for
    // a name drawn from `strip`.
    const uint32_t *native;
    bool            rtl;      // native reads right to left: laid out from the right
    // A name the keycap renderer cannot spell character by character — joined letters,
    // attached vowel signs, or glyphs the fonts lack — pre-rendered offline into keycap
    // tiles (dev/tools/gen_tutorial_names.py -> features/anim/tutorial_names_gen.h).
    const tut_name_strip_t *strip;
    // The left status panel's lead-in; the right panel finishes it with `name` and a
    // "?" (tutorial_preview_name()), so every lead-in must open a QUESTION. ONE per row,
    // never a rotation: a 5-phrase cycle over 11 items repeated itself, and every script
    // said "Or write in" ("make sure there is no repeating text", hardware).
    const uint32_t *lead;
} tut_preview_t;

// A tour, not a catalogue: scripts that look nothing like each other, then the
// fantasy faces. Order is the order shown.
//
// Every native name is the LANGUAGE's name for itself (한국어, not the script's name
// 한글). The ones spelled per key are scripts whose letters stand alone and are in the
// fonts; the rest are pre-rendered strips — joined Arabic, Devanagari with its attached
// vowel signs, and 日本語 / 한국어, whose glyphs the keycap fonts do not carry.
static const tut_preview_t s_tut_preview_all[] = {
    {false, LANG_ELGR,      U"Greek",    U"\u0395\u039B\u039B\u0397\u039D\u0399\u039A\u0391", false, NULL, U"How about"}, // ΕΛΛΗΝΙΚΑ
    {false, LANG_ARSA,      U"Arabic",   NULL, false, &TUT_NAME_STRIP_AR, U"Do you speak"},             // العربية
    {false, LANG_HEIL,      U"Hebrew",   U"\u05E2\u05D1\u05E8\u05D9\u05EA", true, NULL, U"Or perhaps"},  // עברית
    {false, LANG_HIIN,      U"Hindi",    NULL, false, &TUT_NAME_STRIP_HI, U"Maybe you read"},            // हिन्दी
    {false, LANG_THTH,      U"Thai",     U"\u0E20\u0E32\u0E29\u0E32\u0E44\u0E17\u0E22", false, NULL, U"Or do you type"}, // ภาษาไทย
    {false, LANG_JAJP,      U"Japanese", NULL, false, &TUT_NAME_STRIP_JA, U"Or is it"},                  // 日本語
    {false, LANG_KOKR,      U"Korean",   NULL, false, &TUT_NAME_STRIP_KO, U"Perhaps even"},              // 한국어
    {true,  GLYPH_TENGWAR,  U"Elvish",   NULL, false, NULL, U"Fancy some"},
    {true,  GLYPH_RUNES,    U"Runes",    NULL, false, NULL, U"Or carving"},
    {true,  GLYPH_AUREBESH, U"Aurebesh", NULL, false, NULL, U"Into sci-fi,"},
    {true,  GLYPH_BRAILLE,  U"Braille",  NULL, false, NULL, U"Or by touch,"},
};
#define TUT_PREVIEW_ALL (sizeof(s_tut_preview_all) / sizeof(s_tut_preview_all[0]))
_Static_assert(TUT_PREVIEW_ALL <= TUT_PREVIEW_MAX, "preview table exceeds the plan's cap");

static uint8_t s_tut_preview[TUT_PREVIEW_ALL];   // indices into the table, renderable only
static uint8_t s_tut_preview_n;

// Can this board draw the entry? Asked of the fonts actually flashed, through the same
// lookup the renderer uses: on a fresh board with no font pack every non-Latin entry
// fails here and the chapter shows only the reveal, rather than a board of blanks.
// Shared with the demo's language and script tour (features/anim/demo_mode.c), which asks the same
// question of the same fonts.
bool poly_preview_renderable(bool script, uint8_t value) {
    uint32_t cp = 0;
    if (script) {
        cp = glyph_script_codepoint(value, KC_A);
    } else {
        const uint32_t *t = translate_keycode(value, KC_A, false, false);
        cp = (t != NULL) ? t[0] : 0;
    }
    return cp != 0 && kdisp_gfx_glyph(g_all_fonts, g_all_font_count, cp) != NULL;
}

static bool tut_preview_renderable(const tut_preview_t *e) {
    return poly_preview_renderable(e->script, e->value);
}

uint8_t tutorial_preview_prepare(void) {
    s_tut_preview_n = 0;
    const uint8_t own = poly_reported_lang();
    for (uint8_t i = 0; i < TUT_PREVIEW_ALL; ++i) {
        const tut_preview_t *e = &s_tut_preview_all[i];
        // Skip the user's own language: the chapter shows what ELSE the board speaks.
        // (The lesson runs on TUT_PARKED_LANG, so showing it would change the board, but
        // a Greek user does not need Greek introduced.)
        if (!e->script && e->value == own) continue;
        if (tut_preview_renderable(e)) s_tut_preview[s_tut_preview_n++] = i;
    }
    return s_tut_preview_n;
}

uint8_t tutorial_lang_slot(void) {
    for (uint8_t r = 0; r < MATRIX_ROWS; ++r) {
        for (uint8_t c = 0; c < MATRIX_COLS; ++c) {
            if (keymaps[_BL][r][c] != KC_LANG) continue;
            const uint8_t slot = tutorial_slot_of(r, c);
            if (slot != TUT_SLOT_NONE) return slot;
        }
    }
    return TUT_SLOT_NONE;
}

// ---- the key tour: the language menu, then the emoji menu -----------------------
// Each step is a real key on a real layer; the lesson points at it, the user presses it,
// and it ACTS (see the pass-through in process_record_user()). The kinds and the layer
// each step leaves on are kept here beside the slots, on both halves, so the status
// prose and the layer guard need nothing but the step index.
typedef enum {
    TUT_TOUR_LANG = 0,   // the Lang key: opens _LL
    TUT_TOUR_LCAT,       // a region tab on _LL (arg = region)
    TUT_TOUR_BASE_LL,    // KC_BASE on _LL: back home
    TUT_TOUR_EMJ,        // TO(_EMJ) on the base layer
    TUT_TOUR_ECAT,       // a category tab on _EMJ (arg = category)
    TUT_TOUR_EPAGE,      // KC_EMJ_PAGE_NEXT on _EMJ
    TUT_TOUR_BASE_EMJ,   // KC_BASE on _EMJ
    TUT_TOUR_FN,         // hold MO(_FL) and look
    TUT_TOUR_NUM,        // hold MO(_NL) and look
    // ---- the Intl chapter: the only sequence whose steps need a key HELD ----
    TUT_TOUR_INTL_LOOK,  // hold Intl: every letter shows its chosen accent
    TUT_TOUR_INTL_ARM,   // hold Intl again (the picker needs it held throughout)
    TUT_TOUR_INTL_CTRL,  // tap Ctrl: the picker opens
    TUT_TOUR_INTL_LETTER,// press the chosen letter: its accents fill the number row
    TUT_TOUR_INTL_ALT,   // press the chosen accent: saved, the picker closes
    TUT_TOUR_INTL_AGAIN, // hold Intl once more
    TUT_TOUR_INTL_TYPE,  // press the letter: it now shows the new accent (INERT)
} tut_tour_kind_t;

// What a step needs to be pressable, beyond being the key the ring is on.
#define TUT_NEED_INTL    0x01u   // Intl held (_ADDLANG1 on)
#define TUT_NEED_PICKER  0x02u   // the picker latched (Intl + Ctrl tapped)
#define TUT_NEED_CLOSED  0x04u   // the picker NOT latched (the Ctrl tap must open it)
#define TUT_INERT        0x08u   // accepted, but the press does NOT reach the board: the
                                 // Intl letter would type its accent into whatever app
                                 // has focus, which a lesson must never do

// A few emoji tabs rather than all twelve — the menu is taught by then, and the point is
// the variety: two tabs on each half, so the ring crosses the split. Candidates per half
// in preference order; the ACTIVE category is skipped (pressing the tab that is already
// open changes nothing on screen — "we should not ask to press the first tab which is
// active by default", hardware), so each half has a spare.
static const uint8_t s_tut_emj_left[]  = {0, 4, 5};    // smileys, animals, food (left-half tabs)
static const uint8_t s_tut_emj_right[] = {7, 8, 9};    // travel, sports, tools  (right-half tabs)
#define TUT_EMJ_TABS_PER_HALF 2u

// Progress keycap per section (base/tutorial_plan.c has the first five).
#define TUT_PROG_LANGMENU 6u
#define TUT_PROG_EMOJI    7u
#define TUT_PROG_LAYERS   8u
#define TUT_PROG_INTL     9u

static uint8_t s_tour_kind[TUT_TOUR_MAX];
static uint8_t s_tour_arg[TUT_TOUR_MAX];
static uint8_t s_tour_n;

// ⚠️ The accent the Intl chapter picks is KEPT: the one setting a lesson deliberately
// changes, agreed for this instance ("for that one instance it would be fine to accept
// the alternative"). Everything else the lesson touches — the layout, the layers, the
// preview language — is handed back.

// The first key on `layer` holding `kc` that has a panel, or TUT_SLOT_NONE.
static uint8_t tut_find_slot(uint8_t layer, uint16_t kc) {
    for (uint8_t r = 0; r < MATRIX_ROWS; ++r) {
        for (uint8_t c = 0; c < MATRIX_COLS; ++c) {
            if (keymaps[layer][r][c] != kc) continue;
            const uint8_t slot = tutorial_slot_of(r, c);
            if (slot != TUT_SLOT_NONE) return slot;
        }
    }
    return TUT_SLOT_NONE;
}

// The picker keycode for slot n (KC_LAT10/11 are not contiguous with 0..9).
static uint16_t latin_slot_keycode(uint8_t n) {
    if (n <= 9) return (uint16_t)(KC_LAT0 + n);
    return n == 10 ? KC_LAT10 : KC_LAT11;
}

// Everything about a step that follows from its KIND, in flash. These used to be three
// per-step RAM arrays filled beside the slots (84 B of SRAM on a chip with ~1.7 KB free)
// holding nothing a kind does not already decide.
typedef struct {
    uint8_t after;   // the layer on top of _L0 once pressed; 0xFF none (ENFORCED)
    uint8_t allow;   // a layer the user may HOLD here, allowed but never forced; 0xFF none
    uint8_t need;    // TUT_NEED_* / TUT_INERT
    uint8_t dwell;   // TUT_TOUR_SEEN in 100 ms units (0 = the default)
    uint8_t prog;    // the progress keycap's value
} tut_tour_kind_info_t;

static const tut_tour_kind_info_t k_tour_kind[] = {
    [TUT_TOUR_LANG]        = {_LL,   0xFFu,     0u, 0u, TUT_PROG_LANGMENU},
    // A tab's dwell holds the cascade (features/anim/menu_cascade.c, ~1.7 s with the last fade)
    // plus a moment to look.
    [TUT_TOUR_LCAT]        = {_LL,   0xFFu,     0u, 26u, TUT_PROG_LANGMENU},
    [TUT_TOUR_BASE_LL]     = {0xFFu, 0xFFu,     0u, 0u, TUT_PROG_LANGMENU},
    [TUT_TOUR_EMJ]         = {_EMJ,  0xFFu,     0u, 0u, TUT_PROG_EMOJI},
    [TUT_TOUR_ECAT]        = {_EMJ,  0xFFu,     0u, 26u, TUT_PROG_EMOJI},
    [TUT_TOUR_EPAGE]       = {_EMJ,  0xFFu,     0u, 26u, TUT_PROG_EMOJI},
    [TUT_TOUR_BASE_EMJ]    = {0xFFu, 0xFFu,     0u, 0u, TUT_PROG_EMOJI},
    // A held layer: allowed, never forced, 3 s to look at it.
    [TUT_TOUR_FN]          = {0xFFu, _FL,       0u, 30u, TUT_PROG_LAYERS},
    [TUT_TOUR_NUM]         = {0xFFu, _NL,       0u, 30u, TUT_PROG_LAYERS},
    [TUT_TOUR_INTL_LOOK]   = {0xFFu, _ADDLANG1, 0u, 30u, TUT_PROG_INTL},
    // "Hold Intl again" only arms the next step: almost no dwell.
    [TUT_TOUR_INTL_ARM]    = {0xFFu, _ADDLANG1, 0u, 3u, TUT_PROG_INTL},
    [TUT_TOUR_INTL_CTRL]   = {0xFFu, _ADDLANG1, TUT_NEED_INTL | TUT_NEED_CLOSED, 5u, TUT_PROG_INTL},
    [TUT_TOUR_INTL_LETTER] = {0xFFu, _ADDLANG1, TUT_NEED_INTL | TUT_NEED_PICKER, 8u, TUT_PROG_INTL},
    [TUT_TOUR_INTL_ALT]    = {0xFFu, _ADDLANG1, TUT_NEED_INTL | TUT_NEED_PICKER, 0u, TUT_PROG_INTL},
    [TUT_TOUR_INTL_AGAIN]  = {0xFFu, _ADDLANG1, 0u, 3u, TUT_PROG_INTL},
    [TUT_TOUR_INTL_TYPE]   = {0xFFu, _ADDLANG1, TUT_NEED_INTL | TUT_INERT, 20u, TUT_PROG_INTL},
};
_Static_assert(sizeof(k_tour_kind) / sizeof(k_tour_kind[0]) == TUT_TOUR_INTL_TYPE + 1u,
               "every tour kind needs a k_tour_kind row");

// Append one step (skipped when its key has no panel, or the tour is full).
static void tut_tour_add(tut_tour_step_t *out, uint8_t kind, uint8_t arg, uint8_t slot) {
    if (slot == TUT_SLOT_NONE || s_tour_n >= TUT_TOUR_MAX) return;
    s_tour_kind[s_tour_n] = kind;
    s_tour_arg[s_tour_n]  = arg;
    out[s_tour_n]         = (tut_tour_step_t){slot, k_tour_kind[kind].dwell, k_tour_kind[kind].prog};
    s_tour_n++;
}

// The kind info for step `step` (callers bound-check it first).
static const tut_tour_kind_info_t *tut_step_info(int16_t step) {
    return &k_tour_kind[s_tour_kind[step]];
}

// Choose the Intl chapter's letter and accent: a letter on the base layer with at least
// three accents on the picker's first page, and an accent that is NOT the one already
// picked (choosing the current one would change nothing on screen). `seed` varies the
// letter on the master; the slave builds with any seed and is SENT the two slots over
// the link (tut[5]), so the halves cannot disagree about which keys to point at.
static bool tut_choose_intl(uint32_t seed, uint16_t *letter, uint8_t *alt_slot) {
    // Vowels only (hardware round 34): their accents are the ones a reader recognises
    // (é, ü, å), where a consonant's row is mostly marks nobody types. The pass over
    // every letter stays as the fallback, for a keymap that moved all five vowels off
    // the base layer.
    static const uint16_t k_vowels[] = {KC_A, KC_E, KC_I, KC_O, KC_U};
    uint16_t cand[26];
    uint8_t  n = 0;
    for (uint8_t pass = 0; pass < 2 && n == 0; ++pass) {
        const uint8_t count = pass == 0 ? (uint8_t)(sizeof(k_vowels) / sizeof(k_vowels[0])) : (uint8_t)26u;
        for (uint8_t i = 0; i < count; ++i) {
            const uint16_t kc = pass == 0 ? k_vowels[i] : (uint16_t)(KC_A + i);
            if (!latin_has_row(kc) || tut_find_slot(_BL, kc) == TUT_SLOT_NONE) continue;
            const uint8_t row = latin_picker_row(kc, false);
            uint8_t       on_page = latin_variation_count(row);
            if (on_page > LATIN_PICKER_SLOTS) on_page = LATIN_PICKER_SLOTS;
            if (on_page >= 3) cand[n++] = kc;
        }
    }
    if (n == 0) return false;
    const uint16_t kc  = cand[seed % n];
    const uint8_t  row = latin_picker_row(kc, false);
    uint8_t        on_page = latin_variation_count(row);
    if (on_page > LATIN_PICKER_SLOTS) on_page = LATIN_PICKER_SLOTS;
    const uint8_t cur = latin_pick_get(get_global_latin_table()->ex,
                                       latin_pick_field(latin_target_slot(kc), false));
    for (uint8_t k = 0; k < on_page; ++k) {
        const uint8_t s = (uint8_t)((1u + (seed >> 8) + k) % on_page);
        if (s == cur) continue;
        if (tut_find_slot(_ADDLANG1, latin_slot_keycode(s)) == TUT_SLOT_NONE) continue;
        *letter   = kc;
        *alt_slot = s;
        return true;
    }
    return false;
}

uint8_t tutorial_tour_build(tut_tour_step_t out[TUT_TOUR_MAX], uint32_t seed) {
    s_tour_n = 0;
    // A menu whose opening key is missing is skipped whole: its tabs cannot be reached.
    const uint8_t lang = tutorial_lang_slot();
    if (lang != TUT_SLOT_NONE) {
        tut_tour_add(out, TUT_TOUR_LANG, 0, lang);
        // Every region tab but the one already open: pressing it would change nothing on
        // screen. The region is synced, so both halves skip the same tab and agree on the
        // steps. (The empty-region test is a guard only; all six regions carry languages.)
        for (uint8_t r = 0; r < NUM_LANG_REGIONS; ++r) {
            if (r == lang_active_region() || lang_region_count(r) == 0) continue;
            tut_tour_add(out, TUT_TOUR_LCAT, r, tut_find_slot(_LL, LCAT(r)));
        }
        tut_tour_add(out, TUT_TOUR_BASE_LL, 0, tut_find_slot(_LL, KC_BASE));
    }
    const uint8_t emj = tut_find_slot(_BL, TO(_EMJ));
    if (emj != TUT_SLOT_NONE) {
        tut_tour_add(out, TUT_TOUR_EMJ, 0, emj);
        uint8_t tabs[2 * TUT_EMJ_TABS_PER_HALF];
        uint8_t n_tabs = 0;
        for (uint8_t half = 0; half < 2; ++half) {
            const uint8_t *cand = half ? s_tut_emj_right : s_tut_emj_left;
            uint8_t        took = 0;
            for (uint8_t i = 0; i < 3 && took < TUT_EMJ_TABS_PER_HALF; ++i) {
                if (cand[i] == emj_active_category() || emj_page_count(cand[i]) == 0) continue;
                tabs[n_tabs++] = cand[i];
                took++;
            }
        }
        // The page key follows the last tab whose category HAS a second page, so the
        // press visibly turns it (the arrow is blank on a one-page category).
        uint8_t page_after = 0xFFu;
        for (uint8_t i = 0; i < n_tabs; ++i) {
            if (emj_page_count(tabs[i]) > 1) page_after = i;
        }
        for (uint8_t i = 0; i < n_tabs; ++i) {
            tut_tour_add(out, TUT_TOUR_ECAT, tabs[i], tut_find_slot(_EMJ, KC_EMJ_CAT(tabs[i])));
            if (i == page_after) {
                tut_tour_add(out, TUT_TOUR_EPAGE, 0, tut_find_slot(_EMJ, KC_EMJ_PAGE_NEXT));
            }
        }
        tut_tour_add(out, TUT_TOUR_BASE_EMJ, 0, tut_find_slot(_EMJ, KC_BASE));
    }
    // Hold Fn, then hold Num: a layer that exists only while its key is down, so the
    // step ALLOWS the layer rather than enforcing it, and dwells 3 s to look at it.
    tut_tour_add(out, TUT_TOUR_FN, 0, tut_find_slot(_BL, MO(_FL)));
    tut_tour_add(out, TUT_TOUR_NUM, 0, tut_find_slot(_BL, MO(_NL)));
    // The Intl chapter. Skipped whole if the board has no Intl key, no Ctrl, or no letter
    // with accents to pick from.
    const uint8_t intl = tut_find_slot(_BL, MO(_ADDLANG1));
    const uint8_t ctrl = tut_find_slot(_BL, KC_LEFT_CTRL);
    uint16_t      letter = KC_NO;
    uint8_t       alt    = 0;
    if (intl != TUT_SLOT_NONE && ctrl != TUT_SLOT_NONE && tut_choose_intl(seed, &letter, &alt)) {
        const uint8_t lslot = tut_find_slot(_BL, letter);
        const uint8_t aslot = tut_find_slot(_ADDLANG1, latin_slot_keycode(alt));
        tut_tour_add(out, TUT_TOUR_INTL_LOOK, 0, intl);
        tut_tour_add(out, TUT_TOUR_INTL_ARM, 0, intl);
        tut_tour_add(out, TUT_TOUR_INTL_CTRL, 0, ctrl);
        tut_tour_add(out, TUT_TOUR_INTL_LETTER, 0, lslot);
        tut_tour_add(out, TUT_TOUR_INTL_ALT, alt, aslot);
        tut_tour_add(out, TUT_TOUR_INTL_AGAIN, 0, intl);
        tut_tour_add(out, TUT_TOUR_INTL_TYPE, 0, lslot);
    }
    return s_tour_n;
}

// The layer the lesson should be on right now, on top of _L0 (0xFF: _L0 alone). While a
// step WAITS, it is what the previous step left on; once pressed, what this one did.
static uint8_t tut_tour_layer(void) {
    const int16_t step = tutorial_tour_step();
    if (step < 0 || step >= s_tour_n) return 0xFFu;
    if (tutorial_tour_seen()) return tut_step_info(step)->after;
    return step == 0 ? 0xFFu : tut_step_info(step - 1)->after;
}

// A layer the current step lets the user HOLD (Fn, Num, Intl), or 0xFF.
static uint8_t tut_tour_allowed_layer(void) {
    const int16_t step = tutorial_tour_step();
    return (step < 0 || step >= s_tour_n) ? 0xFFu : tut_step_info(step)->allow;
}

// Can the current step's key be pressed right now? Only the Intl chapter says no: its
// Ctrl, letter and accent do something else entirely without Intl held and the picker
// in the right state (the letter would TYPE its accent).
static bool tut_tour_step_ready(void) {
    const int16_t step = tutorial_tour_step();
    if (step < 0 || step >= s_tour_n) return true;
    const uint8_t need = tut_step_info(step)->need;
    if ((need & TUT_NEED_INTL) && !IS_LAYER_ON(_ADDLANG1)) return false;
    if ((need & TUT_NEED_PICKER) && !poly_picker_latched()) return false;
    if ((need & TUT_NEED_CLOSED) && poly_picker_latched()) return false;
    return true;
}

static bool tut_tour_step_inert(void) {
    const int16_t step = tutorial_tour_step();
    return step >= 0 && step < s_tour_n && (tut_step_info(step)->need & TUT_INERT) != 0u;
}

// Master, every housekeeping pass: the Intl picker only exists while Intl is held, so a
// user who lets go mid-sequence would face a Ctrl or a letter that no longer does what
// the lesson says. Rewind to the step that asks for the hold again; and a picker closed
// by a second Ctrl tap rewinds to the Ctrl step.
static void poly_tutorial_tour_rewind_if_let_go(void) {
    const int16_t step = tutorial_tour_step();
    if (step < 0 || step >= s_tour_n || tutorial_tour_seen()) return;
    const uint8_t need = tut_step_info(step)->need;
    if (!(need & (TUT_NEED_INTL | TUT_NEED_PICKER))) return;
    uint8_t back = 0xFFu;
    if ((need & TUT_NEED_INTL) && !IS_LAYER_ON(_ADDLANG1)) {
        const uint8_t want = (s_tour_kind[step] == TUT_TOUR_INTL_TYPE) ? TUT_TOUR_INTL_AGAIN
                                                                       : TUT_TOUR_INTL_ARM;
        for (int16_t i = step - 1; i >= 0; --i) {
            if (s_tour_kind[i] == want) { back = (uint8_t)i; break; }
        }
    } else if ((need & TUT_NEED_PICKER) && !poly_picker_latched()) {
        for (int16_t i = step - 1; i >= 0; --i) {
            if (s_tour_kind[i] == TUT_TOUR_INTL_CTRL) { back = (uint8_t)i; break; }
        }
    }
    if (back != 0xFFu) tutorial_tour_rewind(back);
}

// "letter X" for the Intl chapter's prose, the letter in capitals.
//
// ⚠️ Read from the KEY the letter step points at, never from a variable holding the
// draw. The letter is drawn at random on the MASTER; the slave builds its own tour with
// its own seed and only learns the master's key over the link. Prose from the local
// draw said "letter Y" on the right panel while the ring and the pulse sat on E
// (hardware). tutorial_tour_slot() is the synced key on the slave.
static const uint32_t *tut_letter_words(const uint32_t *prefix) {
    uint32_t *const buf = s_tut_prose;
    uint8_t         n = 0;
    for (const uint32_t *p = prefix; *p != 0 && n < 13; ++p) buf[n++] = *p;
    uint32_t cp = 0;
    for (uint8_t i = 0; i < s_tour_n; ++i) {
        if (s_tour_kind[i] == TUT_TOUR_INTL_LETTER) {
            cp = tutorial_slot_letter(tutorial_tour_slot(i));
            break;
        }
    }
    buf[n++] = cp != 0 ? cp : (uint32_t)'?';
    buf[n]   = 0;
    return buf;
}

// The status prose: the LEFT panel opens the sentence, the right finishes it. Every
// line is different — the same words twice read as the board repeating itself.
// `seen` is the dwell after the press, where a held layer is on screen.
const uint32_t *tutorial_tour_line(uint8_t step, bool left, bool seen) {
    if (step >= s_tour_n) return NULL;
    switch (s_tour_kind[step]) {
        case TUT_TOUR_LANG:     return left ? U"Pick yours in" : U"the Lang menu";
        case TUT_TOUR_BASE_LL:  return left ? U"Now back" : U"home";
        case TUT_TOUR_EMJ:      return left ? U"Now for" : U"some emoji";
        case TUT_TOUR_BASE_EMJ: return left ? U"And home" : U"again";
        case TUT_TOUR_EPAGE:    return left ? U"Flip to" : U"the next page";
        case TUT_TOUR_FN:
            if (seen) return left ? U"F1 to F12," : U"and more";
            return left ? U"Hold Fn for" : U"the F-keys";
        case TUT_TOUR_NUM:
            if (seen) return left ? U"Digits and" : U"math keys";
            return left ? U"Hold Num for" : U"a number pad";
        case TUT_TOUR_INTL_LOOK:
            if (seen) return left ? U"Each letter's" : U"chosen accent";
            return left ? U"Hold Intl" : U"for accents";
        case TUT_TOUR_INTL_ARM:   return left ? U"Hold Intl" : U"once more";
        case TUT_TOUR_INTL_CTRL:
            if (seen) return left ? U"The picker" : U"is open";
            // The key is drawn, not named: tutorial_tour_key() frames its legend.
            return left ? U"Keep holding," : U"tap";
        case TUT_TOUR_INTL_LETTER:
            if (seen) return left ? U"Its accents" : U"are on top";
            return left ? U"Pick the" : tut_letter_words(U"letter ");
        case TUT_TOUR_INTL_ALT:
            if (seen) return left ? U"Saved for" : tut_letter_words(U"the ");
            return left ? U"Now take" : U"the lit accent";
        case TUT_TOUR_INTL_AGAIN: return left ? U"Hold Intl" : U"one last time";
        case TUT_TOUR_INTL_TYPE:
            if (seen) return left ? U"That's how" : U"accents work";
            return left ? U"And press" : tut_letter_words(U"the ");
        case TUT_TOUR_LCAT: {
            static const uint32_t *const lines[NUM_LANG_REGIONS][2] = {
                {U"From Canada", U"to Chile"},       // America
                {U"All across", U"Europe"},
                {U"The Middle", U"East"},
                {U"Languages of", U"Africa"},
                {U"The whole of", U"Asia"},
                {U"And down to", U"Oceania"},
            };
            const uint8_t r = s_tour_arg[step];
            return r < NUM_LANG_REGIONS ? lines[r][left ? 0 : 1] : NULL;
        }
        case TUT_TOUR_ECAT:
            switch (s_tour_arg[step]) {
                case 0:  return left ? U"Smileys" : U"and faces";
                case 4:  return left ? U"Animals," : U"big and small";
                case 5:  return left ? U"Plants" : U"and food";
                case 9:  return left ? U"Tools" : U"and objects";
                case 7:  return left ? U"Travel" : U"and places";
                case 8:  return left ? U"Sports" : U"and games";
                default: return left ? U"More" : U"emoji";
            }
        default:
            return NULL;
    }
}

// Round 45: the emoji steps show one emoji on the status panel, after the LEFT line
// ("Smileys 🙂 | and faces"). The left lines are the short ones (38..62 px), so a
// full-size emoji (up to 45 px) still fits the 128 px panel; after the right line it
// would not ("big and small" is 97 px). A pack glyph, so the caller draws it only when
// the font pack actually has it.
uint32_t tutorial_tour_emoji(uint8_t step) {
    if (step >= s_tour_n) return 0;
    switch (s_tour_kind[step]) {
        case TUT_TOUR_EMJ: return 0x1F642;   // 🙂
        case TUT_TOUR_ECAT:
            switch (s_tour_arg[step]) {
                case 0:  return 0x1F642;     // 🙂 smileys and faces
                case 4:  return 0x1F418;     // 🐘 animals, big and small
                case 5:  return 0x1F33B;     // 🌻 plants and food
                case 9:  return 0x1F528;     // 🔨 tools and objects
                case 7:  return 0x2708;      // ✈ travel and places
                case 8:  return 0x26BD;      // ⚽ sports and games
                default: return 0x1F389;     // 🎉 more emoji
            }
        default:
            return 0;
    }
}

const uint32_t *tutorial_tour_key(uint8_t step, bool left, bool seen) {
    if (step >= s_tour_n || left || seen) return NULL;
    // The keycap draws INTL_PICKER_LEGEND in its 14 pt face; the panel draws the same
    // three characters baked at 18 px (ICON_INTL_PICKER_SMALL), because the 14 pt face
    // made the framed key fill the panel. ⚠️ If the keycap legend ever changes, this
    // glyph has to be re-baked (gfx_icons.h) or the panel names a key nobody can see.
    return s_tour_kind[step] == TUT_TOUR_INTL_CTRL ? ICON_INTL_PICKER_SMALL : NULL;
}

uint8_t tutorial_preview_table_row(uint8_t pos) {
    return pos < s_tut_preview_n ? s_tut_preview[pos] : 0xFFu;
}

// The master's live preview entry (applied only while SHOWN), or NULL.
static const tut_preview_t *tut_preview_live(void) {
    const int16_t i = tutorial_preview_index();
    if (i < 0 || i >= s_tut_preview_n) return NULL;
    return &s_tut_preview_all[s_tut_preview[i]];
}

// The item being named or shown, on either half (the slave is sent the table row).
static const tut_preview_t *tut_preview_current(void) {
    const uint8_t row = tutorial_preview_entry();
    return row < TUT_PREVIEW_ALL ? &s_tut_preview_all[row] : NULL;
}

// The status panel's name ends the lead-in's question, so it carries the "?". Only the
// PANEL: the keys spell `name` itself, where a "?" would be a keycap of its own.
const uint32_t *tutorial_preview_name(void) {
    uint32_t *const      buf = s_tut_prose;
    const tut_preview_t *e = tut_preview_current();
    if (e == NULL) return U"...";
    uint8_t n = 0;
    while (n < 14 && e->name[n] != 0) { buf[n] = e->name[n]; n++; }
    buf[n++] = '?';
    buf[n]   = 0;
    return buf;
}

// The status panels read as one sentence: the row's own lead-in on the left, the name on
// the right. Keyed by the table row — the one thing both halves know.
const uint32_t *tutorial_preview_phrase(void) {
    const tut_preview_t *e = tut_preview_current();
    return e != NULL ? e->lead : U"...";
}

// Which half spells the NATIVE name for this item: alternating by table row, so the
// Latin name is not always on the same side ("too static", hardware). The name row is
// the one thing both halves know about the item, so they agree without being told.
static bool tut_native_on(uint8_t side) {
    const uint8_t row = tutorial_preview_entry();
    const uint8_t native_side = (row & 1u) ? 0u : 1u;
    return side == native_side;
}

// ---- the name spelled on the keys (TUT_LANG_NAME) ----
// One half: the Latin name; the other: the language's own name (they swap sides from one
// item to the next, tut_native_on()), per character or as
// pre-rendered tiles; a glyph script spells its Latin name through its own glyphs. A
// name of up to 7 units sits on the middle display row (row 2); a longer one is split
// over row 1 and row 2, the first half on top. Every letter row has 7 panels per half,
// ordered here by board x, built once per half from the shared geometry table.
#define TUT_NAME_ROW_TOP  1u
#define TUT_NAME_ROW      2u
#define TUT_NAME_KEYS     7u
#define TUT_NAME_UNITS   14u
#define TUT_SCRATCH_STRIDE 128   // scratch bytes per page row (as focus_ring.c)
static uint8_t s_tut_name_keys[2][2][TUT_NAME_KEYS];   // [side][row 1/2] display idx, by x
static uint8_t s_tut_name_n[2][2];
static bool    s_tut_name_built[2];

static void tut_name_keys_build(uint8_t side) {
    for (uint8_t r = 0; r < 2; ++r) {
        int16_t        xs[TUT_NAME_KEYS];
        const uint8_t  drow = r == 0 ? TUT_NAME_ROW_TOP : TUT_NAME_ROW;
        uint8_t       *keys = s_tut_name_keys[side][r];
        uint8_t        n    = 0;
        for (uint8_t c = 0; c < MATRIX_COLS; ++c) {
            const uint8_t   idx = (uint8_t)(drow * MATRIX_COLS + c);
            const sa_geom_t g   = startup_anim_key_geom(side != 0, idx);
            if (!g.valid || n >= TUT_NAME_KEYS) continue;
            uint8_t k = n++;
            while (k > 0 && xs[k - 1] > g.cx) {       // insertion sort on x
                xs[k]   = xs[k - 1];
                keys[k] = keys[k - 1];
                --k;
            }
            xs[k]   = g.cx;
            keys[k] = idx;
        }
        s_tut_name_n[side][r] = n;
    }
    s_tut_name_built[side] = true;
}

// The units a half spells for `e`, in left-to-right order: the native name when
// `native`, else the Latin one. Returns the count.
static uint8_t tut_name_units(const tut_preview_t *e, bool native, uint32_t out[TUT_NAME_UNITS]) {
    uint8_t n = 0;
    if (!native || (!e->script && e->native == NULL)) {
        for (; n < TUT_NAME_UNITS && e->name[n] != 0; ++n) {
            uint32_t ch = e->name[n];
            if (ch >= 'a' && ch <= 'z') ch -= 'a' - 'A';
            out[n] = ch;
        }
        return n;
    }
    if (e->script) {   // a glyph script: the Latin name through the script
        for (; n < TUT_NAME_UNITS && e->name[n] != 0; ++n) {
            uint32_t ch = e->name[n];
            if (ch >= 'a' && ch <= 'z') ch -= 'a' - 'A';
            const uint32_t cp = (ch >= 'A' && ch <= 'Z')
                                    ? glyph_script_codepoint(e->value, (uint16_t)(KC_A + (ch - 'A')))
                                    : 0u;
            out[n] = cp != 0 ? cp : ch;
        }
        return n;
    }
    while (n < TUT_NAME_UNITS && e->native[n] != 0) ++n;
    for (uint8_t i = 0; i < n; ++i) {
        // Right to left: the FIRST character goes on the RIGHTMOST key.
        out[i] = e->native[e->rtl ? (uint8_t)(n - 1u - i) : i];
    }
    return n;
}

// How a name of `n` units splits: up to 7 fill row 2 alone; more put the larger half on
// row 1.
static uint8_t tut_name_top(uint8_t n) {
    return n > TUT_NAME_KEYS ? (uint8_t)((n + 1u) / 2u) : 0u;
}

// Which of `n` units lands on key `idx` of this half, or -1: the first `top` units on
// row 1, the rest on row 2, each row centred on its own keys.
static int8_t tut_name_slot_unit(uint8_t side, uint8_t idx, uint8_t n, uint8_t top) {
    for (uint8_t r = 0; r < 2; ++r) {
        const uint8_t first = r == 0 ? 0u : top;
        const uint8_t count = r == 0 ? top : (uint8_t)(n - top);
        const uint8_t avail = s_tut_name_n[side][r];
        if (count == 0 || count > avail) continue;
        // Centred, and when the spare key count is odd the odd key goes to the OUTER
        // edge, so the word leans toward the split (keys are ordered by board x, so the
        // split is the right end of the left half and the left end of the right half).
        const uint8_t slack = (uint8_t)(avail - count);
        const uint8_t start = side == 0 ? (uint8_t)((slack + 1u) / 2u) : (uint8_t)(slack / 2u);
        for (uint8_t k = 0; k < count; ++k) {
            if (s_tut_name_keys[side][r][start + k] == idx) return (int8_t)(first + k);
        }
    }
    return -1;
}

// What this key shows while the board is naming an item (or on the "more" screens).
// Returns 0 (not a name key), 1 (a character in *cp), 2 (a pre-rendered tile in *tile)
// or 3 (a character in *cp, drawn in the heavy splash face).
// The "more" screens, one at a time: the NUMBER on the left half and the WORD on the
// right — first the layouts this firmware knows ("160 | LAYOUTS"), then the alternative
// glyph scripts ("10 FUN | SCRIPTS").
// Read from the enums, so the screens stay true as languages and scripts are added.
// Returns the unit count for THIS half's middle row.
static uint8_t tut_more_units(bool right, bool scripts, uint32_t out[TUT_NAME_UNITS]) {
    uint8_t n = 0;
    if (right) {
        const uint32_t *word = scripts ? U"SCRIPTS" : U"LAYOUTS";
        for (const uint32_t *p = word; *p != 0 && n < TUT_NAME_UNITS; ++p) out[n++] = *p;
        return n;
    }
    char     digits[6];
    uint8_t  nd = 0;
    uint16_t v  = scripts ? (uint16_t)(GLYPH_SCRIPT_COUNT - 1) : (uint16_t)NUM_LANG;
    do { digits[nd++] = (char)('0' + v % 10u); v /= 10u; } while (v != 0 && nd < sizeof(digits));
    while (nd > 0) out[n++] = (uint32_t)digits[--nd];
    // Round 45: "10 FUN | SCRIPTS". The space is a key left blank (the heavy face's
    // space has no ink).
    if (scripts) {
        static const uint32_t fun[] = {' ', 'F', 'U', 'N'};
        for (uint8_t i = 0; i < 4u && n < TUT_NAME_UNITS; ++i) out[n++] = fun[i];
    }
    return n;
}

// The heavy splash face (the one "BOOT- / LOADER!" and the POLY KYBD splash use),
// one character centred on the selected keycap from its measured box.
static void tut_draw_heavy(uint32_t cp) {
    const GFXfont *const one[1] = {poly_heavy_font()};   // not the header: see poly_util.h
    const uint32_t       txt[2] = {cp, 0};
    int8_t               x0 = 0, x1 = 0, y0 = 0, y1 = 0;
    kdisp_gfx_text_bbox(one, 1, txt, &x0, &x1, &y0, &y1);
    kdisp_write_gfx_text(one, 1, (int8_t)(BUFFER_X + (SCREEN_WIDTH - (x1 - x0 + 1)) / 2 - x0),
                         (int8_t)((SCREEN_HEIGHT - (y1 - y0 + 1)) / 2 - y0), txt);
}

static uint8_t tut_name_key(uint8_t row, uint8_t col, uint32_t *cp, const uint8_t **tile) {
    const bool more = tutorial_telling_more();
    if (!more && !tutorial_naming()) return 0;
    if (tutorial_wipe_covers(row, col)) return 0;   // the ring has drawn the item here
    const uint8_t slot = tutorial_slot_of(row, col);
    if (slot == TUT_SLOT_NONE) return 0;
    const uint8_t side = TUT_SLOT_RIGHT(slot) ? 1u : 0u;
    if (!s_tut_name_built[side]) tut_name_keys_build(side);
    uint32_t units[TUT_NAME_UNITS];
    if (more) {
        const uint8_t n = tut_more_units(side != 0, tutorial_more_scripts(), units);
        const int8_t  u = tut_name_slot_unit(side, TUT_SLOT_IDX(slot), n, 0);
        if (u < 0) return 0;
        *cp = units[u];
        return 3;   // heavy face
    }
    const tut_preview_t *e = tut_preview_current();
    if (e == NULL) return 0;
    const bool native = tut_native_on(side);
    if (native && e->strip != NULL) {
        const uint8_t n = e->strip->n;
        const int8_t  u = tut_name_slot_unit(side, TUT_SLOT_IDX(slot), n, tut_name_top(n));
        if (u < 0) return 0;
        *tile = e->strip->tiles + (size_t)u * 360u;
        return 2;
    }
    const uint8_t n = tut_name_units(e, native, units);
    const int8_t  u = tut_name_slot_unit(side, TUT_SLOT_IDX(slot), n, tut_name_top(n));
    if (u < 0) return 0;
    *cp = units[u];
    return 1;
}

static uint32_t tut_name_letter(uint8_t row, uint8_t col) {
    uint32_t       cp   = 0;
    const uint8_t *tile = NULL;
    return tut_name_key(row, col, &cp, &tile) != 0 ? 1u : 0u;
}

// Blit a 72x40 pre-rendered tile (row-major, 9 bytes per row, MSB first) into the
// selected, cleared scratch buffer.
static void tut_draw_name_tile(const uint8_t *tile) {
    uint8_t *buf = get_scratch_buffer();
    for (uint8_t ly = 0; ly < SCREEN_HEIGHT; ++ly) {
        for (uint8_t lx = 0; lx < SCREEN_WIDTH; ++lx) {
            if (tile[ly * 9u + (lx >> 3)] & (0x80u >> (lx & 7u))) {
                buf[(size_t)(ly >> 3) * TUT_SCRATCH_STRIDE + (BUFFER_X + lx)] |=
                    (uint8_t)(1u << (ly & 7));
            }
        }
    }
}

// The language preview's bookkeeping. s_tut_real_lang is the user's language while a
// preview is written over it (0xFF = no preview); s_tut_written_lang is what we wrote,
// so a host SET_LANG that lands mid-preview is recognised as the new real value rather
// than being overwritten and then "restored" to the old one.
static uint8_t s_tut_real_lang    = 0xFF;
static uint8_t s_tut_written_lang = 0xFF;

uint8_t poly_reported_lang(void) {
    const uint8_t cur = get_local_state()->lang;
    if (s_tut_real_lang != 0xFF && cur == s_tut_written_lang) return s_tut_real_lang;
    return cur;
}

void poly_set_host_lang(uint8_t lang) {
    access_local_state()->lang = lang;
    if (s_tut_real_lang != 0xFF) s_tut_real_lang = lang;
}

// The language to STORE. Only the master keeps s_tut_real_lang; the slave receives the
// preview through the ordinary sync and cannot tell it from the user's language. So
// while the lesson runs the slave keeps the language it already stored, or a flush
// then (a suspend, SAVE_EEPROM) would persist a board-only preview on that half.
uint8_t poly_persisted_lang(void) {
    if (!is_keyboard_master() && (tutorial_active() || demo_sync_active())) return load_user_eeconf().lang;
    return poly_reported_lang();
}

// Master only, once per housekeeping pass: write or retire the preview. Returns the
// glyph script the board should DRAW (the preview's, or the user's own).
//
// The demo's language tour writes its preview through here too (demo_preview()), so the
// host-facing guarantees above hold for it unchanged: GET_LANG and the settings save keep
// reading the user's real language.
//
// ⚠️ The whole tutorial runs on TUT_PARKED_LANG, the way tutorial_enter_base_layout()
// parks the layout on _L0. The lesson names letters from keymaps[_BL]
// (tutorial_slot_letter()) while the keycap draws them in the board's language, so on
// Korean the status panel asked for "A" over a key showing a Hangul letter. A script
// item keeps the parked language underneath; only a language item replaces it.
#define TUT_PARKED_LANG LANG_ENUS
static uint8_t poly_tutorial_apply_preview(void) {
    poly_sync_t          *ls = access_local_state();
    const tut_preview_t *e  = tut_preview_live();
    bool    has    = e != NULL;
    bool    script = has && e->script;
    uint8_t value  = has ? e->value : 0;
    if (!has) has = demo_preview(&script, &value);
    bool    lang_has   = has && !script;
    uint8_t lang_value = value;
    if (!lang_has && tutorial_active()) {
        lang_has   = true;
        lang_value = TUT_PARKED_LANG;
    }
    if (lang_has) {
        if (s_tut_real_lang == 0xFF || ls->lang != s_tut_written_lang) {
            s_tut_real_lang = ls->lang;           // first write, or the host moved it
        }
        if (ls->lang != lang_value) {
            ls->lang = lang_value;
            request_disp_refresh();
        }
        s_tut_written_lang = lang_value;
    } else if (s_tut_real_lang != 0xFF) {
        if (ls->lang == s_tut_written_lang && ls->lang != s_tut_real_lang) {
            ls->lang = s_tut_real_lang;
            request_disp_refresh();
        }
        s_tut_real_lang    = 0xFF;
        s_tut_written_lang = 0xFF;
    }
    return (has && script) ? value : get_glyph_script();
}

// Forget the tour's let-through presses. ⚠️ An entry is removed only when its key's
// release arrives while the lesson runs, so a lesson that ENDS with a passed key still
// held (Intl held on an Intl step, Esc held to skip) leaks it; after four leaks a later
// MO() release would be swallowed and its layer left on. Cleared at every arming and at
// the finish edge.
void poly_tutorial_forget_passes(void) {
    for (uint8_t i = 0; i < TUT_PASS_MAX; ++i) s_tut_pass_row[i] = s_tut_pass_col[i] = 0xFFu;
}

// The hold-to-skip timer starts over: the skip key has to be held again from now.
void poly_tutorial_skip_reset(void) {
    s_tut_skip_since = 0;
}

// The tutorial branch of process_record_user(), for an event the key tour does not let
// through (poly_tutorial_tour_passes()). Returns what process_record_user() returns.
bool poly_tutorial_record(keyrecord_t *record) {
    const uint8_t row = record->event.key.row, col = record->event.key.col;
    // ⚠️ SHIFT IS THE ONE EXCEPTION, and it has to be a real one. Chapter 2 asks
    // the user to hold Shift and watch every legend change — and the legends follow
    // local_layer->mods, which only moves if QMK actually registers the modifier.
    // Faking it would make the chapter a simulation of the board rather than the
    // board. A bare Shift hold types nothing, so "nothing reaches the host
    // mid-lesson" survives; this is also the standing rule that modifiers and layer
    // keys must fall through a swallow, which the Intl layer learned twice.
    uint16_t kc = poly_keycode_at(_BL, row, col);
    kc = poly_mt_tap(kc);
    // ⚠️ SHIFT AND THE LAYER KEYS ARE THE EXCEPTIONS, and they have to be real
    // ones. Chapters 2 and 3 ask the user to hold a key and watch every legend
    // change — and the legends follow local_layer->mods and ->layer, which only
    // move if QMK actually registers the modifier / applies the layer. Faking it
    // would make the chapters a simulation of the board rather than the board. A
    // bare Shift or a held Fn types nothing on its own, so "nothing reaches the
    // host mid-lesson" survives; this is also the standing rule that modifiers and
    // layer keys must fall through a swallow, which the Intl layer learned twice.
    if (kc == KC_LEFT_SHIFT || kc == KC_RIGHT_SHIFT) {
        tutorial_hold(TUT_HOLD_SHIFT, record->event.pressed, tutorial_slot_of(row, col));
        return true;        // let QMK register/unregister it
    }
    if (tutorial_is_layer_key(kc)) {
        // The layer chapter's momentary key acts for real, but only IN that chapter.
        if (tutorial_is_chapter3_key(kc) && tutorial_in_layer_chapter()) {
            tutorial_hold(TUT_HOLD_LAYER, record->event.pressed, tutorial_slot_of(row, col));
            return true;
        }
        // ⚠️ Every other layer key: swallow the PRESS, pass the RELEASE. Passing the
        // press let TO(_EMJ) — beside B on the base layer, and DARK in chapter 1 —
        // move the whole board onto the emoji layer, where it latched: every letter
        // key showed an emoji for the rest of the lesson (hardware). "Hidden" is two
        // invariants, blank AND inert, and this key was only blank.
        // The release still goes through: swallowing a layer key's release leaves the
        // board stuck on that layer (the bug MO(_ADDLANG1) shipped), and a release
        // with no press behind it is a no-op for MO/TO/TG/OSL and for KC_BASE, which
        // acts on the press.
        return !record->event.pressed;
    }
    if (record->event.pressed) {
        // A press that is not the key being asked for does NOTHING, deliberately:
        // silence is the correction. The only other meaning a key can carry here
        // is the documented hold-to-skip.
        if (!tutorial_press(tutorial_slot_of(row, col)) && tutorial_is_skip_key(row, col)) {
            if (s_tut_skip_since == 0) s_tut_skip_since = timer_read32();
        }
    } else if (tutorial_is_skip_key(row, col)) {
        s_tut_skip_since = 0;   // released early — the hold has to be continuous
    }
    return false;
}
