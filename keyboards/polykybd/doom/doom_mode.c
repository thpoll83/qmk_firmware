// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Game-mode state machine for the Doom easter egg. Owns the runtime handoff
// of the overlay RAM pool (DOOM_FEASIBILITY.md "Challenge 2"): entering game
// mode borrows the 226,800 B `overlays[]` arena as game memory; leaving hands
// it back blank exactly as a fresh boot / pack wipe would, and the host
// repopulates it on its next overlay push. The MASTER half runs the real
// game; the SLAVE half runs its own engine instance as a lockstep-drone
// mirror of it (a ticcmd stream over the split bridge — doom_mirror.h)
// and shows the automap on its viewport while the outer columns stay the
// ESC/weapon control pad.
#include QMK_KEYBOARD_H

#include "doom_mode.h"
#include "doom_arena.h"
#include "doom_blit.h"
#include "doom_game.h"
#include "doom_mirror.h"
#include "doom_playpal_luma.h"

#include "split_sync.h"        // sync_succeeded (mirror bridge sends)
#include "polymod_crc32.h"     // crc32_1byte (mirror message framing)
#include "transactions.h"      // USER_SYNC_OVERLAY_MAP_DATA (mirror rides it)

#include "bridge_helper.h"
#include "side.h"               // is_left_side() (HUD column selection)
#include "base/overlay.h"
#include "base/update.h"
#include "base/fw_staging.h"
#include "base/multicore/core1.h"

#include "doomkeys.h"           // engine key codes (doom_translate_key)
#include "doom_weapon_icons.h"  // slave weapon-pad bitmaps (shareware sprites)
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
        // back to the overlay-RLE service. The relaunch handshake is BOUNDED:
        // if core1 is not back in the bootrom wait loop, the plain
        // multicore_launch_core1() blocks forever — a deaf/wedged keyboard on
        // session exit (field rounds 19+20; the host even declared a
        // disconnect during the ~15 s post-exit silence). On a miss, PSM-reset
        // core1 and retry; worst case the RLE service stays down (overlay
        // decompression degrades until reboot) but the keyboard stays alive.
        const uint32_t t0 = timer_read32();
        bool ok = false;
        for (uint8_t attempt = 0; attempt < 3 && !ok; ++attempt) {
            doom_core1_reset();
            ok = multicore_launch_core1_bounded(100u * 1000u);
        }
        // The engine is GONE: every standalone vpatch decoder (ESC/label
        // STCFN, tall digits, menu tiles, the face) gates on this — with it
        // stale at 4, a post-exit render resolved vpatches through zone
        // tables living in the just-zeroed overlay pool -> HardFault, the
        // "keyboard halts on exit, reset required" of rounds 19+20 (v19
        // introduced the STCFN ESC corner, which is exactly when exits
        // started dying). Clear it BEFORE the pool is handed back.
        doom_shim_progress = 0;
        printf("doom: engine stopped, RLE core relaunch %s (%lu ms)\n",
               ok ? "ok" : "FAILED", (unsigned long)timer_elapsed32(t0));
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
// automap and the menu confirm keys. (The plain number keys dropped out in
// round 17 with the weapon switch — the pad's outer-column cells select
// weapons now.)
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
            return false;
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
static void doom_mirror_session_reset(void); // master pump statics, mirror block
static void doom_rgb_session_reset(void);    // sound->RGB pulses (no-op sans RGB matrix)
static void doom_rgb_task(void);             // ditto, called every doom_tick on both halves

// Pool take + engine boot, shared by the master's doom_enter and the slave's
// mirror session. False when blocked (fw flash in flight) or unviable — the
// caller retries / stays out.
static bool doom_session_start(void) {
    // Never take the pool while the fw/font-pack stager owns the split link and
    // flash — the two "exclusive" modes don't compose.
    if (fw_staging_fw_up_active() || fw_staging_commit_pending()) {
        return false;
    }
    // The runtime handoff: the overlay pool becomes the game arena. The pool
    // is entirely reconstructible — the host re-sends overlays on every app
    // switch, so nothing is lost (see DOOM_FEASIBILITY.md, Challenge 2).
    s_fb = (uint8_t *)get_overlays();
    // Zero the whole shared block: the engine's zero-init statics live at its
    // front (.doom_shared is not crt0-zeroed) — every entry starts the game
    // from virgin static state (including the mirror mailbox).
    memset(__doom_shared_base__, 0, (size_t)(__doom_shared_end__ - __doom_shared_base__));
    // The engine statics eat the pool from the front; refuse to start if that
    // left the zone unviable (the link only guards the 226,800 B total).
    int zone_size = 0;
    (void)doom_arena_zone(&zone_size);
    if (zone_size < 40 * 1024) {
        printf("doom: zone too small (%d) — engine statics have outgrown the pool\n", zone_size);
        s_fb = NULL;
        return false;
    }
    doom_fire_init(doom_arena_framebuffer());
    s_evq_w = s_evq_r = 0;
    doom_session_reset();        // fresh HUD + frame/stats counters per entry
    doom_mirror_session_reset(); // fresh mirror-pump handshakes per entry
    doom_shim_set_role(is_usb_host_side());
    doom_engine_start();
    return true;
}

static void doom_enter(void) {
    if (!doom_session_start()) {
        return;
    }
    // Release anything still registered host-side (the trigger letters have
    // already been sent; nothing may stay held while we swallow events).
    clear_keyboard();

    s_active     = true;
    s_esc_down   = false;
    s_last_frame = 0;
    set_last_update((int32_t)timer_read32());
    doom_blit_blank_all();
}

static void doom_exit(void) {
    // Breadcrumbs around every teardown stage — the round-20 log showed a
    // ~15 s post-exit silence with no way to tell which stage stalled.
    printf("doom: exit begin\n");
    s_active = false;
    // Drop the synced pad flag IMMEDIATELY — housekeeping only refreshes it
    // at the END of its pass, so the repaint doom_exit requests below would
    // otherwise still run update_displays' doom_ctl branch on this half
    // (pad chrome incl. the STCFN ESC corner) against the torn-down engine.
    // This also gets the 0 onto the very next POLY sync to the slave.
    access_local_state()->doom_ctl = 0;
    access_local_state()->doom_rgb = 0; // lights out with the same sync
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
    printf("doom: exit done\n");
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

// Fixed positional CONTROL layout (field round 23: "we should stick to this
// layout, independent of the current base layout"): the game controls are
// POSITIONS, not keymap keycodes, so the pad plays identically on every
// base/default layer — and because the layers are never touched, there is
// nothing to restore on exit. The arrangement is the QWERTY! (_L1) one the
// game was tuned on: the SLAVE half's bottom row carries the cursor cluster
// on its outer four keys (reading left-to-right LEFT/UP/DOWN/RIGHT), the big
// thumb key is USE and the inner bottom key ENTER; the MASTER pins
// fire/run/strafe/use/map to the physical Ctrl/Shift/Alt/Space/Tab positions
// every shipped base layer already agrees on (aliased anyway, so a custom
// layer can't strand the game). MIRRORED when the master is the RIGHT half:
// the cursor cluster moves to the left half's bottom outer corner (same
// reading order) and the right master gets fire on its bottom outer key —
// the bottom of its HUD column, where the reticle renders. KC_NO = no fixed
// role, the keymap keycode passes through (menu typing, cheats).
uint16_t doom_ctl_keycode(uint8_t row, uint8_t col) {
#if defined(KEYBOARD_polykybd_split72)
    const bool    row_right   = row >= MATRIX_ROWS_PER_SIDE;
    const uint8_t r           = row_right ? (uint8_t)(row - MATRIX_ROWS_PER_SIDE) : row;
    // Valid on either controller: the master is the USB side, the slave the
    // other half (matches doom_process_record's from_slave test).
    const bool    master_left = is_usb_host_side() == is_left_side();
    const bool    on_slave    = row_right == master_left;
    const bool    bottom      = r == MATRIX_ROWS_PER_SIDE - 1;
    if (on_slave) {
        if (!bottom) {
            return KC_NO; // rows 0-3: ESC corner + weapon pad (doom_pad_keycode)
        }
        if (row_right) { // right-half slave (the tested master-left setup)
            switch (col) {
                case 1: return KC_ENTER;
                case 3: return KC_SPACE; // big thumb key: use/open
                case 4: return KC_LEFT;
                case 5: return KC_UP;
                case 6: return KC_DOWN;
                case 7: return KC_RIGHT;
            }
        } else {         // left-half slave (mirror), same reading order
            switch (col) {
                case 0: return KC_LEFT;
                case 1: return KC_UP;
                case 2: return KC_DOWN;
                case 3: return KC_RIGHT;
                case 4: return KC_SPACE; // big thumb key: use/open
                case 6: return KC_ENTER;
            }
        }
        return KC_NO;
    }
    if (!row_right) { // left-half master
        if (col == 0) {
            switch (r) {
                case 1: return KC_TAB;  // automap
                case 3: return KC_LSFT; // run
                case 4: return KC_LCTL; // fire (the reticle key)
            }
        } else if (bottom) {
            switch (col) {
                case 2: return KC_LALT;  // strafe
                case 4: return KC_SPACE; // use/open
                case 6: return KC_ENTER;
            }
        }
    } else {          // right-half master (mirror)
        if (bottom) {
            switch (col) {
                case 1: return KC_ENTER;
                case 3: return KC_SPACE; // use/open
                case 7: return KC_LCTL;  // fire (HUD-column bottom = reticle)
            }
        } else if (col == 7) {
            switch (r) {
                case 1: return KC_TAB;  // automap
                case 3: return KC_RSFT; // run
            }
        }
    }
#else
    (void)row; (void)col;
#endif
    return KC_NO;
}

bool doom_weapon_state(uint8_t *owned_mask, uint8_t *ready_slot) {
    return s_engine_running && doom_shim_weapon_state(owned_mask, ready_slot);
}

const uint8_t *doom_weapon_icon(uint8_t slot, uint8_t *w, uint8_t *h) {
    if (slot >= 8 || !DOOM_WPN_ICONS[slot].bmp) {
        return NULL;
    }
    *w = DOOM_WPN_ICONS[slot].w;
    *h = DOOM_WPN_ICONS[slot].h;
    return DOOM_WPN_ICONS[slot].bmp;
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
    uint16_t pad     = doom_pad_keycode(row, col);
    bool     aliased = false;
    if (pad == KC_ESC) {
        keycode = KC_ESC;
    } else if (pad != KC_NO) {
        const bool from_slave = (row >= MATRIX_ROWS_PER_SIDE) == is_left_side();
        if (from_slave) {
            keycode = pad;
            aliased = true;
        }
    }
    // Fixed positional control layout (field round 23) — the cursor cluster
    // on the slave's bottom corner, fire/use/run/strafe/map pinned on the
    // master. Applied after (and never overlapping) the pad aliases above.
    if (!aliased && pad != KC_ESC) {
        const uint16_t ctl = doom_ctl_keycode(row, col);
        if (ctl != KC_NO) {
            keycode = ctl;
        }
    }
    // ESC held long enough exits (checked in doom_tick so a press-and-hold
    // needs no repeat events); a short ESC tap still reaches the game (menu).
    if (keycode == KC_ESC) {
        s_esc_down    = pressed;
        s_esc_down_at = timer_read32();
    }
    if (s_engine_running) {
        // Plain number keys no longer switch weapons — the slave pad cells
        // (aliased above) are the weapon selector (field round 17: "remove
        // the weapon switch from the normal 1 to 7 keys").
        if (!aliased && keycode >= KC_1 && keycode <= KC_7) {
            return true;
        }
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
    doom_rgb_session_reset();
}

static void doom_hud_tick(void) {
    // The exit hint owns the corner key for the whole session (demo, menu
    // and level alike) — the position alias makes it act as ESC.
    if (!s_hud_esc_drawn) {
        s_hud_esc_drawn = true;
        doom_blit_esc_key(0, doom_hud_disp_col());
        // Fire hint (field rounds 17+23): fire is the bottom key of the HUD
        // column on either master — the physical Ctrl on a left master, the
        // doom_ctl_keycode position alias on a right one — so the reticle
        // renders at the HUD column's bottom on both.
        doom_blit_fire_key(MATRIX_ROWS_PER_SIDE - 1, doom_hud_disp_col());
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
    // Values in the game's own tall red status-bar digits ("extract the
    // font", field round 14); font digits only as the fallback when the
    // glyph decode is unavailable.
    if (hp != s_hud_hp || !s_hud_shown) {
        s_hud_hp = hp;
        if (!doom_blit_stat_num_key(1, doom_hud_disp_col(), U"Health", hp, DOOM_PLAYPAL_LUMA)) {
            doom_hud_format(value, hp);
            doom_blit_stat_key(1, doom_hud_disp_col(), U"Health", value);
        }
    }
    if (ar != s_hud_ar || !s_hud_shown) {
        s_hud_ar = ar;
        if (!doom_blit_stat_num_key(2, doom_hud_disp_col(), U"Armor", ar, DOOM_PLAYPAL_LUMA)) {
            doom_hud_format(value, ar);
            doom_blit_stat_key(2, doom_hud_disp_col(), U"Armor", value);
        }
    }
    if (am != s_hud_am || !s_hud_shown) {
        s_hud_am = am;
        if (!doom_blit_stat_num_key(3, doom_hud_disp_col(), U"Ammo", am, DOOM_PLAYPAL_LUMA)) {
            doom_hud_format(value, am);
            doom_blit_stat_key(3, doom_hud_disp_col(), U"Ammo", value);
        }
    }
    s_hud_shown = true;
}

// Frame consume + diagnostics, shared by the master and the slave halves
// (each pumps ITS OWN engine instance's frames onto ITS OWN keycaps).
static void doom_frame_pump(bool with_hud) {
    if (s_engine_running) {
        // Frame consume: the game (core1) signals a completed frame and blocks
        // on display_frame_freed once it is a full frame ahead — the blit pace
        // here IS the game's frame pace. (Single view buffer: the next frame
        // renders into the buffer being blitted — tearing accepted for v1.)
        if (doom_shim_take_frame()) {
            doom_blit_frame_engine(DOOM_PLAYPAL_LUMA, false, false);
            doom_shim_release_frame();
            if (with_hud) {
                doom_hud_tick();
            }
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

// ---------------------------------------------------------------------------
// Slave lockstep mirror (doom_mirror.h): master core0 drains the shim's TX
// ring / control outbox over the split bridge; the slave boots its own
// engine on the synced doom_ctl flag and blits the drone's automap view on
// its 5x5 viewport while a mirrored game is live.
// ---------------------------------------------------------------------------

static uint32_t s_mir_ctl_seq_sent;      // last ctl_out_seq delivered to the slave
static uint32_t s_mir_backoff_at;        // bridge-failure send backoff
static bool     s_mir_dead_reported;     // tx_overflow logged + BREAK sent
static int      s_mir_menu_count;        // last menu snapshot delivered
static int      s_mir_menu_item_on;
static uint16_t s_mir_menu_items[DOOM_MIRROR_MENU_MAX_ITEMS];

static void doom_mirror_session_reset(void) {
    s_mir_ctl_seq_sent  = 0;
    s_mir_backoff_at    = 0;
    s_mir_dead_reported = false;
    s_mir_menu_count    = 0;
    s_mir_menu_item_on  = 0;
    memset(s_mir_menu_items, 0, sizeof(s_mir_menu_items));
}

// The QMK transaction table is full (32-id cap), so mirror messages
// multiplex onto USER_SYNC_OVERLAY_MAP_DATA by their distinct payload size
// (72 B vs the 68 B map chunks / 37 B MRU snapshots), exactly like the MRU
// snapshots already do. No cross-talk: the host's overlay-map commands are
// frozen while game mode holds the pool, so the id is otherwise silent.
static bool doom_mirror_send(doom_mirror_msg_t *msg, uint8_t retries) {
    msg->crc32 = crc32_1byte(((uint8_t *)msg) + 4, sizeof(*msg) - 4, 0);
    if (sync_succeeded(send_to_bridge(USER_SYNC_OVERLAY_MAP_DATA, msg, sizeof(*msg), retries))) {
        s_mir_backoff_at = 0;
        return true;
    }
    s_mir_backoff_at = timer_read32();
    return false;
}

// Master core0, once per housekeeping pass: at most ONE bridge transaction —
// the control channel (START/BREAK, latest-wins) first, then a batch of up
// to 7 queued ticcmds. Loss is retried inherently (tx_r only advances on a
// confirmed send), so TIC messages use few retries and a short backoff keeps
// a dead bridge from stalling the main loop every pass.
static void doom_mirror_master_pump(void) {
    if (!s_engine_running) {
        return;
    }
    doom_mirror_t *m = (doom_mirror_t *)doom_arena_at(DOOM_ARENA_MIRROR_OFF);
    if (!m) {
        return;
    }
    if (s_mir_backoff_at && timer_elapsed32(s_mir_backoff_at) < 250) {
        return;
    }
    doom_mirror_msg_t msg;
    const uint32_t seq = m->ctl_out_seq;
    if (seq != s_mir_ctl_seq_sent) {
        memset(&msg, 0, sizeof(msg));
        __asm volatile("dmb" ::: "memory");
        msg.kind  = m->ctl_out_kind;
        msg.tic   = m->ctl_out_tic;
        msg.skill = m->ctl_out_skill;
        msg.epi   = m->ctl_out_epi;
        msg.map   = m->ctl_out_map;
        __asm volatile("dmb" ::: "memory");
        if (m->ctl_out_seq != seq) {
            return; // core1 superseded it mid-read; send the newer one next pass
        }
        if (doom_mirror_send(&msg, 3)) {
            s_mir_ctl_seq_sent = seq;
        }
        return;
    }
    if (m->tx_overflow) {
        // The ring outlived the bridge (~2 s of undeliverable cmds) — the
        // lockstep is unrecoverable this session (exit + re-enter resets).
        // One best-effort BREAK drops the slave to its pad; if even that is
        // undeliverable the slave keeps its last frame until session exit.
        if (!s_mir_dead_reported) {
            s_mir_dead_reported = true;
            printf("doom: mirror tx overflow — mirror off for this session\n");
            memset(&msg, 0, sizeof(msg));
            msg.kind = DOOM_MIRROR_MSG_BREAK;
            (void)doom_mirror_send(&msg, 3);
        }
        return;
    }
    // Readable menu mirror (field round 17): ship the sampled menu snapshot
    // whenever it differs from the last delivered one — open/close, item
    // change and selection moves are all rare next to the tic stream, and a
    // menu sits still while the player reads it, so this never starves TICs.
    {
        uint16_t items[DOOM_MIRROR_MENU_MAX_ITEMS] = {0};
        int      item_on = 0;
        const int n = doom_shim_menu_snapshot(items, DOOM_MIRROR_MENU_MAX_ITEMS, &item_on);
        if (n != s_mir_menu_count || item_on != s_mir_menu_item_on ||
            (n > 0 && memcmp(items, s_mir_menu_items, sizeof(items)) != 0)) {
            memset(&msg, 0, sizeof(msg));
            msg.kind  = DOOM_MIRROR_MSG_MENU;
            msg.count = (uint8_t)n;
            msg.skill = (uint8_t)item_on;
            memcpy(msg.cmds, items, sizeof(items));
            if (doom_mirror_send(&msg, 2)) {
                s_mir_menu_count   = n;
                s_mir_menu_item_on = item_on;
                memcpy(s_mir_menu_items, items, sizeof(items));
            }
            return;
        }
    }
    const uint32_t r = m->tx_r;
    const uint32_t pending = m->tx_w - r;
    if (!pending) {
        return;
    }
    uint8_t k = pending > DOOM_MIRROR_MSG_MAX_CMDS ? DOOM_MIRROR_MSG_MAX_CMDS : (uint8_t)pending;
    memset(&msg, 0, sizeof(msg));
    msg.kind  = DOOM_MIRROR_MSG_TIC;
    msg.count = k;
    msg.tic   = r;
    for (uint8_t i = 0; i < k; ++i) {
        memcpy(msg.cmds[i], m->tx_cmds[(r + i) % DOOM_MIRROR_TX_LEN], DOOM_MIRROR_CMD_BYTES);
    }
    if (doom_mirror_send(&msg, 2)) {
        m->tx_r = r + k;
    }
}

// Slave-half game mode: driven purely by the synced doom_ctl flag — a rising
// edge boots THIS half's own engine instance on its own core1 from its own
// pool + WHX slot; a falling edge (the master exited) tears it down. The
// engine idles unseen through the attract/menu phase (frames consumed and
// dropped — the pad keeps the keycaps); when the master starts a real game
// the mirrored START turns it into a lockstep drone and the blitter takes
// the viewport with the force-revealed automap.
static bool     s_slave;        // mirror session up (pool + engine)
static bool     s_slave_blit;   // viewport currently owned by the blitter
static bool     s_slave_blit_bottom; // ...including the bottom row (attract only)
static uint32_t s_slave_tab_at; // automap re-toggle throttle
static bool     s_slave_menu;   // readable menu mirror currently shown
static uint32_t s_slave_menu_seq;   // last rendered snapshot seq
static uint32_t s_slave_skull_at;   // skull blink timer
static bool     s_slave_skull_alt;  // which skull frame is up

static void doom_slave_stop(void) {
    s_slave      = false;
    s_slave_blit = false;
    s_slave_blit_bottom = false;
    s_slave_menu = false;
    doom_engine_stop();
    s_fb = NULL;
    // Same pool hand-back contract as doom_exit: blank + reconstructible.
    reset_overlay_buffers();
    reset_overlay_usage();
    reset_overlay_mapping();
    reset_fragment_context();
    request_disp_refresh();
}

static void doom_slave_tick(void) {
    const bool want = get_local_state()->doom_ctl != 0;
    if (want && !s_slave) {
        // Without game data this half stays a plain control pad (flash the
        // WHX to the slave over BOOTSEL like the master, see README.md).
        if (!doom_whx_present() || !doom_session_start()) {
            return; // blocked (fw flash) -> retried while doom_ctl stays set
        }
        s_slave       = true;
        s_slave_blit  = false;
        s_slave_menu  = false;
        s_slave_tab_at = 0;
        printf("doom: slave mirror engine up\n");
        return;
    }
    if (!want) {
        if (s_slave) {
            doom_slave_stop();
        }
        return;
    }
    if (!s_engine_running) {
        return;
    }
    // The readable menu mirror owns the viewport whenever the master has a
    // mirrorable menu up (field round 17) — it outranks the attract/map blit
    // but leaves the bottom row to the pad (Enter/Space legends stay).
    doom_mirror_t *mbox = (doom_mirror_t *)doom_arena_at(DOOM_ARENA_MIRROR_OFF);
    const bool menu = mbox && mbox->menu_in_count > 0;
    const bool live = menu || doom_shim_slave_view_live();
    // The ATTRACT blit fills the whole 5x5 incl. the bottom row ("looks more
    // uniform", field round 14); the MAP leaves the bottom row to the pad so
    // the thumb/cursor-key legends stay (field round 13).
    const bool bottom = live && !menu && doom_shim_attract_active();
    if (bottom != s_slave_blit_bottom) {
        const bool fell = s_slave_blit_bottom && !bottom;
        s_slave_blit_bottom = bottom;
        if (fell && live) {
            request_disp_refresh(); // attract -> map/menu: thumb legends return
        }
    }
    if (live != s_slave_blit) {
        s_slave_blit = live;
        if (!live) {
            request_disp_refresh(); // hand the viewport back to the pad legends
        }
    }
    if (menu) {
        if (!s_slave_menu || mbox->menu_in_seq != s_slave_menu_seq) {
            s_slave_menu     = true;
            s_slave_menu_seq = mbox->menu_in_seq;
            s_slave_skull_at = timer_read32();
            doom_blit_menu(DOOM_PLAYPAL_LUMA, s_slave_skull_alt, true);
        } else if (timer_elapsed32(s_slave_skull_at) > 250) {
            // Local skull blink — the master's whichSkull is not mirrored
            // (it would resend the snapshot 4x a second for nothing).
            s_slave_skull_at  = timer_read32();
            s_slave_skull_alt = !s_slave_skull_alt;
            doom_blit_menu(DOOM_PLAYPAL_LUMA, s_slave_skull_alt, false);
        }
    } else {
        s_slave_menu = false;
    }
    // Consume frames even while not blitting — the engine loop must keep
    // turning (blocked on the frame semaphore otherwise) to see a START.
    if (doom_shim_take_frame()) {
        if (s_slave_blit && !menu) {
            doom_blit_frame_engine(DOOM_PLAYPAL_LUMA, !s_slave_blit_bottom,
                                   doom_shim_drone_map_live());
        }
        doom_shim_release_frame();
        s_frames++;
    }
    // AM_Stop() runs on every level exit — re-toggle the automap whenever a
    // live in-level mirror is showing the first-person view instead.
    if (doom_shim_slave_wants_map_key() && timer_elapsed32(s_slave_tab_at) > 500) {
        s_slave_tab_at = timer_read32();
        doom_push_key_event(KC_TAB, true);
        doom_push_key_event(KC_TAB, false);
    }
    // Periodic vitals, mirroring the master's stats line.
    if (s_stats_at == 0) {
        s_stats_at = timer_read32();
    } else if (timer_elapsed32(s_stats_at) > 5000) {
        s_stats_at = timer_read32();
        printf("doom: slave stats frames=%lu gametic=%d live=%u vt=%u\n",
               (unsigned long)s_frames, doom_shim_gametic(),
               (unsigned)s_slave_blit, doom_shim_video_type());
    }
}

bool doom_slave_viewport_live(void) {
    return s_slave_blit;
}

bool doom_slave_bottom_row_live(void) {
    return s_slave_blit_bottom;
}

// ---------------------------------------------------------------------------
// Status-OLED support: the doomguy face while the master plays (the keycap-
// canvas status bar is 1:1 tiny), and the logo's hardware scroll gated to the
// attract phase.
// ---------------------------------------------------------------------------

static int s_face_drawn = -1;

int doom_status_face_render(uint8_t *buf1024) {
    // Master only — its player IS the game; the slave keeps the logo.
    if (!s_active || !s_engine_running) {
        s_face_drawn = -1;
        return 0;
    }
    const int idx = doom_shim_face_index();
    if (idx < 0) {
        s_face_drawn = -1;
        return 0;
    }
    if (idx == s_face_drawn) {
        return 1; // the panel already shows this face
    }
    if (!doom_shim_face_oled(buf1024, DOOM_PLAYPAL_LUMA)) {
        s_face_drawn = -1;
        return 0;
    }
    s_face_drawn = idx;
    return 2;
}

bool doom_status_scroll(void) {
    if ((s_active || s_slave) && s_engine_running) {
        return doom_shim_attract_active();
    }
    // Game mode with no engine on this half (no WHX / fire demo): keep the
    // legacy always-scrolling logo.
    return true;
}

// ---------------------------------------------------------------------------
// Sound -> RGB matrix (field round 23): the game's audio, substituted onto
// the per-key RGB LEDs. The master edge-detects the shim's sound counters
// (S_StartSound classifies every audible sound, qmk_shim.c) into the synced
// doom_rgb byte — see state.h for the bit layout — and BOTH halves render
// that byte locally in rgb_matrix_indicators_kb: a yellow flash when the
// player fires, a blue flash for world/monster sounds (suppressed while the
// fire flash runs so the two never blend green), over a steady red base that
// grows as health degrades. All "not too bright" — peaks stay well under
// half drive, in the same range the fw-flash breathing uses.
// ---------------------------------------------------------------------------
#ifdef RGB_MATRIX_ENABLE

#define DOOM_RGB_FLASH_MS 180u

// Master publisher state (consumed counter totals + the published pulses).
static uint32_t s_rgb_fire_seen, s_rgb_world_seen;
static uint8_t  s_rgb_fire_pulse, s_rgb_world_pulse; // 1..3 while flashing, 0 idle
static uint32_t s_rgb_fire_at, s_rgb_world_at;
static bool     s_rgb_forced_on; // we woke a user-disabled matrix for the cue

static void doom_rgb_session_reset(void) {
    // The shim counters restart at 0 with every engine launch
    // (doom_shim_set_role) — restart the edge detector with them.
    s_rgb_fire_seen  = s_rgb_world_seen  = 0;
    s_rgb_fire_pulse = s_rgb_world_pulse = 0;
    s_rgb_fire_at    = s_rgb_world_at    = 0;
}

static void doom_rgb_task(void) {
    if (is_usb_host_side()) {
        uint8_t rgb = 0;
        // Attract demos fire weapons too — only real gameplay drives the
        // lights (the demo loop staying dark also reads as "idle").
        if (s_active && s_engine_running && !doom_shim_attract_active()) {
            const uint32_t now  = timer_read32();
            const uint32_t fire = doom_shim_snd_fire;
            if (fire != s_rgb_fire_seen) {
                s_rgb_fire_seen  = fire;
                s_rgb_fire_pulse = (uint8_t)(s_rgb_fire_pulse % 3u) + 1u;
                s_rgb_fire_at    = now;
            } else if (s_rgb_fire_pulse && now - s_rgb_fire_at > DOOM_RGB_FLASH_MS) {
                s_rgb_fire_pulse = 0;
            }
            const uint32_t world = doom_shim_snd_world;
            if (world != s_rgb_world_seen) {
                s_rgb_world_seen = world;
                // Fire outranks the world: a blue pulse neither starts nor
                // re-arms while the yellow flash runs (the renderer also
                // suppresses it), so the mix can't read green.
                if (!s_rgb_fire_pulse) {
                    s_rgb_world_pulse = (uint8_t)(s_rgb_world_pulse % 3u) + 1u;
                    s_rgb_world_at    = now;
                }
            } else if (s_rgb_world_pulse && now - s_rgb_world_at > DOOM_RGB_FLASH_MS) {
                s_rgb_world_pulse = 0;
            }
            int hp, ar, am;
            if (doom_shim_hud_stats(&hp, &ar, &am)) {
                // Full health = dark; each ~7 hp lost lights the red base one
                // step (0..15, saturating at death).
                const int lvl = hp >= 100 ? 0 : hp <= 0 ? 15 : ((100 - hp) * 15 + 50) / 100;
                rgb = (uint8_t)lvl;
            }
            rgb |= (uint8_t)(s_rgb_fire_pulse << 4) | (uint8_t)(s_rgb_world_pulse << 6);
        }
        access_local_state()->doom_rgb = rgb;
    }
    // Both halves: keep the matrix awake for the cue even when the user has
    // RGB off, exactly like the fw-flash breathing — enable_noeeprom touches
    // no persisted mode/colour, so the previous state returns afterwards.
    const bool want = get_local_state()->doom_ctl != 0;
    if (want && !s_rgb_forced_on && !rgb_matrix_is_enabled()) {
        s_rgb_forced_on = true;
        rgb_matrix_enable_noeeprom();
    } else if (!want && s_rgb_forced_on) {
        s_rgb_forced_on = false;
        rgb_matrix_disable_noeeprom();
    }
}

bool doom_rgb_indicators(void) {
    // Local fade clocks, restarted whenever the synced pulse counter moves —
    // rapid re-fires re-arm the flash even when the bit never dropped.
    static uint8_t  s_last_fire, s_last_world;
    static uint32_t s_fire_at, s_world_at;
    static bool     s_fire_live, s_world_live;

    const uint8_t rgb   = get_local_state()->doom_rgb;
    const bool    ctl   = get_local_state()->doom_ctl != 0;
    const uint8_t fire  = (uint8_t)((rgb >> 4) & 3u);
    const uint8_t world = (uint8_t)((rgb >> 6) & 3u);
    if (fire != s_last_fire) {
        s_last_fire = fire;
        if (fire) {
            s_fire_live = true;
            s_fire_at   = timer_read32();
        }
    }
    if (world != s_last_world) {
        s_last_world = world;
        if (world) {
            s_world_live = true;
            s_world_at   = timer_read32();
        }
    }
    if (s_fire_live && timer_elapsed32(s_fire_at) >= DOOM_RGB_FLASH_MS) {
        s_fire_live = false;
    }
    if (s_world_live && timer_elapsed32(s_world_at) >= DOOM_RGB_FLASH_MS) {
        s_world_live = false;
    }
    if (!ctl && !s_fire_live && !s_world_live) {
        return true; // not ours — fall through to the normal indicators
    }
    // Steady red base from lost health (caps at 75/255).
    uint8_t r = (uint8_t)((rgb & 0x0Fu) * 5u);
    uint8_t g = 0, b = 0;
    if (s_fire_live) {
        const uint32_t k = 255u - (timer_elapsed32(s_fire_at) * 255u) / DOOM_RGB_FLASH_MS;
        const uint8_t  fr = (uint8_t)((100u * k) / 255u); // muzzle-flash yellow,
        const uint8_t  fg = (uint8_t)((70u * k) / 255u);  // fading out linearly
        if (fr > r) {
            r = fr;
        }
        g = fg;
    } else if (s_world_live) { // fire wins the frame: yellow never mixes to green
        b = (uint8_t)((110u * (255u - (timer_elapsed32(s_world_at) * 255u) / DOOM_RGB_FLASH_MS)) / 255u);
    }
    rgb_matrix_set_color_all(r, g, b);
    return false;
}

#else // !RGB_MATRIX_ENABLE (split42): the cue has no hardware; keep the seams

static void doom_rgb_session_reset(void) {}
static void doom_rgb_task(void) {}

#endif // RGB_MATRIX_ENABLE

void doom_tick(void) {
    // Relay the game core's buffered printf output first — also after exit, so
    // late lines still reach the console.
    doom_shim_drain_core1_log();
    // Sound->RGB cue, both halves, active or not (the fade-out + the matrix
    // enable/restore run past the session end).
    doom_rgb_task();
    if (!is_usb_host_side()) {
        doom_slave_tick();
        return;
    }
    if (!s_active) {
        return;
    }
    if (s_esc_down && timer_elapsed32(s_esc_down_at) > DOOM_EXIT_HOLD_MS) {
        doom_exit();
        return;
    }
    if (doom_shim_quit_requested()) {
        // Menu "Quit Game" (I_Quit parked the game core) — same exit as the
        // ESC hold (field round 18).
        printf("doom: menu quit\n");
        doom_exit();
        return;
    }
    // Hold off the idle/fade/turn-off pipeline — the pulse/jitter machinery
    // must never repaint the keycaps while the blitter owns them.
    set_last_update((int32_t)timer_read32());
    doom_mirror_master_pump(); // before the (slow) blit: keeps the tic stream fresh
    doom_frame_pump(true);
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
