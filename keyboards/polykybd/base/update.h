#pragma once

#include <stdint.h>
#include <stdbool.h>

enum refresh_mode { START_FIRST_HALF, START_SECOND_HALF, DONE_ALL, ALL_AT_ONCE };

// Records current timestamp for idle timeout tracking and idle display animation.
// Marks idle tracking active. Global variables: last_update, idle_tracking
void update_performed(void);

uint32_t get_last_update(void);

// Legacy sentinel API: a negative value disables idle-timeout tracking (the old
// `set_last_update(-1)` = "idle off"); a non-negative value sets the activity
// timestamp and (re)enables tracking. Prefer the explicit helpers below.
void set_last_update(int32_t update);

// Disables idle-timeout tracking (suspend / host display-off / turn-off reached).
void disable_idle_tracking(void);

// True while the idle-timeout logic in housekeeping should run. The timestamp is a
// full uint32_t, so this is a SEPARATE flag rather than a sign bit — that is what
// keeps idle working past ~24.86 days of uptime (when timer_read32() sets bit 31)
// and lets the timestamp be backdated below the epoch without colliding with a
// sentinel. Global variables: idle_tracking
bool is_idle_tracking(void);

// Backdate the activity timestamp by `ms` milliseconds and (re)enable tracking, so
// the idle fade begins `ms` ms sooner (used by the host "start idle" command to make
// it begin immediately). Uses modular uint32 arithmetic, so it is correct even in
// the first `ms` ms after boot — the old signed `timer_read32() - ms` underflowed
// there and was clamped to 0, which read as "just active" and never idled.
void backdate_last_update(uint32_t ms);

uint32_t get_time_since_last_update(void);

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
