// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The key-matrix LEDs: rgb_matrix_indicators_kb() picks what the LEDs show each frame
// (the flash cue, the Doom cue, the slave's bootloader and display-off states, the
// tutorial and AI mode), flash_rgb_tick() drives the flash cue from housekeeping, and
// the RM_* adjust keys repeat while held. Moved out of keys_com/poly_keymap.c. Compiled
// only with RGB_MATRIX_ENABLE.

#include QMK_KEYBOARD_H

#include "keys_com/poly_keymap_internal.h"   // flash_rgb_tick(), rgb_adjust_key_down/up()
#include "lib/system/state.h"                // get_local_state()
#include "lib/drivers/fw_staging.h"          // fw_staging_commit_pending(), fw_staging_fw_up_active()
#include "base/com.h"                        // STATUS_DISP_ON, BOOTLOADER_DISPLAY
#include "features/anim/tutorial_rgb.h"      // tutorial_rgb_paint()
#include "features/modes/ai_mode.h"          // ai_rgb_paint()
#include "doom/doom_mode.h"                  // doom_rgb_indicators()

#ifdef RGB_MATRIX_ENABLE

// Font-pack / firmware flash: light the whole matrix (breathing cyan) so the user
// sees the board is busy updating and must not unplug it. The override below only
// writes the per-frame LED buffer — the persistent mode/hue/sat/val config is
// untouched, so the previous effect resumes automatically when flashing ends.
// flash_rgb_tick(), called from housekeeping_task_user(), drives s_flash_rgb_active
// with a short hold (so the brief gaps between the six bundles don't flicker it) and
// re-enables RGB if it was off, disabling it again afterwards.
#define FLASH_RGB_HOLD_MS 2500
// The slave shows the cue this much later, to match the master. While a flash
// stages, the master's main loop relays every chunk to the slave and runs its LED
// task only about every 80 ms (the slave: every frame), so each brightness the
// master shows is about 50 ms old. The HIL rig measured the master 47.6 to 60.2 ms
// behind the slave over seven runs (2026-11-26); agreed with the maintainer. If
// staging gets faster or slower, re-measure: the rig's phase check reports it.
#define FLASH_RGB_SLAVE_LAG_MS 50u
static bool     s_flash_rgb_active      = false;
static bool     s_flash_rgb_was_enabled = false;
static uint16_t s_flash_rgb_seen        = 0;

// Continuously suppress RGB on the bridge when display is off.
// The split transport may re-enable RGB by copying master's rgb_matrix_config; this
// indicator callback runs every render cycle (before flush) and zeros the LED buffer,
// ensuring LEDs stay dark regardless of what the transport wrote to enable.
bool rgb_matrix_indicators_kb(void) {
    if (s_flash_rgb_active) {
        // The split's shared clock, so both halves breathe in step. Each half's own
        // timer_read32() counts from its own boot, and the halves boot at different
        // moments: they breathed up to a whole cycle apart (744 ms measured, 2026-11-26).
        // The apply and the FW-2 prompt stream no chunks, so the master's LED task
        // runs every frame there and the slave needs no delay.
        bool     cant_type = fw_staging_commit_pending() || get_local_state()->fw_confirm;
        uint32_t now       = sync_timer_read32();
        if (!is_keyboard_master() && !cant_type) now -= FLASH_RGB_SLAVE_LAG_MS;   // see the define
        uint8_t phase = (uint8_t)(now >> 3);                    // ~2 s cycle
        uint8_t tri   = phase < 128 ? phase : (uint8_t)(255 - phase);  // triangle breath 0..127..0
        uint8_t v     = 5 + (tri >> 2);                         // ~5..36 (half brightness — bright enough)
        if (cant_type) {
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
void flash_rgb_tick(void) {
    // Light the matrix while a flash is staging, and also while an apply is pending
    // (commit_pending) so a standalone "apply staged firmware" still shows the orange
    // reboot cue even though no chunks are streaming.
    // ...and while the FW-2 confirmation prompt is up (synced, so both halves glow):
    // the board is modal there, so it belongs to the same "can't type" cue.
    // ⚠️ Staging lights the cue from the FIRST WRITTEN CHUNK, not from BEGIN. During
    // the erase after BEGIN the master's main loop runs only between erase units and
    // the host's BEGIN polls, so its LED task hardly runs: the rig camera saw the
    // slave light 601 ms before the master in two runs, then the master hold one
    // level for 1.35 s (2026-11-27). Chunks flow only once both halves finished
    // erasing, so both start within one relayed chunk. The status OLED still shows
    // the update screen from BEGIN.
    if ((fw_staging_fw_up_active() && fw_staging_written()) || fw_staging_commit_pending() ||
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

// An RM_* adjust key went down: apply one step now and repeat while it is held.
void rgb_adjust_key_down(uint16_t keycode) {
    rgb_held_keycode = keycode;
    apply_rgb_adjust(keycode);
    rgb_repeat_token = defer_exec(RGB_REPEAT_INITIAL_DELAY_MS, rgb_repeat_callback, NULL);
}

// The RM_* adjust key went up: stop the repeat and save the result to EEPROM.
void rgb_adjust_key_up(void) {
    rgb_held_keycode = KC_NO;
    cancel_deferred_exec(rgb_repeat_token);
    rgb_repeat_token = INVALID_DEFERRED_TOKEN;
    eeconfig_update_rgb_matrix(&rgb_matrix_config);
}
#endif
