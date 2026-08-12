// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// LTR-559 ambient-light + proximity sensor (Pimoroni breakout, I2C 0x23).
//
// Listing this module is the whole opt-in: it registers its own
// keyboard_post_init / housekeeping_task hooks, so a keymap needs no code to get
// a probed, polled sensor. Everything below is then read-only accessors.
//
// It is a **clean no-op when no sensor is fitted**: the probe fails and the
// driver disables itself after LTR559_MAX_RETRIES bounded retries, so it stops
// touching the bus. That is what makes it safe to list unconditionally on a board
// where the part is an optional, user-soldered extra.
//
// What the LTR-559 actually measures:
//   * Ambient light  — two 16-bit channels (CH1 = visible+IR, CH0 = IR) which we
//                       fold into an approximate lux value. This is the reading
//                       intended to drive display brightness.
//   * Proximity      — an 11-bit (0..2047) RELATIVE reflected-IR value, NOT a
//                       calibrated distance. Good for "something is within a few
//                       cm" (hand approaching) — intended to inhibit/exit idle.
//   * Colour         — NOT supported by this part (no RGB channels).
// The proximity range is ~5 cm max because it is reflectance-based, not
// time-of-flight; beyond that the return drops into the noise floor.
//
// ⚠️ The proximity resting baseline is HOUSING-dependent — ~129 on an open bench
// but ~325 once mounted, because enclosure walls reflect IR back. Re-check it
// after any housing/hole change before trusting a near-threshold.
//
// The sensor shares whatever I2C bus the board already has; ltr559_init() calls
// i2c_init() itself, so no other peripheral has to be enabled for it to work.

#pragma once

#include <stdint.h>
#include <stdbool.h>

// 7-bit I2C address of the LTR-559 (fixed). The QMK i2c_master API takes the
// address pre-shifted (addr << 1), see LTR559_I2C_ADDR_8 in the .c.
#define LTR559_I2C_ADDR 0x23

// A full snapshot of everything the part reports.
typedef struct {
    bool     present;    // PART_ID / MANUFAC_ID matched at init
    uint16_t ch0;        // raw ALS IR channel (0..65535)
    uint16_t ch1;        // raw ALS visible+IR channel (0..65535)
    uint16_t lux;        // computed approximate lux (see polymod_ltr559.c)
    uint16_t prox;       // raw proximity 0..2047 (relative, not distance)
    bool     prox_sat;   // proximity saturation flag
    bool     als_valid;  // ALS data-valid bit from the last read
} ltr559_reading_t;

// Probe + configure the sensor. Called for you from the module's
// keyboard_post_init hook; exposed so a consumer can re-probe explicitly.
// Returns true if the part answered with the expected IDs. Idempotent.
bool ltr559_init(void);

// True once ltr559_init() has confirmed the part is on the bus.
bool ltr559_available(void);

// Poll step. Called for you from the module's housekeeping_task hook; internally
// throttled and non-blocking (it only reads when the part signals fresh data).
// Maintains the latest snapshot, a 5-second rolling average of lux, and the
// latest proximity. While the sensor is absent this is the bounded retry probe.
void ltr559_task(void);

// Latest full snapshot.
void ltr559_get_reading(ltr559_reading_t *out);

// Rolling average lux (intended input for auto-brightness). The window grows to
// 5 seconds: it averages whatever samples have been collected so far, so it
// returns a real value as soon as the first VALID ALS sample lands and is a full
// 5 s average once the ring is full. Stays 0 only until that first sample — which
// is the documented "sensor has not warmed up yet" signal, and the reason a
// brightness driver must not engage while this reads 0 (it would dip the display
// to the near-off floor for the first ~1 s of every boot).
uint16_t ltr559_avg_lux(void);

// Latest raw proximity value (0..2047). Intended input for idle-inhibit.
uint16_t ltr559_prox(void);

#ifdef LTR559_UNIT_TEST
// Resets all driver state to power-on defaults. Test-only: the driver is a
// singleton over file-scope statics, so each test case needs a clean slate.
void ltr559_reset_for_test(void);
#endif
