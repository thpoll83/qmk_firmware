// SPDX-License-Identifier: GPL-2.0-or-later
//
// Bit-level run-length encoding used by the PolyKybd firmware to ship
// keycap overlay bitmaps from the host over HID and between split halves.
//
// Encoding (single byte per run):
//   byte & 0x80 == 0  -> the low 7 bits are the count of consecutive zero bits
//   byte & 0x80 != 0  -> the low 7 bits are the count of consecutive one  bits
// Maximum run length per byte is therefore 127 bits.

#pragma once

#include <stdint.h>

// Decompress an RLE byte stream into `dest`, packing bits MSB-first within
// each destination byte (bit 7 first), continuing from `bit_index` (allows
// resuming across fragments). Stops early if the next bit would land at or
// past `max` bytes — in that case the return value is `max * 8`. Otherwise
// returns the number of bits produced by this call.
//
// `dest` is the byte that contains the current bit position (callers
// typically pass `buffer + bit_index/8`). `max` is measured FROM `dest`: it
// is the number of writable bytes available at `dest`, so the write is bounded
// to `dest[0 .. max-1]` — NOT the size of the whole buffer. Because the first
// bit is written at sub-byte offset `bit_index % 8` within `dest[0]`, this call
// can emit at most `max*8 - (bit_index % 8)` bits; the guard folds that start
// offset in so a non-byte-aligned resume can't spill one byte into `dest[max]`.
// Callers keep `dest`, `bit_index` and `max` on the same origin (all offset by
// `bit_index/8` together), which is the precondition this bound relies on.
// `compressed` is the input run stream of `len` bytes. `compressed` is
// `volatile`-qualified because the PolyKybd core1 worker reads from a buffer
// shared with core0.
uint16_t rle_decompress(uint8_t* dest, uint16_t max, volatile const uint8_t* compressed, uint8_t len, uint16_t bit_index);

// Count how many output bits `len` bytes of the RLE stream would expand to,
// without writing anything. Returns `max * 8` if the count reaches that
// cap (same saturation rule as rle_decompress).
uint16_t rle_count(uint16_t max, const uint8_t* compressed, uint8_t len);
