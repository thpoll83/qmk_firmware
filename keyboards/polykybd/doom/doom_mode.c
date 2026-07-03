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
#include "side.h"               // is_left_side() (HUD column selection)
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

// The keys the SLAVE half keeps lit as a control pad (poly_sync_t.doom_ctl,
// update_displays filter). Deliberately narrower than doom_translate_key —
// that one admits every letter/digit for menu typing, but the pad shows only
// what you play with: move/strafe, use/fire, run, menu navigation, the
// automap, weapon slots and the menu confirm keys.
bool doom_key_is_control(uint16_t keycode) {
    switch (keycode) {
        case KC_W: case KC_A: case KC_S: case KC_D:
        case KC_UP: case KC_DOWN: case KC_LEFT: case KC_RGHT:
        case KC_ESC: case KC_ENTER: case KC_SPACE: case KC_TAB:
        case KC_LCTL: case KC_RCTL:
        case KC_LSFT: case KC_RSFT:
        case KC_LALT: case KC_RALT:
        case KC_Y: case KC_N:
            return true;
        default:
            return keycode >= KC_1 && keycode <= KC_7; // weapon slots
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

static void doom_session_reset(void); // HUD + counters, defined by the HUD block

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
    doom_session_reset(); // fresh HUD + frame/stats counters per entry
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

// Position alias map (GLOBAL matrix coords, either half): the top outer
// corner key is ESC; the outer two columns' cells are the weapon pad —
// inner column rows 0-3 = slots 1-4, outer column rows 1-3 = slots 5-7
// (row 0 of the outer column is the ESC corner). Where "outer" respects
// each half's matrix layout: left cols 0(outer)/1(inner), right cols
// 7(outer)/6(inner) — the right half's upper rows carry keys in matrix
// cols 1-7 (see split72.c invert_display).
uint16_t doom_pad_keycode(uint8_t row, uint8_t col) {
#if defined(KEYBOARD_polykybd_split72)
    const bool    right = row >= MATRIX_ROWS_PER_SIDE;
    const uint8_t r     = right ? (uint8_t)(row - MATRIX_ROWS_PER_SIDE) : row;
    if (r > 3) {
        return KC_NO; // thumb row: viewport bottom, no aliases
    }
    const uint8_t outer = right ? 7 : 0;
    const uint8_t inner = right ? 6 : 1;
    if (col == outer) {
        return (r == 0) ? KC_ESC : (uint16_t)(KC_1 + 4 + (r - 1)); // slots 5-7
    }
    if (col == inner) {
        return (uint16_t)(KC_1 + r); // slots 1-4
    }
#else
    (void)row; (void)col;
#endif
    return KC_NO;
}

bool doom_weapon_state(uint8_t *owned_mask, uint8_t *ready_slot) {
    return s_engine_running && doom_shim_weapon_state(owned_mask, ready_slot);
}

bool doom_process_record(uint16_t keycode, bool pressed, uint8_t row, uint8_t col) {
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

    // Game mode: swallow EVERYTHING — the host sees no keystrokes. Position
    // aliases first: the top outer corners act as ESC on both halves (the
    // slave has no physical ESC), and the slave-half pad cells select
    // weapons regardless of their keymap keycode. The MASTER's outer column
    // carries the vitals HUD, so its pad cells stay un-aliased.
    uint16_t pad = doom_pad_keycode(row, col);
    if (pad == KC_ESC) {
        keycode = KC_ESC;
    } else if (pad != KC_NO) {
        const bool from_slave = (row >= MATRIX_ROWS_PER_SIDE) == is_left_side();
        if (from_slave) {
            keycode = pad;
        }
    }
    // ESC held long enough exits (checked in doom_tick so a press-and-hold
    // needs no repeat events); a short ESC tap still reaches the game (menu).
    if (keycode == KC_ESC) {
        s_esc_down    = pressed;
        s_esc_down_at = timer_read32();
    }
    if (s_engine_running) {
        doom_push_key_event(keycode, pressed);
    }
    return true;
}

// Outer-column keycap HUD: player vitals on the OUTERMOST display column —
// the column freed by shifting the viewport inward (left half: col 0; right
// half: col 6) — rows 0-2, a word label over a full-size value. The composed
// status bar in the canvas is 1:1 tiny; this is the readable copy (field
// rounds 7+8 feedback). Redraws only on change, throttled so a firefight's
// ammo churn doesn't monopolise the shared display SPI (the status-OLED
// task logged "offset command failed" bursts in round 8); blanks once when
// leaving a level (demo/menu).
#define DOOM_HUD_MIN_REDRAW_MS 300

static uint8_t doom_hud_disp_col(void) {
    return is_left_side() ? 0 : 6;
}

// Value as UTF-32 digits ('-' = weapon with no ammo type, fist/chainsaw).
static void doom_hud_format(uint32_t *out, int v) {
    unsigned n = 0;
    if (v < 0) {
        out[n++] = '-';
    } else {
        if (v > 999) {
            v = 999;
        }
        char d[4];
        int  len = 0;
        do {
            d[len++] = (char)('0' + (v % 10));
            v /= 10;
        } while (v);
        while (len) {
            out[n++] = (uint32_t)d[--len];
        }
    }
    out[n] = 0;
}

// File-scope so doom_enter() can reset a fresh session (function-statics
// leaked stale frame counters/HUD state into re-entries — field round 9's
// "frames=800" at boot).
static int      s_hud_hp, s_hud_ar, s_hud_am;
static bool     s_hud_shown;
static bool     s_hud_esc_drawn;
static uint32_t s_hud_drawn_at;
static uint32_t s_frames, s_hb_at, s_stats_at;

static void doom_session_reset(void) {
    s_hud_hp = s_hud_ar = s_hud_am = -9999;
    s_hud_shown     = false;
    s_hud_esc_drawn = false;
    s_hud_drawn_at  = 0;
    s_frames = s_hb_at = s_stats_at = 0;
}

static void doom_hud_tick(void) {
    // The exit hint owns the corner key for the whole session (demo, menu
    // and level alike) — the position alias makes it act as ESC.
    if (!s_hud_esc_drawn) {
        s_hud_esc_drawn = true;
        doom_blit_stat_key(0, doom_hud_disp_col(), U"hold", U"Esc");
    }
    int hp, ar, am;
    if (!doom_shim_hud_stats(&hp, &ar, &am)) {
        if (s_hud_shown) {
            s_hud_shown = false;
            s_hud_hp = s_hud_ar = s_hud_am = -9999;
            for (uint8_t r = 1; r <= 3; ++r) {
                doom_blit_blank_key(r, doom_hud_disp_col());
            }
        }
        return;
    }
    if (hp == s_hud_hp && ar == s_hud_ar && am == s_hud_am && s_hud_shown) {
        return;
    }
    if (s_hud_shown && timer_elapsed32(s_hud_drawn_at) < DOOM_HUD_MIN_REDRAW_MS) {
        return; // fresh values picked up on a later frame
    }
    s_hud_drawn_at = timer_read32();
    uint32_t value[8];
    if (hp != s_hud_hp || !s_hud_shown) {
        s_hud_hp = hp;
        doom_hud_format(value, hp);
        doom_blit_stat_key(1, doom_hud_disp_col(), U"Health", value);
    }
    if (ar != s_hud_ar || !s_hud_shown) {
        s_hud_ar = ar;
        doom_hud_format(value, ar);
        doom_blit_stat_key(2, doom_hud_disp_col(), U"Armor", value);
    }
    if (am != s_hud_am || !s_hud_shown) {
        s_hud_am = am;
        doom_hud_format(value, am);
        doom_blit_stat_key(3, doom_hud_disp_col(), U"Ammo", value);
    }
    s_hud_shown = true;
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
        if (doom_shim_take_frame()) {
            doom_blit_frame_engine(DOOM_PLAYPAL_LUMA);
            doom_shim_release_frame();
            doom_hud_tick();
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
