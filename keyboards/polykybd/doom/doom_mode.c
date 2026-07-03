// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Game-mode state machine for the Doom easter egg (dev harness, master half
// only). Owns the runtime handoff of the overlay RAM pool (DOOM_FEASIBILITY.md
// "Challenge 2"): entering game mode borrows the 226,800 B `overlays[]` arena
// as game memory; leaving hands it back blank exactly as a fresh boot / pack
// wipe would, and the host repopulates it on its next overlay push.
#include QMK_KEYBOARD_H

#include "doom_mode.h"
#include "doom_arena.h"
#include "doom_blit.h"
#include "doom_game.h"

#include "bridge_helper.h"
#include "base/overlay.h"
#include "base/update.h"
#include "base/fw_staging.h"

#ifdef POLYKYBD_DOOM

// The engine entry (d_main.c). Declared directly — pulling doom/d_main.h into
// this QMK-side file would drag the whole engine header graph next to
// QMK_KEYBOARD_H for one prototype.
extern void D_DoomMain(void);

// Rooted-but-not-yet-launched: launching needs the core1 handover + the
// scanout loop (next milestone). The volatile gate keeps the call genuinely
// reachable, so the linker must resolve the COMPLETE engine — the flagged
// image carries the real engine cost from here on instead of gc'ing it.
static volatile bool s_engine_launch_enabled = false;

static void doom_engine_start(void) {
    if (s_engine_launch_enabled) {
        D_DoomMain(); // never returns
    }
}

// Trigger: type IDDQD (plain, unmodified) on the master half. Dev-harness
// placeholder — the shipping easter egg gates this behind a held layer so it
// can't fire while actually typing the cheat into a game/editor.
static const uint16_t TRIGGER_SEQ[]   = {KC_I, KC_D, KC_D, KC_Q, KC_D};
#define TRIGGER_LEN (sizeof(TRIGGER_SEQ) / sizeof(TRIGGER_SEQ[0]))
#define TRIGGER_TIMEOUT_MS 3000
// Exit: hold ESC this long while in game mode.
#define DOOM_EXIT_HOLD_MS 1500
// Frame pacing for the placeholder scene (~12 fps).
#define DOOM_FRAME_MS 80

static bool     s_active;
static uint8_t  s_trigger_pos;
static uint32_t s_trigger_last;
static bool     s_esc_down;
static uint32_t s_esc_down_at;
static uint32_t s_last_frame;
static uint8_t *s_fb; // borrowed overlay pool; framebuffer = first 64,000 B

bool doom_mode_active(void) {
    return s_active;
}

// The doom linker script's shared block: pool base, end of the engine's
// zero-init statics (= the arena base), and pool end (base + 226,800). See
// doom_arena.h for the tiering.
extern uint8_t __doom_shared_base__[];
extern uint8_t __doom_shared_statics_end__[];
extern uint8_t __doom_shared_end__[];

uint8_t *doom_arena_at(unsigned offset) {
    return s_active || s_fb ? __doom_shared_statics_end__ + offset : NULL;
}

uint8_t *doom_arena_framebuffer(void) {
    return doom_arena_at(DOOM_ARENA_FB_OFF);
}

uint8_t *doom_arena_zone(int *size) {
    if (size) {
        *size = (int)(__doom_shared_end__ - __doom_shared_statics_end__) - DOOM_ARENA_ZONE_OFF;
    }
    return doom_arena_at(DOOM_ARENA_ZONE_OFF);
}

static void doom_enter(void) {
    // Never take the pool while the fw/font-pack stager owns the split link and
    // flash — the two "exclusive" modes don't compose.
    if (fw_staging_fw_up_active() || fw_staging_commit_pending()) {
        return;
    }
    // Release anything still registered host-side (the trigger letters have
    // already been sent; nothing may stay held while we swallow events).
    clear_keyboard();

    // The runtime handoff: the overlay pool becomes the game arena. The pool
    // is entirely reconstructible — the host re-sends overlays on every app
    // switch, so nothing is lost (see DOOM_FEASIBILITY.md, Challenge 2).
    s_fb = (uint8_t *)get_overlays();
    // Zero the whole shared block: the engine's zero-init statics live at its
    // front (.doom_shared is not crt0-zeroed) — every entry starts the game
    // from virgin static state.
    memset(__doom_shared_base__, 0, (size_t)(__doom_shared_end__ - __doom_shared_base__));
    // The engine statics eat the pool from the front; refuse to start if that
    // left the zone unviable (the link only guards the 226,800 B total).
    int zone_size = 0;
    (void)doom_arena_zone(&zone_size);
    if (zone_size < 40 * 1024) {
        printf("doom: zone too small (%d) — engine statics have outgrown the pool\n", zone_size);
        s_fb = NULL;
        return;
    }
    doom_fire_init(doom_arena_framebuffer());

    s_active     = true;
    s_esc_down   = false;
    s_last_frame = 0;
    set_last_update((int32_t)timer_read32());
    doom_blit_blank_all();
    doom_engine_start();
}

static void doom_exit(void) {
    s_active = false;
    s_fb     = NULL;
    // Hand the pool back in the same state a fresh boot / font-pack wipe leaves
    // it: blank buffers, no usage bits, identity mapping. The host's next
    // overlay push (app switch / reconnect) repopulates it.
    reset_overlay_buffers();
    reset_overlay_usage();
    reset_overlay_mapping();
    reset_fragment_context();
    set_last_update((int32_t)timer_read32());
    request_disp_refresh();
}

bool doom_process_record(uint16_t keycode, bool pressed) {
    if (!s_active) {
        // Trigger matcher — master side only (the game runs where USB is).
        if (!pressed || !is_usb_host_side()) {
            return false;
        }
        if (s_trigger_pos > 0 && timer_elapsed32(s_trigger_last) > TRIGGER_TIMEOUT_MS) {
            s_trigger_pos = 0;
        }
        if (keycode == TRIGGER_SEQ[s_trigger_pos]) {
            s_trigger_last = timer_read32();
            if (++s_trigger_pos == TRIGGER_LEN) {
                s_trigger_pos = 0;
                doom_enter();
                return s_active; // swallow the completing keypress
            }
        } else {
            // restart, allowing the mismatch to begin a new sequence (I-I-D…)
            s_trigger_pos = (keycode == TRIGGER_SEQ[0]) ? 1 : 0;
            s_trigger_last = timer_read32();
        }
        return false;
    }

    // Game mode: swallow EVERYTHING — the host sees no keystrokes. ESC held
    // long enough exits (checked in doom_tick so a press-and-hold needs no
    // repeat events).
    if (keycode == KC_ESC) {
        s_esc_down    = pressed;
        s_esc_down_at = timer_read32();
    }
    // TODO(engine): translate the event into a doom key + D_PostEvent() once
    // the rp2040-doom core is in.
    return true;
}

void doom_tick(void) {
    if (!s_active || !is_usb_host_side()) {
        return;
    }
    if (s_esc_down && timer_elapsed32(s_esc_down_at) > DOOM_EXIT_HOLD_MS) {
        doom_exit();
        return;
    }
    // Hold off the idle/fade/turn-off pipeline — the pulse/jitter machinery
    // must never repaint the keycaps while the blitter owns them.
    set_last_update((int32_t)timer_read32());

    if (timer_elapsed32(s_last_frame) < DOOM_FRAME_MS) {
        return;
    }
    s_last_frame = timer_read32();
    doom_fire_step(doom_arena_framebuffer());
    doom_blit_frame(doom_arena_framebuffer(), doom_fire_luma());
}

bool doom_hid_frozen(uint8_t cmd) {
    if (!s_active) {
        return false;
    }
    switch (cmd) {
        case 10: // overlay segment            (0x0A)
        case 16: // RLE compressed overlay     (0x10)
        case 17: // RLE compressed overlay     (0x11)
        case 18: // start ROI overlay          (0x12)
        case 19: // ROI overlay data           (0x13)
        case 21: // overlay mapping            (0x15)
            // All ACKless bulk writes into the borrowed pool / fragment
            // context — the dispatcher drops them without a reply.
            return true;
        default:
            return false;
    }
}

#endif // POLYKYBD_DOOM
