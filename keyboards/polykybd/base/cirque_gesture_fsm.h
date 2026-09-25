/* PolyKybd Cirque gesture decision — PURE.
 *
 * No quantum.h, no timer, no I2C, no report_mouse_t. Samples and a millisecond clock
 * go in, cursor deltas / wheel / buttons come out. That is the whole point: "tapping
 * does not work" was reported three times for three different causes, and each guess
 * cost a hardware round because the arithmetic shared a function with the sensor.
 * Same seam as fw_up_verdict.c and macro_decode.c, for the same reason.
 *
 * Geometry and thresholds are #ifndef so a test can override them.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

/* ---- geometry: sensor frame -> pad frame ------------------------------------
 * Least-squares affine fit to the four physical corners measured on the real pad
 * (2026-09-15): right-top (1380,1340), right-bottom (280,1080), left-bottom
 * (650,190), left-top (1660,400). Q12 fixed point. The pad is skewed 13-20 degrees
 * as well as rotated, deliberately, to meet a hand bent inwards, so a plain axis
 * swap put the physical bottom-right 5% inside its own zone and right click was
 * unreachable. Re-fit rather than nudge if the pad is remounted. */
#ifndef POLY_GEST_Q
#    define POLY_GEST_Q 4096
#endif
#ifndef POLY_GEST_SPAN
#    define POLY_GEST_SPAN 896
#endif
#define POLY_GEST_CENTRE (POLY_GEST_SPAN / 2)
#ifndef POLY_GEST_PX_A
#    define POLY_GEST_PX_A (-830)
#    define POLY_GEST_PX_B 3715
#    define POLY_GEST_PX_C (-136858)
#    define POLY_GEST_PY_A (-3217)
#    define POLY_GEST_PY_B (-1140)
#    define POLY_GEST_PY_C 5885600
#endif

/* Raw window, for the MOTION frame only — a plain 90-degree rotation with the skew
 * left IN, because sliding the finger along the pad's angle is the natural stroke and
 * should send the cursor straight up. Zones want the pad's true rectangle, motion
 * wants the hand's direction; they are different questions. */
#ifndef POLY_GEST_XLO
#    define POLY_GEST_XLO 280
#    define POLY_GEST_XHI 1660
#    define POLY_GEST_YLO 190
#    define POLY_GEST_YHI 1340
#endif

/* ---- thresholds -------------------------------------------------------------
 * Measured z at 4X gain: incidental contact and hover 0, sustained light touch ~30,
 * normal 38-42, hard 45. A TAP is an impact and peaks higher than a sustained press:
 * ~44-47 on a light tap, "or even a bit lower". */
#ifndef POLY_GEST_Z_TOUCH
#    define POLY_GEST_Z_TOUCH 16 /* start tracking */
#endif
#ifndef POLY_GEST_Z_RELEASE
#    define POLY_GEST_Z_RELEASE 10 /* ...and end it (hysteresis, so resting cannot chatter) */
#endif
#ifndef POLY_GEST_Z_TAP
#    define POLY_GEST_Z_TAP 24 /* a click needs a real press; incidental contact is 0 */
#endif
/* 400, not 250. The pad is on the SLAVE half, where the driver is polled from a split
 * transaction rather than the 1 ms pointing task, so the sample rate seen here is
 * whatever that loop runs at. Swept off-hardware: at 50 ms per sample a normal tap
 * already measures 200 ms, and at 100 ms it measures 400 and was rejected as a held
 * touch. The term has to cover the sampling, not just the finger. */
#ifndef POLY_GEST_TAP_MS
#    define POLY_GEST_TAP_MS 500
#endif
/* 150, on a SMOOTHED position, and both halves of that matter. Swept off-hardware,
 * the raw-position version rejected a motionless tap at +-40 raw units of scatter —
 * and scatter of that order is exactly what "the values fluctuate a lot depending on
 * how the finger lands" describes. A two-sample average halves it while leaving a
 * real drag, which moves hundreds of units, entirely intact. */
#ifndef POLY_GEST_TAP_SLOP
#    define POLY_GEST_TAP_SLOP 150
#endif
#ifndef POLY_GEST_CLICK_MS
#    define POLY_GEST_CLICK_MS 50 /* the slave path zeroes the report each call */
#endif
/* The right-click corner is TOP-right, not bottom-right.
 *
 * Bottom-right is the near edge, hard against the key wells, so reaching it means
 * curling the thumb back into the keys. Top-right — around 1 to 2 o'clock — is the
 * far edge, where the thumb already travels. A pad zone has to sit where the hand
 * can go, and that is not something the geometry can tell you.
 *
 * 26, brought back IN from 22 after the corner proved a little hard to reach. Unlike
 * the dial ring, this one has no geometry ceiling to run into: it is a square region
 * in the corner, so raising the percentage simply extends it toward the centre. The
 * inner point sits at radius 304 of the 634 a corner reaches. */
#ifndef POLY_GEST_CORNER_PCT
#    define POLY_GEST_CORNER_PCT 26
#endif
/* How far out a dial must START, of 448 on-axis (633 into a corner).
 *
 * 405 of 448 on-axis, and that is close to the hard limit rather than a free choice.
 * The pad reaches 448 pad units on the axes and 633 diagonally, so the annulus a
 * touchdown can land in is 43 units wide on an axis (10% of the half-width) and 228
 * wide toward a corner. Pushing past ~440 makes the axes unreachable outright and
 * the dial becomes a corners-only gesture. 220 was far too generous: a finger that
 * lands anywhere but the middle starts a dial, so the pad scrolls when it should
 * point. 220 was chosen while the radius test below was broken and aborting dials
 * after 23-29 units of wobble, which made a big ring look necessary. With that fixed
 * the ring can be where it belongs, and "only at the outer area" is the requirement.
 *
 * This is the one number to move if dialling is hard to start OR starts by accident;
 * nothing else needs to change with it. */
#ifndef POLY_GEST_RING_R
#    define POLY_GEST_RING_R 405
#endif

/* A DEDICATED dial-start wedge at the top left, 10-11 o'clock, mirroring the
 * right-click sector across the vertical axis.
 *
 * The ring alone is a bad shape for starting a gesture: the pad reaches 448 pad units
 * on the axes, so pushing the ring outward makes the band thinner everywhere until
 * the axes are unreachable. A wedge does not have that problem. It reaches INWARD
 * from POLY_GEST_DIAL_WEDGE_R, so it stays a generous target however far out the ring
 * goes, and it puts the start where a thumb already rests -- the field description of
 * where dialling first worked was "11 o'clock".
 *
 * Angles are integer slope ratios, the same 29..61 degrees the right-click sector
 * uses, mirrored: tan(29) and tan(61) are 554 and 1804 per 1000.
 *
 * ⚠️ The inner radius is the ACCIDENTAL-SCROLL knob, and the wedge -- not the ring --
 * is what fires by accident. The ring sits at 405 of the 448 the pad reaches on an
 * axis, so starting a dial there means touching the rim deliberately. The wedge runs
 * up the 45 degree diagonal, where the pad reaches 633, so its inner radius is a
 * fraction of a much longer run: at 200 it armed from 32% of the way out, i.e. it
 * owned the outer TWO THIRDS of that diagonal and a finger resting at the top left
 * scrolled instead of pointing (field report, 2026-09-16). 320 is 51% of 633, so the
 * wedge is now the outer half of its diagonal and still 313 units deep -- a target
 * far bigger than the 43-unit annulus the ring gets on an axis.
 *
 * Raise it further if it still trips; the tests derive their sample radii from this
 * constant rather than hardcoding, so moving it does not silently invalidate them. */
#ifndef POLY_GEST_DIAL_WEDGE_R
#    define POLY_GEST_DIAL_WEDGE_R 320
#endif

#ifndef POLY_GEST_SCROLL_COMMIT
#    define POLY_GEST_SCROLL_COMMIT 1024 /* ~5.6 deg of rotation */
#endif
#ifndef POLY_GEST_SCROLL_STEP
#    define POLY_GEST_SCROLL_STEP 2731 /* ~15 deg per wheel click, 24 per turn */
#endif
/* Radius change, in REAL pad units, that says drag rather than dial.
 *
 * INWARD it is measured from r_floor, the smaller of the touchdown radius and the
 * radius that armed the dial (POLY_GEST_RING_R, or POLY_GEST_DIAL_WEDGE_R for a wedge
 * start). It used to be measured from the touchdown radius alone, so a dial started
 * far outside the ring had a smaller margin than one started on it. A thumb that
 * lands in a corner at radius ~600 and slides in to ~430 before it turns moved 170
 * units inward and was aborted into a drag, while the same stroke started at 440
 * scrolled. Field report 2026-09-25: "scrolling does not trigger when starting too
 * far outside". With the floor, how far outside the ring a dial starts does not
 * matter. OUTWARD it is still measured from the touchdown radius.
 *
 * This was compared in a scaled SQUARED domain (rsq / (RING_R/2) against 120) on the
 * theory that it avoided a sqrt. It does, and it also made the threshold mean
 * something else entirely: d(r^2)/dr is 2r, about 660 at the ring, so dividing by 150
 * gives ~4.4 units of "score" per unit of radius and the 120 tripped after 23-29
 * units of real wobble. A dial wobbles more than that, so nearly every dial aborted
 * into a drag before it could turn 5.6 degrees. Measured off-hardware, not guessed.
 *
 * It is a real radius now, via integer sqrt, so the number means what it says. */
#ifndef POLY_GEST_SCROLL_DR
#    define POLY_GEST_SCROLL_DR 120
#endif

/* Per-sample cursor delta above which a sample is treated as a glitch rather than
 * movement. A fast swipe crosses the pad in ~100 ms, i.e. ~90 units per 10 ms
 * sample, so this rejects only the impossible. */
#ifndef POLY_GEST_JUMP_MAX
#    define POLY_GEST_JUMP_MAX 250
#endif

/* Cursor speed, as a percentage of raw pad travel.
 *
 * There was no speed knob at all. pointing_device_set_cpi(650) in poly_keymap.c has
 * been inert for the cursor since this layer took over get_report: CPI only feeds
 * cirque_pinnacle_scale_data(), which the stock get_report calls and this one does
 * not. Motion is normalised to POLY_GEST_SPAN across the pad instead, so the speed
 * was simply whatever that came to — reported from hardware as "a bit fast".
 *
 * The scaling carries a RESIDUAL (see poly_gest_t.res_x). Dividing each delta on its
 * own would floor every 1-unit move to 0 and a slow, careful drag would stop moving
 * the cursor entirely; keeping the remainder means slow movement is delayed, never
 * lost. */
#ifndef POLY_GEST_SPEED_PCT
#    define POLY_GEST_SPEED_PCT 65
#endif

/* One-pole low pass on the filtered position, in sixteenths.
 *
 * The median kills lone outliers; this takes the hand tremor that survives it, which
 * is what "shakey" is. 8/16 is a mild filter — half the correction per sample — so it
 * costs about one sample of lag and does not smear a real stroke. Raise it towards 16
 * for less smoothing, lower for more. */
#ifndef POLY_GEST_SMOOTH_Q4
#    define POLY_GEST_SMOOTH_Q4 8
#endif

/* Pointer acceleration: above POLY_GEST_ACCEL_KNEE motion units in one sample, the
 * factor grows with the SQUARE of the excess, (mag - knee)^2 / POLY_GEST_ACCEL_DIV
 * percent on top of POLY_GEST_SPEED_PCT, capped at ACCEL_MAX_PCT.
 *
 * The knee keeps acceleration away from the filters: tremor and slow tracking sit
 * below it and are scaled by POLY_GEST_SPEED_PCT alone. The residual carries through
 * the varying factor unchanged, so a stroke still loses nothing to truncation.
 *
 * Quadratic, not linear. The linear ramp (knee 8, +3 % per unit) reached the 130 %
 * cap at 30 units per sample, so a quick stroke already ran at double the base
 * speed. Field report 2026-09-25: "acceleration starts too fast, a small move makes
 * the cursor jump". The quadratic ramp is flat near the knee and steep near the cap:
 *
 *     motion units/sample   13    20    26    33    43+
 *     linear  (old)         80   101   119   130   130  %
 *     quadratic (DIV 20)    67    74    85   101   130  %
 *
 * Measured over the same 1000-raw-unit stroke (test sample spacing, 10 ms): 13 units
 * per sample moved the cursor 416 against 494 before, 20 units 441 against 593. The
 * top speed barely moved: at 42 units per sample it is 676 against 708.
 *
 * ⚠️ The cap is not independent of the curve. It has been lowered twice because a
 * mutation run could not detect removing it: the 127-per-report HID clamp in
 * clamp_xy was already bounding the fast end. The quadratic curve reaches 130 only at
 * 43 units per sample, so AccelerationIsCapped crosses the pad faster than that.
 * Re-measure that test whenever the knee, the divisor or the cap moves. */
#ifndef POLY_GEST_ACCEL_KNEE
#    define POLY_GEST_ACCEL_KNEE 6
#endif
#ifndef POLY_GEST_ACCEL_DIV
#    define POLY_GEST_ACCEL_DIV 20
#endif
#ifndef POLY_GEST_ACCEL_MAX_PCT
#    define POLY_GEST_ACCEL_MAX_PCT 130
#endif

#define POLY_GEST_BTN_L 0x01
#define POLY_GEST_BTN_R 0x02

typedef enum { POLY_GEST_IDLE = 0, POLY_GEST_MOVE, POLY_GEST_PENDING, POLY_GEST_SCROLL } poly_gest_mode_t;
typedef enum { POLY_GEST_OK = 0, POLY_GEST_REJ_LONG, POLY_GEST_REJ_FAR, POLY_GEST_REJ_LIGHT } poly_gest_verdict_t;

typedef struct {
    bool     valid;
    uint16_t x, y, z;
    uint32_t t_ms;
} poly_gest_sample_t;

typedef struct {
    int16_t dx, dy;
    int8_t  wheel;
    uint8_t buttons;
} poly_gest_out_t;

typedef struct {
    bool     down;
    uint32_t down_ms;
    uint16_t px, py, px0, py0, px_prev, py_prev;
    uint16_t mhist_x[3], mhist_y[3];
    int32_t  fx_q4, fy_q4; /* low-passed position, sixteenths */
    int32_t  res_x, res_y; /* speed-scaling remainder         */
    bool     z_dipped;     /* z fell into the release band; re-anchor on recovery */
    uint8_t  mhist_n;
    uint32_t r0;       /* radius at touchdown, real units */
    uint32_t r_floor;  /* min(r0, arming radius): an inward drag is measured from here */
    uint16_t mx, my;
    uint16_t travel;   /* max displacement while z was solid */
    uint8_t  z_peak;   /* peak z within this touch */
    bool     corner;
    uint32_t r0sq;
    uint16_t ang;
    int32_t  ang_acc, ang_total;

    poly_gest_mode_t    mode;
    poly_gest_verdict_t verdict;
    uint16_t            last_travel, last_ms, taps;

    uint8_t  click_btn;
    uint32_t click_ms;
} poly_gest_t;

void poly_gest_reset(poly_gest_t *g);
void poly_gest_feed(poly_gest_t *g, const poly_gest_sample_t *s, poly_gest_out_t *out);

/* Exposed for the tests and the on-keyboard readout.
 *
 * The two helpers are exposed for ONE reason: a mutation run showed that breaking
 * either of them — a median that returns the maximum, an integer sqrt off by a
 * factor — changed no observable gesture outcome the suite asserted. Both are
 * arithmetic with a right answer, so testing them through a gesture was indirect
 * enough to be no test at all. */
void     poly_gest_to_pad(uint16_t sx, uint16_t sy, uint16_t *px, uint16_t *py);
uint32_t poly_gest_isqrt(uint32_t v);
uint16_t poly_gest_median3(uint16_t a, uint16_t b, uint16_t c);
