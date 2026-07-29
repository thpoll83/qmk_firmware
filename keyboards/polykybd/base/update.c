#include "update.h"

#include "timer.h"

static volatile uint32_t last_update = 0;
// Idle-timeout tracking active by default so a fresh boot idles like before. Kept
// SEPARATE from last_update (was the sign bit of a signed int32) — see update.h.
static volatile bool     idle_tracking = true;
static volatile enum refresh_mode g_refresh = DONE_ALL;
// Timestamp of the last bulk overlay/mapping command (0 = none since boot). See
// note_overlay_activity() in update.h for the coalescing rationale.
static volatile uint32_t g_last_overlay = 0;

void update_performed(void) {
    last_update   = timer_read32();
    idle_tracking = true;
}

uint32_t get_last_update(void) {
    return last_update;
}

void set_last_update(int32_t update) {
    if (update < 0) {
        idle_tracking = false;
    } else {
        last_update   = (uint32_t)update;
        idle_tracking = true;
    }
}

void disable_idle_tracking(void) {
    idle_tracking = false;
}

bool is_idle_tracking(void) {
    return idle_tracking;
}

void backdate_last_update(uint32_t ms) {
    last_update   = timer_read32() - ms;   // modular; correct even when now < ms
    idle_tracking = true;
}

uint32_t get_time_since_last_update(void) {
    return timer_elapsed32(last_update);
}

void request_disp_refresh(void) {
    g_refresh = ALL_AT_ONCE;
    //use the following for partial update (during housekeeping)
    // g_refresh = START_FIRST_HALF;
}

void note_overlay_activity(void) {
    g_last_overlay = timer_read32();
}

uint32_t overlay_activity_elapsed(void) {
    // At boot g_last_overlay is 0, so this reads as a large elapsed time (never
    // "mid-burst") — no false deferral before the first overlay ever arrives.
    return timer_elapsed32(g_last_overlay);
}

void set_disp_refresh(enum refresh_mode mode) {
    g_refresh = mode;
}

enum refresh_mode get_refresh_mode(void) {
    return g_refresh;
}

