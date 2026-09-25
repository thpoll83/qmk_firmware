/* Cirque gesture decision tests.
 *
 * Every sequence here is built from numbers MEASURED on the real pad, not invented:
 * the four physical corners, and the z profile at 4X gain (hover and incidental
 * contact 0, sustained light ~30, normal 38-42, a light TAP peaking 44-47).
 *
 * This suite exists because "tapping does not work" was reported three times for
 * three different causes — a touchdown magic coordinate, path length standing in for
 * displacement, and a z bar sitting on top of the tap distribution — and each guess
 * cost a hardware round. A tap is a few dozen samples; there is no reason to need a
 * keyboard to find out whether one is accepted.
 */
#include "gtest/gtest.h"
extern "C" {
#include "cirque_gesture_fsm.h"
}

#include <cmath>
#include <cstdlib>
#include <vector>

namespace {

/* The measured corners, sensor frame. */
constexpr uint16_t RT_X = 1380, RT_Y = 1340;
constexpr uint16_t RB_X = 280, RB_Y = 1080;
constexpr uint16_t LB_X = 650, LB_Y = 190;
constexpr uint16_t LT_X = 1660, LT_Y = 400;
/* Roughly the middle of the pad. */
constexpr uint16_t MID_X = (RT_X + RB_X + LB_X + LT_X) / 4;
constexpr uint16_t MID_Y = (RT_Y + RB_Y + LB_Y + LT_Y) / 4;

struct Runner {
    poly_gest_t     g{};
    uint32_t        t = 1000;
    uint8_t         buttons_seen = 0;
    int             wheel_total = 0;
    int             wheel_pos = 0, wheel_neg = 0;
    int             dx_total = 0, dy_total = 0;
    int             step_max = 0; /* worst single-sample cursor move */
    int             samples = 0, first_move = -1;

    Runner() { poly_gest_reset(&g); }

    void feed(uint16_t x, uint16_t y, uint16_t z, uint32_t dt = 10) {
        poly_gest_sample_t s{true, x, y, z, t};
        poly_gest_out_t    o{};
        poly_gest_feed(&g, &s, &o);
        buttons_seen |= o.buttons;
        wheel_total += o.wheel;
        if (o.wheel > 0) wheel_pos += o.wheel;
        if (o.wheel < 0) wheel_neg += -o.wheel;
        dx_total += o.dx;
        dy_total += o.dy;
        const int step = std::abs((int)o.dx) + std::abs((int)o.dy);
        if (step > step_max) step_max = step;
        if (step > 0 && first_move < 0) first_move = samples;
        samples++;
        t += dt;
    }
    /* Idle samples so the click hold can expire / lift-off can be observed. */
    void idle(int n = 10) {
        for (int i = 0; i < n; i++) feed(0, 0, 0);
    }
};

/* A tap as the sensor really reports one: z ramps up, peaks, decays, and the
 * position jitters a few units because a fingertip is not a point. */
void tap_at(Runner &r, uint16_t x, uint16_t y, uint16_t peak, uint32_t dt = 10) {
    const int jitter[] = {0, 2, -3, 1, -2, 3, -1};
    const uint16_t zs[] = {(uint16_t)(peak / 3), (uint16_t)(peak * 2 / 3), peak, (uint16_t)(peak * 3 / 4), (uint16_t)(peak / 3), 4};
    int i = 0;
    for (uint16_t z : zs) {
        r.feed((uint16_t)(x + jitter[i % 7]), (uint16_t)(y + jitter[(i + 3) % 7]), z, dt);
        i++;
    }
}

TEST(CirqueGesture, PadCornersMapToPadCorners) {
    uint16_t px, py;
    poly_gest_to_pad(RB_X, RB_Y, &px, &py);
    EXPECT_GT(px, POLY_GEST_SPAN * 3 / 4) << "physical right-bottom must be far right";
    EXPECT_GT(py, POLY_GEST_SPAN * 3 / 4) << "...and far down";
    poly_gest_to_pad(LT_X, LT_Y, &px, &py);
    EXPECT_LT(px, POLY_GEST_SPAN / 4);
    EXPECT_LT(py, POLY_GEST_SPAN / 4);
}

TEST(CirqueGesture, LightTapClicks) {
    Runner r;
    tap_at(r, MID_X, MID_Y, 44); /* the LOW end of a measured light tap */
    r.idle();
    EXPECT_EQ(r.g.verdict, POLY_GEST_OK) << "verdict " << r.g.verdict << " travel " << r.g.last_travel << " ms " << r.g.last_ms << " zpk " << (int)r.g.z_peak;
    EXPECT_EQ(r.g.taps, 1u);
    EXPECT_EQ(r.buttons_seen, POLY_GEST_BTN_L);
}

TEST(CirqueGesture, TapBelowTheMeasuredFloorStillClicks) {
    Runner r;
    tap_at(r, MID_X, MID_Y, 36); /* "44 or even a bit lower" — with margin */
    r.idle();
    EXPECT_EQ(r.g.verdict, POLY_GEST_OK);
    EXPECT_EQ(r.buttons_seen, POLY_GEST_BTN_L);
}

TEST(CirqueGesture, IncidentalContactDoesNotClick) {
    Runner r;
    /* Hover and a brush both read 0 at 4X; nothing should ever latch. */
    for (int i = 0; i < 20; i++) r.feed(MID_X, MID_Y, 0);
    EXPECT_EQ(r.g.taps, 0u);
    EXPECT_EQ(r.buttons_seen, 0);
}

TEST(CirqueGesture, CornerTapIsRightClick) {
    /* TOP-right. Bottom-right is hard against the key wells and needs the thumb
     * curled back into them; the far corner is where it already travels. */
    Runner r;
    tap_at(r, RT_X, RT_Y, 45);
    r.idle();
    EXPECT_EQ(r.g.verdict, POLY_GEST_OK);
    EXPECT_EQ(r.buttons_seen, POLY_GEST_BTN_R) << "physical TOP-right must be the right-click corner";
}

TEST(CirqueGesture, TheNearCornerIsAPlainLeftClick) {
    /* The corner that moved must not still be right-clicking. */
    Runner r;
    tap_at(r, RB_X, RB_Y, 45);
    r.idle();
    EXPECT_EQ(r.g.verdict, POLY_GEST_OK);
    EXPECT_EQ(r.buttons_seen, POLY_GEST_BTN_L) << "bottom-right is no longer the right-click corner";
}

TEST(CirqueGesture, TheMiddleOfThePadDoesNotArmADial) {
    /* "we start scrolling everywhere when the finger lands not exactly in the middle"
     * — the ring must leave the pointing area alone.
     *
     * The top-left WEDGE is skipped here, not forgotten: it reaches inward on purpose
     * and TheWedgeArmsADialWellInsideTheRing asserts that it does. A test that swept
     * every angle would have failed on the feature. */
    const double A = POLY_GEST_PX_A / (double)POLY_GEST_Q, B = POLY_GEST_PX_B / (double)POLY_GEST_Q, C = POLY_GEST_PX_C / (double)POLY_GEST_Q;
    const double D = POLY_GEST_PY_A / (double)POLY_GEST_Q, E = POLY_GEST_PY_B / (double)POLY_GEST_Q, F = POLY_GEST_PY_C / (double)POLY_GEST_Q;
    const double det = A * E - B * D;
    for (int deg = 0; deg < 360; deg += 45) {
        if (deg == 225) continue; /* the dial wedge, tested below */
        for (double rad : {0.0, 80.0, 160.0, 240.0}) {
            Runner       r;
            const double th = deg * M_PI / 180.0;
            /* a short curved stroke, which WOULD dial if the ring reached here */
            for (int i = 0; i <= 30; i++) {
                const double a  = th + i * 3.0 * M_PI / 180.0;
                const double px = POLY_GEST_CENTRE + rad * std::cos(a), py = POLY_GEST_CENTRE + rad * std::sin(a);
                r.feed((uint16_t)std::lround((E * (px - C) - B * (py - F)) / det), (uint16_t)std::lround((A * (py - F) - D * (px - C)) / det), 40);
            }
            EXPECT_EQ(r.wheel_total, 0) << "a stroke at radius " << rad << ", " << deg << " deg scrolled";
        }
    }
}

TEST(CirqueGesture, HeldTouchIsNotATap) {
    Runner r;
    /* Comfortably past POLY_GEST_TAP_MS, which had to widen to 500 to cover a slow
     * poll rate — see TapSurvivesASlowPollRate. */
    for (int i = 0; i < 90; i++) r.feed(MID_X, MID_Y, 40); /* 900 ms */
    r.idle();
    EXPECT_EQ(r.g.verdict, POLY_GEST_REJ_LONG);
    EXPECT_EQ(r.buttons_seen, 0);
}

TEST(CirqueGesture, DragIsNotATap) {
    Runner r;
    for (int i = 0; i < 12; i++) r.feed((uint16_t)(MID_X + i * 30), MID_Y, 40);
    r.idle();
    EXPECT_EQ(r.g.verdict, POLY_GEST_REJ_FAR);
    EXPECT_EQ(r.buttons_seen, 0);
    EXPECT_NE(r.dx_total + r.dy_total, 0) << "a drag must still move the cursor";
}

/* Walk the rim of the pad in pad coordinates, then invert the fit to get sensor
 * coordinates, so the dial is a real circle on the real pad. */
void dial(Runner &r, double from_deg, double to_deg, double radius = 432.0, uint16_t z = 40) {
    const double A = POLY_GEST_PX_A / (double)POLY_GEST_Q, B = POLY_GEST_PX_B / (double)POLY_GEST_Q, C = POLY_GEST_PX_C / (double)POLY_GEST_Q;
    const double D = POLY_GEST_PY_A / (double)POLY_GEST_Q, E = POLY_GEST_PY_B / (double)POLY_GEST_Q, F = POLY_GEST_PY_C / (double)POLY_GEST_Q;
    const double det = A * E - B * D;
    const int    steps = (int)(std::fabs(to_deg - from_deg) / 3.0) + 2;
    for (int i = 0; i <= steps; i++) {
        const double th = (from_deg + (to_deg - from_deg) * i / steps) * M_PI / 180.0;
        const double px = POLY_GEST_CENTRE + radius * std::cos(th);
        const double py = POLY_GEST_CENTRE + radius * std::sin(th);
        /* invert the 2x2 */
        const double sx = (E * (px - C) - B * (py - F)) / det;
        const double sy = (A * (py - F) - D * (px - C)) / det;
        r.feed((uint16_t)std::lround(sx), (uint16_t)std::lround(sy), z);
    }
}

TEST(CirqueGesture, DialingOneWayScrollsOneWayContinuously) {
    Runner r;
    dial(r, 0, 360); /* a full turn */
    EXPECT_GT(r.wheel_pos + r.wheel_neg, 10) << "a full turn must produce a useful number of clicks";
    EXPECT_TRUE(r.wheel_pos == 0 || r.wheel_neg == 0)
        << "turning in ONE direction reversed: +" << r.wheel_pos << " -" << r.wheel_neg;
}

TEST(CirqueGesture, DialingBackScrollsBack) {
    Runner a, b;
    dial(a, 0, 180);
    dial(b, 180, 0);
    EXPECT_NE(a.wheel_total, 0);
    EXPECT_NE(b.wheel_total, 0);
    EXPECT_NE((a.wheel_total > 0), (b.wheel_total > 0)) << "reversing the dial must reverse the wheel";
}

TEST(CirqueGesture, DialCrossingTheSeamDoesNotJump) {
    Runner r;
    dial(r, -30, 30); /* straight through 0 degrees */
    EXPECT_TRUE(r.wheel_pos == 0 || r.wheel_neg == 0) << "the 0/360 seam reversed the wheel";
}

TEST(CirqueGesture, RadialDragInTheRingMovesRatherThanScrolls) {
    Runner r;
    /* Straight in from the rim towards the centre: a drag, not a dial. */
    for (int i = 0; i < 14; i++) {
        const double f = 1.0 - i * 0.05;
        uint16_t     sx, sy;
        (void)sx;
        (void)sy;
        const double px = POLY_GEST_CENTRE + 440 * f, py = POLY_GEST_CENTRE;
        const double A = POLY_GEST_PX_A / (double)POLY_GEST_Q, B = POLY_GEST_PX_B / (double)POLY_GEST_Q, C = POLY_GEST_PX_C / (double)POLY_GEST_Q;
        const double D = POLY_GEST_PY_A / (double)POLY_GEST_Q, E = POLY_GEST_PY_B / (double)POLY_GEST_Q, F = POLY_GEST_PY_C / (double)POLY_GEST_Q;
        const double det = A * E - B * D;
        r.feed((uint16_t)std::lround((E * (px - C) - B * (py - F)) / det), (uint16_t)std::lround((A * (py - F) - D * (px - C)) / det), 40);
    }
    EXPECT_EQ(r.wheel_total, 0) << "a straight radial drag scrolled";
    EXPECT_NE(r.dx_total + r.dy_total, 0);
}

TEST(CirqueGesture, TouchdownMagicCoordinateIsNotAPosition) {
    Runner r;
    /* The ASIC announces a landing with (0,0) while z is already up. */
    r.feed(0, 0, 20);
    tap_at(r, MID_X, MID_Y, 45);
    r.idle();
    EXPECT_EQ(r.g.verdict, POLY_GEST_OK) << "travel " << r.g.last_travel;
    EXPECT_EQ(r.buttons_seen, POLY_GEST_BTN_L);
}

TEST(CirqueGesture, ClickIsHeldLongEnoughToBeSeen) {
    Runner r;
    tap_at(r, MID_X, MID_Y, 45);
    int held = 0;
    for (int i = 0; i < 12; i++) {
        poly_gest_sample_t s{true, 0, 0, 0, r.t};
        poly_gest_out_t    o{};
        poly_gest_feed(&r.g, &s, &o);
        if (o.buttons) held++;
        r.t += 10;
    }
    EXPECT_GE(held, 3) << "a one-sample button does not survive the split's slave path";
}

/* ---- regressions for the two things the off-hardware sweep found ------------
 * Both were invisible to the first suite because it modelled a tidier sensor than
 * the real one: a few units of scatter and a sample every 10 ms. */

TEST(CirqueGesture, TapSurvivesHeavyPositionScatter) {
    /* "the values fluctuate a lot depending on how the finger lands" — swept, the
     * unsmoothed version rejected a motionless tap from +-40 raw units. */
    for (int amp : {10, 20, 40, 60, 80, 120, 160}) {
        Runner   r;
        uint32_t t = 1000;
        for (int i = 0; i < 6; i++) {
            const uint16_t zs[] = {15, 30, 45, 34, 15, 4};
            r.feed((uint16_t)(MID_X + (int)(amp * std::sin(i * 2.7))), (uint16_t)(MID_Y + (int)(amp * std::cos(i * 1.9))), zs[i]);
        }
        (void)t;
        r.idle();
        EXPECT_EQ(r.g.verdict, POLY_GEST_OK) << "scatter +-" << amp << " raw units rejected the tap, travel " << r.g.last_travel;
        EXPECT_EQ(r.buttons_seen, POLY_GEST_BTN_L) << "scatter +-" << amp;
    }
}

TEST(CirqueGesture, TapSurvivesASlowPollRate) {
    /* The pad sits on the SLAVE half, polled from a split transaction rather than the
     * 1 ms pointing task, so the rate seen here is whatever that loop runs at. */
    for (int dt : {10, 20, 33, 50, 100}) {
        Runner r;
        const uint16_t zs[] = {15, 30, 45, 34, 15, 4};
        for (int i = 0; i < 6; i++) r.feed(MID_X, MID_Y, zs[i], dt);
        r.idle(6);
        EXPECT_EQ(r.g.verdict, POLY_GEST_OK) << dt << " ms/sample rejected the tap, measured " << r.g.last_ms << " ms";
        EXPECT_EQ(r.buttons_seen, POLY_GEST_BTN_L) << dt << " ms/sample";
    }
}

TEST(CirqueGesture, ARealDragIsStillNotATapAtTheWiderSlop) {
    Runner r;
    for (int i = 0; i < 14; i++) r.feed((uint16_t)(MID_X + i * 40), MID_Y, 40);
    r.idle();
    EXPECT_EQ(r.g.verdict, POLY_GEST_REJ_FAR) << "travel " << r.g.last_travel;
    EXPECT_EQ(r.buttons_seen, 0);
}

TEST(CirqueGesture, ATouchTooLightToMeanItIsRejectedByTheZBar) {
    /* Between POLY_GEST_Z_TOUCH and POLY_GEST_Z_TAP: solid enough to track the
     * cursor, not solid enough to be a click. Nothing else in this suite reaches
     * that band — the incidental-contact case is thrown out by the touch threshold
     * before the tap threshold is ever consulted, so it proved nothing about it. */
    Runner r;
    const uint16_t zs[] = {17, 20, 21, 20, 17, 4};
    for (int i = 0; i < 6; i++) r.feed(MID_X, MID_Y, zs[i]);
    r.idle();
    EXPECT_EQ(r.g.verdict, POLY_GEST_REJ_LIGHT) << "zpk " << (int)r.g.z_peak;
    EXPECT_EQ(r.buttons_seen, 0);
    EXPECT_EQ(r.g.taps, 0u);
}

/* ---- regressions for the dial and the cursor -------------------------------- */

TEST(CirqueGesture, DialSurvivesRadialWobble) {
    /* A real dial is not a compass-drawn circle. The squared-domain radius test
     * aborted after 23-29 units of wobble while claiming 120, so nearly every dial
     * became a drag and only a path that happened to hold its radius scrolled —
     * which is what "you have to know the starting point very precisely" was. */
    for (int wobble : {0, 20, 40, 60, 90}) {
        Runner r;
        const double A = POLY_GEST_PX_A / (double)POLY_GEST_Q, B = POLY_GEST_PX_B / (double)POLY_GEST_Q, C = POLY_GEST_PX_C / (double)POLY_GEST_Q;
        const double D = POLY_GEST_PY_A / (double)POLY_GEST_Q, E = POLY_GEST_PY_B / (double)POLY_GEST_Q, F = POLY_GEST_PY_C / (double)POLY_GEST_Q;
        const double det = A * E - B * D;
        for (int i = 0; i <= 90; i++) {
            const double th  = i * 4.0 * M_PI / 180.0;
            /* Comfortably inside the ring, so this tests the WOBBLE and not the
             * boundary — starting exactly on POLY_GEST_RING_R meant a round trip
             * through integer sensor coords could land a unit short of it. */
            const double rad = 432 + wobble * std::sin(i * 0.7);
            const double px = POLY_GEST_CENTRE + rad * std::cos(th), py = POLY_GEST_CENTRE + rad * std::sin(th);
            r.feed((uint16_t)std::lround((E * (px - C) - B * (py - F)) / det), (uint16_t)std::lround((A * (py - F) - D * (px - C)) / det), 40);
        }
        EXPECT_GT(r.wheel_pos + r.wheel_neg, 5) << "radial wobble +-" << wobble << " killed the dial";
    }
}

TEST(CirqueGesture, DialArmsAllRoundTheRing) {
    /* Not just at 11 o'clock. */
    for (int deg = 0; deg < 360; deg += 30) {
        Runner       r;
        const double A = POLY_GEST_PX_A / (double)POLY_GEST_Q, B = POLY_GEST_PX_B / (double)POLY_GEST_Q, C = POLY_GEST_PX_C / (double)POLY_GEST_Q;
        const double D = POLY_GEST_PY_A / (double)POLY_GEST_Q, E = POLY_GEST_PY_B / (double)POLY_GEST_Q, F = POLY_GEST_PY_C / (double)POLY_GEST_Q;
        const double det = A * E - B * D;
        for (int i = 0; i <= 40; i++) {
            const double th = (deg + i * 3.0) * M_PI / 180.0;
            const double px = POLY_GEST_CENTRE + 432 * std::cos(th), py = POLY_GEST_CENTRE + 432 * std::sin(th);
            r.feed((uint16_t)std::lround((E * (px - C) - B * (py - F)) / det), (uint16_t)std::lround((A * (py - F) - D * (px - C)) / det), 40);
        }
        EXPECT_GT(r.wheel_pos + r.wheel_neg, 3) << "no dial starting at " << deg << " degrees";
    }
}

TEST(CirqueGesture, ScatterDoesNotJumpTheCursor) {
    /* A still finger must not move the pointer. The motion path was unsmoothed, so
     * +-40 raw units of scatter became up to 52 units of cursor movement per sample. */
    Runner r;
    for (int i = 0; i < 30; i++) {
        /* Small continuous noise with an occasional wild sample — a median rejects
         * the spike, which a two-sample average only halves. Modelling scatter as a
         * smooth 40-unit oscillation instead was wrong and made the filter look
         * worse than no filter at all. */
        int nx = (int)(6 * std::sin(i * 2.7)), ny = (int)(6 * std::cos(i * 1.9));
        if (i % 7 == 3) {
            nx += 120;
            ny -= 110;
        }
        r.feed((uint16_t)(MID_X + nx), (uint16_t)(MID_Y + ny), 40);
    }
    const int drift = std::abs(r.dx_total) + std::abs(r.dy_total);
    EXPECT_LT(drift, 60) << "a motionless finger moved the cursor " << drift << " units";
    /* The SUM cancels out whether or not the position is smoothed, which is exactly
     * how "cursor smoothing removed" survived a mutation run. A jump is a single
     * sample, so the single sample is what has to be asserted. */
    EXPECT_LT(r.step_max, 20) << "one sample jumped the cursor " << r.step_max << " units";
}

TEST(CirqueGesture, RealMovementStillGetsThrough) {
    /* 500 raw units of travel is ~325 motion units, and POLY_GEST_SPEED_PCT scales
     * that deliberately — the bound is about the filters not SWALLOWING the stroke,
     * not about the speed factor, so it sits below the scaled figure. */
    Runner r;
    for (int i = 0; i < 20; i++) r.feed((uint16_t)(MID_X + i * 25), MID_Y, 40);
    const int moved = std::abs(r.dx_total) + std::abs(r.dy_total);
    EXPECT_GT(moved, 150) << "smoothing swallowed real movement: only " << moved << " units";
}

TEST(CirqueGesture, OneImpossibleSampleIsDroppedNotReplayed) {
    /* A LONE outlier is the median's job. Asserting only that the step stays under
     * POLY_GEST_JUMP_MAX proved nothing, because the median already absorbs it — the
     * bound has to be tight enough that a leaked spike fails. */
    Runner r;
    for (int i = 0; i < 6; i++) r.feed((uint16_t)(MID_X + i * 10), MID_Y, 40);
    const int settled = r.step_max;
    r.feed(1650, 200, 40); /* a wild sample at the far corner */
    r.feed((uint16_t)(MID_X + 60), MID_Y, 40);
    EXPECT_LE(r.step_max, settled + 15) << "a lone glitch sample leaked " << r.step_max << " units into the cursor";
}

TEST(CirqueGesture, SustainedImpossibleMotionIsCappedNotReported) {
    /* Three wild samples in a row is more than a median can reject, which is what the
     * separate jump cap is for. */
    Runner r;
    for (int i = 0; i < 6; i++) r.feed(MID_X, MID_Y, 40);
    for (int i = 0; i < 3; i++) r.feed(1650, 200, 40);
    EXPECT_LE(r.step_max, POLY_GEST_JUMP_MAX) << "an impossible move was reported as movement";
}

TEST(CirqueGesture, ModerateRadialDragStillAbortsTheDial) {
    /* The existing radial-drag case sweeps 380 -> 150, far enough that a radius scaled
     * wrongly by any constant still trips the threshold — which let "isqrt returns
     * garbage" pass a mutation run. This one moves just past POLY_GEST_SCROLL_DR, so
     * the radius has to be in real units for it to abort. */
    Runner       r;
    const double A = POLY_GEST_PX_A / (double)POLY_GEST_Q, B = POLY_GEST_PX_B / (double)POLY_GEST_Q, C = POLY_GEST_PX_C / (double)POLY_GEST_Q;
    const double D = POLY_GEST_PY_A / (double)POLY_GEST_Q, E = POLY_GEST_PY_B / (double)POLY_GEST_Q, F = POLY_GEST_PY_C / (double)POLY_GEST_Q;
    const double det = A * E - B * D;
    /* Ends 10 units past the inward floor. A touchdown at 440 is outside the ring, so
     * the floor is POLY_GEST_RING_R, not 440 (see POLY_GEST_SCROLL_DR). */
    const double end = POLY_GEST_RING_R - POLY_GEST_SCROLL_DR - 10;
    for (int i = 0; i <= 12; i++) {
        const double rad = 440 - i * (440 - end) / 12.0;
        const double px = POLY_GEST_CENTRE + rad, py = POLY_GEST_CENTRE;
        r.feed((uint16_t)std::lround((E * (px - C) - B * (py - F)) / det), (uint16_t)std::lround((A * (py - F) - D * (px - C)) / det), 40);
    }
    EXPECT_EQ(r.wheel_total, 0) << "a radial drag past the floor scrolled instead of moving";
}

TEST(CirqueGesture, ModeResolutionDoesNotBurstTheCursor) {
    /* A touch that starts in the ring and turns out to be a drag must not freeze the
     * cursor and then release the held movement in one step. Holding it was a jump by
     * construction, and lowering the ring radius made it the COMMON case rather than
     * a corner one — most touches now begin inside the ring. */
    Runner       r;
    const double A = POLY_GEST_PX_A / (double)POLY_GEST_Q, B = POLY_GEST_PX_B / (double)POLY_GEST_Q, C = POLY_GEST_PX_C / (double)POLY_GEST_Q;
    const double D = POLY_GEST_PY_A / (double)POLY_GEST_Q, E = POLY_GEST_PY_B / (double)POLY_GEST_Q, F = POLY_GEST_PY_C / (double)POLY_GEST_Q;
    const double det = A * E - B * D;
    for (int i = 0; i <= 30; i++) {
        /* Starts comfortably INSIDE the ring so PENDING is actually entered — at
         * exactly POLY_GEST_RING_R the touchdown can miss it and the test then
         * exercises plain MOVE, which is how the held-burst mutation survived.
         *
         * Moves 11 units per sample, deliberately UNDER POLY_GEST_ACCEL_KNEE, so this
         * isolates the burst from pointer acceleration — at 24 per sample the
         * acceleration legitimately produces a 40-unit step and the test was then
         * failing on a feature rather than a bug. */
        const double px = POLY_GEST_CENTRE + 440 - i * 11.0, py = POLY_GEST_CENTRE; /* straight in */
        r.feed((uint16_t)std::lround((E * (px - C) - B * (py - F)) / det), (uint16_t)std::lround((A * (py - F) - D * (px - C)) / det), 40);
    }
    EXPECT_GT(std::abs(r.dx_total) + std::abs(r.dy_total), 100) << "the drag never moved the cursor";
    EXPECT_LT(r.step_max, 40) << "one sample moved the cursor " << r.step_max << " units — a released burst";
    /* "Eventually moved" is not the property: holding the deltas and releasing them
     * later still totals the same distance, which is how the held version survived a
     * mutation run. The cursor has to move while the mode is still undecided. */
    EXPECT_GE(r.first_move, 0);
    EXPECT_LE(r.first_move, 3) << "the cursor stayed frozen for " << r.first_move << " samples before moving";
}

/* ---- the arithmetic helpers, tested for their own answers -------------------
 * Both survived a mutation run while broken, because no gesture outcome the suite
 * asserted could tell. A function with a right answer should be asked for it. */

TEST(CirqueGesture, Median3ReturnsTheMiddle) {
    EXPECT_EQ(poly_gest_median3(1, 2, 3), 2);
    EXPECT_EQ(poly_gest_median3(3, 2, 1), 2);
    EXPECT_EQ(poly_gest_median3(2, 1, 3), 2);
    EXPECT_EQ(poly_gest_median3(3, 1, 2), 2);
    EXPECT_EQ(poly_gest_median3(100, 900, 105), 105) << "a lone spike must be rejected, not returned";
    EXPECT_EQ(poly_gest_median3(500, 500, 500), 500);
    EXPECT_EQ(poly_gest_median3(0, 65535, 7), 7);
}

TEST(CirqueGesture, IsqrtIsAnActualSquareRoot) {
    for (uint32_t n : {0u, 1u, 2u, 3u, 4u, 15u, 16u, 17u, 99u, 100u, 101u, 90000u, 123456u, 1000000u}) {
        const uint32_t r = poly_gest_isqrt(n);
        EXPECT_LE(r * r, n) << "isqrt(" << n << ") = " << r << " is too big";
        EXPECT_GT((r + 1) * (r + 1), n) << "isqrt(" << n << ") = " << r << " is too small";
    }
    EXPECT_EQ(poly_gest_isqrt(90000u), 300u); /* the ring radius, squared */
}

TEST(CirqueGesture, SlowMovementIsNotFlooredAway) {
    /* Speed scaling divides each delta. Without the residual, a careful 1-unit-per-
     * sample drag would floor to zero every time and the cursor would not move at
     * all — the classic way a speed knob breaks precision work. */
    Runner r;
    for (int i = 0; i < 60; i++) r.feed((uint16_t)(MID_X + i), MID_Y, 40);
    EXPECT_GT(std::abs(r.dx_total) + std::abs(r.dy_total), 15) << "a slow drag moved the cursor only "
                                                               << std::abs(r.dx_total) + std::abs(r.dy_total) << " units";
}

TEST(CirqueGesture, CursorSpeedIsScaledButProportional) {
    /* Deliberately BELOW the acceleration knee, so this tests the base factor alone:
     * 1000 raw units delivered 5 at a time. Measured 416 with POLY_GEST_SPEED_PCT and
     * ~640 without, so the bound separates the two. A bound that does not separate its
     * cases is not a test — "under 800" passed both. */
    Runner r;
    for (int i = 0; i < 200; i++) r.feed((uint16_t)(MID_X - 500 + i * 5), MID_Y, 40);
    const int moved = std::abs(r.dx_total) + std::abs(r.dy_total);
    EXPECT_GT(moved, 280) << "the stroke barely moved the cursor";
    EXPECT_LT(moved, 520) << "the speed scaling had no effect: " << moved << " units";
}

TEST(CirqueGesture, AFastSweepTravelsFurtherThanASlowOne) {
    /* Pointer acceleration: the SAME pad distance, covered quickly, must move the
     * cursor further. Measured 417 at 5 raw units per sample and 566 at 50 with the
     * quadratic curve (749 with the old linear one). */
    Runner slow, fast;
    for (int i = 0; i < 200; i++) slow.feed((uint16_t)(MID_X - 500 + i * 5), MID_Y, 40);
    for (int i = 0; i < 20; i++) fast.feed((uint16_t)(MID_X - 500 + i * 50), MID_Y, 40);
    const int s_moved = std::abs(slow.dx_total) + std::abs(slow.dy_total);
    const int f_moved = std::abs(fast.dx_total) + std::abs(fast.dy_total);
    EXPECT_GT(f_moved, s_moved * 5 / 4) << "same distance, fast " << f_moved << " vs slow " << s_moved;
}

TEST(CirqueGesture, AccelerationLeavesSlowTrackingAlone) {
    /* The knee is the whole point: precision work must not be amplified. A crawl and
     * a gentle stroke both sit below it and must scale by the base factor only. */
    Runner crawl;
    for (int i = 0; i < 60; i++) crawl.feed((uint16_t)(MID_X + i), MID_Y, 40);
    const int moved = std::abs(crawl.dx_total) + std::abs(crawl.dy_total);
    EXPECT_GT(moved, 15) << "the crawl was floored away";
    EXPECT_LT(moved, 40) << "acceleration amplified a crawl: " << moved << " units";
}

TEST(CirqueGesture, TremorDoesNotShakeTheCursor) {
    /* A hand is never still, and its tremor is CONTINUOUS — the median rejects lone
     * outliers and does nothing about this, which is what the one-pole low pass is
     * for. Measured: worst single step 3 with the filter, 5 without, so the bound has
     * to sit between. The spike test could not tell: the median already handled it. */
    Runner r;
    for (int i = 0; i < 40; i++) {
        const int n = (int)(7 * std::sin(i * 1.9) + 5 * std::cos(i * 3.1));
        r.feed((uint16_t)(MID_X + n), (uint16_t)(MID_Y - n), 40);
    }
    EXPECT_LE(r.step_max, 3) << "tremor moved the cursor " << r.step_max << " units in one sample";
    EXPECT_LT(std::abs(r.dx_total) + std::abs(r.dy_total), 20) << "tremor drifted the cursor";
}

TEST(CirqueGesture, AccelerationIsCapped) {
    /* A fast pad crossing: 18 samples at 80 raw units each. Measured 998 with
     * POLY_GEST_ACCEL_MAX_PCT and 1257 without.
     *
     * 80, not the 65 this used before. The quadratic curve reaches the cap only at
     * ~43 motion units per sample, and 65 raw is 42, so at 65 the cap never bit
     * (947 capped against 950 uncapped) and removing it passed.
     *
     * This bound has been re-measured twice, because the cap's effect depends on the
     * SLOPE: a bound that separated the two cases at slope 5 stopped separating them
     * at slope 3 (1193 against 1237, under 4%) and the mutation walked straight
     * through it. A mutation run does not report a weak knob, it reports an untested
     * one — and the answer both times was to make the cap bite, not to widen the
     * bound. Re-measure this pair whenever POLY_GEST_ACCEL_SLOPE moves. */
    Runner r;
    for (int i = 0; i <= 17; i++) r.feed((uint16_t)(300 + i * 80), MID_Y, 40);
    const int moved = std::abs(r.dx_total) + std::abs(r.dy_total);
    EXPECT_GT(moved, 750) << "the fast crossing was not accelerated at all";
    EXPECT_LT(moved, 1100) << "the acceleration cap did nothing: " << moved << " units";
    EXPECT_LT(r.step_max, 80) << "one sample moved " << r.step_max << " units";
}

/* Sample radii DERIVED from the wedge constant, not hardcoded.
 *
 * These used to be a literal 260 (arms) and 140 (does not arm), which pinned the
 * suite to one particular POLY_GEST_DIAL_WEDGE_R. Moving the wedge outward to stop
 * accidental scrolling then broke TheWedgeArmsADialWellInsideTheRing for the right
 * reason, and -- worse -- would have left the two NEGATIVE tests below passing for
 * the WRONG one: their sample would have fallen inside the new minimum radius, so
 * they would have been asserting "too close to the centre" while claiming to assert
 * "wrong quadrant" and "wrong angle".
 *
 * WEDGE_IN is inside the wedge but still inside the ring, so a dial that arms there
 * can only have come from the wedge. The static_assert is what keeps that true if
 * either constant moves again. */
static constexpr int WEDGE_IN  = POLY_GEST_DIAL_WEDGE_R + 40;
static constexpr int WEDGE_OUT = POLY_GEST_DIAL_WEDGE_R - 60;
static_assert(WEDGE_IN < POLY_GEST_RING_R,
              "WEDGE_IN must stay inside the ring or the wedge tests prove nothing");
static_assert(WEDGE_OUT > 0, "WEDGE_OUT must be a reachable radius");

TEST(CirqueGesture, TheWedgeArmsADialWellInsideTheRing) {
    /* The point of the wedge: a dial can start at 10-11 o'clock without reaching the
     * rim. WEDGE_IN is inside the wedge and still inside POLY_GEST_RING_R. */
    const double A = POLY_GEST_PX_A / (double)POLY_GEST_Q, B = POLY_GEST_PX_B / (double)POLY_GEST_Q, C = POLY_GEST_PX_C / (double)POLY_GEST_Q;
    const double D = POLY_GEST_PY_A / (double)POLY_GEST_Q, E = POLY_GEST_PY_B / (double)POLY_GEST_Q, F = POLY_GEST_PY_C / (double)POLY_GEST_Q;
    const double det = A * E - B * D;
    for (int deg : {210, 225, 240}) {
        Runner r;
        for (int i = 0; i <= 40; i++) {
            const double th = (deg + i * 3.0) * M_PI / 180.0;
            const double px = POLY_GEST_CENTRE + WEDGE_IN * std::cos(th), py = POLY_GEST_CENTRE + WEDGE_IN * std::sin(th);
            r.feed((uint16_t)std::lround((E * (px - C) - B * (py - F)) / det), (uint16_t)std::lround((A * (py - F) - D * (px - C)) / det), 40);
        }
        EXPECT_GT(r.wheel_pos + r.wheel_neg, 3) << "no dial from the wedge at " << deg << " degrees";
    }
}

TEST(CirqueGesture, TheWedgeDoesNotReachTheCentre) {
    /* It must not swallow the pointing area it sits over. Inside
     * POLY_GEST_DIAL_WEDGE_R the same stroke has to move the cursor, not scroll. */
    const double A = POLY_GEST_PX_A / (double)POLY_GEST_Q, B = POLY_GEST_PX_B / (double)POLY_GEST_Q, C = POLY_GEST_PX_C / (double)POLY_GEST_Q;
    const double D = POLY_GEST_PY_A / (double)POLY_GEST_Q, E = POLY_GEST_PY_B / (double)POLY_GEST_Q, F = POLY_GEST_PY_C / (double)POLY_GEST_Q;
    const double det = A * E - B * D;
    Runner r;
    for (int i = 0; i <= 40; i++) {
        const double th = (225 + i * 3.0) * M_PI / 180.0;
        const double px = POLY_GEST_CENTRE + WEDGE_OUT * std::cos(th), py = POLY_GEST_CENTRE + WEDGE_OUT * std::sin(th);
        r.feed((uint16_t)std::lround((E * (px - C) - B * (py - F)) / det), (uint16_t)std::lround((A * (py - F) - D * (px - C)) / det), 40);
    }
    EXPECT_EQ(r.wheel_total, 0) << "the wedge armed a dial inside its own minimum radius";
}

TEST(CirqueGesture, TheOppositeCornerIsNotAWedge) {
    /* Mirrored the wrong way round is the obvious way to get this wrong, and it would
     * put the dial start on top of the right-click sector. */
    const double A = POLY_GEST_PX_A / (double)POLY_GEST_Q, B = POLY_GEST_PX_B / (double)POLY_GEST_Q, C = POLY_GEST_PX_C / (double)POLY_GEST_Q;
    const double D = POLY_GEST_PY_A / (double)POLY_GEST_Q, E = POLY_GEST_PY_B / (double)POLY_GEST_Q, F = POLY_GEST_PY_C / (double)POLY_GEST_Q;
    const double det = A * E - B * D;
    for (int deg : {45, 315, 135}) { /* bottom-right, top-right, bottom-left */
        Runner r;
        for (int i = 0; i <= 40; i++) {
            const double th = (deg + i * 3.0) * M_PI / 180.0;
            const double px = POLY_GEST_CENTRE + WEDGE_IN * std::cos(th), py = POLY_GEST_CENTRE + WEDGE_IN * std::sin(th);
            r.feed((uint16_t)std::lround((E * (px - C) - B * (py - F)) / det), (uint16_t)std::lround((A * (py - F) - D * (px - C)) / det), 40);
        }
        EXPECT_EQ(r.wheel_total, 0) << "a dial armed at " << deg << " degrees, inside the ring and outside the wedge";
    }
}

TEST(CirqueGesture, TheWedgeIsAWedgeNotAQuadrant) {
    /* Inside the top-left quadrant but OUTSIDE the 29..61 degree band. Dropping the
     * angle limits turns the wedge into the whole quadrant, and every other test
     * passed with that done — TheOppositeCornerIsNotAWedge samples other quadrants
     * and the middle-of-pad sweep skips this one entirely. */
    const double A = POLY_GEST_PX_A / (double)POLY_GEST_Q, B = POLY_GEST_PX_B / (double)POLY_GEST_Q, C = POLY_GEST_PX_C / (double)POLY_GEST_Q;
    const double D = POLY_GEST_PY_A / (double)POLY_GEST_Q, E = POLY_GEST_PY_B / (double)POLY_GEST_Q, F = POLY_GEST_PY_C / (double)POLY_GEST_Q;
    const double det = A * E - B * D;
    for (int deg : {195, 255}) { /* shallow and steep of the wedge, same quadrant */
        Runner r;
        for (int i = 0; i <= 40; i++) {
            const double th = (deg + i * 3.0) * M_PI / 180.0;
            const double px = POLY_GEST_CENTRE + WEDGE_IN * std::cos(th), py = POLY_GEST_CENTRE + WEDGE_IN * std::sin(th);
            r.feed((uint16_t)std::lround((E * (px - C) - B * (py - F)) / det), (uint16_t)std::lround((A * (py - F) - D * (px - C)) / det), 40);
        }
        EXPECT_EQ(r.wheel_total, 0) << "a dial armed at " << deg << " degrees — the wedge is acting as a quadrant";
    }
}

/* pad coords -> sensor coords, inverting the fit. Several tests build strokes in pad
 * space; this is the one place that undoes the transform. */
void pad_to_sensor(double px, double py, uint16_t *sx, uint16_t *sy) {
    const double A = POLY_GEST_PX_A / (double)POLY_GEST_Q, B = POLY_GEST_PX_B / (double)POLY_GEST_Q, C = POLY_GEST_PX_C / (double)POLY_GEST_Q;
    const double D = POLY_GEST_PY_A / (double)POLY_GEST_Q, E = POLY_GEST_PY_B / (double)POLY_GEST_Q, F = POLY_GEST_PY_C / (double)POLY_GEST_Q;
    const double det = A * E - B * D;
    *sx = (uint16_t)std::lround((E * (px - C) - B * (py - F)) / det);
    *sy = (uint16_t)std::lround((A * (py - F) - D * (px - C)) / det);
}

TEST(CirqueGesture, TheRightClickCornerReachesInAsFarAsItClaims) {
    /* Pins the EXTENT, not just the direction. The corner has been moved outward
     * three times and back in once on feel alone; without this, POLY_GEST_CORNER_PCT
     * could drift and only a hardware round would notice. Just inside the boundary is
     * a right click, just outside it is an ordinary left click. */
    const int lo = (POLY_GEST_SPAN * (100 - POLY_GEST_CORNER_PCT)) / 100;
    struct {
        int      px, py;
        uint8_t  want;
        const char *what;
    } cases[] = {
        {lo + 8, POLY_GEST_SPAN - lo - 8, POLY_GEST_BTN_R, "just inside the corner"},
        {lo - 20, POLY_GEST_SPAN - lo - 8, POLY_GEST_BTN_L, "just left of the corner"},
        {lo + 8, POLY_GEST_SPAN - lo + 20, POLY_GEST_BTN_L, "just below the corner"},
    };
    for (auto &c : cases) {
        Runner   r;
        uint16_t sx, sy;
        pad_to_sensor(c.px, c.py, &sx, &sy);
        tap_at(r, sx, sy, 45);
        r.idle();
        EXPECT_EQ(r.g.verdict, POLY_GEST_OK) << c.what;
        EXPECT_EQ(r.buttons_seen, c.want) << c.what << " (pad " << c.px << "," << c.py << ")";
    }
}

/* ---- 2026-09-25 field report: gentler acceleration, far-out dial starts ------ */

void feed_pad(Runner &r, double px, double py, uint16_t z = 40) {
    const double A = POLY_GEST_PX_A / (double)POLY_GEST_Q, B = POLY_GEST_PX_B / (double)POLY_GEST_Q, C = POLY_GEST_PX_C / (double)POLY_GEST_Q;
    const double D = POLY_GEST_PY_A / (double)POLY_GEST_Q, E = POLY_GEST_PY_B / (double)POLY_GEST_Q, F = POLY_GEST_PY_C / (double)POLY_GEST_Q;
    const double det = A * E - B * D;
    px               = std::min(std::max(px, 0.0), (double)POLY_GEST_SPAN);
    py               = std::min(std::max(py, 0.0), (double)POLY_GEST_SPAN);
    r.feed((uint16_t)std::lround((E * (px - C) - B * (py - F)) / det), (uint16_t)std::lround((A * (py - F) - D * (px - C)) / det), z);
}

TEST(CirqueGesture, DialStartedFarOutsideTheRingStillScrolls) {
    /* "Scrolling does not trigger when starting too far outside." A thumb that lands
     * in a corner slides in towards a comfortable circle before it turns. Measured
     * from the touchdown radius, 560 -> 432 is a 128-unit inward drag and the dial
     * aborted at every corner; measured from the ring it is no drag at all. The
     * 440 start is the control: it scrolled before the fix too. */
    for (int deg = 45; deg < 360; deg += 90) {
        for (double r0 : {440.0, 560.0, 620.0}) {
            Runner r;
            for (int i = 0; i <= 40; i++) {
                /* four samples straight in, then a 108-degree turn at 432 */
                const double rad = i < 4 ? r0 + (432 - r0) * i / 4.0 : 432;
                const double th  = (i < 4 ? deg : deg + (i - 4) * 3.0) * M_PI / 180.0;
                feed_pad(r, POLY_GEST_CENTRE + rad * std::cos(th), POLY_GEST_CENTRE + rad * std::sin(th));
            }
            EXPECT_GT(r.wheel_pos + r.wheel_neg, 3) << "no dial from radius " << r0 << " at " << deg << " degrees";
        }
    }
}

TEST(CirqueGesture, RadialDragFromTheFarCornerStillMoves) {
    /* The other side of the floor: a start far outside must still become a drag once
     * it crosses inside the ring by POLY_GEST_SCROLL_DR. */
    Runner r;
    for (int i = 0; i <= 20; i++) {
        const double rad = 620 - i * 20.0; /* 620 -> 220 */
        const double th  = 45 * M_PI / 180.0;
        feed_pad(r, POLY_GEST_CENTRE + rad * std::cos(th), POLY_GEST_CENTRE + rad * std::sin(th));
    }
    EXPECT_EQ(r.wheel_total, 0) << "a straight drag in from the corner scrolled";
    EXPECT_EQ(r.g.mode, POLY_GEST_MOVE) << "the drag never left PENDING";
    EXPECT_GT(std::abs(r.dx_total) + std::abs(r.dy_total), 100);
}

TEST(CirqueGesture, AShortQuickMoveIsBarelyAccelerated) {
    /* "Acceleration starts too fast": 13 motion units per sample is a short, quick
     * nudge. The linear ramp gave it +19 % (494 against 417 for the same distance
     * slowly); the quadratic one gives it under 1 %. */
    Runner slow, quick;
    for (int i = 0; i <= 200; i++) slow.feed((uint16_t)(MID_X - 500 + i * 5), MID_Y, 40);
    for (int i = 0; i <= 50; i++) quick.feed((uint16_t)(MID_X - 500 + i * 20), MID_Y, 40);
    const int s_moved = std::abs(slow.dx_total) + std::abs(slow.dy_total);
    const int q_moved = std::abs(quick.dx_total) + std::abs(quick.dy_total);
    EXPECT_LT(q_moved, s_moved * 21 / 20) << "a quick nudge was accelerated: " << q_moved << " vs " << s_moved;
}

TEST(CirqueGesture, LiftOffWanderDoesNotMoveTheCursor) {
    /* As a finger leaves, z decays through the release band and the position wanders
     * with it. The tap test has ignored those samples for a long time; the cursor did
     * not, so the end of a small move carried a jump the finger never made. */
    Runner r;
    for (int i = 0; i < 10; i++) r.feed(MID_X, MID_Y, 40);
    const int before = std::abs(r.dx_total) + std::abs(r.dy_total);
    uint16_t  z      = 15;
    for (int i = 1; i <= 5; i++, z--) r.feed((uint16_t)(MID_X + i * 30), (uint16_t)(MID_Y - i * 20), z);
    r.feed(MID_X + 150, MID_Y - 100, 0); /* lifted */
    const int after = std::abs(r.dx_total) + std::abs(r.dy_total);
    EXPECT_EQ(after - before, 0) << "lift-off wander moved the cursor " << after - before << " units";
}

TEST(CirqueGesture, AnOutwardDragFromTheWedgeStillAbortsTheDial) {
    /* The floor only moved the INWARD test. Outward is still measured from the
     * touchdown radius, so a drag that starts in the wedge and runs out to the rim is
     * a drag. The straight line is offset from the centre, so its angle drifts; the
     * outward test has to resolve the mode before that drift reaches the commit. */
    Runner r;
    for (int i = 0; i <= 20; i++) {
        const double t  = i / 20.0;
        const double px = POLY_GEST_CENTRE - (WEDGE_IN + 230 * t) * 0.707 - 60 * t;
        const double py = POLY_GEST_CENTRE - (WEDGE_IN + 230 * t) * 0.707;
        feed_pad(r, px, py);
    }
    EXPECT_EQ(r.wheel_total, 0) << "an outward drag from the wedge scrolled";
    EXPECT_EQ(r.g.mode, POLY_GEST_MOVE);
}

} // namespace

