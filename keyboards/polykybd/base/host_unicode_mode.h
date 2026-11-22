// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdbool.h>
#include <stdint.h>

// The unicode input mode values the host sends in HID cmd 20, and what each one
// sets on the keyboard.
//
// Header-only and PURE (no quantum.h, no config.h), so the decoding is unit-tested
// on the host (make test:polykybd_host_unicode_mode).
//
// The wire values are the host's InputMethod enum (polyhost/input/unicode_input.py),
// not QMK's enum unicode_input_modes: QMK numbers macOS 0 and Linux 1, the host
// numbers them the other way round. hid_com.c checks with _Static_assert that the
// POLY_QMK_UNICODE_* values below match QMK's enum.
//
// A CLOSED set: an unknown value is NACKed, as it always was. Value 5 is the host's
// InputMethod.Unknown, which it never sends.
//
// MacCompose (v38) is QMK's macOS mode with F20 held instead of Option while the
// hex digits are typed (see poly_unicode_key_mac() in hid_com.c). The host's
// keyboard event tap reads the digits between F20 down and F20 up and posts the
// character itself, so it works with every macOS input source, not only Unicode
// Hex Input. It is a RAM-only flag on top of the macOS mode: a stored mode is
// always plain macOS, so a keyboard moved to a Mac without the host types
// Option+hex again instead of bare hex digits.

enum host_unicode_wire {
    HOST_UNICODE_LINUX       = 0,
    HOST_UNICODE_MAC         = 1,
    HOST_UNICODE_WINDOWS     = 2,
    HOST_UNICODE_WINCOMPOSE  = 3,
    HOST_UNICODE_BSD         = 4,
    // 5 is InputMethod.Unknown on the host: never sent, NACKed here.
    HOST_UNICODE_MAC_COMPOSE = 6,  // protocol v38+
};

// QMK's enum unicode_input_modes values (quantum/unicode/unicode.h).
#define POLY_QMK_UNICODE_MACOS      0
#define POLY_QMK_UNICODE_LINUX      1
#define POLY_QMK_UNICODE_WINDOWS    2
#define POLY_QMK_UNICODE_BSD        3
#define POLY_QMK_UNICODE_WINCOMPOSE 4

typedef struct {
    bool    ok;           // false: NACK, change nothing
    uint8_t qmk_mode;     // the QMK mode to apply (and to store when persisting)
    bool    mac_compose;  // hold F20 instead of Option in the macOS mode
} host_unicode_mode_t;

static inline host_unicode_mode_t host_unicode_mode_decode(uint8_t wire) {
    host_unicode_mode_t m = {false, 0, false};
    switch (wire) {
        case HOST_UNICODE_LINUX:       m.qmk_mode = POLY_QMK_UNICODE_LINUX;      break;
        case HOST_UNICODE_MAC:         m.qmk_mode = POLY_QMK_UNICODE_MACOS;      break;
        case HOST_UNICODE_WINDOWS:     m.qmk_mode = POLY_QMK_UNICODE_WINDOWS;    break;
        case HOST_UNICODE_WINCOMPOSE:  m.qmk_mode = POLY_QMK_UNICODE_WINCOMPOSE; break;
        case HOST_UNICODE_BSD:         m.qmk_mode = POLY_QMK_UNICODE_BSD;        break;
        case HOST_UNICODE_MAC_COMPOSE:
            m.qmk_mode    = POLY_QMK_UNICODE_MACOS;
            m.mac_compose = true;
            break;
        default:
            return m;
    }
    m.ok = true;
    return m;
}
