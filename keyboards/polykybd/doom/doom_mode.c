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
#include "doom_playpal_luma.h"

#include "bridge_helper.h"
#include "base/overlay.h"
#include "base/update.h"
#include "base/fw_staging.h"
#include "base/multicore/core1.h"

#include "doomkeys.h"           // engine key codes (doom_translate_key)
#include "hardware/structs/psm.h"

#ifdef POLYKYBD_DOOM

// The engine entry (d_main.c). Declared directly — pulling doom/d_main.h into
// this QMK-side file would drag the whole engine header graph next to
// QMK_KEYBOARD_H for one prototype. (The pico_sync frame semaphores live in
// qmk_shim.c behind doom_shim_take/release_frame — pico/sem.h cannot be
// included here: ChibiOS also defines semaphore_t, and pico/time.h's param
// asserts break inside QMK translation units.)
extern void D_DoomMain(void);

// The doom linker script's shared block: pool base, end of the engine's
// zero-init statics (= the arena base), and pool end (base + 226,800). See
// doom_arena.h for the tiering.
extern uint8_t __doom_shared_base__[];
extern uint8_t __doom_shared_statics_end__[];
extern uint8_t __doom_shared_end__[];

// The engine's view height (SCREENHEIGHT 200 - the 32 px vpatch status bar).
#define DOOM_VIEW_BUFFER_ROWS (DOOM_ARENA_FB_BYTES / 320)

// True when the engine is live on core1 (WHX found, core launched); false in
// game mode without a WHX — then the fire demo runs as the "NO WAD" gag.
static bool s_engine_running;

static bool doom_whx_present(void) {
    // The WHX ships XIP-mapped at TINY_WAD_ADDR (flash 0x600000); magic "IWHX"
    // (WHD_SUPER_TINY format — w_file_memory.c checks the same bytes).
    const uint8_t *whx = (const uint8_t *)TINY_WAD_ADDR;
    return whx[0] == 'I' && whx[1] == 'W' && whx[2] == 'H' && whx[3] == 'X';
}

// Hard-reset core1 via the power-on state machine (pico-sdk
// multicore_reset_core1 — not compiled here because the SDK's multicore.c
// collides with the firmware's local base/multicore/core1.c launcher).
static void doom_core1_reset(void) {
    io_rw_32 *power_off     = (io_rw_32 *)(PSM_BASE + PSM_FRCE_OFF_OFFSET);
    io_rw_32 *power_off_set = hw_set_alias(power_off);
    io_rw_32 *power_off_clr = hw_clear_alias(power_off);
    *power_off_set = PSM_FRCE_OFF_PROC1_BITS;
    while (!(*power_off & PSM_FRCE_OFF_PROC1_BITS)) {
        tight_loop_contents();
    }
    *power_off_clr = PSM_FRCE_OFF_PROC1_BITS;
}

static void doom_core1_entry(void) {
    // Interrupts masked on core1 for the same reason as multicore_exec.c's
    // core1_entry (the Vector80/NMI hang — see that file and CLAUDE.md). The
    // engine polls; the pico_sync semaphores wake via SEV/WFE, which PRIMASK
    // does not block.
    __asm volatile("cpsid i" ::: "memory");
    D_DoomMain(); // never returns
    while (true) {}
}

static void doom_engine_start(void) {
    if (!doom_whx_present()) {
        printf("doom: no WHX at %p — running the fire demo instead\n", (void *)TINY_WAD_ADDR);
        s_engine_running = false;
        return;
    }
    // Take core1 from the overlay-RLE service (idle in game mode — the
    // pool-writing HID commands are frozen) and give it to the game, with its
    // stack at the tail of the pool.
    doom_core1_reset();
    // (uintptr_t detour: negative offsets from a zero-size linker symbol trip
    // GCC's array-bounds check)
    uint32_t *stack_bottom = (uint32_t *)((uintptr_t)__doom_shared_end__ - DOOM_ARENA_STACK_BYTES);
    multicore_launch_core1_with_stack(doom_core1_entry, stack_bottom, DOOM_ARENA_STACK_BYTES);
    s_engine_running = true;
}

static void doom_engine_stop(void) {
    if (s_engine_running) {
        s_engine_running = false;
        // Kill the game mid-frame (it only touches pool memory) and hand core1
        // back to the overlay-RLE service.
        doom_core1_reset();
        multicore_launch_core1();
    }
}

// ---------------------------------------------------------------------------
// Input: core0 (process_record) -> SPSC ring -> core1 (I_StartTic drains it
// into D_PostEvent via doom_pop_key_event). Entries: doom key | DOWN bit.
// ---------------------------------------------------------------------------

#define EVQ_LEN  32u // power of two
#define EVQ_DOWN 0x100u

static volatile uint16_t s_evq[EVQ_LEN];
static volatile uint8_t  s_evq_w, s_evq_r;

// QMK keycode -> doom key. WASD plays (W/S forward/back, A/D strafe via the
// default ','/'.' bindings), arrows turn/move, Ctrl fires, Space uses, Shift
// runs, Alt strafe-modifies; letters/digits reach the menus (Y/N, episode/
// skill picks) — except w/a/s/d, which the movement mapping shadows.
static uint16_t doom_translate_key(uint16_t kc) {
    switch (kc) {
        case KC_W:    return KEY_UPARROW;
        case KC_S:    return KEY_DOWNARROW;
        case KC_A:    return ',';
        case KC_D:    return '.';
        case KC_UP:   return KEY_UPARROW;
        case KC_DOWN: return KEY_DOWNARROW;
        case KC_LEFT: return KEY_LEFTARROW;
        case KC_RGHT: return KEY_RIGHTARROW;
        case KC_ENTER: return KEY_ENTER;
        case KC_ESC:  return KEY_ESCAPE;
        case KC_TAB:  return KEY_TAB;
        case KC_BSPC: return KEY_BACKSPACE;
        case KC_SPACE: return ' ';
        case KC_LCTL: case KC_RCTL: return KEY_RCTRL;
        case KC_LSFT: case KC_RSFT: return KEY_RSHIFT;
        case KC_LALT: case KC_RALT: return KEY_RALT;
        case KC_MINUS: return KEY_MINUS;
        case KC_EQUAL: return KEY_EQUALS;
        case KC_COMMA: return ',';
        case KC_DOT:   return '.';
        case KC_SLASH: return '/';
        default:
            if (kc >= KC_A && kc <= KC_Z) return (uint16_t)('a' + (kc - KC_A));
            if (kc >= KC_1 && kc <= KC_9) return (uint16_t)('1' + (kc - KC_1));
            if (kc == KC_0) return '0';
            return 0;
    }
}

static void doom_push_key_event(uint16_t kc, bool pressed) {
    uint16_t key = doom_translate_key(kc);
    if (key == 0) {
        return;
    }
    uint8_t w = s_evq_w;
    if ((uint8_t)(w - s_evq_r) >= EVQ_LEN) {
        return; // full — drop (the game is stalled anyway)
    }
    s_evq[w % EVQ_LEN] = key | (pressed ? EVQ_DOWN : 0);
    __asm volatile("dmb" ::: "memory");
    s_evq_w = (uint8_t)(w + 1);
}

bool doom_pop_key_event(uint8_t *key, bool *pressed) {
    uint8_t r = s_evq_r;
    if (r == s_evq_w) {
        return false;
    }
    uint16_t e = s_evq[r % EVQ_LEN];
    __asm volatile("dmb" ::: "memory");
    s_evq_r = (uint8_t)(r + 1);
    *key     = (uint8_t)(e & 0xff);
    *pressed = (e & EVQ_DOWN) != 0;
    return true;
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

uint8_t *doom_arena_at(unsigned offset) {
    return s_active || s_fb ? __doom_shared_statics_end__ + offset : NULL;
}

uint8_t *doom_arena_framebuffer(void) {
    return doom_arena_at(DOOM_ARENA_FB_OFF);
}

uint8_t *doom_arena_zone(int *size) {
    if (size) {
        *size = (int)(__doom_shared_end__ - __doom_shared_statics_end__)
                - DOOM_ARENA_ZONE_OFF - DOOM_ARENA_STACK_BYTES;
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
    s_evq_w = s_evq_r = 0;
    set_last_update((int32_t)timer_read32());
    doom_blit_blank_all();
    doom_engine_start();
}

static void doom_exit(void) {
    s_active = false;
    doom_engine_stop();
    s_fb = NULL;
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
    // repeat events); a short ESC tap still reaches the game (its menu).
    if (keycode == KC_ESC) {
        s_esc_down    = pressed;
        s_esc_down_at = timer_read32();
    }
    if (s_engine_running) {
        doom_push_key_event(keycode, pressed);
    }
    return true;
}

void doom_tick(void) {
    // Relay the game core's buffered printf output first — also after exit, so
    // late lines still reach the console.
    doom_shim_drain_core1_log();
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

    if (s_engine_running) {
        // Frame consume: the game (core1) signals a completed frame and blocks
        // on display_frame_freed once it is a full frame ahead — the blit pace
        // here IS the game's frame pace. (Single view buffer: the next frame
        // renders into the buffer being blitted — tearing accepted for v1.)
        static uint32_t s_frames;
        static uint32_t s_hb_at;
        static uint32_t s_stats_at;
        if (doom_shim_take_frame()) {
            doom_blit_frame(doom_arena_framebuffer(), DOOM_VIEW_BUFFER_ROWS, DOOM_PLAYPAL_LUMA);
            doom_shim_release_frame();
            if (s_frames++ == 0) {
                printf("doom: first frame on the keycaps\n");
            }
        } else if (s_frames == 0) {
            // Boot heartbeat until the first frame lands: where is core1?
            if (s_hb_at == 0) {
                s_hb_at = timer_read32();
            } else if (timer_elapsed32(s_hb_at) > 2000) {
                s_hb_at = timer_read32();
                printf("doom: waiting for first frame (core1 progress=%u)\n", doom_shim_progress);
            }
        }
        // Periodic vitals: which of the two loops moves? frames = blit/handoff
        // pace (core0), gametic = game simulation (core1), vt = what the frame
        // holds (3 DOUBLE in-level / 4 SINGLE full-screen page / 5 WIPE).
        if (s_stats_at == 0) {
            s_stats_at = timer_read32();
        } else if (timer_elapsed32(s_stats_at) > 5000) {
            s_stats_at = timer_read32();
            printf("doom: stats frames=%lu gametic=%d vt=%u progress=%u\n",
                   (unsigned long)s_frames, doom_shim_gametic(),
                   doom_shim_video_type(), doom_shim_progress);
        }
        return;
    }

    // No WHX: the fire-demo pipeline proof.
    if (timer_elapsed32(s_last_frame) < DOOM_FRAME_MS) {
        return;
    }
    s_last_frame = timer_read32();
    doom_fire_step(doom_arena_framebuffer());
    doom_blit_frame(doom_arena_framebuffer(), DOOM_FB_HEIGHT, doom_fire_luma());
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
