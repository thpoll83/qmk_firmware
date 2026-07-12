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

// Begin the animation (call once from keyboard_post_init_user on first boot).
void startup_anim_start(void);
// Render one frame; call every housekeeping pass while active (like doom_tick()).
void startup_anim_tick(void);
// True while the animation owns the keycaps — update_displays() must early-return.
bool startup_anim_active(void);
