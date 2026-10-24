// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdbool.h>
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

// --- No-legend pairs (protocol v25) -------------------------------------------
//
// A mapping `to` of `slots`..2*slots-1 is pool slot `to - slots`, drawn without
// the key legend (`slots` = NUM_OVERLAY_SLOTS). The decoder and the slave-repair
// packer both go through these two, so they cannot disagree.

// The wire value for pool slot `slot`.
static inline uint16_t map_codec_to_value(uint16_t slot, bool no_legend, uint16_t slots) {
    return no_legend ? (uint16_t)(slot + slots) : slot;
}

// Splits a decoded `to` into the pool slot (returned) and the no-legend flag.
// A value at or past 2*slots comes back unchanged and unflagged, so the caller's
// out-of-pool check still rejects it.
static inline uint16_t map_codec_split_to(uint16_t to, uint16_t slots, bool* no_legend) {
    *no_legend = to >= slots && to < (uint16_t)(2u * slots);
    return *no_legend ? (uint16_t)(to - slots) : to;
}

// --- display_to_pool[] entries (protocol v25) ---------------------------------
//
// Bit 15 of an entry is the no-legend flag. A pool slot needs 10 bits and the
// identity default 11, so the bit is free.
#define MAP_CODEC_ENTRY_NO_LEGEND 0x8000u

static inline uint16_t map_codec_entry_slot(uint16_t entry) {
    return (uint16_t)(entry & ~MAP_CODEC_ENTRY_NO_LEGEND);
}

static inline bool map_codec_entry_no_legend(uint16_t entry) {
    return (entry & MAP_CODEC_ENTRY_NO_LEGEND) != 0;
}

static inline uint16_t map_codec_entry_with_no_legend(uint16_t entry, bool no_legend) {
    return no_legend ? (uint16_t)(entry | MAP_CODEC_ENTRY_NO_LEGEND)
                     : map_codec_entry_slot(entry);
}
