// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
// One-time procedural boot animation for the PolyKybd split72 keycap OLEDs.
//
// Fully procedural (no framebuffer): each keycap's 72x40 window is computed on
// the fly into the shared scratch buffer and pushed, exactly like the splash /
// idle / doom paths. Spark parameters are derived from a hash of the spark
// index, so there is no particle array; the only new data is a const geometry
// table in flash (startup_anim_geom.h). Static RAM cost is a handful of bytes.
//
// Split42 gets no-op stubs (the geometry is split72-specific).
//
// TODO (deferred): play the full Eden intro at firmware STARTUP.
//   The one-shot Eden animation (startup_anim_start(), s_loop == false — the comet
//   field that converges into the "EDEN" letters then fades) currently only runs on
//   the KC_EDEN key / host REPLAY_ANIM (cmd 31); real power-on still shows the plain
//   POLY_SPLASH boot splash. Goal: run the intro on boot instead of / before the
//   splash. Left for later — it interacts with the boot-time busy window (initial
//   72-keycap render + the one-shot split sync to the just-booted slave; see the
//   qmk CLAUDE.md "boot-time busy window" notes) and with USB-enumeration timing, so
//   it needs care to not delay enumeration or stall the master main loop. Pick up
//   from where startup_anim_start() is invoked and wire a boot trigger in
//   keyboard_post_init_user() / the splash path in poly_keymap.c.
//   Plan: start the intro AFTER the boot splash (splash → Eden), and add a short
//   "fade in" at the head of the one-shot animation so the comet field ramps up from
//   black instead of popping in — smooths the splash→Eden hand-off. The fade-in would
//   be a brightness/coverage ramp over the first N frames, gated on s_loop == false so
//   the looping idle screensaver (which is meant to already be running) is unaffected.
#pragma once
#include <stdbool.h>
#include <stdint.h>

// Begin the one-shot animation (KC_EDEN / boot replay). Runs to black then ends.
void startup_anim_start(void);
// Begin the LOOPING idle screensaver (IDLE_STYLE_EDEN): a perpetual comet field —
// the boot intro's opening look (streaming L→R comets over the plasma/ripple haze)
// with NO letters, converge, or fade — held open forever. Draws at `contrast` (the
// active idle brightness) rather than the boot's full brightness. Stopped via
// startup_anim_stop() on wake/suspend. No-op if already running.
void startup_anim_start_loop(uint8_t contrast);
// Stop immediately (idle wake / suspend). Safe to call when not running.
void startup_anim_stop(void);
// True while the LOOPING screensaver owns the keycaps (idle Eden). Distinguishes it
// from the one-shot boot/KC_EDEN animation, which callers gate differently.
bool startup_anim_is_loop(void);
// Render one frame; call every housekeeping pass while active (like doom_tick()).
void startup_anim_tick(void);
// True while the animation owns the keycaps — update_displays() must early-return.
bool startup_anim_active(void);
