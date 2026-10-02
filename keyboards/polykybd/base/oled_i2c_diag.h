// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Pure bookkeeping + console wording for status-OLED I2C write failures.
//
// The status OLED is an SSD1306 on I2CD0 (GP0/GP1). Stock QMK reports a failed
// write as one bare line (`oled_render offset command failed`) and drops the
// frame, which says neither WHY the write failed nor whether the panel came back.
// A field report (2026-10-02, one failure in ~7300 idle frames) could not be
// diagnosed past "a write did not land". oled_i2c.c overrides the driver's weak
// oled_send_cmd()/oled_send_data(), retries a failed write once, and hands the
// outcome to this module, which decides what to print:
//
//   * one DETAIL line per failure — error class (timeout / nack / arb_lost /
//     overrun), the raw ChibiOS flags, the SDA/SCL levels right after the
//     failure, the payload length, the time since the last good write, and
//     whether the retry landed. At most OLED_I2C_DIAG_LINES_PER_WINDOW per
//     OLED_I2C_DIAG_WINDOW_MS; the rest are counted and summarised.
//   * one STUCK line once OLED_I2C_DIAG_STUCK_STREAK writes in a row failed even
//     after their retry. From then on writes are NOT retried, because a timeout
//     costs OLED_I2C_TIMEOUT (100 ms) per attempt on the loop that scans the
//     matrix, and a retry against a dead panel only doubles that.
//   * one RECOVERED line when a write lands after a stuck streak.
//
// ⚠️ The host's problem scan (PolyKybdHost services/problem_scan.py) matches
// these lines by their wording. Change a line here, change the pattern there.
//
// Free of quantum.h and ChibiOS so `make test:polykybd_oled_i2c_diag` reaches it;
// the caller passes the clock and the already-classified failure.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define OLED_I2C_DIAG_WINDOW_MS 10000u
#define OLED_I2C_DIAG_LINES_PER_WINDOW 3u
#define OLED_I2C_DIAG_STUCK_STREAK 3u

enum oled_i2c_kind {
    OLED_I2C_KIND_CMD = 0,   // a command write (the "offset command" stock QMK names)
    OLED_I2C_KIND_DATA,      // a pixel-data write
};

enum oled_i2c_class {
    OLED_I2C_CLASS_TIMEOUT = 0,   // no completion within OLED_I2C_TIMEOUT
    OLED_I2C_CLASS_NACK,          // aborted with no flag set: on the RP2040 LLD, a NACK
    OLED_I2C_CLASS_ARB_LOST,      // another master (or a glitch) won the bus
    OLED_I2C_CLASS_OVERRUN,       // FIFO over/underrun
};

typedef struct {
    enum oled_i2c_kind  kind;
    enum oled_i2c_class cls;         // first attempt
    uint32_t            flags;       // raw i2cGetErrors() after the first attempt
    bool                sda;         // line levels read right after the first attempt
    bool                scl;
    uint16_t            len;         // bytes in the write, the I2C control byte included
    bool                retried;     // false once the panel is known to be stuck
    bool                retry_ok;
    enum oled_i2c_class retry_cls;   // meaningful only when retried && !retry_ok
} oled_i2c_failure_t;

typedef struct {
    uint32_t total;             // failed first attempts since boot
    uint32_t retry_failed;      // of those, failed again (or were not retried)
    uint16_t streak;            // consecutive writes that did not land at all
    bool     stuck;             // the STUCK line has been printed for this streak
    bool     have_ok;
    uint32_t last_ok_ms;
    uint32_t window_start_ms;
    uint8_t  printed_in_window;
    uint16_t suppressed;        // detail lines not printed in the current window
} oled_i2c_diag_t;

void oled_i2c_diag_init(oled_i2c_diag_t *d);

// Should the caller retry the write that just failed? False once stuck.
bool oled_i2c_diag_should_retry(const oled_i2c_diag_t *d);

// A write landed (first try or retry). Writes any lines to print into `out`
// ('\n'-terminated, possibly several, possibly none) and returns the length.
size_t oled_i2c_diag_ok(oled_i2c_diag_t *d, uint32_t now_ms, char *out, size_t n);

// A first attempt failed; `f` carries the retry outcome too. Same output contract.
size_t oled_i2c_diag_fail(oled_i2c_diag_t *d, const oled_i2c_failure_t *f, uint32_t now_ms,
                          char *out, size_t n);

const char *oled_i2c_class_name(enum oled_i2c_class cls);
