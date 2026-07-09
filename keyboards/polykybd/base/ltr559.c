// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// LTR-559 ambient-light + proximity driver — see ltr559.h for the rationale and
// what the part can/can't measure. Compiled only when POLYKYBD_LTR559 is set.

#include "ltr559.h"

#ifdef POLYKYBD_LTR559

#    include "i2c_master.h"
#    include "timer.h"
#    include <string.h>  // memset

// The QMK i2c_master API expects the address already shifted left by one bit
// (the same convention the Cirque driver uses: CIRQUE_PINNACLE_ADDR << 1).
#    define LTR559_I2C_ADDR_8 (LTR559_I2C_ADDR << 1)
#    define LTR559_I2C_TIMEOUT 30  // ms; generous, reads are a few bytes

// --- Register map (LTR-559 datasheet) ---------------------------------------
#    define LTR559_REG_ALS_CONTR 0x80      // ALS gain + mode
#    define LTR559_REG_PS_CONTR 0x81       // PS mode
#    define LTR559_REG_PS_LED 0x82         // PS LED pulse freq/duty/current
#    define LTR559_REG_PS_N_PULSES 0x83    // PS LED pulse count
#    define LTR559_REG_PS_MEAS_RATE 0x84   // PS measurement repeat rate
#    define LTR559_REG_ALS_MEAS_RATE 0x85  // ALS integration time + repeat rate
#    define LTR559_REG_PART_ID 0x86        // expected 0x92 (part 0x9, rev 0x2)
#    define LTR559_REG_MANUFAC_ID 0x87     // expected 0x05
#    define LTR559_REG_ALS_DATA 0x88       // CH1_0, CH1_1, CH0_0, CH0_1 (auto-inc)
#    define LTR559_REG_ALS_PS_STATUS 0x8C  // data-new / valid / gain bits
#    define LTR559_REG_PS_DATA 0x8D        // PS_DATA_0, PS_DATA_1 (auto-inc)

#    define LTR559_PART_ID 0x92
#    define LTR559_MANUFAC_ID 0x05

// ALS_CONTR: bit0 = active, bits[4:2] = gain (000=1x, 010=4x, 011=8x, 111=96x).
// 4x gain: indoor light gave only ~12 raw counts at 1x (coarse, noisy lux); 4x
// lifts that into a usable range while keeping headroom before the 16-bit
// channels saturate. LTR559_ALS_GAIN must track the gain bits — the lux formula
// divides the result by it so the reported lux stays gain-independent.
#    define LTR559_ALS_CONTR_CFG 0x09  // active, gain 4x
#    define LTR559_ALS_GAIN 4
// PS_CONTR: bits[1:0] = 11 -> PS active.
#    define LTR559_PS_CONTR_CFG 0x03  // PS active
// ALS_MEAS_RATE: int time 100 ms (bits[5:3]=000), repeat 100 ms (bits[2:0]=001).
// ~10 fresh ALS samples/s, plenty for a 5 s average.
#    define LTR559_ALS_MEAS_RATE_CFG 0x01
// PS pulse count — 8 pulses is a solid default for reflectance sensing.
#    define LTR559_PS_N_PULSES_CFG 0x08

// ALS_PS_STATUS bits we care about.
#    define LTR559_STATUS_PS_DATA_NEW (1 << 0)
#    define LTR559_STATUS_ALS_DATA_NEW (1 << 2)
#    define LTR559_STATUS_ALS_DATA_VALID (1 << 7)  // 0 = valid, 1 = invalid

// Poll cadence (ms). The ALS/PS parts update on their own ~100 ms rate; polling
// faster just re-reads the same values, so 100 ms keeps the bus quiet.
#    define LTR559_POLL_MS 100
// 5-second rolling average window for lux.
#    define LTR559_AVG_WINDOW_MS 5000
#    define LTR559_AVG_SAMPLES (LTR559_AVG_WINDOW_MS / LTR559_POLL_MS)  // 50

// Retry the probe this often while the sensor is absent, so a sensor that is
// slow to wake at boot, or plugged in later, still gets picked up.
#    define LTR559_RETRY_MS 1000
// Give up probing after this many retries on a half that clearly hasn't got the
// sensor (~30 s of boot-window retries), so it stops touching the bus.
#    define LTR559_MAX_RETRIES 30

static bool             s_present    = false;
static ltr559_reading_t s_reading;
static uint16_t         s_avg_lux    = 0;
static uint32_t         s_last_poll   = 0;
static uint32_t         s_last_retry  = 0;
static uint8_t          s_retry_count = 0;

// Simple rolling sum: keep a ring of the last N lux samples so the average
// tracks a true 5 s window without storing floats.
static uint16_t s_lux_ring[LTR559_AVG_SAMPLES];
static uint8_t  s_ring_idx   = 0;
static uint8_t  s_ring_count = 0;
static uint32_t s_ring_sum   = 0;

static inline bool ltr559_read(uint8_t reg, uint8_t *buf, uint8_t len) {
    return i2c_read_register(LTR559_I2C_ADDR_8, reg, buf, len, LTR559_I2C_TIMEOUT) == I2C_STATUS_SUCCESS;
}

static inline bool ltr559_write(uint8_t reg, uint8_t val) {
    return i2c_write_register(LTR559_I2C_ADDR_8, reg, &val, 1, LTR559_I2C_TIMEOUT) == I2C_STATUS_SUCCESS;
}

// Approximate lux from the two ALS channels, using the datasheet piecewise fit
// (gain 1x, 100 ms integration -> unity divisor). Integer math (coefficients
// scaled ×10000); int64 intermediate so a near-saturated channel can't overflow.
// This is a prototype estimate, not a calibrated photometric lux — tune later.
static uint16_t ltr559_compute_lux(uint16_t ch0, uint16_t ch1) {
    uint32_t sum = (uint32_t)ch0 + (uint32_t)ch1;
    if (sum == 0) {
        return 0;
    }
    uint32_t ratio_k = ((uint32_t)ch1 * 1000u) / sum;  // ch1/(ch0+ch1) ×1000
    int64_t  lux;
    if (ratio_k < 450) {
        lux = (17743LL * ch0 + 11059LL * ch1) / 10000;
    } else if (ratio_k < 640) {
        lux = (42785LL * ch0 - 19548LL * ch1) / 10000;
    } else if (ratio_k < 850) {
        lux = (5926LL * ch0 + 1185LL * ch1) / 10000;
    } else {
        lux = 0;
    }
    lux /= LTR559_ALS_GAIN;   // formula assumes 1x; keep lux gain-independent
    if (lux < 0) {
        lux = 0;
    }
    if (lux > 65535) {
        lux = 65535;
    }
    return (uint16_t)lux;
}

// Read + verify the ID registers. Returns true when both IDs match. Cheap (2
// register reads) so it is safe to re-run every retry.
static bool ltr559_probe(void) {
    i2c_init();   // idempotent; ensure the bus is up even without the pointing device
    s_present = false;
    memset(&s_reading, 0, sizeof(s_reading));

    uint8_t part = 0, manu = 0;
    i2c_status_t st = i2c_read_register(LTR559_I2C_ADDR_8, LTR559_REG_PART_ID, &part, 1, LTR559_I2C_TIMEOUT);
    if (st == I2C_STATUS_SUCCESS) {
        st = i2c_read_register(LTR559_I2C_ADDR_8, LTR559_REG_MANUFAC_ID, &manu, 1, LTR559_I2C_TIMEOUT);
    }

    if (st != I2C_STATUS_SUCCESS || part != LTR559_PART_ID || manu != LTR559_MANUFAC_ID) {
        return false;
    }

    // Configure. Order isn't critical; leave PS_LED/PS_MEAS_RATE at reset
    // defaults (functional out of the box). Abort if any config write fails —
    // the IDs matched but a bus timeout here would leave the part unconfigured,
    // and marking it present anyway would feed garbage into the poll path.
    if (!ltr559_write(LTR559_REG_ALS_MEAS_RATE, LTR559_ALS_MEAS_RATE_CFG) ||
        !ltr559_write(LTR559_REG_PS_N_PULSES, LTR559_PS_N_PULSES_CFG) ||
        !ltr559_write(LTR559_REG_PS_CONTR, LTR559_PS_CONTR_CFG) ||
        !ltr559_write(LTR559_REG_ALS_CONTR, LTR559_ALS_CONTR_CFG)) {
        return false;
    }

    s_present         = true;
    s_reading.present = true;
    s_last_poll       = timer_read32();
    return true;
}

bool ltr559_init(void) {
    return ltr559_probe();
}

bool ltr559_available(void) {
    return s_present;
}

static void ltr559_push_avg(uint16_t lux) {
    if (s_ring_count == LTR559_AVG_SAMPLES) {
        s_ring_sum -= s_lux_ring[s_ring_idx];
    } else {
        s_ring_count++;
    }
    s_lux_ring[s_ring_idx] = lux;
    s_ring_sum += lux;
    s_ring_idx = (uint8_t)((s_ring_idx + 1) % LTR559_AVG_SAMPLES);
    s_avg_lux  = (uint16_t)(s_ring_sum / s_ring_count);
}

void ltr559_task(void) {
    if (!s_present) {
        // Retry the probe for a bounded window: a sensor slow to wake at boot is
        // still picked up, but the half WITHOUT the sensor (both halves poll now,
        // since it can be soldered to either) stops probing after
        // LTR559_MAX_RETRIES so its I2C reads can't keep stalling the loop.
        if (s_retry_count < LTR559_MAX_RETRIES && timer_elapsed32(s_last_retry) >= LTR559_RETRY_MS) {
            s_last_retry = timer_read32();
            s_retry_count++;
            ltr559_probe();
        }
        return;
    }
    if (timer_elapsed32(s_last_poll) < LTR559_POLL_MS) {
        return;
    }
    s_last_poll = timer_read32();

    uint8_t status = 0;
    if (!ltr559_read(LTR559_REG_ALS_PS_STATUS, &status, 1)) {
        return;  // transient bus error; try again next poll
    }

    if (status & LTR559_STATUS_ALS_DATA_NEW) {
        uint8_t d[4];
        if (ltr559_read(LTR559_REG_ALS_DATA, d, 4)) {
            // 0x88..0x8B = CH1_0, CH1_1, CH0_0, CH0_1 (little-endian per channel)
            s_reading.ch1       = (uint16_t)(d[0] | (d[1] << 8));
            s_reading.ch0       = (uint16_t)(d[2] | (d[3] << 8));
            s_reading.als_valid = (status & LTR559_STATUS_ALS_DATA_VALID) == 0;
            // Only fold VALID samples into the snapshot + rolling average — an
            // invalid/out-of-range ALS reading would otherwise skew the 5 s avg
            // that drives auto-brightness. On an invalid sample keep the last
            // good lux and don't push it.
            if (s_reading.als_valid) {
                s_reading.lux = ltr559_compute_lux(s_reading.ch0, s_reading.ch1);
                ltr559_push_avg(s_reading.lux);
            }
        }
    }

    if (status & LTR559_STATUS_PS_DATA_NEW) {
        uint8_t p[2];
        if (ltr559_read(LTR559_REG_PS_DATA, p, 2)) {
            s_reading.prox     = (uint16_t)(p[0] | ((p[1] & 0x07) << 8));  // 11-bit
            s_reading.prox_sat = (p[1] & 0x80) != 0;
        }
    }
}

void ltr559_get_reading(ltr559_reading_t *out) {
    if (out) {
        *out = s_reading;
    }
}

uint16_t ltr559_avg_lux(void) {
    return s_avg_lux;
}

uint16_t ltr559_prox(void) {
    return s_reading.prox;
}

#endif  // POLYKYBD_LTR559
