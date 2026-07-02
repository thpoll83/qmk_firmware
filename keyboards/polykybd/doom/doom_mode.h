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

// Arena layout inside the borrowed overlay pool (see DOOM_FEASIBILITY.md,
// "Game-mode RAM budget"). Valid only while game mode is active — the shim's
// I_ZoneBase hands the zone region to the engine's Z_Init.
uint8_t *doom_arena_framebuffer(void);   // 320x200 8bpp frame, DOOM_FB_SIZE bytes
uint8_t *doom_arena_zone(int *size);     // the rest of the pool (engine zone memory)

#else

static inline bool doom_mode_active(void) { return false; }
static inline bool doom_process_record(uint16_t keycode, bool pressed) {
    (void)keycode; (void)pressed;
    return false;
}
static inline void doom_tick(void) {}
static inline bool doom_hid_frozen(uint8_t cmd) { (void)cmd; return false; }

#endif
