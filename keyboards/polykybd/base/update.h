#pragma once

#include <stdint.h>

enum refresh_mode { START_FIRST_HALF, START_SECOND_HALF, DONE_ALL, ALL_AT_ONCE };

// Records current timestamp for idle timeout tracking and idle display animation.
// Global variables: last_update
void update_performed(void);

int32_t get_last_update(void);

void set_last_update(int32_t update);

int32_t get_time_since_last_update(void);

// Requests full display refresh on all displays at once.
// Global variables: g_refresh
void request_disp_refresh(void);

// Called just before a font-pack / firmware flash begins (which blocks normal
// interaction): drop to the base/default layer and render it once, so the user
// can still type plain characters and the keycaps show legible legends while the
// flash holds the main loop. Must run before fw_up freezes display updates.
void poly_prepare_for_flash(void);

void set_disp_refresh(enum refresh_mode mode);

enum refresh_mode get_refresh_mode(void) ;

// Clears the per-key idle anti-burn-in "was dark" latch so the next idle session
// starts from the centred awake legend and relocates every key cleanly. Call on any
// wake / suspend / stop-idle path. Defined in poly_keymap.c.
void reset_idle_jitter(void);
