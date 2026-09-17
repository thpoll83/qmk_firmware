// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdint.h>
// static_assert: a C11 macro from <assert.h>, a keyword in C++. Spelled the
// portable way rather than _Static_assert because this header is compiled by the
// host test suite as C++ (base/tests/idle_timeout_tests.cpp).
#include <assert.h>

// The idle TIMEOUT presets (HID cmd 40, protocol v18+): how long the keyboard sits
// without a key event before it starts fading into the idle style. This replaces
// what was the compile-time FADE_OUT_TIME, which every board had at 2 minutes.
//
// Header-only and PURE — no quantum.h, no config.h, no display — so the EEPROM
// encoding below is unit-testable on the host (make test:polykybd_idle_timeout).
// That is the half with a bug history in this repo: an unwritten EEPROM byte reads
// back as ZERO, not 0xFF, and getting that wrong has twice shipped a board that
// silently ran someone else's settings (latin_assign's "every key hosts 'a'", and
// the idle_style default that needed a whole sentinel byte to undo).
//
// The keyboard-config half — that every preset still leaves the fade room before
// TURN_OFF_TIME — cannot live here, because those deadlines are in config.h. It is
// a _Static_assert in state.c, over this same list.

// A CLOSED preset set, not a free-form millisecond value, and not an open range.
// Closed for the same reason GLYPH_SIZE is and GLYPH_SCRIPT is not (PROTOCOL_HISTORY
// on v13): an unknown SCRIPT falls through to the normal legend and costs nothing,
// while an unknown TIMEOUT would be stored, persisted and then silently resolved to
// some other duration. Presets rather than raw ms because the only consumer is a
// menu of six entries, and a free-form value would have to be range-checked against
// TURN_OFF_TIME on every set instead of once, at build time.
//
// Values are append-only: persisted in poly_eeconf_t.idle_timeout and on the wire.
enum poly_idle_timeout {
    IDLE_TIMEOUT_15S  = 0,
    IDLE_TIMEOUT_30S  = 1,
    IDLE_TIMEOUT_45S  = 2,
    IDLE_TIMEOUT_1MIN = 3,
    IDLE_TIMEOUT_2MIN = 4,   // what FADE_OUT_TIME always was — the default
    IDLE_TIMEOUT_5MIN = 5,
    IDLE_TIMEOUT_COUNT
};

// ONE source for the durations: the lookup below, the ceiling assert in state.c and
// the count check all expand from this list, so a preset cannot be added to one and
// missed by the others.
#define POLY_IDLE_TIMEOUT_LIST(X)  \
    X(IDLE_TIMEOUT_15S,   15000u)  \
    X(IDLE_TIMEOUT_30S,   30000u)  \
    X(IDLE_TIMEOUT_45S,   45000u)  \
    X(IDLE_TIMEOUT_1MIN,  60000u)  \
    X(IDLE_TIMEOUT_2MIN, 120000u)  \
    X(IDLE_TIMEOUT_5MIN, 300000u)

// The timeout a board comes up with when nothing has been chosen: the 2 minutes
// every board had before this setting existed, so an existing keyboard behaves
// exactly as it did until someone picks something else.
#define POLY_DEFAULT_IDLE_TIMEOUT    IDLE_TIMEOUT_2MIN
#define POLY_DEFAULT_IDLE_TIMEOUT_MS 120000u

#define POLY_IDLE_TIMEOUT_ONE(name, ms) + 1
static_assert((0 POLY_IDLE_TIMEOUT_LIST(POLY_IDLE_TIMEOUT_ONE)) == IDLE_TIMEOUT_COUNT,
              "enum poly_idle_timeout and POLY_IDLE_TIMEOUT_LIST have drifted");
#undef POLY_IDLE_TIMEOUT_ONE

// Milliseconds for a preset. Out of range answers the DEFAULT's duration rather
// than 0: every caller here is a deadline, and a zero deadline would idle the board
// on the pass it was read. A switch rather than a table keeps that property
// structural — a designated-initialiser array would leave a silent zero hole for a
// preset added to the enum and missed in the list.
static inline uint32_t idle_timeout_ms_of(uint8_t value) {
    switch (value) {
#define POLY_IDLE_TIMEOUT_CASE(name, ms) case name: return (ms);
        POLY_IDLE_TIMEOUT_LIST(POLY_IDLE_TIMEOUT_CASE)
#undef POLY_IDLE_TIMEOUT_CASE
        default: break;
    }
    return POLY_DEFAULT_IDLE_TIMEOUT_MS;
}


// ---- The EEPROM encoding ---------------------------------------------------
//
// poly_eeconf_t.idle_timeout stores the enum BIASED BY ONE, so that zero — which is
// what an unwritten byte reads back as — means "never chosen".
//
// ⚠️ This deliberately replaces a sentinel byte of the idle_style_fmt kind. That
// byte had to exist because IDLE_STYLE_PULSE is 0 and QMK's wear levelling hands
// back a cleared byte as ZERO, so "chose pulse" and "never chose" were the same
// byte and no scheme could separate them afterwards. Biasing removes the collision
// at the source instead of detecting it: it costs one byte instead of two, and —
// unlike idle_style — a FUTURE change of POLY_DEFAULT_IDLE_TIMEOUT cannot overwrite
// a real choice, because a real choice was never stored as zero.
#define IDLE_TIMEOUT_UNSET 0

static inline uint8_t idle_timeout_pack(uint8_t value) {
    return (uint8_t)(value + 1u);
}

// An erased 0xFF, a byte written by a build that predates the field (0), and any
// other nonsense all land in the same arm: the board default.
static inline uint8_t idle_timeout_unpack(uint8_t stored) {
    return (stored == IDLE_TIMEOUT_UNSET || stored > IDLE_TIMEOUT_COUNT)
               ? (uint8_t)POLY_DEFAULT_IDLE_TIMEOUT
               : (uint8_t)(stored - 1u);
}
