// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Status-OLED I2C writes with a diagnosis attached.
//
// Overrides the QMK OLED driver's WEAK oled_send_cmd()/oled_send_data()
// (drivers/oled/oled_driver.c), so no upstream file is patched. Each write is the
// same i2c_transmit()/i2c_write_register() the driver would make. On a failure it
// reads what the hardware can still tell us, retries once, and lets
// base/oled_i2c_diag.c decide what to print; the wording and the rate limit live
// there.
//
// What the extra fields can tell apart:
//   * timeout vs nack vs arb_lost: a NACK means the panel did not acknowledge (a
//     panel reset, a brown-out or a bad contact); a timeout means the transfer did
//     not finish (SCL held low, clock stretching past 100 ms); arb_lost means the
//     controller saw a level on SDA it did not drive (noise, or a second driver).
//   * sda/scl after the failure: both should read 1 on an idle bus. A 0 means
//     something is holding that line low, which a retry cannot fix.
//   * retry ok vs failed: a write that lands on the second try is a transient;
//     three writes of one kind (cmd or data) that fail both tries mark that
//     kind stuck.
//
// ⚠️ This prints only on the half whose console reaches the host. A failure on the
// slave half's status OLED is counted there and never seen.
#include QMK_KEYBOARD_H

#if defined(OLED_ENABLE) && defined(OLED_TRANSPORT_I2C)

#    include <hal.h>
#    include "i2c_master.h"
#    include "base/oled_i2c_diag.h"

// Same values as drivers/oled/oled_driver.c, which keeps them file-local.
#    define OLED_I2C_DATA_CONTROL 0x40

// Zero-initialised, which is oled_i2c_diag_init()'s state. Only the main loop
// writes to the status OLED, so one static line buffer is enough and keeps
// 320 bytes off its stack.
static oled_i2c_diag_t s_diag;
static char            s_line[320];

static enum oled_i2c_class classify(i2c_status_t st, i2cflags_t flags) {
    if (st == I2C_STATUS_TIMEOUT) {
        return OLED_I2C_CLASS_TIMEOUT;
    }
    if (flags & I2C_ARBITRATION_LOST) {
        return OLED_I2C_CLASS_ARB_LOST;
    }
    if (flags & I2C_OVERRUN) {
        return OLED_I2C_CLASS_OVERRUN;
    }
    // The RP2040 LLD raises no flag for an address or data NACK: the transfer
    // aborts with MSG_RESET and an empty mask.
    return OLED_I2C_CLASS_NACK;
}

static i2c_status_t write_once(enum oled_i2c_kind kind, const uint8_t *data, uint16_t size) {
    if (kind == OLED_I2C_KIND_CMD) {
        return i2c_transmit((OLED_DISPLAY_ADDRESS << 1), data, size, OLED_I2C_TIMEOUT);
    }
    return i2c_write_register((OLED_DISPLAY_ADDRESS << 1), OLED_I2C_DATA_CONTROL, data, size, OLED_I2C_TIMEOUT);
}

static void emit(const char *buf, size_t len) {
    if (len > 0) {
        uprintf("%s", buf);
    }
}

static bool diag_write(enum oled_i2c_kind kind, const uint8_t *data, uint16_t size) {
    i2c_status_t st = write_once(kind, data, size);
    if (st == I2C_STATUS_SUCCESS) {
        emit(s_line, oled_i2c_diag_ok(&s_diag, kind, timer_read32(), s_line, sizeof(s_line)));
        return true;
    }

    oled_i2c_failure_t f = {
        .kind  = kind,
        .cls   = classify(st, i2cGetErrors(&I2C_DRIVER)),
        .flags = (uint32_t)i2cGetErrors(&I2C_DRIVER),
        .sda   = gpio_read_pin(I2C1_SDA_PIN),
        .scl   = gpio_read_pin(I2C1_SCL_PIN),
        // The command buffer already starts with the control byte; the data write
        // prepends one through i2c_write_register().
        .len     = (uint16_t)(kind == OLED_I2C_KIND_CMD ? size : size + 1u),
        .retried = oled_i2c_diag_should_retry(&s_diag, kind),
    };
    if (f.retried) {
        i2c_status_t st2 = write_once(kind, data, size);
        f.retry_ok       = (st2 == I2C_STATUS_SUCCESS);
        if (!f.retry_ok) {
            f.retry_cls = classify(st2, i2cGetErrors(&I2C_DRIVER));
        }
    }
    emit(s_line, oled_i2c_diag_fail(&s_diag, &f, timer_read32(), s_line, sizeof(s_line)));
    return f.retried && f.retry_ok;
}

bool oled_send_cmd(const uint8_t *data, uint16_t size) {
    return diag_write(OLED_I2C_KIND_CMD, data, size);
}

bool oled_send_data(const uint8_t *data, uint16_t size) {
    return diag_write(OLED_I2C_KIND_DATA, data, size);
}

#endif
