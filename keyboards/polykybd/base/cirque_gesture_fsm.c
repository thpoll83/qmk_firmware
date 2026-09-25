/* PolyKybd Cirque gesture decision — pure. See cirque_gesture_fsm.h for why. */
#include "cirque_gesture_fsm.h"

#include <stdlib.h>

/* Integer sqrt, so the radius can be compared in units that mean something. */
uint32_t poly_gest_isqrt(uint32_t v) {
    uint32_t r = 0, bit = 1UL << 30;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) {
            v -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return r;
}

/* Median of three, per axis.
 *
 * A two-sample AVERAGE was tried first and is the wrong tool: it halves a single wild
 * sample instead of discarding it, so a still finger still moved the cursor 38 units
 * in one sample — measured, and exactly the "jumps" reported from hardware. A median
 * rejects a lone outlier outright, which is what scatter is. Sums do not show this:
 * scatter cancels over time, so only the WORST SINGLE SAMPLE reveals it, and that is
 * what the test asserts. */
uint16_t poly_gest_median3(uint16_t a, uint16_t b, uint16_t c) {
    if (a > b) {
        uint16_t t = a;
        a          = b;
        b          = t;
    }
    if (b > c) {
        uint16_t t = b;
        b          = c;
        c          = t;
    }
    if (a > b) b = a;
    return b;
}

static int16_t clamp_xy(int32_t v) {
    if (v > 127) return 127;
    if (v < -127) return -127;
    return (int16_t)v;
}

static uint16_t clamp_span(int32_t v) {
    if (v < 0) return 0;
    if (v > POLY_GEST_SPAN) return POLY_GEST_SPAN;
    return (uint16_t)v;
}

void poly_gest_to_pad(uint16_t sx, uint16_t sy, uint16_t *px, uint16_t *py) {
    *px = clamp_span((POLY_GEST_PX_A * (int32_t)sx + POLY_GEST_PX_B * (int32_t)sy + POLY_GEST_PX_C) / POLY_GEST_Q);
    *py = clamp_span((POLY_GEST_PY_A * (int32_t)sx + POLY_GEST_PY_B * (int32_t)sy + POLY_GEST_PY_C) / POLY_GEST_Q);
}

/* Plain 90-degree rotation of the raw axes — skew left in, on purpose. */
static void to_motion(uint16_t sx, uint16_t sy, uint16_t *mx, uint16_t *my) {
    const int32_t nx = ((int32_t)sx - POLY_GEST_XLO) * POLY_GEST_SPAN / (POLY_GEST_XHI - POLY_GEST_XLO);
    const int32_t ny = ((int32_t)sy - POLY_GEST_YLO) * POLY_GEST_SPAN / (POLY_GEST_YHI - POLY_GEST_YLO);
    *mx              = clamp_span(ny);
    *my              = clamp_span(POLY_GEST_SPAN - nx);
}

/* Angle about the pad centre, 0..65535 per turn. Integer rational approximation,
 * the shape QMK's own cirque gesture code uses — the error is irrelevant once the
 * output is quantised to 15-degree clicks. */
static uint16_t angle_of(int32_t dx, int32_t dy) {
    if (dy == 0) return (dx >= 0) ? 0 : 32768;
    const int32_t ay = dy > 0 ? dy : -dy;
    int16_t       a;
    if (dx >= 0) {
        a = (int16_t)(8192 - (8192 * (dx - ay) / (dx + ay)));
    } else {
        a = (int16_t)(24576 - (8192 * (dx + ay) / (ay - dx)));
    }
    return (dy < 0) ? (uint16_t)(-a) : (uint16_t)a;
}

void poly_gest_reset(poly_gest_t *g) {
    poly_gest_t zero = {0};
    *g               = zero;
}

void poly_gest_feed(poly_gest_t *g, const poly_gest_sample_t *s, poly_gest_out_t *out) {
    const uint16_t corner_lo = (uint16_t)(((uint32_t)POLY_GEST_SPAN * (100 - POLY_GEST_CORNER_PCT)) / 100);

    out->dx = out->dy = 0;
    out->wheel        = 0;
    out->buttons      = 0;

    if (s->valid) {
        const bool touching = g->down ? (s->z >= POLY_GEST_Z_RELEASE) : (s->z >= POLY_GEST_Z_TOUCH);
        /* (0,0) is the ASIC's magic coordinate for "finger just landed", not a
         * position — cirque_pinnacle.c derives touchDown from exactly this test. */
        const bool have_pos = (s->x != 0 || s->y != 0);

        if (touching && have_pos) {
            uint16_t mx, my;
            poly_gest_to_pad(s->x, s->y, &g->px, &g->py);
            to_motion(s->x, s->y, &mx, &my);

            if (s->z > g->z_peak) g->z_peak = (uint8_t)s->z;

            const int32_t rx = (int32_t)g->px - POLY_GEST_CENTRE;
            const int32_t ry = (int32_t)g->py - POLY_GEST_CENTRE;

            if (!g->down) {
                g->down    = true;
                g->down_ms = s->t_ms;
                g->px0     = g->px;
                g->py0     = g->py;
                g->mx      = mx;
                g->my      = my;
                g->z_peak  = (uint8_t)s->z;
                g->travel  = 0;
                g->px_prev = g->px;
                g->py_prev = g->py;
                g->corner  = (g->px >= corner_lo && g->py <= (POLY_GEST_SPAN - corner_lo));
                g->ang     = angle_of(rx, ry);
                g->ang_acc = g->ang_total = 0;
                g->r0sq                 = (uint32_t)(rx * rx + ry * ry);
                g->r0                   = poly_gest_isqrt(g->r0sq);
                g->mhist_x[0] = g->mhist_x[1] = g->mhist_x[2] = mx;
                g->mhist_y[0] = g->mhist_y[1] = g->mhist_y[2] = my;
                g->mhist_n                                    = 0;
                g->fx_q4                                      = (int32_t)mx * 16;
                g->fy_q4                                      = (int32_t)my * 16;
                g->res_x = g->res_y = 0;
                const bool in_ring      = g->r0sq >= ((uint32_t)POLY_GEST_RING_R * POLY_GEST_RING_R);
                /* ...or the dedicated wedge at the top left, which reaches inward and
                 * so stays a generous target however far out the ring is pushed. */
                const bool in_wedge = (rx < 0 && ry < 0 && g->r0sq >= ((uint32_t)POLY_GEST_DIAL_WEDGE_R * POLY_GEST_DIAL_WEDGE_R) && (-ry) * 1000 >= 554 * (-rx) && (-ry) * 1000 <= 1804 * (-rx));
                /* The corner does NOT veto the ring. It used to, and with the corner
                 * widened to 38% that carved a dead arc out of the ring exactly where
                 * the two overlap — the dial stopped arming at 2 of 12 test angles.
                 * They answer different questions: the ring decides whether a DIAL can
                 * start, the corner only decides which BUTTON a tap sends. A corner tap
                 * still works from PENDING, because a tap does not rotate. */
                g->mode                 = (in_ring || in_wedge) ? POLY_GEST_PENDING : POLY_GEST_MOVE;
                /* The inward drag test starts at the arming radius, not at the
                 * touchdown radius, so a dial can start any distance outside it. */
                const uint32_t r_arm    = in_ring ? POLY_GEST_RING_R : POLY_GEST_DIAL_WEDGE_R;
                g->r_floor              = (g->r0 < r_arm) ? g->r0 : r_arm;
            } else {
                /* Smoothed, for the same reason the tap displacement is: the raw
                 * position scatters by tens of units, and on the CURSOR path that is
                 * not a rejected tap, it is a visible jump. +-40 raw units is up to
                 * 52 motion units of movement the finger never made. */
                g->mhist_x[g->mhist_n] = mx;
                g->mhist_y[g->mhist_n] = my;
                g->mhist_n             = (uint8_t)((g->mhist_n + 1) % 3);
                const int32_t med_x    = poly_gest_median3(g->mhist_x[0], g->mhist_x[1], g->mhist_x[2]);
                const int32_t med_y    = poly_gest_median3(g->mhist_y[0], g->mhist_y[1], g->mhist_y[2]);
                /* One-pole low pass on top of the median: the median removes lone
                 * outliers, this removes the tremor that survives it. */
                g->fx_q4 += ((med_x * 16) - g->fx_q4) * POLY_GEST_SMOOTH_Q4 / 16;
                g->fy_q4 += ((med_y * 16) - g->fy_q4) * POLY_GEST_SMOOTH_Q4 / 16;
                const int32_t smx = g->fx_q4 / 16;
                const int32_t smy = g->fy_q4 / 16;
                int32_t       dx  = smx - (int32_t)g->mx;
                int32_t       dy  = smy - (int32_t)g->my;
                /* A single impossible sample is a glitch, not movement. Dropping it
                 * keeps the origin, so the next real sample picks the motion up
                 * rather than replaying the jump. */
                if (labs(dx) + labs(dy) > POLY_GEST_JUMP_MAX) {
                    dx = dy = 0;
                } else {
                    g->mx = (uint16_t)smx;
                    g->my = (uint16_t)smy;
                }
                /* No cursor motion while z sits in the release band. As a finger
                 * lifts, z decays from Z_TOUCH to Z_RELEASE and the reported position
                 * wanders with it; the tap test below already ignores those samples,
                 * and the cursor path now does too. On a short nudge that wander was
                 * larger than the nudge itself, so it read as a jump. The origin still
                 * advances above, so the wander is dropped, not replayed later. */
                if (s->z < POLY_GEST_Z_TOUCH) dx = dy = 0;
                /* Speed scaling with acceleration, keeping the remainder so slow
                 * movement is delayed rather than floored away. Below the knee the
                 * factor is constant, which is what leaves precision work and the
                 * tremor filter alone; above it the factor grows with the square of
                 * the excess, so a small quick move is barely accelerated. */
                const int32_t mag = labs(dx) + labs(dy);
                int32_t       pct = POLY_GEST_SPEED_PCT;
                if (mag > POLY_GEST_ACCEL_KNEE) {
                    const int32_t over = mag - POLY_GEST_ACCEL_KNEE;
                    pct += over * over / POLY_GEST_ACCEL_DIV;
                    if (pct > POLY_GEST_ACCEL_MAX_PCT) pct = POLY_GEST_ACCEL_MAX_PCT;
                }
                g->res_x += dx * pct;
                g->res_y += dy * pct;
                dx = g->res_x / 100;
                dy = g->res_y / 100;
                g->res_x -= dx * 100;
                g->res_y -= dy * 100;

                /* int16 wraparound IS the shortest signed rotation, so the 0/360 seam
                 * needs no special case — the reason the angle is in 1/65536 turns. */
                const uint16_t ang  = angle_of(rx, ry);
                const int16_t  dang = (int16_t)(ang - g->ang);
                g->ang              = ang;

                /* Displacement only while z is solid: as a finger leaves, z decays
                 * through the hysteresis band and the reported position wanders with
                 * it. Charging that wander to the tap is what made a still finger
                 * look like a drag. */
                if (s->z >= POLY_GEST_Z_TOUCH) {
                    /* Smoothed with the previous sample: per-sample scatter is the
                     * thing being rejected here, and averaging two halves it while a
                     * real drag, which moves hundreds of units, is untouched. */
                    const int32_t  sx   = ((int32_t)g->px + (int32_t)g->px_prev) / 2;
                    const int32_t  sy   = ((int32_t)g->py + (int32_t)g->py_prev) / 2;
                    const uint16_t disp = (uint16_t)(labs(sx - (int32_t)g->px0) + labs(sy - (int32_t)g->py0));
                    if (disp > g->travel) g->travel = disp;
                }
                g->px_prev = g->px;
                g->py_prev = g->py;

                if (g->mode == POLY_GEST_PENDING) {
                    /* Movement is emitted LIVE while the mode is undecided, not held
                     * and released in a burst when it resolves.
                     *
                     * Holding it was a jump BY CONSTRUCTION, and lowering the ring
                     * radius to 220 made it the common case: most touches now start
                     * in the ring, so most touches began with the cursor frozen and
                     * then leaping. The cost of emitting instead is a few units of
                     * cursor drift on a touch that turns out to be a dial — under six
                     * degrees of rotation, which is not visible. A jump is. */
                    out->dx = clamp_xy(dx);
                    out->dy = clamp_xy(dy);
                    g->ang_total += dang;
                    /* Dial or drag, asked directly rather than by racing two
                     * counters: turning keeps the radius, dragging changes it. In
                     * REAL units — see POLY_GEST_SCROLL_DR for what comparing this
                     * in a scaled squared domain silently did to the threshold. */
                    const int32_t r_now = (int32_t)poly_gest_isqrt((uint32_t)(rx * rx + ry * ry));
                    if (labs(g->ang_total) >= POLY_GEST_SCROLL_COMMIT) {
                        g->mode    = POLY_GEST_SCROLL;
                        g->ang_acc = g->ang_total;
                    } else if (r_now <= (int32_t)g->r_floor - POLY_GEST_SCROLL_DR || r_now >= (int32_t)g->r0 + POLY_GEST_SCROLL_DR) {
                        g->mode = POLY_GEST_MOVE;
                    }
                } else if (g->mode == POLY_GEST_SCROLL) {
                    g->ang_acc += dang;
                    while (g->ang_acc >= POLY_GEST_SCROLL_STEP) {
                        out->wheel -= 1;
                        g->ang_acc -= POLY_GEST_SCROLL_STEP;
                    }
                    while (g->ang_acc <= -POLY_GEST_SCROLL_STEP) {
                        out->wheel += 1;
                        g->ang_acc += POLY_GEST_SCROLL_STEP;
                    }
                } else {
                    out->dx = clamp_xy(dx);
                    out->dy = clamp_xy(dy);
                }
            }
        } else if (g->down && !touching) {
            g->down        = false;
            g->last_travel = g->travel;
            g->last_ms     = (uint16_t)(s->t_ms - g->down_ms);

            /* Mode is not consulted: a touch that started in the ring but never
             * turned is a tap, not a zero-length scroll.
             *
             * The verdict is RECORDED, not just taken. Three tests can reject a tap
             * and from outside they are indistinguishable. */
            if (g->last_ms >= POLY_GEST_TAP_MS) {
                g->verdict = POLY_GEST_REJ_LONG;
            } else if (g->last_travel >= POLY_GEST_TAP_SLOP) {
                g->verdict = POLY_GEST_REJ_FAR;
            } else if (g->z_peak < POLY_GEST_Z_TAP) {
                g->verdict = POLY_GEST_REJ_LIGHT;
            } else {
                g->verdict   = POLY_GEST_OK;
                g->click_btn = g->corner ? POLY_GEST_BTN_R : POLY_GEST_BTN_L;
                g->click_ms  = s->t_ms;
                g->taps++;
            }
            g->mode = POLY_GEST_IDLE;
        }
    }

    /* Hold the click regardless of what the sensor does next: the split's slave path
     * hands get_report a zeroed report every call, so a single-sample button would
     * not survive to the host. */
    if (g->click_btn) {
        if ((uint32_t)(s->t_ms - g->click_ms) < POLY_GEST_CLICK_MS) {
            out->buttons = g->click_btn;
        } else {
            g->click_btn = 0;
        }
    }
}
