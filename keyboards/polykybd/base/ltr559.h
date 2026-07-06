// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// LTR-559 ambient-light + proximity sensor (Pimoroni breakout, I2C 0x23).
//
// Optional expansion-port peripheral: it shares the same I2C0 bus (GP0/GP1) as
// the Cirque trackpad (0x2A), so no new pins/peripheral are needed. Because the
// trackpad — and therefore this expansion connector — lives on the RIGHT half
// (POINTING_DEVICE_RIGHT), the sensor is read on the right half only.
//
// The whole module is compiled in only when POLYKYBD_LTR559 is defined
// (rules.mk: POLYKYBD_LTR559 = yes). It is a prototype/test surface: it reads
// every value the part exposes and the caller renders them on the status OLED.
//
// What the LTR-559 actually measures:
//   * Ambient light  — two 16-bit channels (CH1 = visible+IR, CH0 = IR) which we
//                       fold into an approximate lux value. This is the reading
//                       intended to drive per-keycap display brightness.
//   * Proximity      — an 11-bit (0..2047) RELATIVE reflected-IR value, NOT a
//                       calibrated distance. Good for "something is within a few
//                       cm" (hand approaching) — intended to inhibit/exit idle.
//   * Colour         — NOT supported by this part (no RGB channels).
// The proximity range is ~5 cm max because it is reflectance-based, not
// time-of-flight; beyond that the return drops into the noise floor.

#pragma once

#include <stdint.h>
#include <stdbool.h>

// 7-bit I2C address of the LTR-559 (fixed). The QMK i2c_master API takes the
// address pre-shifted (addr << 1), see LTR559_I2C_ADDR_8 in the .c.
#define LTR559_I2C_ADDR 0x23

// A full snapshot of everything the part reports, for the status-OLED test view.
typedef struct {
    bool     present;    // PART_ID / MANUFAC_ID matched at init
    uint16_t ch0;        // raw ALS IR channel (0..65535)
    uint16_t ch1;        // raw ALS visible+IR channel (0..65535)
    uint16_t lux;        // computed approximate lux (see ltr559.c)
    uint16_t prox;       // raw proximity 0..2047 (relative, not distance)
    bool     prox_sat;   // proximity saturation flag
    bool     als_valid;  // ALS data-valid bit from the last read
} ltr559_reading_t;

// Probe + configure the sensor. Safe to call once from keyboard_post_init_user()
// on the sensor half. Returns true if the part answered with the expected IDs.
bool ltr559_init(void);

// True once ltr559_init() has confirmed the part is on the bus.
bool ltr559_available(void);

// Poll step. Call from housekeeping on the sensor half; internally throttled and
// non-blocking (it only reads when the part signals fresh data). Maintains the
// latest snapshot, a 5-second rolling average of lux, and the latest proximity.
void ltr559_task(void);

// Latest full snapshot (for the status-OLED test rendering).
void ltr559_get_reading(ltr559_reading_t *out);

// 5-second average lux (intended input for auto-brightness). 0 until enough
// samples have accumulated.
uint16_t ltr559_avg_lux(void);

// Latest raw proximity value (0..2047). Intended input for idle-inhibit.
uint16_t ltr559_prox(void);
