#pragma once

#include <stdint.h>
#include <stdbool.h>

enum refresh_mode { START_FIRST_HALF, START_SECOND_HALF, DONE_ALL, ALL_AT_ONCE };

// Records current timestamp for idle timeout tracking and idle display animation.
// Marks idle tracking active. Global variables: last_update, idle_tracking
//
// ⚠️ Call this ONLY for real USER activity on the keyboard — a keypress, a wake
// (key / proximity / host stop-idle), or a deliberate host command. It is NOT a
// generic "something changed, redraw" hook; use request_disp_refresh() for that.
//
// In particular the overlay-upload completion paths must NOT call it. An overlay
// push is the HOST reacting to a window switch on the computer, which says nothing
// about whether the user is at the keyboard — and because the host re-pushes
// overlays on every focus change, a keyboard attached to a busy machine had its
// idle countdown restarted indefinitely and never reached the fade. Worse, it is
// silent: update_performed() does not clear DISP_IDLE, so it never produced a wake
// log line, it just reset the pulse phase and pushed the TURN_OFF deadline back.
// It also re-enabled idle_tracking after a deliberate disable_idle_tracking()
// (turn-off reached / host display-off cmd 24), quietly re-arming STATUS_DISP_ON.
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

// Overlay-burst coalescing. The host streams a program switch as a BURST of
// overlay/mapping HID reports, and each one currently triggers a full ~50-100 ms
// keycap re-render of half-staged overlay state that the very next report
// immediately obsoletes (measured: ~12 renders per switch). note_overlay_activity()
// timestamps every bulk overlay/mapping command (hid_com.c on the master, the
// bridged handlers in split_sync.c on the slave); sync_and_refresh_displays() then
// DEFERS starting a fresh render while overlay_activity_elapsed() is small (the
// burst is still arriving) — but flushes anyway once overlay_pending_count() reaches
// a threshold, so a LONG burst renders in a few reactive chunks (the keys visibly
// fill in) instead of holding one frame back until the whole transfer ends.
// clear_overlay_pending() is called by the render path once it consumes them.
// Globals: g_last_overlay, g_overlay_pending
void     note_overlay_activity(void);
uint32_t overlay_activity_elapsed(void);
uint16_t overlay_pending_count(void);
void     clear_overlay_pending(void);

void set_disp_refresh(enum refresh_mode mode);

enum refresh_mode get_refresh_mode(void) ;

// poly_keymap.c's own exports (poly_prepare_for_flash / reset_idle_jitter /
// overlay_slot_displayed) used to be declared here for the convenience of this
// header's includers. They live in poly_keymap.h now.
