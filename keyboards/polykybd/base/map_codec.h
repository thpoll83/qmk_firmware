// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdint.h>

// Pure bit codec for the packed overlay-mapping format (HID cmd 21 fixed 10-bit,
// cmd 33 host-chosen width, and the slave-repair bridge frames): consecutive
// `width`-bit values packed LSB-first into a byte buffer.
//
// Extracted from fill_overlay.c so the encoder and decoder are ONE pair in one
// place and unit-testable off-hardware (make test:polykybd_map_codec). Both of
// this codec's historical bugs were in exactly this arithmetic, and neither was
// observable from the rig — cmd 33 is silent by design, so the HIL test can only
// assert liveness, never the decoded values:
//   - an unconditional second-byte read ran past the last data byte at width 8
//     (every value is one whole byte there, gcd(8,8) == 8);
//   - a fixed `0xff >> (8 - n)` mask shifted by a negative count at the offsets
//     only the odd widths 9/11 reach (gcd 1 walks all eight bit offsets).
//
// ⚠️ How many bytes a value spans depends on the width, so BOTH the second and
// third byte accesses are conditional — a read must never touch past the last
// data byte a value actually occupies, and the writer mirrors the reader exactly
// so it can never dirty a byte the reader would not consume. The unit tests pin
// this with guard bytes; verify any change there, not by eye.
//
// Widths up to 16 bits are representable (16 + 7 offset bits = 23 < 24); the
// protocol bounds live in config.h (OVERLAY_MAP_WIDTH_MIN/MAX) and stay with the
// command handlers — this codec is just the arithmetic.

// Reads value `idx` of `width` bits from `buf`.
static inline uint16_t map_codec_read(const uint8_t* buf, uint16_t idx, uint8_t width) {
    // start_bit must be wide enough to hold idx*width (up to ~488 bits for a
    // 61-byte report) — a uint8_t would wrap.
    uint16_t start_bit  = (uint16_t)(idx * (uint16_t)width);
    uint16_t start_byte = start_bit / 8;
    uint8_t  s          = start_bit % 8;
    uint32_t acc        = (uint32_t)buf[start_byte];
    if (s + width > 8) {
        acc |= (uint32_t)buf[start_byte + 1] << 8;
    }
    if (s + width > 16) {
        acc |= (uint32_t)buf[start_byte + 2] << 16;
    }
    return (uint16_t)((acc >> s) & ((1u << width) - 1u));
}

// Writes `v` as value `idx` of `width` bits into `buf`. `buf` must be zeroed
// first — this ORs in, so the caller can pack values in any order.
static inline void map_codec_write(uint8_t* buf, uint16_t idx, uint16_t v, uint8_t width) {
    uint16_t start_bit  = (uint16_t)(idx * (uint16_t)width);
    uint16_t start_byte = start_bit / 8;
    uint8_t  s          = start_bit % 8;
    uint32_t shifted    = (uint32_t)(v & ((1u << width) - 1u)) << s;
    buf[start_byte] |= (uint8_t)(shifted & 0xff);
    if (s + width > 8) {
        buf[start_byte + 1] |= (uint8_t)((shifted >> 8) & 0xff);
    }
    if (s + width > 16) {
        buf[start_byte + 2] |= (uint8_t)((shifted >> 16) & 0xff);
    }
}
