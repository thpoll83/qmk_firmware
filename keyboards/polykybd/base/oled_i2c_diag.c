// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#include "oled_i2c_diag.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void oled_i2c_diag_init(oled_i2c_diag_t *d) {
    memset(d, 0, sizeof(*d));
}

bool oled_i2c_diag_should_retry(const oled_i2c_diag_t *d) {
    return !d->stuck;
}

const char *oled_i2c_class_name(enum oled_i2c_class cls) {
    switch (cls) {
        case OLED_I2C_CLASS_TIMEOUT:
            return "timeout";
        case OLED_I2C_CLASS_NACK:
            return "nack";
        case OLED_I2C_CLASS_ARB_LOST:
            return "arb_lost";
        case OLED_I2C_CLASS_OVERRUN:
            return "overrun";
    }
    return "?";
}

// Append with snprintf semantics clamped to the buffer: `*len` never passes n-1.
__attribute__((format(printf, 4, 5)))
static void append(char *out, size_t n, size_t *len, const char *fmt, ...) {
    if (n == 0 || *len >= n - 1) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    int w = vsnprintf(out + *len, n - *len, fmt, ap);
    va_end(ap);
    if (w < 0) {
        return;
    }
    *len += (size_t)w;
    if (*len > n - 1) {
        *len = n - 1;
    }
}

// Close the rate-limit window once it has run out, summarising what it hid.
static void roll_window(oled_i2c_diag_t *d, uint32_t now, char *out, size_t n, size_t *len) {
    if (now - d->window_start_ms < OLED_I2C_DIAG_WINDOW_MS) {
        return;
    }
    if (d->suppressed > 0) {
        append(out, n, len, "oled_i2c: %u more failed write(s) not printed in the last %u s (%lu since boot)\n",
               (unsigned)d->suppressed, (unsigned)(OLED_I2C_DIAG_WINDOW_MS / 1000u), (unsigned long)d->total);
    }
    d->window_start_ms   = now;
    d->printed_in_window = 0;
    d->suppressed        = 0;
}

static void landed(oled_i2c_diag_t *d, uint32_t now, char *out, size_t n, size_t *len) {
    if (d->stuck) {
        append(out, n, len, "oled_i2c: status display responding again after %u failed write(s)\n", (unsigned)d->streak);
    }
    d->streak     = 0;
    d->stuck      = false;
    d->have_ok    = true;
    d->last_ok_ms = now;
}

size_t oled_i2c_diag_ok(oled_i2c_diag_t *d, uint32_t now_ms, char *out, size_t n) {
    size_t len = 0;
    if (n > 0) {
        out[0] = '\0';
    }
    roll_window(d, now_ms, out, n, &len);
    landed(d, now_ms, out, n, &len);
    return len;
}

size_t oled_i2c_diag_fail(oled_i2c_diag_t *d, const oled_i2c_failure_t *f, uint32_t now_ms, char *out, size_t n) {
    size_t len = 0;
    if (n > 0) {
        out[0] = '\0';
    }
    roll_window(d, now_ms, out, n, &len);

    d->total++;
    const bool lost = !(f->retried && f->retry_ok);
    if (lost) {
        d->retry_failed++;
    }

    if (d->printed_in_window < OLED_I2C_DIAG_LINES_PER_WINDOW) {
        d->printed_in_window++;
        append(out, n, &len, "oled_i2c: %s write failed #%lu (%s, flags=0x%02lx, sda=%u scl=%u, len=%u, ",
               f->kind == OLED_I2C_KIND_CMD ? "cmd" : "data", (unsigned long)d->total, oled_i2c_class_name(f->cls),
               (unsigned long)f->flags, (unsigned)f->sda, (unsigned)f->scl, (unsigned)f->len);
        if (d->have_ok) {
            append(out, n, &len, "%lu ms after the last good write)", (unsigned long)(now_ms - d->last_ok_ms));
        } else {
            append(out, n, &len, "no good write since boot)");
        }
        if (!f->retried) {
            append(out, n, &len, " - not retried, display not responding\n");
        } else if (f->retry_ok) {
            append(out, n, &len, " - retry ok\n");
        } else {
            append(out, n, &len, " - retry failed (%s)\n", oled_i2c_class_name(f->retry_cls));
        }
    } else {
        d->suppressed++;
    }

    if (!lost) {
        landed(d, now_ms, out, n, &len);
        return len;
    }

    if (d->streak < UINT16_MAX) {
        d->streak++;
    }
    if (!d->stuck && d->streak >= OLED_I2C_DIAG_STUCK_STREAK) {
        d->stuck = true;
        append(out, n, &len, "oled_i2c: status display not responding: %u writes in a row failed after a retry\n",
               (unsigned)d->streak);
    }
    return len;
}
