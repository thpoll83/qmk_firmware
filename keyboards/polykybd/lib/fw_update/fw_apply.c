// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The keyboard side of a firmware apply: the step machine that runs the self-flash one
// stage per housekeeping pass, the reboot path, the status-OLED firmware screens and
// their timers, and the "board unusable" cue. Moved out of keys_com/poly_keymap.c;
// housekeeping_task_user() calls fw_apply_screen_tick() and fw_apply_tick().

#include QMK_KEYBOARD_H

#include "print.h"

#include "keys_com/poly_keymap.h"           // poly_fw_screen_t, set_displays()
#include "lib/drivers/fw_staging.h"         // the staging and apply state
#include "lib/system/poly_util.h"           // clear_all_displays()
#include "base/com.h"                        // DISP_IDLE, IDLE_TRANSITION
#include "lib/system/state.h"               // poly_sync_t, save_all_dirty()
#include "base/update.h"                    // request_disp_refresh()
#include "polymod_core1_owner.h"            // core1_hold() / core1_release()
#include "features/anim/demo_mode.h"        // demo_stop()
#include "features/anim/startup_anim.h"     // startup_anim_stop()
#include "doom/doom_mode.h"                 // doom_screensaver_stop()
#include "base/hand_stamp.h"                // poly_hand_flush_pending()

#include "lib/fw_update/fw_apply.h"

void oled_fw_apply_screen(void);   // oled_helper.c — firmware-apply status screen
void oled_fw_restart_screen(void); // oled_helper.c — "Restart Now" before mcu_reset()
void oled_fw_failed_screen(void);  // oled_helper.c — "Update FAILED", the apply was refused
#ifdef RGB_MATRIX_ENABLE
void rgb_matrix_update_pwm_buffers(void);
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

// How long a firmware notice that does NOT end in a reboot stays on the status OLED.
// Only the failure notice needs it: every other caller is followed by a self-flash or
// a reset, so the panel simply stops being repainted. 5 s is long enough to read two
// words and short enough that the board does not look wedged.
#define POLY_FW_NOTICE_MS 5000

// How long the panel is left UNTOUCHED after a firmware phase ends, so the gap before
// the next phase does not show as a flash of the status screen. Deliberately equal to
// FLASH_RGB_HOLD_MS: the LEDs already bridge these gaps with exactly this timer, and a
// panel that went back to normal while the matrix was still orange was the visible
// half of that asymmetry.
#define POLY_FW_HOLD_MS 2500

static uint16_t s_fw_notice_at = 0;
static bool     s_fw_notice_armed = false;
static uint16_t s_fw_phase_at   = 0;
static bool     s_fw_phase_seen = false;

// Why the last apply was refused, and the numbers behind it. Kept here rather than
// passed down the call because oled_fw_failed_screen() repaints from them on EVERY
// tick the notice is held — the housekeeping pass that recorded them is long gone by
// the second repaint.
static fw_apply_verdict_t s_fw_fail_why  = FW_APPLY_OK;
static uint32_t           s_fw_fail_size = 0;
static uint32_t           s_fw_fail_want = 0;
static uint32_t           s_fw_fail_got  = 0;

fw_apply_verdict_t poly_fw_failure_detail(uint32_t *size, uint32_t *want, uint32_t *got) {
    if (size) *size = s_fw_fail_size;
    if (want) *want = s_fw_fail_want;
    if (got)  *got  = s_fw_fail_got;
    return s_fw_fail_why;
}

// The phase that is live RIGHT NOW, in priority order.
//
// ⚠️ CONFIRM is tested first and from TWO sources. The synced poly_sync_t.fw_confirm
// is what both halves agree on, but the MASTER only sets it from housekeeping — one
// pass AFTER fw_staging_awaiting_confirm() goes true, and COMMIT has already cleared
// fw_up_active by then. That one-pass hole put a status screen between the progress
// bar and the prompt, which is exactly how this was reported from hardware.
static poly_fw_screen_t fw_screen_live(void) {
    if (get_local_state()->fw_confirm || fw_staging_awaiting_confirm()) return POLY_FW_SCREEN_CONFIRM;
    if (fw_staging_fw_up_active())                                      return POLY_FW_SCREEN_UPDATE;
    if (fw_staging_reboot_pending())                                    return POLY_FW_SCREEN_RESTART;
    if (fw_staging_commit_pending())                                    return POLY_FW_SCREEN_APPLY;
    return POLY_FW_SCREEN_NONE;
}

// Stamp the "a firmware phase was live" clock; runs every housekeeping pass.
//
// ⚠️ fw_staging_confirm_in_progress() is deliberately NOT stamped here, though it is
// exactly the second gap (answered prompt -> the host's FW_UP_APPLY). It clears only
// inside fw_staging_finalize(), i.e. only when the host sends another COMMIT — so a
// host that disappears right after the user presses A leaves it true FOREVER, and
// stamping on it would freeze the status OLED on the confirm screen for good. The
// last CONFIRM pass already stamped, so the plain POLY_FW_HOLD_MS window bridges that
// gap anyway, and it cannot latch.
void fw_apply_screen_tick(void) {
    if (fw_screen_live() != POLY_FW_SCREEN_NONE) {
        s_fw_phase_at   = timer_read();
        s_fw_phase_seen = true;
    }
}

poly_fw_screen_t poly_fw_screen(void) {
    const poly_fw_screen_t live = fw_screen_live();
    if (live != POLY_FW_SCREEN_NONE) return live;
    // The refusal notice is LATCHED rather than derived: commit_pending is already
    // clear by the time it is raised, so nothing in the live state describes it.
    if (s_fw_notice_armed && timer_elapsed(s_fw_notice_at) < POLY_FW_NOTICE_MS) {
        return POLY_FW_SCREEN_FAILED;
    }
    s_fw_notice_armed = false;
    return POLY_FW_SCREEN_NONE;
}

bool poly_fw_hold_active(void) {
    if (!s_fw_phase_seen) return false;
    if (timer_elapsed(s_fw_phase_at) < POLY_FW_HOLD_MS) return true;
    s_fw_phase_seen = false;
    return false;
}

// The board is about to stop being usable: latch the orange cue AND blank every
// keycap, so the two halves say the same thing at once.
//
// ⚠️ The keycaps are the half that used to be missing. The orange RGB says "you
// cannot type", while 72 displays went on showing a full, inviting legend set for
// the whole apply — the one moment the keys genuinely do nothing. Both are pushed
// out SYNCHRONOUSLY here for the same reason: the callers are about to enter a
// blocking self-flash or a reset, so anything left to the next housekeeping pass is
// never painted (see poly_flash_rgb_now()'s own note). clear_all_displays() is one
// broadcast write over the shift-register chip-select, not 72 of them.
//
// NOT used for the staging transfer: the board still runs there, the cue is cyan
// rather than orange, and poly_prepare_for_flash() has deliberately just drawn
// legible base legends so you CAN keep typing.
void poly_board_unusable_cue(void) {
    // Take the keycaps off whatever else owns them FIRST, or that writer paints the
    // blank straight back. update_displays() early-returns for both of these, which
    // is precisely the sign that they are writers in their own right — and the idle
    // pulse is a third, reached through set_displays(contrast, idle) -> kdisp_idle()
    // rather than through update_displays() at all. A standalone "apply staged image"
    // can arrive on an idling board with no preceding transfer, so none of this is
    // hypothetical: poly_prepare_for_flash() does the same teardown for the same
    // reason at the START of an update, and this is its counterpart at the end.
    demo_stop();               // the showroom demo, too: the apply owns the board now
    doom_screensaver_stop();   // self-guards: only an active attract demo
    startup_anim_stop();       // looping Eden (and a one-shot mid-flight)
    poly_sync_t* local_state = access_local_state();
    local_state->flags &= ~((uint8_t)DISP_IDLE) & ~((uint8_t)IDLE_TRANSITION);
    poly_flash_rgb_now();
    clear_all_displays();
}

// A refused apply: record why for the status OLED, which repaints the failure screen
// from these on every tick the notice is held, show it now, and give the keycaps their
// legends back (stage 0 blanked them for an apply that is not going to happen). The
// two refusal paths in fw_apply_tick() both end here.
static void fw_apply_show_refusal(fw_apply_verdict_t why, uint32_t size, uint32_t want, uint32_t got) {
    s_fw_fail_why  = why;
    s_fw_fail_size = size;
    s_fw_fail_want = want;
    s_fw_fail_got  = got;
    oled_fw_failed_screen();
    s_fw_notice_at    = timer_read();
    s_fw_notice_armed = true;
    set_displays(get_local_state()->contrast, false);
    request_disp_refresh();
}

// One housekeeping pass of the firmware apply. Returns true when a stage ran and the
// pass must end here: the stages are split across passes ON PURPOSE (see below).
bool fw_apply_tick(void) {
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
                // Orange cue AND 72 blanked keycaps. From here the matrix is never
                // scanned again, so a keycap still showing its legend is advertising a
                // key that does nothing — for the whole multi-second copy.
                poly_board_unusable_cue();
                // Paint the "⟳Applying / Firmware⟳" notice and flush it fully BEFORE the
                // blocking self-flash below (which never returns), so the status OLED is
                // completely refreshed — not torn mid-transition — as the apply begins.
                // Runs on both halves; each draws its own side's word. Its ~26 ms of I2C
                // also gives the clear_keyboard() report time to leave over USB.
                oled_fw_apply_screen();
                uprintf("APPLY 1/4: keys cleared, RGB + keycaps blanked, OLED flushed\n");
                apply_step = 1;
                return true;
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
                core1_hold();
                save_all_dirty();
                core1_release();
                uprintf("APPLY 2/4: EEPROM flushed with core1 held off\n");
                apply_step = 2;
                return true;
            case 2: {
                // Verify the staged bytes IN FLASH and report them, then RETURN so the
                // line actually goes out. Everything below this point is a call that
                // does not come back, and the console only drains from the main loop --
                // a marker printed immediately before it is simply lost, which is how
                // "did it enter the copy?" stayed unanswerable for several rounds.
                uint32_t size = 0, want = 0, got = 0;
                // ⚠️ The out-params stay UNTOUCHED on FW_APPLY_NO_IMAGE (there is no
                // header to read), which is why they are initialised above.
                const fw_apply_verdict_t why = fw_staging_verify_staged_flash(&size, &want, &got);
                uprintf("APPLY 3/4: staged %lu B (%lu sectors) crc want=%08lx got=%08lx -> %s\n",
                        (unsigned long)size,
                        (unsigned long)((size + 4095u) / 4096u),
                        (unsigned long)want, (unsigned long)got,
                        why == FW_APPLY_OK      ? "OK"
                        : why == FW_APPLY_NO_IMAGE ? "NO IMAGE"
                                                   : "MISMATCH");
                if (why != FW_APPLY_OK) {
                    // Refuse rather than erase a working firmware with an image we
                    // cannot vouch for. The board stays usable and the host can retry.
                    uprintf("APPLY: refusing to overwrite firmware from a bad staged image\n");
                    fw_staging_cancel_apply();
                    apply_step = 0;
                    // ⚠️ This is the ONE apply path that comes back, so it is the one
                    // that has to undo the "board is unusable" cue it just put up.
                    // Say WHY on the panel — until now the refusal existed only as the
                    // console line above, so from the outside the update simply did
                    // nothing — and hand the keycaps their legends back, since stage 0
                    // blanked them for an apply that is not going to happen.
                    fw_apply_show_refusal(why, size, want, got);
                    return true;
                }
                apply_step = 3;
                return true;
            }
            default:
                // ⚠️ THE LAST PAINTABLE INSTANT. fw_staging_do_apply() below runs with
                // interrupts off for SECONDS and resets from inside itself, so there is
                // no window after the copy and before the reboot — the panel keeps
                // whatever is on it right now for the whole write and then the board is
                // gone. That is why "Restart Now" was never seen on an update: it lived
                // on fw_staging_arm_reboot()'s path, which an APPLY does not take.
                //
                // So the reboot is named on the apply screen itself, which is already
                // up and stays up for the whole copy — no second paint is needed here,
                // and none is possible after the copy. See oled_fw_apply_screen().
                // Nothing but the call: if the 3/4 line above is the last thing in the
                // log, the copy was entered and did not come back.
                apply_step = 0;
                fw_staging_apply_and_reboot();
                // Only reached when the apply was refused (no valid staged image /
                // failed flash CRC); it disarms itself there and the board keeps
                // running. Stage 2 already handed core1 back, and the refusal checks
                // sit above the PSM halt inside do_apply, so this relaunches a service
                // that is already running today -- kept because it is the one path
                // where a future halt-then-refuse would otherwise leave the RLE
                // service down for good. It respects any outstanding hold.
                (void)core1_restore_service();
                // ...and it is now also the path that has to take the screen back. The
                // board is alive and the panel is showing a restart that is not coming,
                // with the keycaps still blanked from stage 0.
                // Ask which of its two refusals it was rather than assuming. It returns
                // on a missing header OR a failed re-verify, and stage 2 passed moments
                // ago, so whichever it is the user needs the real one. The extra ~25 ms
                // CRC only ever runs on a path that has already failed.
                {
                    uint32_t size = 0, want = 0, got = 0;
                    fw_apply_verdict_t why = fw_staging_verify_staged_flash(&size, &want, &got);
                    if (why == FW_APPLY_OK) why = FW_APPLY_NO_IMAGE;
                    fw_apply_show_refusal(why, size, want, got);
                }
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
        poly_board_unusable_cue();
        // This path had no status-OLED state at all: it went orange and reset while the
        // panel still showed the ordinary status screen. Flushed synchronously, like the
        // apply notice, because mcu_reset() below never returns.
        oled_fw_restart_screen();
        // A handedness change arrives on this path (the reset-sync carrier). The
        // handler only records it -- it runs inside a split-transaction callback
        // with a ~20 ms budget, and this write can erase a sector.
        poly_hand_flush_pending();
        // ⚠️ Under the core1 lockout, like stage 1 of the apply: this can force a
        // wear-levelling consolidation (a flash erase), and core1 fetching from XIP
        // while the QSPI is out of XIP mode stalls the bus, so the erase never returns
        // and the reset below never comes. poly_hand_flush_pending() above holds core1
        // itself. No release: mcu_reset() does not return.
        core1_hold();
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
    return false;
}
