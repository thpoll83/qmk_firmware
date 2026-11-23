// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Phases of housekeeping_task_user() that need nothing private to poly_keymap.c: the
// macro-recorder and confirm-prompt state the slave mirrors, the LTR-559 telemetry and
// drive, the idle fade state machine, and the master-authoritative fields of the synced
// state. Moved out of keys_com/poly_keymap.c; housekeeping_task_user() still calls them
// in the same order as when the code was inline.

#include QMK_KEYBOARD_H

#include "print.h"

#include "keys_com/poly_keymap.h"            // poly_suspend()
#include "keys_com/poly_os.h"                // POLY_OS_AUTO_FLAG
#include "keys_com/poly_macro_record.h"      // the on-keyboard macro recorder
#include "keys_com/poly_keymap_internal.h"   // poly_apply_draw_script()
#include "lib/system/state.h"                // poly_sync_t and the settings getters
#include "lib/split_link/bridge_helper.h"    // is_usb_host_side()
#include "lib/drivers/fw_staging.h"          // the unsigned-image confirmation
#include "base/com.h"                        // STATUS_DISP_ON, DISP_IDLE, IDLE_TRANSITION
#include "base/update.h"                     // the idle timer, request_disp_refresh()
#include "base/idle_style.h"                 // idle_style_desc()
#include "base/status_brightness.h"          // get_active_brightness()
#include "lib/drivers/helpers.h"             // PK_MAX
#include "features/anim/startup_anim.h"      // startup_anim_active()
#include "features/anim/tutorial.h"          // tutorial_active()
#include "doom/doom_mode.h"                  // the Doom game mode, screensaver and pack prompt

// The LTR-559 driver is the polykybd/polymod_ltr559 community module; see the note at
// the same include in poly_keymap.c.
#ifdef COMMUNITY_MODULE_POLYMOD_LTR559_ENABLE
#    include "polymod_ltr559.h"
#    define LTR559_LOG_MS 600000   // sensor telemetry log cadence: 10 min
#endif
#include "lib/peripherals/ltr559_policy.h"   // poly_ltr559_drive() (POLYKYBD_LTR559_DRIVE)


// On-keyboard macro recording. The master owns the gesture (only it runs
// process_record -- the slave's matrix is pulled over the link), and the state is
// mirrored into the synced struct so the SLAVE draws its half of the slot picker.
// Outside housekeeping_task_user()'s !fw_up_active gate for the same reason the FW-2
// prompt is: a dialog that could not be drawn or advanced would be unanswerable.
void macro_rec_sync_tick(void) {
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
        // prompt in fw_confirm_sync_tick() does. The panel IS the only indicator a recording has, and
        // update_displays() early-returns once DISP_IDLE is set — so a slow-typed
        // macro that crossed the idle timeout would dim the picker and the REC readout
        // out from under the user, with no way back short of a keypress that the
        // recorder would then capture.
        if (rs != (uint8_t)POLY_REC_IDLE) {
            update_performed();
        }
    }
}

// FW-2: drive the unsigned-image confirmation prompt. The master owns the
// decision, but poly_sync_t.fw_confirm is synced so BOTH halves turn their
// keycaps into the dialog (the slave's own render is driven by the sync
// handler's refresh). Deliberately outside housekeeping_task_user()'s !fw_up_active
// gate: the prompt only goes up at COMMIT, i.e. after finalize has already cleared
// fw_up_active, and a prompt that could not be drawn would be unanswerable.
// is_keyboard_master(), not is_usb_host_side(): the confirmation state only
// ever exists on the half that verified the signature, and fw_staging gates
// that on is_keyboard_master() (which the HIL images override per side).
void fw_confirm_sync_tick(void) {
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
}

void ltr559_housekeeping_tick(void) {
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

void idle_fade_tick(void) {
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
}

void master_state_sync_tick(void) {
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
        // legends on the SLAVE half follow a change made here. idle_fade_tick()
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
