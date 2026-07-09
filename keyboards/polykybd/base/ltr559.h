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

// Rolling average lux (intended input for auto-brightness). The window grows to
// 5 seconds: it averages whatever samples have been collected so far, so it
// returns a real value as soon as the first VALID ALS sample lands and is a full
// 5 s average once the ring is full. Stays 0 only until that first sample (which
// the drive path uses as its "sensor warmed up" signal).
uint16_t ltr559_avg_lux(void);

// Latest raw proximity value (0..2047). Intended input for idle-inhibit.
uint16_t ltr559_prox(void);

// --- Troubleshooting -------------------------------------------------------
// Diagnostic snapshot, surfaced on the status OLED when the sensor is NOT
// detected so a bring-up problem can be told apart without a debugger:
//   * scan_found / addr_23 / addr_2a — an I2C bus scan. If the Cirque (0x2A)
//     ACKs but the LTR-559 (0x23) does not, the bus/pins are fine and the sensor
//     itself isn't answering (power, wiring, or address). If NOTHING ACKs, the
//     bus never came up (init/pins/SDA-SCL). If 0x23 ACKs but part/manuf don't
//     match, the device is there but returned unexpected IDs.
//   * part_id / manuf_id / id_status — the raw PART_ID (expect 0x92) and
//     MANUFAC_ID (expect 0x05) bytes and the i2c_status_t of that read.
typedef struct {
    bool     init_done;      // ltr559_init() has run at least once
    bool     present;        // part+manuf IDs matched
    uint8_t  part_id;        // raw PART_ID read (0 if the read failed)
    uint8_t  manuf_id;       // raw MANUFAC_ID read
    int16_t  id_status;      // i2c_status_t of the ID read (0 = SUCCESS)
    bool     addr_23;        // LTR-559 (0x23) ACKed during the scan
    bool     addr_2a;        // Cirque trackpad (0x2A) ACKed during the scan
    uint8_t  scan_found[8];  // up to 8 ACKing 7-bit addresses
    uint8_t  scan_count;     // number of entries in scan_found
} ltr559_diag_t;

// Copy out the latest diagnostic snapshot (valid on the sensor half).
void ltr559_get_diag(ltr559_diag_t *out);
