// One-time procedural boot animation for the PolyKybd split72 keycap OLEDs.
//
// Fully procedural (no framebuffer): each keycap's 72x40 window is computed on
// the fly into the shared scratch buffer and pushed, exactly like the splash /
// idle / doom paths. Spark parameters are derived from a hash of the spark
// index, so there is no particle array; the only new data is a const geometry
// table in flash (startup_anim_geom.h). Static RAM cost is a handful of bytes.
//
// Split42 gets no-op stubs (the geometry is split72-specific).
#pragma once
#include <stdbool.h>
#include <stdint.h>

// Begin the one-shot animation (KC_EDEN / boot replay). Runs to black then ends.
void startup_anim_start(void);
// Begin the LOOPING animation as an idle screensaver (IDLE_STYLE_EDEN): it never
// self-terminates — tick restarts it at the end — and draws at `contrast` (the
// active idle brightness) instead of the boot's full brightness. Stopped via
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
