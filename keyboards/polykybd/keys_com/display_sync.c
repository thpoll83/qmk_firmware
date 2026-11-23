// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The per-pass display sync and the choice of who draws the keycaps. The master
// pushes the synced state, the MRU lists, the latin table, the layer state and the
// last key to the slave; both halves then apply a state change and start or continue
// a keycap refresh. Also the Eden idle loop and the hand-off from Eden to the
// first-run tutorial. Moved out of keys_com/poly_keymap.c without behaviour changes.

#include QMK_KEYBOARD_H

#include "print.h"
#include "debug.h"
#include "oled_driver.h"
#include <transactions.h>   // the USER_SYNC_* transaction ids
#include "split_util.h"   // is_transport_connected() — gate the boot layer-resync one-shot

#include "keys_com/poly_keymap.h"            // update_displays(), set_displays()
#include "keys_com/poly_keymap_internal.h"   // the tutorial calls, status_oled_level()
#include "keys_com/layers.h"                 // _NL
#include "keys_com/lang_layer.h"             // lang_pack_state()
#include "keys_com/mru.h"                    // the MRU push
#include "lib/system/state.h"                // poly_sync_t, poly_layer_t, get_active_brightness()
#include "lib/split_link/bridge_helper.h"    // send_to_bridge(), differ(), is_usb_host_side()
#include "lib/split_link/split_sync.h"       // mru_sync_t
#include "lib/display/status_idle.h"         // status_idle_holds_bus() (split72 only)
#include "polymod_core1_owner.h"             // core1_hold(), core1_release()
#include "base/com.h"                        // the flag helpers
#include "base/update.h"                     // the refresh mode, the overlay burst
#include "base/idle_style.h"                 // IDLE_STYLE_EDEN, EDEN_IDLE_BRIGHTNESS
#include "base/tutorial_plan.h"              // TUT_SYNC_ARMED
#include "dev/profiling/loop_profile.h"
#ifdef POLYKYBD_LOOP_PROFILE
#    include "hardware/structs/timer.h"   // timer_hw->timerawl — raw 1 MHz us counter
#endif
#include "features/anim/startup_anim.h"
#include "features/anim/tutorial.h"
#include "features/anim/demo_mode.h"         // demo_display_mods()
#include "features/anim/menu_cascade.h"      // menu_cascade_tick()
#include "features/anim/lang_sparkle.h"      // lang_sparkle_tick()
#include "features/emoji/emoji_layer.h"      // emj_active_category(), emj_search_export()
#include "features/modes/midi_legend.h"      // midi_cfg_pack()

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
// FORCE_LAYER_RESYNC_TRIES is in poly_keymap_internal.h; keyboard_post_init_user() re-arms both.
bool g_force_layer_resync = true;
uint8_t g_force_resync_tries = FORCE_LAYER_RESYNC_TRIES;

// Freeze slave display while bootloader is active; re-assert RGB each cycle.
// True = the slave is frozen and sync_and_refresh_displays() does nothing else.
static bool slave_bootloader_freeze(void) {
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
        return true;
    }
    return false;
}

// Master: copy the master-side values into the local synced state. Returns the
// local flags.
static uint8_t master_fill_local_state(uint8_t global_flags) {
    uint8_t local_flags = set_flag(get_local_state()->flags, DBG_ON, debug_enable);
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
    return local_flags;
}

// Master: push the synced state when it differs from the last synced copy. Returns
// true only when there was a diff AND the slave acked it.
static bool master_push_state(void) {
    bool state_diff = differ(get_local_state(), get_global_state(), sizeof(poly_sync_t));
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
            // Failed: clear state_diff so the copy_global_state() in
            // apply_state_diff() is
            // SKIPPED — global stays != local, so next pass differ() is still
            // true and re-fires the send. The diff IS the retry queue; global
            // only advances to local on a successful sync.
            state_diff = false;
            uprint("USER_SYNC_POLY_DATA failed to send\n");
        }
    }
    return state_diff;
}

// Master: the pushes that carry their own pending flag, the MRU lists and the
// latin table.
static void master_push_mru_and_latin(void) {
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
}

// Master: copy the lock state and the modifiers into the local layer snapshot.
static void master_fill_local_layer(void) {
    access_local_layer()->led_state = host_keyboard_led_state();
    // ⚠️ The tutorial holds caps INTERNALLY for chapter 1, and this is the only place
    // it can: the line above re-reads the host's real lock state every pass, so an
    // override set anywhere else is erased before update_displays() runs (which it
    // does from run_refresh(), later in the same sync_and_refresh_displays() pass). It was previously set in the
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
}

// Master: push the layer state on a diff, or for the bounded boot resync. Returns
// true only when the layer state was sent and acked.
static bool master_push_layer(void) {
    bool layer_diff = differ(get_local_layer(), get_global_layer(), sizeof(poly_layer_t));
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
            layer_diff = true;             // ensure global advances (copy_global_layer in sync_and_refresh_displays())
        } else {
            layer_diff = false; // failed: skip copy_global_layer() in sync_and_refresh_displays() so a real diff
                                // persists and the send re-fires next pass
            uprint("USER_SYNC_LAYER_DATA failed to send\n");
        }
    }
    return layer_diff;
}

// Master: push the last latin key on a diff.
static void master_push_last_latin(void) {
    if ( differ(get_local_last_latin(), get_global_last_latin(), sizeof(poly_last_t)) ) {
        if(sync_succeeded(send_to_bridge(USER_SYNC_LASTKEY_DATA, access_local_last_latin(), sizeof(poly_last_t), PERIODIC_SYNC_RETRIES))) {
            copy_global_last_latin(get_local_last_latin());
        } else {
            // if failed to sync, do not consider it a diff and try again later
            uprint("USER_SYNC_LASTKEY_DATA failed to send\n");
        }
    }
}

// Both halves: act on a synced state change (status OLED, RGB, contrast), then
// advance the global copy and request a refresh.
static void apply_state_diff(uint8_t local_flags, uint8_t global_flags) {
    const bool in_idle_mode = (local_flags & DISP_IDLE) != 0;

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

// Overlay-burst coalescing: while a program-switch overlay burst is still
// arriving, DEFER starting a fresh render. Each report would otherwise redraw
// half-staged overlays that the next report obsoletes (measured ~12 full renders
// per switch). Render on whichever fires first: the burst going quiet
// (OVERLAY_COALESCE_QUIET_MS), enough overlays piling up to stay reactive on a
// long transfer (OVERLAY_COALESCE_FLUSH_COUNT), or the hard cap
// (OVERLAY_COALESCE_MAX_MS). Only a FRESH render is held — a render already in
// progress (START_SECOND_HALF) always finishes, and non-overlay refreshes fall
// straight through (their overlay_activity_elapsed() is already large, pending 0).
// The state/bridge sync earlier in sync_and_refresh_displays() already ran, so deferring the render doesn't stall
// the slave; the loop stays fast (no ~100 ms render) while held.
// True = hold the render this pass.
static bool overlay_burst_hold(enum refresh_mode refresh) {
    static bool     s_holding    = false;
    static uint32_t s_hold_since = 0;
    if (refresh == ALL_AT_ONCE || refresh == START_FIRST_HALF) {
        bool settled = overlay_activity_elapsed() >= OVERLAY_COALESCE_QUIET_MS;
        bool enough  = overlay_pending_count()   >= OVERLAY_COALESCE_FLUSH_COUNT;
        if (!settled && !enough) {
            if (!s_holding) { s_holding = true; s_hold_since = timer_read32(); }
            if (timer_elapsed32(s_hold_since) < OVERLAY_COALESCE_MAX_MS) {
                return true;   // hold the render; g_refresh stays pending for a later pass
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
    return false;
}

// Start or continue the keycap refresh: the first half, the second half, or all at once.
static void run_refresh(enum refresh_mode refresh) {
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

void sync_and_refresh_displays(void) {
    if (slave_bootloader_freeze()) {
        return;
    }

    bool layer_diff = false;
    bool state_diff = false;

    uint8_t local_flags;
    uint8_t global_flags = get_global_state()->flags;

    if (is_usb_host_side()) {

        local_flags = master_fill_local_state(global_flags);
        state_diff = master_push_state();
        master_push_mru_and_latin();
        master_fill_local_layer();
        layer_diff = master_push_layer();
        master_push_last_latin();
    } else {
        layer_diff = differ(get_local_layer(), get_global_layer(), sizeof(poly_layer_t));
        state_diff = differ(get_local_state(), get_global_state(), sizeof(poly_sync_t));

        local_flags = get_local_state()->flags;
    }

    if(state_diff) {
        apply_state_diff(local_flags, global_flags);
    }

    if(layer_diff) {
        copy_global_layer(get_local_layer());   // advance global only on a synced change (see send site)
        request_disp_refresh();
    }

    enum refresh_mode refresh = get_refresh_mode();

    if (overlay_burst_hold(refresh)) {
        return;
    }

    run_refresh(refresh);
}

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
void eden_idle_tick(void) {
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
void arm_tutorial_after_intro(void) {
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

// Who draws the keycaps this pass: the ONE-SHOT boot/KC_EDEN animation, the chapter 1
// tutorial, or the normal path. In this file with s_tutorial_armed, which the Eden
// finish edge reads.
void housekeeping_display_tick(void) {
    // One-time startup animation: render a frame while active (both halves
    // render their own keycaps). On the finishing edge, persist the "played"
    // marker and request a normal refresh so the base legends come back. Gated
    // to the ONE-SHOT animation — the looping idle screensaver is driven by
    // eden_idle_tick() in housekeeping_task_user() and must not run the finish edge / nonce sync here.
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
}
