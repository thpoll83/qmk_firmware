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

#include "hid_com.h"           // poly_mark_fresh_boot (host overlay-cache reset on exit)
#include "keycode_helper.h"    // KC_IDDQD (the armed utilities-layer menu item)
#include "layers.h"            // _UL (the egg menu position is utilities-layer-gated)
#include "split_sync.h"        // sync_succeeded (mirror bridge sends)
#include "polymod_crc32.h"     // crc32_1byte (mirror message framing)
#include "transactions.h"      // USER_SYNC_OVERLAY_MAP_DATA (mirror rides it)

#include "bridge_helper.h"
#include "side.h"               // is_left_side() (HUD column selection)
#include "base/overlay.h"
#include "base/update.h"
#include "base/fw_staging.h"
#include "polymod_core1.h"

#include "doomkeys.h"           // engine key codes (doom_translate_key)
#include "doom_weapon_icons.h"  // slave weapon-pad bitmaps (shareware sprites)
#include "hardware/structs/psm.h"
#include "../poly_keymap.h"

#ifdef POLYKYBD_DOOM

// The engine entry (d_main.c). Declared directly — pulling doom/d_main.h into
// this QMK-side file would drag the whole engine header graph next to
// QMK_KEYBOARD_H for one prototype. (The pico_sync frame semaphores live in
// qmk_shim.c behind doom_shim_take/release_frame — pico/sem.h cannot be
// included here: ChibiOS also defines semaphore_t, and pico/time.h's param
// asserts break inside QMK translation units.)
#ifndef POLYKYBD_DOOM_PACK
extern void D_DoomMain(void);

// The doom linker script's shared block: pool base, end of the engine's
// zero-init statics (= the arena base), and pool end (base + 226,800). See
// doom_arena.h for the tiering.
extern uint8_t __doom_shared_base__[];
extern uint8_t __doom_shared_statics_end__[];
extern uint8_t __doom_shared_end__[];
#else
// DoomPack flavour (PACK_DESIGN.md): no engine in the image, no custom
// linker script. The pool is base/overlay.c's plain array (borrowed via
// get_overlay_pool() as always) and the engine-statics/arena split inside it
// comes from the loaded pack's header (doom_pack_arena_off()).
#define DOOM_POOL_BYTES ((uint32_t)NUM_OVERLAY_SLOTS * (72 * 40 / 8))
#endif

// The engine's view height (SCREENHEIGHT 200 - the 32 px vpatch status bar).
#define DOOM_VIEW_BUFFER_ROWS (DOOM_ARENA_FB_BYTES / 320)

// True when the engine is live on core1 (WHX found, core launched); false in
// game mode without a WHX — then the fire demo runs as the "NO WAD" gag.
static bool s_engine_running;

// Borrowed overlay pool (defined with the mode statics below; forward
// declaration for doom_engine_start's pack-flavour stack carve).
static uint8_t *s_fb;

static bool doom_whx_present(void) {
    // The WHX ships XIP-mapped at TINY_WAD_ADDR (flash 0x600000); magic "IWHX"
    // (WHD_SUPER_TINY format — w_file_memory.c checks the same bytes).
    const uint8_t *whx = (const uint8_t *)TINY_WAD_ADDR;
    return whx[0] == 'I' && whx[1] == 'W' && whx[2] == 'H' && whx[3] == 'X';
}

// Hard-reset core1 via the power-on state machine (pico-sdk
// multicore_reset_core1 — not compiled here because the SDK's multicore.c
// collides with the polymod_core1 launcher).
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
#ifdef POLYKYBD_DOOM_PACK
    doom_pack()->engine_main(); // never returns
#else
    D_DoomMain(); // never returns
#endif
    while (true) {}
}

static void doom_engine_start(void) {
    if (!doom_whx_present()) {
        printf("doom: no WHX at %p — running the fire demo instead\n", (void *)TINY_WAD_ADDR);
        s_engine_running = false;
        return;
    }
#ifdef POLYKYBD_DOOM_PACK
    if (!doom_pack_loaded()) {
        // doom_session_start already tried (and logged why it refused) —
        // same degradation as a missing WHX.
        printf("doom: no usable engine pack — running the fire demo instead\n");
        s_engine_running = false;
        return;
    }
#endif
    // Take core1 from the overlay-RLE service (idle in game mode — the
    // pool-writing HID commands are frozen) and give it to the game, with its
    // stack at the tail of the pool.
    doom_core1_reset();
#ifdef POLYKYBD_DOOM_PACK
    uint32_t *stack_bottom = (uint32_t *)(s_fb + DOOM_POOL_BYTES - DOOM_ARENA_STACK_BYTES);
#else
    // (uintptr_t detour: negative offsets from a zero-size linker symbol trip
    // GCC's array-bounds check)
    uint32_t *stack_bottom = (uint32_t *)((uintptr_t)__doom_shared_end__ - DOOM_ARENA_STACK_BYTES);
#endif
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
#ifdef POLYKYBD_DOOM_PACK
        // Back to the stub table — the pool is about to be handed back to
        // the overlays, so nothing may dispatch into the dead pack (its
        // statics lived in that pool). The next session re-loads it.
        doom_pack_unload();
#endif
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

// Trigger (two-stage since field round 44): typing IDDQD (plain, unmodified,
// master half) no longer starts the game — it silently ARMS the egg, which
// makes the KC_IDDQD key on the utilities layer show "IDDQD"; pressing THAT
// starts the game. Typing the cheat into an editor/game therefore changes
// nothing visible and swallows no keystrokes; the armed state survives game
// exits (relaunch from the menu without retyping) and clears on power-off.
static const uint16_t TRIGGER_SEQ[]   = {KC_I, KC_D, KC_D, KC_Q, KC_D};
#define TRIGGER_LEN (sizeof(TRIGGER_SEQ) / sizeof(TRIGGER_SEQ[0]))
#define TRIGGER_TIMEOUT_MS 3000
// Exit: hold ESC this long while in game mode.
#define DOOM_EXIT_HOLD_MS 1500
// Frame pacing for the placeholder scene (~12 fps).
#define DOOM_FRAME_MS 80
// Attract-screensaver runtime: the demo plays for the same wall-clock window
// the idle pulse would have covered (fade end -> TURN_OFF suspend), then
// doom_tick tears down and suspends exactly like the pulse path would.
#define DOOM_SAVER_MAX_MS (TURN_OFF_TIME - FADE_OUT_TIME - FADE_TRANSITION_TIME)

// poly_keymap.c — the shared suspend path (displays off, state flushed); the
// screensaver deadline hands over to it so its end state matches the pulse's.

static bool     s_active;
static bool     s_screensaver;  // this session is the attract screensaver
static uint32_t s_saver_start;  // for the DOOM_SAVER_MAX_MS deadline
static bool     s_egg_armed; // master-local; see the trigger comment above

// IDDQD screensaver anti-burn-in placement: the 5x4 attract block (bottom UI
// row dropped) is repositioned every DOOM_SAVER_MOVE_MS so the lit keycaps
// migrate — down 0/1 keycap rows, and 0..3 empty keycap columns counted from
// this half's outer edge (select_display_placed() reclaims the game's per-role
// margin so the count is edge-relative). At inset 3 + row_off 1 the block's
// bottom row reaches the INNER thumb key (bottom-row-only display; the upper
// rows have no panel at the inner phantom column, so those cells just drop out —
// see doom_blit's select_display_placed). Placement CYCLES deterministically
// (doom_saver_reroll) so the thumb-reaching corner is guaranteed to come around;
// the two halves run the same cycle a half-step apart (whichever half this
// firmware is uses these statics for its own blit).
#define DOOM_SAVER_MOVE_MS 15000
#define DOOM_SAVER_INSETS  4            // grid insets 0..3 (0 sweeps outer edge, 3 the inner thumb column)
#define DOOM_SAVER_STEPS   (2 * DOOM_SAVER_INSETS)  // 2 row offsets x 4 insets
static uint8_t  s_saver_row_off;    // 0 or 1
static uint8_t  s_saver_col_inset;  // 0..3 (grid columns slid inward; max 3 empty outer cols)
static uint32_t s_saver_move_at;
static uint8_t  s_saver_step;       // deterministic placement cursor

// Advance to the next placement and blank the viewport so keys vacated by the
// move don't keep their last (burning-in) frame. The placement CYCLES
// deterministically through all 8 combinations (2 row offsets x 4 insets) rather
// than a random walk — so every keycap the block can reach, INCLUDING both stacked
// inner thumb keys (the inner grid column at inset 3) and the staggered in-between
// key, is covered in turn (even anti-burn-in, and actually verifiable: a random
// draw was easy to keep missing on hardware). The two halves start a half-cycle
// apart so they don't sit in visual lockstep.
static void doom_saver_reroll(void) {
    s_saver_row_off   = (uint8_t)(s_saver_step % 2u);            // 0 or 1
    s_saver_col_inset = (uint8_t)(s_saver_step / 2u);            // 0..4
    s_saver_step      = (uint8_t)((s_saver_step + 1u) % DOOM_SAVER_STEPS);
    s_saver_move_at   = timer_read32();
    doom_blit_blank_all();
}
static uint8_t  s_trigger_pos;
static uint32_t s_trigger_last;
static bool     s_esc_down;
static uint32_t s_esc_down_at;
static uint32_t s_last_frame;
static uint8_t *s_fb; // borrowed overlay pool; framebuffer = first 64,000 B

bool doom_mode_active(void) {
    return s_active;
}

bool doom_mode_screensaver(void) {
    return s_active && s_screensaver;
}

bool doom_egg_armed(void) {
    return s_egg_armed;
}

// The armed menu item is a matrix-POSITION alias, not (only) a keymap entry:
// DYNAMIC_KEYMAP means the runtime keymap lives in EEPROM, so the compiled
// keymaps[] KC_IDDQD never reaches an already-provisioned keyboard (field
// round 46 — key stayed blank and inert; same class the control pad solved
// in round 23). While armed and the utilities layer is active, the KC_NO
// that EEPROM delivers at the menu position — [1,5] left (next to Break) /
// [6,6] right (next to the media keys) — is rewritten to KC_IDDQD for both
// input (doom_process_record) and rendering (update_displays). A fresh
// EEPROM seeds KC_IDDQD from keymaps[] directly and passes through here
// unchanged. Master-side only: the matcher/armed state live there, and the
// legend renders on whichever half is the master.
uint16_t doom_egg_menu_keycode(uint16_t keycode, uint8_t row, uint8_t col) {
    if (keycode == KC_NO && s_egg_armed && is_usb_host_side() &&
        get_highest_layer(layer_state) == _UL &&
        ((row == 1 && col == 5) || (row == 6 && col == 6))) {
        return KC_IDDQD;
    }
    return keycode;
}

#ifdef POLYKYBD_DOOM_PACK
// Pack flavour: the statics/arena split comes from the loaded pack's header
// (0 while unloaded — the fire demo then owns the pool from its base, which
// is exactly the monolith's no-WHX behaviour with zero engine statics).
uint8_t *doom_arena_at(unsigned offset) {
    return s_active || s_fb ? s_fb + doom_pack_arena_off() + offset : NULL;
}

uint8_t *doom_arena_zone(int *size) {
    if (size) {
        *size = (int)(DOOM_POOL_BYTES - doom_pack_arena_off())
                - DOOM_ARENA_ZONE_OFF - DOOM_ARENA_STACK_BYTES;
    }
    return doom_arena_at(DOOM_ARENA_ZONE_OFF);
}
#else
uint8_t *doom_arena_at(unsigned offset) {
    return s_active || s_fb ? __doom_shared_statics_end__ + offset : NULL;
}

uint8_t *doom_arena_zone(int *size) {
    if (size) {
        *size = (int)(__doom_shared_end__ - __doom_shared_statics_end__)
                - DOOM_ARENA_ZONE_OFF - DOOM_ARENA_STACK_BYTES;
    }
    return doom_arena_at(DOOM_ARENA_ZONE_OFF);
}
#endif

uint8_t *doom_arena_framebuffer(void) {
    return doom_arena_at(DOOM_ARENA_FB_OFF);
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
    s_fb = (uint8_t *)get_overlay_pool();
#ifdef POLYKYBD_DOOM_PACK
    // Zero the whole pool, then bring up the flashed engine pack: its init
    // re-runs the pack crt0 (.data copy + .bss zero inside the pool), so
    // every session starts from virgin engine statics INCLUDING initialized
    // .data — the class the monolith's doom_shim_set_role reset list only
    // approximates. A refused pack (missing/stale/corrupt — already logged)
    // leaves the stub table: doom_engine_start then runs the fire demo.
    memset(s_fb, 0, DOOM_POOL_BYTES);
    if (!doom_pack_load(s_fb, DOOM_POOL_BYTES)) {
        // No valid engine pack flashed (missing/stale/corrupt — already logged).
        // On the pack flavour there is nothing to run without it, so REFUSE the
        // whole session: doom_screensaver_start() then returns false and the idle
        // pipeline falls back to the next style (jitter/pulse), and the armed
        // utils-menu item is a no-op (doom_enter ignores the false). We do NOT
        // fall through to the compiled-in fire demo here — the attract/game is
        // meant to show the real engine, not a stand-in.
        s_fb = NULL;
        return false;
    }
#else
    // Zero the whole shared block: the engine's zero-init statics live at its
    // front (.doom_shared is not crt0-zeroed) — every entry starts the game
    // from virgin static state (including the mirror mailbox).
    memset(__doom_shared_base__, 0, (size_t)(__doom_shared_end__ - __doom_shared_base__));
#endif
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

static void doom_exit(void); // defined below; doom_screensaver_stop tears down early

static bool doom_begin(bool screensaver) {
    if (s_active || !doom_session_start()) {
        return false;
    }
    // Release anything still registered host-side (the trigger letters have
    // already been sent; nothing may stay held while we swallow events).
    clear_keyboard();

    s_active      = true;
    s_screensaver = screensaver;
    s_saver_start = timer_read32();
    s_esc_down    = false;
    s_last_frame  = 0;
    set_last_update((int32_t)timer_read32());
    doom_blit_blank_all();
    if (screensaver) {
        // Phase the two halves a half-cycle apart so their blocks aren't in
        // visual lockstep, then pick the first anti-burn-in placement.
        s_saver_step = is_left_side() ? 0 : (DOOM_SAVER_STEPS / 2);
        doom_saver_reroll();
    }
    return true;
}

static void doom_enter(void) {
    (void)doom_begin(false);
}

bool doom_screensaver_start(void) {
    if (!is_usb_host_side()) {
        return false; // the idle pipeline is master-only; so is the session
    }
    if (!doom_begin(true)) {
        return false;
    }
    printf("doom: attract screensaver up\n");
    return true;
}

void doom_screensaver_stop(void) {
    if (s_active && s_screensaver) {
        printf("doom: screensaver stopped (suspend)\n");
        doom_exit();
    }
}

static void doom_exit(void) {
    // Breadcrumbs around every teardown stage — the round-20 log showed a
    // ~15 s post-exit silence with no way to tell which stage stalled.
    printf("doom: exit begin\n");
    s_active      = false;
    s_screensaver = false;
    // Drop the synced pad flag IMMEDIATELY — housekeeping only refreshes it
    // at the END of its pass, so the repaint doom_exit requests below would
    // otherwise still run update_displays' doom_ctl branch on this half
    // (pad chrome incl. the STCFN ESC corner) against the torn-down engine.
    // This also gets the 0 onto the very next POLY sync to the slave.
    access_local_state()->doom_ctl = 0;
    access_local_state()->doom_rgb = 0; // lights out with the same sync
    doom_engine_stop();
#ifdef POLYKYBD_DOOM_PACK
    // Covers the fire-demo case too (pack loaded but engine never launched —
    // doom_engine_stop only unloads when it actually stopped an engine).
    doom_pack_unload();
#endif
    s_fb = NULL;
    // Hand the pool back in the same state a fresh boot / font-pack wipe leaves
    // it: blank buffers, no usage bits, identity mapping.
    reset_overlay_pool();
    clear_display_has_overlay();
    reset_display_to_pool();
    reset_fragment_context();
    // Actively tell the host its overlays are gone: re-raise the GET_ID fresh-boot
    // marker so the next reconnect probe (~1 s) resets the host MRU cache and
    // re-pushes the current app's overlays. Without this the host only re-pushes on
    // an app switch / reconnect, so staying on the same app after the egg left the
    // keycaps blank (field). Harmless on the slave (its GET_ID is never read).
    poly_mark_fresh_boot();
    set_last_update((int32_t)timer_read32());
    // Symmetric with doom_slave_stop(): the blitter drew untracked full-window
    // frames, so force a full-window repaint on the handback. On the master the
    // generic s_disp_render_active path already covers this, but keeping it
    // explicit makes both teardown halves identical and independent of that gate.
    doom_blit_invalidate_windows();
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
        // The armed menu item (utilities layer): starts the game once IDDQD
        // has been typed; inert (and rendered blank) before that. Position
        // alias first — an EEPROM dynamic keymap delivers KC_NO here (see
        // doom_egg_menu_keycode). Swallow both edges always — it is never a
        // real HID key. The slave half's copy of the position works too
        // (the master processes the whole matrix) but stays blank (armed
        // state is master-local).
        keycode = doom_egg_menu_keycode(keycode, row, col);
        if (keycode == KC_IDDQD) {
            if (pressed && s_egg_armed && is_usb_host_side()) {
                doom_enter();
            }
            return true;
        }
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
                if (!s_egg_armed) {
                    s_egg_armed = true;
                    printf("doom: armed — IDDQD is now on the utilities layer\n");
                    request_disp_refresh(); // in case that layer is showing
                }
                // Deliberately NOT swallowed: typing the cheat stays invisible
                // (the host receives the full word; nothing on-screen changes).
            }
        } else {
            // restart, allowing the mismatch to begin a new sequence (I-I-D…)
            s_trigger_pos = (keycode == TRIGGER_SEQ[0]) ? 1 : 0;
            s_trigger_last = timer_read32();
        }
        return false;
    }

    // Screensaver: the FIRST key press dismisses it and then PASSES THROUGH —
    // like the normal idle pulse, whose wake key both lights the displays and
    // registers the keystroke (display_wakeup() returns accept_keypress; the
    // DEAD_KEY_ON_WAKEUP swallow flag is off). doom_exit() restores the legends
    // and re-arms the idle timer, then we return false so the key continues into
    // normal processing (the later display_wakeup() handles the wake).
    if (s_screensaver) {
        if (pressed) {
            printf("doom: screensaver dismissed\n");
            doom_exit();
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
        // Fire hint (field rounds 17+23+24): fire is the bottom outer key on
        // either master — the physical Ctrl on a left master, the
        // doom_ctl_keycode alias on a right one. ⚠️ The right half's BOTTOM
        // row keeps raw matrix columns (invert_display's col-1 shift covers
        // rows 5-8 only), so its outermost bottom display is col 7, not the
        // upper rows' HUD col 6 — col 6 drew the reticle one key inward
        // (field round 24).
        doom_blit_fire_key(MATRIX_ROWS_PER_SIDE - 1, is_left_side() ? 0 : 7);
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

// Viewport luma with a mild contrast gain (round 42: "increase the contrast
// on the viewport just a bit") — saturating gain around mid-grey, applied
// only to the master's 3D-view blit. Menus (own threshold rule), the slave's
// automap (already line-art on black) and the HUD/face draws keep the
// straight PLAYPAL table. Built lazily; 256 B of plain .bss.
// Round 44 dialed 1.25x back to 1.125x ("not bad, but now a bit too dark —
// half way of what we had and now"): the gain darkens below-mid pixels, and
// DOOM's palette is mostly below mid-grey, so 1.25 cost more shadow detail
// than the highlight pop was worth.
#define DOOM_VIEW_CONTRAST_NUM 9
#define DOOM_VIEW_CONTRAST_DEN 8
static const uint8_t *doom_view_luma(void) {
    static uint8_t lut[256];
    static bool    built;
    if (!built) {
        for (int i = 0; i < 256; ++i) {
            const int v = 128 + (((int)DOOM_PLAYPAL_LUMA[i] - 128) * DOOM_VIEW_CONTRAST_NUM) / DOOM_VIEW_CONTRAST_DEN;
            lut[i] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
        }
        built = true;
    }
    return lut;
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
            // Screensaver: drop the bottom (mostly-UI) canvas row and place the
            // 5x4 block at the rolled anti-burn-in offset. Game mode: full 5x5,
            // no offset (0/0 is byte-identical to the old call).
            doom_blit_frame_engine(doom_view_luma(), s_screensaver, false,
                                   s_screensaver ? s_saver_row_off : 0,
                                   s_screensaver ? (int8_t)s_saver_col_inset : -1);
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
// Self-healing START (round 24: the second-session slave stayed in its own
// attract with zero breadcrumbs): every mirror ack carries the slave's
// engagement (SYNC_ACK_SIG = drone up); while a START is outstanding and the
// slave still reports un-engaged, the master re-offers the stored START —
// the slave's handler bumps start_in_seq on every receipt, so a re-offer
// re-applies it (G_DeferedInitNew is idempotent for an un-engaged drone).
static doom_mirror_msg_t s_mir_start_msg;      // last START sent, for re-offers
static bool     s_mir_start_outstanding;
static uint32_t s_mir_start_sent_at;
static bool     s_mir_slave_engaged;           // last acked engagement state
#define DOOM_MIRROR_START_REOFFER_MS 1500u

// Slave fire-flash timing — the rounds 26-38 saga, closed: the SLAVE flashes
// directly on its own drone's sound edges (zero hold — the fastest that half
// goes), and the MASTER delays its edges by DOOM_RGB_MASTER_FIRE_LAG_MS to
// meet it (round 38: with both halves at zero hold, video showed the slave
// ~0.1 s behind — structural, its tics only run when a display frame turns —
// so the compensation belongs on the master, which has slack to burn). Two
// dead ends documented so they aren't retried: (1) the BT_ATTACK-receipt
// fast path was pinned to the WRONG EVENT — DOOM weapons have a windup
// (A_FirePistol runs several tics after the press enters the weapon state
// machine), so it led the bang by a weapon-dependent 100-200 ms no constant
// could bridge; (2) a fixed HOLD on the drone's sound edges assumed the
// drone leads the master by (build-ahead − delivery) — but the field bisect
// (50/25/12 ms all read "slave late", shrinking with the hold) showed the
// drone's frame-gated tic drain + render pipeline already eats that lead
// entirely — the drone never leads, so holding it only widened the gap.

static void doom_mirror_session_reset(void) {
    s_mir_ctl_seq_sent  = 0;
    s_mir_backoff_at    = 0;
    s_mir_dead_reported = false;
    s_mir_menu_count    = 0;
    s_mir_menu_item_on  = 0;
    memset(s_mir_menu_items, 0, sizeof(s_mir_menu_items));
    memset(&s_mir_start_msg, 0, sizeof(s_mir_start_msg));
    s_mir_start_outstanding = false;
    s_mir_start_sent_at     = 0;
    s_mir_slave_engaged     = false;
}

// The QMK transaction table is full (32-id cap), so mirror messages
// multiplex onto USER_SYNC_OVERLAY_MAP_DATA by their distinct payload size
// (72 B vs the 68 B map chunks / 37 B MRU snapshots), exactly like the MRU
// snapshots already do. No cross-talk: the host's overlay-map commands are
// frozen while game mode holds the pool, so the id is otherwise silent.
static bool doom_mirror_send(doom_mirror_msg_t *msg, uint8_t retries) {
    msg->crc32 = crc32_1byte(((uint8_t *)msg) + 4, sizeof(*msg) - 4, 0);
    const uint8_t ack = send_to_bridge(USER_SYNC_OVERLAY_MAP_DATA, msg, sizeof(*msg), retries);
    if (sync_succeeded(ack)) {
        // The ack byte carries the slave's mirror status (split_sync.c):
        // SYNC_ACK_SIG = drone engaged. Log the transitions — the slave's own
        // console prints are unreachable from the host.
        const bool engaged = ack == SYNC_ACK_SIG;
        if (engaged != s_mir_slave_engaged) {
            s_mir_slave_engaged = engaged;
            printf("doom: slave mirror %s\n", engaged ? "engaged" : "not engaged");
            if (engaged) {
                s_mir_start_outstanding = false; // delivered — stop re-offering
            }
        }
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
            printf("doom: mirror ctl %u sent (tic=%lu)\n", msg.kind, (unsigned long)msg.tic);
            if (msg.kind == DOOM_MIRROR_MSG_START) {
                s_mir_start_msg         = msg;
                s_mir_start_outstanding = true;
                s_mir_start_sent_at     = timer_read32();
            } else {
                s_mir_start_outstanding = false; // BREAK supersedes any START
            }
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
    // Self-healing START: while the slave keeps acking "not engaged" after a
    // START went out, re-offer it — the receipt bumps start_in_seq, so the
    // slave re-applies (round 24: second-session drone never engaged, cause
    // invisible; this makes the mirror converge regardless).
    if (s_mir_start_outstanding && !s_mir_slave_engaged &&
        timer_elapsed32(s_mir_start_sent_at) > DOOM_MIRROR_START_REOFFER_MS) {
        s_mir_start_sent_at = timer_read32();
        printf("doom: re-offering mirror START (tic=%lu)\n",
               (unsigned long)s_mir_start_msg.tic);
        (void)doom_mirror_send(&s_mir_start_msg, 2);
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
#ifdef POLYKYBD_DOOM_PACK
    doom_pack_unload(); // as in doom_exit — nothing may dispatch into the dead pack
#endif
    s_fb = NULL;
    // Same pool hand-back contract as doom_exit: blank + reconstructible.
    reset_overlay_pool();
    clear_display_has_overlay();
    reset_display_to_pool();
    reset_fragment_context();
    // The game blitter owned these panels with untracked full-window sends and the
    // slave never drove s_disp_render_active false (doom_mode_active() is the
    // master-only s_active, and the pad-legend render kept it true), so the generic
    // invalidate in update_displays() won't fire here — force it so the repaint below
    // erases the whole window per panel instead of streaming a stale sub-rectangle
    // (DOOM-exit leftovers on the slave). Eden is unaffected: its startup_anim_active()
    // gate IS true on the slave, so that path invalidates on its own.
    doom_blit_invalidate_windows();
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
            // Bottom row was blitted (untracked full window); pad legends return
            // there — force the full-window repaint or the dirty-window send leaves
            // DOOM pixels on the handed-back keys (same as the exit case).
            doom_blit_invalidate_windows();
            request_disp_refresh(); // attract -> map/menu: thumb legends return
        }
    }
    if (live != s_slave_blit) {
        s_slave_blit = live;
        if (!live) {
            // Viewport blitter released these panels; update_displays repaints the
            // legends — invalidate the stale per-panel windows first (see above).
            doom_blit_invalidate_windows();
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
    // IDDQD screensaver on the slave (doom_ctl == 2): drop the bottom UI row and
    // migrate the block like the master, rolling this half's own placement.
    const bool saver = get_local_state()->doom_ctl == 2;
    if (saver && (s_saver_move_at == 0 || timer_elapsed32(s_saver_move_at) > DOOM_SAVER_MOVE_MS)) {
        doom_saver_reroll();
    }
    // Consume frames even while not blitting — the engine loop must keep
    // turning (blocked on the frame semaphore otherwise) to see a START.
    if (doom_shim_take_frame()) {
        if (s_slave_blit && !menu) {
            doom_blit_frame_engine(DOOM_PLAYPAL_LUMA,
                                   saver ? true : !s_slave_blit_bottom,
                                   doom_shim_drone_map_live(),
                                   saver ? s_saver_row_off : 0,
                                   saver ? (int8_t)s_saver_col_inset : -1);
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
// Master-only fire-edge delay to line up with the slave's structural render
// lag (see the timing-saga note above doom_mirror_session_reset). Round 38's
// video read ~0.1 s, but 100 was too much in play (round 39: master late)
// and 60 looked no different from 100 (round 40) — likely everything in the
// 60-100 band still parks the master's pulse on the same RGB-frame boundary.
#define DOOM_RGB_MASTER_FIRE_LAG_MS 20u

// Edge-detector state (only one half's compute runs per device).
static uint32_t s_rgb_fire_seen, s_rgb_world_seen;
static uint8_t  s_rgb_fire_pulse, s_rgb_world_pulse; // 1..3 while flashing, 0 idle
static uint32_t s_rgb_fire_at, s_rgb_world_at;
// Master fire edges in flight during the lag window — a chaingun fires every
// ~2 tics (~57 ms), so a couple can overlap one 100 ms hold.
#define DOOM_RGB_FIRE_Q 4u
static uint32_t s_rgb_fire_q[DOOM_RGB_FIRE_Q];
static uint8_t  s_rgb_fire_q_head, s_rgb_fire_q_len;
static uint8_t  s_rgb_last;       // last locally computed byte (this half's engine)
static bool     s_rgb_local_live; // this half's engine is a valid local source
static bool     s_rgb_forced_on;  // we woke a user-disabled matrix for the cue

static void doom_rgb_session_reset(void) {
    // The shim counters restart at 0 with every engine launch
    // (doom_shim_set_role) — restart the edge detector with them.
    s_rgb_fire_seen  = s_rgb_world_seen  = 0;
    s_rgb_fire_pulse = s_rgb_world_pulse = 0;
    s_rgb_fire_at    = s_rgb_world_at    = 0;
    s_rgb_last       = 0;
    s_rgb_local_live = false;
    s_rgb_fire_q_head = s_rgb_fire_q_len = 0;
}

// Sound counters + health -> the doom_rgb byte (state.h layout). Runs on
// whichever half has a live engine: the master publishes it into the synced
// state; the slave keeps it local — its drone plays the same sounds in
// lockstep, so deriving locally beats waiting a bridge round-trip (round 24:
// "on the slave side it is delayed").
static uint8_t doom_rgb_compute(void) {
    const uint32_t now  = timer_read32();
    // Fire: the slave flashes the instant its drone plays the weapon sound;
    // the master queues its edges for DOOM_RGB_MASTER_FIRE_LAG_MS to meet the
    // slave's structural lag (see the timing-saga note above the mirror
    // session reset).
    const uint32_t fire = doom_shim_snd_fire;
    bool fire_edge = false;
    if (fire != s_rgb_fire_seen) {
        uint32_t delta = fire - s_rgb_fire_seen;
        s_rgb_fire_seen = fire;
        if (is_usb_host_side()) {
            for (; delta && s_rgb_fire_q_len < DOOM_RGB_FIRE_Q; delta--) {
                s_rgb_fire_q[(uint8_t)((s_rgb_fire_q_head + s_rgb_fire_q_len) % DOOM_RGB_FIRE_Q)] = now;
                s_rgb_fire_q_len++;
            }
        } else {
            fire_edge = true;
        }
    }
    if (s_rgb_fire_q_len && now - s_rgb_fire_q[s_rgb_fire_q_head] >= DOOM_RGB_MASTER_FIRE_LAG_MS) {
        s_rgb_fire_q_head = (uint8_t)((s_rgb_fire_q_head + 1u) % DOOM_RGB_FIRE_Q);
        s_rgb_fire_q_len--;
        fire_edge = true;
    }
    if (fire_edge) {
        s_rgb_fire_pulse = (uint8_t)(s_rgb_fire_pulse % 3u) + 1u;
        s_rgb_fire_at    = now;
    } else if (s_rgb_fire_pulse && now - s_rgb_fire_at > DOOM_RGB_FLASH_MS) {
        s_rgb_fire_pulse = 0;
    }
    const uint32_t world = doom_shim_snd_world;
    if (world != s_rgb_world_seen) {
        s_rgb_world_seen = world;
        // Fire outranks the world: a blue pulse neither starts nor re-arms
        // while the yellow flash runs (the renderer also suppresses it), so
        // the mix can't read green.
        if (!s_rgb_fire_pulse) {
            s_rgb_world_pulse = (uint8_t)(s_rgb_world_pulse % 3u) + 1u;
            s_rgb_world_at    = now;
        }
    } else if (s_rgb_world_pulse && now - s_rgb_world_at > DOOM_RGB_FLASH_MS) {
        s_rgb_world_pulse = 0;
    }
    uint8_t rgb = 0;
    int hp, ar, am;
    if (doom_shim_hud_stats(&hp, &ar, &am)) {
        // Full health = dark; each ~7 hp lost lights the red base one
        // step (0..15, saturating at death).
        const int lvl = hp >= 100 ? 0 : hp <= 0 ? 15 : ((100 - hp) * 15 + 50) / 100;
        rgb = (uint8_t)lvl;
    }
    return rgb | (uint8_t)(s_rgb_fire_pulse << 4) | (uint8_t)(s_rgb_world_pulse << 6);
}

static void doom_rgb_task(void) {
    // Attract demos fire weapons too — only real gameplay drives the
    // lights (the demo loop staying dark also reads as "idle"). Both halves
    // derive the cue from THEIR OWN engine when it is live — the slave's
    // lockstep drone plays the same sounds, so no bridge round-trip
    // (rounds 24+25, "use the lock-step for the RGB fire as well"). The
    // actual sampling runs at render rate in doom_rgb_indicators; here we
    // only gate the source and publish the master's byte for the fallback
    // consumers (a pad-only slave with no engine).
    const bool live = s_engine_running && !doom_shim_attract_active() &&
                      (is_usb_host_side() ? s_active : s_slave);
    s_rgb_local_live = live;
    if (is_usb_host_side()) {
        access_local_state()->doom_rgb = live ? s_rgb_last : 0;
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

    // A half with a live engine samples its own sound counters HERE, at
    // render rate — the flash starts the same RGB frame the (lockstep)
    // engine plays the sound, instead of waiting for the next housekeeping
    // pass (round 25: "still a delay on the slave"). Everything else (a
    // pad-only slave) renders the master's synced byte.
    if (s_rgb_local_live) {
        s_rgb_last = doom_rgb_compute();
    }
    const uint8_t rgb   = s_rgb_local_live ? s_rgb_last : get_local_state()->doom_rgb;
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
    // Steady red base from lost health. Stepped down twice in the field
    // (round 24 "too bright" -> 1/3; round 32 "still a bit more") — now
    // ~2/9 of the v24 values; red caps at 18/255.
    uint8_t r = (uint8_t)(((rgb & 0x0Fu) * 5u) / 4u);
    uint8_t g = 0, b = 0;
    if (s_fire_live) {
        const uint32_t k = 255u - (timer_elapsed32(s_fire_at) * 255u) / DOOM_RGB_FLASH_MS;
        const uint8_t  fr = (uint8_t)((22u * k) / 255u); // muzzle-flash yellow,
        const uint8_t  fg = (uint8_t)((15u * k) / 255u); // fading out linearly
        if (fr > r) {
            r = fr;
        }
        g = fg;
    } else if (s_world_live) { // fire wins the frame: yellow never mixes to green
        b = (uint8_t)((25u * (255u - (timer_elapsed32(s_world_at) * 255u) / DOOM_RGB_FLASH_MS)) / 255u);
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
    if (s_screensaver && timer_elapsed32(s_saver_start) > DOOM_SAVER_MAX_MS) {
        // The screensaver has run the window the idle pulse would have covered
        // — tear down and suspend, landing in the same end state the pulse's
        // TURN_OFF_TIME branch produces (doom_exit's fresh last_update is
        // deliberately overridden back to "idle timer expired").
        printf("doom: screensaver deadline — suspending\n");
        doom_exit();
        poly_suspend();
        set_last_update(-1);
        return;
    }
    if (s_screensaver && timer_elapsed32(s_saver_move_at) > DOOM_SAVER_MOVE_MS) {
        doom_saver_reroll();  // migrate the block (anti-burn-in), blanks vacated keys
    }
    // Hold off the idle/fade/turn-off pipeline — the pulse/jitter machinery
    // must never repaint the keycaps while the blitter owns them.
    set_last_update((int32_t)timer_read32());
    doom_mirror_master_pump(); // before the (slow) blit: keeps the tic stream fresh
    // Screensaver runs chrome-free: no ESC corner, no fire hint, no vitals HUD.
    doom_frame_pump(!s_screensaver);
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
        case 33: // overlay mapping, sized      (0x21)
            // All ACKless bulk writes into the borrowed pool / fragment
            // context — the dispatcher drops them without a reply.
            return true;
        default:
            return false;
    }
}

#endif // POLYKYBD_DOOM
