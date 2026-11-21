/* PolyKybd Cirque gesture layer — the ADAPTER.
 *
 * All the decision-making lives in base/cirque_gesture_fsm.c, which is pure: no
 * quantum.h, no timer, no I2C, no report_mouse_t. This file is only the seam between
 * that and QMK — read a sample, hand it over with a clock, copy the answer into a
 * mouse report.
 *
 * The split is not tidiness. "Tapping does not work" was reported FOUR times for four
 * different causes — the ASIC's (0,0) touchdown coordinate taken as a position, path
 * length standing in for displacement, a z bar sitting on top of the tap
 * distribution, and per-sample scatter plus a slow poll rate — and every one cost a
 * hardware round to guess at, because the arithmetic shared a function with the
 * sensor and could not be run anywhere else. `make test:polykybd_cirque_gesture`
 * now replays taps, drags and dials built from the measured pad numbers, and is
 * mutation-checked against nine deliberate breaks.
 *
 * WHY THE STOCK GESTURES ARE NOT USED, in short: the ASIC's zones live in the
 * sensor's frame, and POINTING_DEVICE_ROTATION_90 rotates the reported x/y long
 * after the ASIC has chosen its corner, so the corner-tap right click landed at 11
 * o'clock and moved to 10 when CURVED_OVERLAY changed the edge tuning under it.
 * Neither stock scroll ever fired. And tap force is the ASIC's, with no z-threshold
 * register exposed. The long form is in split72/config.h and the FSM header.
 *
 * The stock driver still owns init: cirque_pinnacle_init() configures the ASIC, and
 * only then does pointing_device_init_kb() swap the get_report function. Nothing
 * upstream is patched.
 */
#include "cirque_gestures.h"

#ifdef POLYKYBD_CIRQUE_GESTURES

#    include "quantum.h"
#    include "pointing_device.h"
#    include "timer.h"
#    include "drivers/sensors/cirque_pinnacle.h"
#    include "base/cirque_gesture_fsm.h"

#    if !CIRQUE_PINNACLE_POSITION_MODE
#        error "poly cirque gestures need CIRQUE_PINNACLE_ABSOLUTE_MODE (do not set -e POLYKYBD_CIRQUE_RELATIVE=yes)"
#    endif

static poly_gest_t s_gest;
/* The pad view replaces the status screen while the pad is IN USE and hands it back
 * afterwards, so it costs nothing when nobody is touching the trackpad. */
#    ifndef POLY_PAD_VIEW_HOLD_MS
#        define POLY_PAD_VIEW_HOLD_MS 1200
#    endif
#    ifndef POLY_PAD_TAP_FLASH_MS
#        define POLY_PAD_TAP_FLASH_MS 250
#    endif
static uint32_t s_last_touch_ms;
static uint32_t s_last_tap_ms;
static uint16_t s_taps_seen;
static uint16_t    s_raw_x, s_raw_y, s_raw_z;
static uint16_t    s_x_lo = CIRQUE_PINNACLE_X_LOWER, s_x_hi = CIRQUE_PINNACLE_X_UPPER;
static uint16_t    s_y_lo = CIRQUE_PINNACLE_Y_LOWER, s_y_hi = CIRQUE_PINNACLE_Y_UPPER;

static report_mouse_t poly_cirque_get_report(report_mouse_t report) {
    const pinnacle_data_t d = cirque_pinnacle_read_data();

    poly_gest_sample_t s = {.valid = d.valid, .x = d.xValue, .y = d.yValue, .z = d.zValue, .t_ms = timer_read32()};
    poly_gest_out_t    o = {0};
    poly_gest_feed(&s_gest, &s, &o);

    if (s_gest.down) s_last_touch_ms = timer_read32();
    if (s_gest.taps != s_taps_seen) {
        s_taps_seen   = s_gest.taps;
        s_last_tap_ms = timer_read32();
    }

    if (d.valid) {
        s_raw_x = d.xValue;
        s_raw_y = d.yValue;
        s_raw_z = d.zValue;
        if (d.xValue || d.yValue) {
            if (d.xValue < s_x_lo) s_x_lo = d.xValue;
            if (d.xValue > s_x_hi) s_x_hi = d.xValue;
            if (d.yValue < s_y_lo) s_y_lo = d.yValue;
            if (d.yValue > s_y_hi) s_y_hi = d.yValue;
        }
    }

    /* We own every field: the master path hands back the previous report, so a
     * leftover delta or button would otherwise repeat forever. */
    report.x       = o.dx;
    report.y       = o.dy;
    report.v       = o.wheel;
    report.h       = 0;
    report.buttons = 0;
    if (o.buttons & POLY_GEST_BTN_L) report.buttons = pointing_device_handle_buttons(report.buttons, true, POINTING_DEVICE_BUTTON1);
    if (o.buttons & POLY_GEST_BTN_R) report.buttons = pointing_device_handle_buttons(report.buttons, true, POINTING_DEVICE_BUTTON2);
    return report;
}

extern const pointing_device_driver_t *pointing_device_driver;

static const pointing_device_driver_t poly_cirque_driver = {
    .init       = cirque_pinnacle_init,
    .get_report = poly_cirque_get_report,
    .set_cpi    = cirque_pinnacle_set_cpi,
    .get_cpi    = cirque_pinnacle_get_cpi,
};

void poly_cirque_install(void) {
    pointing_device_driver = &poly_cirque_driver;
}

/* Weak in QMK, and called from pointing_device_init() AFTER driver->init(), so the
 * ASIC is already configured by the time we take the report function over.
 * pointing_device_init_user() is invoked separately by the same caller, so overriding
 * this one does not swallow it. */
void pointing_device_init_kb(void) {
    poly_cirque_install();
}

void poly_cirque_debug_snapshot(poly_cirque_dbg_t *out) {
    out->raw_x   = s_raw_x;
    out->raw_y   = s_raw_y;
    out->raw_z   = s_raw_z;
    out->x_lo    = s_x_lo;
    out->x_hi    = s_x_hi;
    out->y_lo    = s_y_lo;
    out->y_hi    = s_y_hi;
    out->px      = s_gest.px;
    out->py      = s_gest.py;
    out->z_peak  = s_gest.z_peak;
    out->mode    = (poly_cirque_mode_t)s_gest.mode;
    out->buttons = s_gest.click_btn;
    out->travel  = s_gest.last_travel;
    out->last_ms = s_gest.last_ms;
    out->taps    = s_gest.taps;
    out->reject = (uint8_t)s_gest.verdict;
    out->down      = s_gest.down;
    out->active    = s_gest.down || (timer_elapsed32(s_last_touch_ms) < POLY_PAD_VIEW_HOLD_MS && s_last_touch_ms != 0);
    out->tap_flash = (s_last_tap_ms != 0) && (timer_elapsed32(s_last_tap_ms) < POLY_PAD_TAP_FLASH_MS);
    /* LIVE, from the current position rather than the last touch, so the zones can be
     * found by sliding a finger around and watching the letters — "you have to know
     * the starting point very precisely" is a discoverability problem, and a zone you
     * cannot see is one you have to be told about. */
    {
        const int32_t  rx  = (int32_t)s_gest.px - POLY_GEST_CENTRE;
        const int32_t  ry  = (int32_t)s_gest.py - POLY_GEST_CENTRE;
        const uint16_t cl  = (uint16_t)(((uint32_t)POLY_GEST_SPAN * (100 - POLY_GEST_CORNER_PCT)) / 100);
        out->in_corner     = (s_gest.px >= cl && s_gest.py <= (POLY_GEST_SPAN - cl));
        out->in_ring       = ((uint32_t)(rx * rx + ry * ry) >= ((uint32_t)POLY_GEST_RING_R * POLY_GEST_RING_R));
    }
}

#endif /* POLYKYBD_CIRQUE_GESTURES */
