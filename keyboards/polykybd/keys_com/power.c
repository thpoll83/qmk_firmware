// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Suspend, wake and shutdown: the boot splash's first step, the keycap contrast
// (set_displays()), the wake paths (key press, host "stop idle", USB resume), the USB
// suspend hook with its core1 hold and EEPROM flush, and the flush before every reset.
// Moved out of keys_com/poly_keymap.c; the calls and their order are unchanged.

#include QMK_KEYBOARD_H

#include "print.h"
#include "usb_util.h"                        // usb_vbus_state(): the suspend-time power check

#include "keys_com/poly_keymap.h"            // kdisp_idle(), reset_idle_jitter(), sync_and_refresh_displays()
#include "keys_com/poly_keymap_internal.h"   // restore_suspended_overlays()
#include "lib/system/state.h"                // poly_sync_t, save_all_dirty()
#include "lib/system/poly_util.h"            // select_all_displays()
#include "lib/system/boot_diag.h"            // splash_progress()
#include "lib/drivers/disp_array.h"          // kdisp_enable(), kdisp_set_contrast()
#include "lib/drivers/crash_record.h"        // the watchdog, the phase breadcrumb
#include "lib/display/multicore_exec.h"      // core1_suspend_tick(), core1_resume()
#include "base/com.h"                        // STATUS_DISP_ON, DISP_IDLE, DISPLAY_OVERLAYS
#include "base/update.h"                     // the idle timer, request_disp_refresh()
#include "base/status_brightness.h"          // get_active_brightness()
#include "features/anim/startup_anim.h"      // startup_anim_stop(), POLY_INTRO_CONTRAST
#include "features/anim/tutorial.h"          // tutorial_active()
#include "features/modes/ai_mode.h"          // ai_reset_keys()
#include "doom/doom_mode.h"                  // doom_screensaver_stop()

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
bool restore_suspended_overlays(poly_sync_t* local_state) {
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

// Clears overlay display flags, disables overlays and status display, sets contrast to OFF.
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
