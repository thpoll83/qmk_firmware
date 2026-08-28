// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdint.h>
#include <stdbool.h>

// The one-byte "mode + known + value" EEPROM encoding shared by two persisted
// settings in state.c: bit7 = a mode flag, bit6 = "a real value is known",
// bits0-5 = the value (0..63). It existed as two hand-rolled copies
// (pack/load_auto_brightness and pack/load_os_state) whose bit masks had to
// agree by inspection; this header is the single implementation, unit-tested
// (make test:polykybd_mode_byte).
//
// The POLARITY of bit7 is deliberately the caller's choice, because it is what
// makes a zeroed byte the right default: QMK's wear levelling hands an unwritten
// EEPROM byte back as ZERO (not 0xFF — see the latin_assign trap in CLAUDE.md),
// so each setting picks which meaning of bit7 leaves 0 = "factory default"
// (auto-brightness stores "auto engaged", OS state stores "manual pin").
//
// The KNOWN bit is load-bearing, not decorative: a mode engaged before any real
// value arrives must not persist its placeholder as if real, or the next boot
// snaps to it (the FULL_BRIGHT-jump family of bugs). Callers keep their own
// range checks on the 6-bit value — this codec is just the byte layout.

#define MODE_BYTE_FLAG  0x80u
#define MODE_BYTE_KNOWN 0x40u
#define MODE_BYTE_VALUE 0x3Fu

static inline uint8_t mode_byte_pack(bool flag, bool known, uint8_t value) {
    uint8_t b = (uint8_t)(value & MODE_BYTE_VALUE);
    if (flag) b |= MODE_BYTE_FLAG;
    if (known) b |= MODE_BYTE_KNOWN;
    return b;
}

static inline bool mode_byte_flag(uint8_t packed) {
    return (packed & MODE_BYTE_FLAG) != 0;
}

static inline bool mode_byte_known(uint8_t packed) {
    return (packed & MODE_BYTE_KNOWN) != 0;
}

static inline uint8_t mode_byte_value(uint8_t packed) {
    return (uint8_t)(packed & MODE_BYTE_VALUE);
}
