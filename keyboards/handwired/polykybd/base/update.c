#include "update.h"

#include "timer.h"

static volatile int32_t last_update = 0;
static volatile enum refresh_mode g_refresh = DONE_ALL;

void update_performed(void) {
    last_update = timer_read32();
}

int32_t get_last_update(void) {
    return last_update;
}

void set_last_update(int32_t update) {
    last_update = update;
}

int32_t get_time_since_last_update(void) {
    return timer_elapsed32(last_update);
}

void request_disp_refresh(void) {
    g_refresh = ALL_AT_ONCE;
    //use the following for partial update (during housekeeping)
    // g_refresh = START_FIRST_HALF;
}

void set_disp_refresh(enum refresh_mode mode) {
    g_refresh = mode;
}

enum refresh_mode get_refresh_mode(void) {
    return g_refresh;
}

