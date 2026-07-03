// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// PolyKybd "Can it run Doom?" easter egg — game-mode infrastructure.
// See ../DOOM_FEASIBILITY.md for the full study and ./README.md for the state
// of the port. Compiled only with POLYKYBD_DOOM (dev-harness builds:
// `qmk compile ... -e POLYKYBD_DOOM=yes`); the inline stubs below keep every
// call site free of #ifdefs and cost zero bytes in normal builds.
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef POLYKYBD_DOOM

// True while game mode owns the keycap displays and the borrowed overlay pool.
bool doom_mode_active(void);

// Key-event hook, called first thing in process_record_user. Returns true when
// the event was consumed: in game mode EVERY key event is swallowed (the host
// must see no keystrokes while fragging); outside game mode it only feeds the
// trigger sequence matcher.
bool doom_process_record(uint16_t keycode, bool pressed);

// Frame/housekeeping tick, called from housekeeping_task_user. Runs the game
// only on the master (USB) half; a no-op on the slave and while inactive.
void doom_tick(void);

// True when `cmd` is a HID command that writes into the overlay pool / the
// fragment context — those are frozen while the pool is borrowed as game
// memory. All of them are ACKless bulk commands, so the caller must DROP them
// silently (replying would inject stale reports into the host's read stream).
bool doom_hid_frozen(uint8_t cmd);

// True for keys the slave-half control pad keeps lit while the game runs
// (poly_sync_t.doom_ctl + the update_displays filter): movement, fire/use,
// menu navigation, automap, weapon slots.
bool doom_key_is_control(uint16_t keycode);

// Arena layout inside the borrowed overlay pool (see DOOM_FEASIBILITY.md,
// "Game-mode RAM budget"). Valid only while game mode is active — the shim's
// I_ZoneBase hands the zone region to the engine's Z_Init.
uint8_t *doom_arena_framebuffer(void);   // 320x200 8bpp frame, DOOM_FB_SIZE bytes
uint8_t *doom_arena_zone(int *size);     // the rest of the pool (engine zone memory)

// Consumer side of the core0->core1 input ring: qmk_shim's I_StartTic drains
// key events into D_PostEvent on the game core. Returns false when empty.
bool doom_pop_key_event(uint8_t *key, bool *pressed);

// Frame handoff, implemented in qmk_shim.c over the renderer's pico_sync
// semaphores (which cannot be touched from QMK translation units — ChibiOS
// also defines semaphore_t): non-blocking take of a completed frame, and the
// release that unblocks the game for the next one.
bool doom_shim_take_frame(void);
void doom_shim_release_frame(void);

// core1 console relay (qmk_shim.c): drain the game core's buffered printf
// output through the real console on core0. Called from doom_tick.
void doom_shim_drain_core1_log(void);

// Engine boot progress breadcrumb (qmk_shim.c) for the no-frame heartbeat:
// 1 zone, 2 I_InitGraphics, 3 pd_init done, 4 first input pump.
extern volatile uint8_t doom_shim_progress;

// Core0-side peeks of core1 game state for the periodic stats line
// (qmk_shim.c — doom_mode.c cannot include engine headers).
int doom_shim_gametic(void);
unsigned doom_shim_video_type(void);

// vpatch overlay compose (qmk_shim.c) — menus/HUD/status bar on top of the
// view buffer, the keycap-blit equivalent of upstream's scanout compose.
// begin() once per consumed frame, then line() with a 320-byte 8bpp buffer
// for every canvas scanline y = 0..199 in ASCENDING order (fills the source
// row and draws the overlay patches for that line).
void doom_shim_compose_begin(void);
void doom_shim_compose_line(uint8_t *line, unsigned y);

// Player vitals for the outer-column keycap HUD (qmk_shim.c): health, armor
// and the ready weapon's ammo (-1 for fist/chainsaw). False outside a level.
bool doom_shim_hud_stats(int *health, int *armor, int *ammo);

#else

static inline bool doom_mode_active(void) { return false; }
static inline bool doom_process_record(uint16_t keycode, bool pressed) {
    (void)keycode; (void)pressed;
    return false;
}
static inline void doom_tick(void) {}
static inline bool doom_hid_frozen(uint8_t cmd) { (void)cmd; return false; }
// Never queried when doom is compiled out (doom_ctl is never set), but the
// update_displays filter references it unconditionally.
static inline bool doom_key_is_control(uint16_t keycode) { (void)keycode; return true; }

#endif
