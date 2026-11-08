// Copyright 2026 Thomas Pollak
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The tap-hold timing settings (HID cmd 44, protocol v28): the tapping term and
// the Flow Tap term, their allowed ranges, and their EEPROM encoding.
//
// Header-only and pure (no config.h, no quantum.h), so the ranges and the encoding
// are unit-tested on the host (make test:polykybd_tap_terms). The live values and
// the QMK hooks that return them are in state.c and poly_keymap.c.
//
// Both ranges are CLOSED: the HID command NACKs a value outside them, because an
// accepted value is persisted and then used for every tap-hold key.
//   tapping term   100..500 ms, in 10 ms steps
//   Flow Tap term    0..300 ms, in 10 ms steps (0 turns Flow Tap off)
// QMK clamps a Flow Tap term to 500 ms itself, so 300 is well inside it.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define TAP_TERM_STEP_MS     10u
#define TAPPING_TERM_MIN_MS  100u
#define TAPPING_TERM_MAX_MS  500u
#define FLOW_TAP_TERM_MIN_MS 0u
#define FLOW_TAP_TERM_MAX_MS 300u

// The values every board had before the setting existed (the former compile-time
// TAPPING_TERM and FLOW_TAP_TERM in split72/config.h).
#define POLY_DEFAULT_TAPPING_TERM_MS  200u
#define POLY_DEFAULT_FLOW_TAP_TERM_MS 150u

// Cmd 44 field value meaning "leave this term unchanged".
#define TAP_TERM_KEEP 0xFFFFu

static inline bool tap_term_in_range(uint16_t ms, uint16_t min_ms, uint16_t max_ms) {
    return ms >= min_ms && ms <= max_ms && (ms % TAP_TERM_STEP_MS) == 0u;
}

static inline bool tapping_term_valid(uint16_t ms) {
    return tap_term_in_range(ms, TAPPING_TERM_MIN_MS, TAPPING_TERM_MAX_MS);
}

static inline bool flow_tap_term_valid(uint16_t ms) {
    return tap_term_in_range(ms, FLOW_TAP_TERM_MIN_MS, FLOW_TAP_TERM_MAX_MS);
}

// ---- The EEPROM encoding ---------------------------------------------------
//
// Each term is one byte: ms / 10, BIASED BY ONE, so that zero means "never chosen".
// An unwritten EEPROM byte reads back as 0 after wear levelling, and a Flow Tap
// term of 0 ms (off) is a real choice, so an unbiased byte could not tell them
// apart. Same scheme as poly_eeconf_t.idle_timeout (base/idle_timeout.h).
#define TAP_TERM_UNSET 0u

static inline uint8_t tap_term_pack(uint16_t ms) {
    return (uint8_t)(ms / TAP_TERM_STEP_MS + 1u);
}

// Unset (0), an erased 0xFF and any byte that decodes outside the range all give
// the default.
static inline uint16_t tap_term_unpack(uint8_t stored, uint16_t min_ms, uint16_t max_ms,
                                       uint16_t default_ms) {
    if (stored == TAP_TERM_UNSET) {
        return default_ms;
    }
    const uint16_t ms = (uint16_t)((stored - 1u) * TAP_TERM_STEP_MS);
    return tap_term_in_range(ms, min_ms, max_ms) ? ms : default_ms;
}

static inline uint16_t tapping_term_unpack(uint8_t stored) {
    return tap_term_unpack(stored, TAPPING_TERM_MIN_MS, TAPPING_TERM_MAX_MS,
                           POLY_DEFAULT_TAPPING_TERM_MS);
}

static inline uint16_t flow_tap_term_unpack(uint8_t stored) {
    return tap_term_unpack(stored, FLOW_TAP_TERM_MIN_MS, FLOW_TAP_TERM_MAX_MS,
                           POLY_DEFAULT_FLOW_TAP_TERM_MS);
}

// QMK requires QUICK_TAP_TERM <= TAPPING_TERM. The quick-tap term stays the
// compile-time value, so a tapping term set below it pulls it down.
static inline uint16_t quick_tap_term_for(uint16_t quick_tap_ms, uint16_t tapping_ms) {
    return quick_tap_ms < tapping_ms ? quick_tap_ms : tapping_ms;
}
