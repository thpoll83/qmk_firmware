/* PolyKybd Cirque gesture layer — absolute mode only.
 *
 * Replaces the stock driver's get_report so the gestures are decided HERE, in
 * PolyKybd code, from real pad coordinates. See cirque_gestures.c for why.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef POLYKYBD_CIRQUE_GESTURES

/* Installed from pointing_device_init_kb(), i.e. after the stock
 * cirque_pinnacle_init() has already configured the ASIC. */
void poly_cirque_install(void);

typedef enum { POLY_CIRQUE_IDLE = 0, POLY_CIRQUE_MOVE, POLY_CIRQUE_PENDING, POLY_CIRQUE_SCROLL } poly_cirque_mode_t;

typedef struct {
    uint16_t           raw_x, raw_y, raw_z;   /* last sample, sensor frame     */
    uint16_t           x_lo, x_hi, y_lo, y_hi; /* learned reachable window     */
    uint16_t           px, py;                /* normalised PHYSICAL position  */
    uint16_t           z_peak;                /* max z seen since boot         */
    poly_cirque_mode_t mode;
    uint8_t            buttons;               /* what we last reported         */
    uint16_t           travel;                /* displacement of the last touch */
    uint16_t           last_ms;               /* its duration                  */
    uint16_t           taps;                  /* taps accepted since boot      */
    uint8_t            reject;                /* 0 ok, 1 too long, 2 too far, 3 too light */
    bool               down;                  /* live: a finger is on the pad       */
    bool               active;                /* down, or lifted moments ago        */
    bool               tap_flash;             /* a tap landed just now              */
    bool               in_ring;               /* live: this point would arm a dial  */
    bool               in_corner;             /* live: this point is a right-click  */
} poly_cirque_dbg_t;

void poly_cirque_debug_snapshot(poly_cirque_dbg_t *out);

#endif
