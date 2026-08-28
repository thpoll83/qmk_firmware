// Copyright 2026 Thomas Pollak
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Tests for base/map_codec.h — the packed overlay-mapping bit codec (HID cmd 21
// fixed 10-bit, cmd 33 host-chosen width, the slave-repair frames).
//
// Both of this codec's shipped bugs were byte-span mistakes the rig cannot see
// (cmd 33 is silent, so the HIL test is only a liveness guard):
//   - an unconditional second-byte read past the last data byte at width 8;
//   - a fixed `0xff >> (8 - n)` mask that shifted by a negative count at the
//     offsets only the odd widths reach.
// The guard-byte tests below pin exactly those two shapes; the reference-reader
// comparison pins the layout itself.

#include "gtest/gtest.h"

extern "C" {
#include "map_codec.h"
}

#include <cstring>
#include <vector>

namespace {

constexpr uint8_t kWidthMin = 8;   // OVERLAY_MAP_WIDTH_MIN (config.h)
constexpr uint8_t kWidthMax = 16;  // OVERLAY_MAP_WIDTH_MAX (config.h)

// Slow, obviously-correct reference: read one bit at a time.
uint16_t reference_read(const uint8_t* buf, uint16_t idx, uint8_t width) {
    uint32_t v = 0;
    for (uint8_t bit = 0; bit < width; ++bit) {
        uint32_t abs_bit = (uint32_t)idx * width + bit;
        uint8_t  byte    = buf[abs_bit / 8];
        if (byte & (1u << (abs_bit % 8))) {
            v |= 1u << bit;
        }
    }
    return (uint16_t)v;
}

// How many bytes value `idx` at `width` actually occupies (its span).
size_t span_end_byte(uint16_t idx, uint8_t width) {
    uint32_t first_bit = (uint32_t)idx * width;
    uint32_t last_bit  = first_bit + width - 1;
    return (size_t)(last_bit / 8);
}

TEST(MapCodecTest, MatchesTheReferenceReaderAtEveryWidthAndOffset) {
    // A deterministic pseudo-random buffer, decoded value by value at every
    // supported width — this walks every bit offset a width can produce.
    uint8_t buf[64];
    uint32_t seed = 0x1234567u;
    for (auto& b : buf) {
        seed = seed * 1664525u + 1013904223u;
        b    = (uint8_t)(seed >> 24);
    }
    for (uint8_t width = kWidthMin; width <= kWidthMax; ++width) {
        uint16_t values = (uint16_t)(sizeof(buf) * 8 / width);
        for (uint16_t idx = 0; idx < values; ++idx) {
            EXPECT_EQ(map_codec_read(buf, idx, width), reference_read(buf, idx, width))
                << "width " << (int)width << " idx " << idx;
        }
    }
}

TEST(MapCodecTest, WriteThenReadRoundTripsEveryWidth) {
    for (uint8_t width = kWidthMin; width <= kWidthMax; ++width) {
        uint8_t  buf[64] = {0};
        uint16_t values  = (uint16_t)(sizeof(buf) * 8 / width);
        uint16_t mask    = (uint16_t)((1u << width) - 1u);
        for (uint16_t idx = 0; idx < values; ++idx) {
            // A value pattern that differs per slot and exercises high bits.
            uint16_t v = (uint16_t)((idx * 2654435761u) & mask);
            map_codec_write(buf, idx, v, width);
        }
        for (uint16_t idx = 0; idx < values; ++idx) {
            uint16_t v = (uint16_t)((idx * 2654435761u) & mask);
            EXPECT_EQ(map_codec_read(buf, idx, width), v)
                << "width " << (int)width << " idx " << idx;
        }
    }
}

TEST(MapCodecTest, ReadNeverConsultsBytesPastTheValuesSpan) {
    // Regression pin for the width-8 OOB read: decode the same value from two
    // buffers that differ ONLY in the bytes past the value's span. If the result
    // differs, the codec read a byte it must not.
    for (uint8_t width = kWidthMin; width <= kWidthMax; ++width) {
        uint8_t a[64], b[64];
        uint32_t seed = 0xdeadbeefu;
        for (auto& x : a) {
            seed = seed * 1664525u + 1013904223u;
            x    = (uint8_t)(seed >> 24);
        }
        uint16_t values = (uint16_t)((sizeof(a) - 3) * 8 / width);
        for (uint16_t idx = 0; idx < values; ++idx) {
            std::memcpy(b, a, sizeof(a));
            size_t end = span_end_byte(idx, width);
            for (size_t j = end + 1; j < sizeof(b); ++j) {
                b[j] ^= 0xFF;  // poison everything past the span
            }
            EXPECT_EQ(map_codec_read(a, idx, width), map_codec_read(b, idx, width))
                << "width " << (int)width << " idx " << idx
                << " read past its span (the historical width-8 bug shape)";
        }
    }
}

TEST(MapCodecTest, WriteNeverDirtiesBytesPastTheValuesSpan) {
    for (uint8_t width = kWidthMin; width <= kWidthMax; ++width) {
        uint16_t mask   = (uint16_t)((1u << width) - 1u);
        uint16_t values = (uint16_t)((61 * 8) / width);  // one report's worth
        for (uint16_t idx = 0; idx < values; ++idx) {
            uint8_t buf[64] = {0};
            map_codec_write(buf, idx, mask, width);  // all-ones value
            size_t end = span_end_byte(idx, width);
            for (size_t j = end + 1; j < sizeof(buf); ++j) {
                EXPECT_EQ(buf[j], 0) << "width " << (int)width << " idx " << idx
                                     << " dirtied byte " << j << " past its span";
            }
        }
    }
}

TEST(MapCodecTest, OddWidthsReachAThirdByteAndDecodeCorrectly) {
    // The negative-shift bug lived at the offsets only gcd(width,8)==1 widths
    // reach: a 9/11-bit value starting at bit offset 7 spans three bytes. Place
    // an all-ones value there explicitly and read it back.
    for (uint8_t width : {9, 11, 13, 15}) {
        uint16_t mask = (uint16_t)((1u << width) - 1u);
        // idx 7 at width 9 starts at bit 63 → offset 7; pick the first idx whose
        // offset is 7 for each width.
        for (uint16_t idx = 0; idx < 8; ++idx) {
            if (((uint32_t)idx * width) % 8 != 7) continue;
            uint8_t buf[64] = {0};
            map_codec_write(buf, idx, mask, width);
            EXPECT_EQ(map_codec_read(buf, idx, width), mask)
                << "width " << (int)width << " idx " << idx << " (3-byte span)";
            break;
        }
    }
}

TEST(MapCodecTest, WidthEightIsWholeBytes) {
    // At width 8 every value is exactly one byte at offset 0 — the identity
    // layout the host relies on for its narrowest partition.
    uint8_t buf[8] = {0x00, 0x11, 0x80, 0xFF, 0x5A, 0xA5, 0x01, 0xFE};
    for (uint16_t idx = 0; idx < 8; ++idx) {
        EXPECT_EQ(map_codec_read(buf, idx, 8), buf[idx]);
    }
}

TEST(MapCodecTest, TenBitPairsMatchTheCmd21Layout) {
    // The fixed 10-bit layout of cmd 21: values at bit offsets {0,2,4,6}. A
    // hand-computed vector so a layout change (endianness, bit order) cannot
    // slip through the self-consistent round-trip tests above.
    uint8_t buf[5] = {0};
    map_codec_write(buf, 0, 0x155, 10);  // 01_0101_0101
    map_codec_write(buf, 1, 0x2AA, 10);  // 10_1010_1010
    // value0 bits 0..9  -> buf[0]=0x55, buf[1] low 2 bits = 01
    // value1 bits 10..19 -> buf[1] bits 2..7 = 101010(10), buf[2] bits 0..3 = 1010
    EXPECT_EQ(buf[0], 0x55);
    EXPECT_EQ(buf[1], (0xAA << 2 | 0x1) & 0xFF);
    EXPECT_EQ(buf[2], 0x0A);
    EXPECT_EQ(map_codec_read(buf, 0, 10), 0x155);
    EXPECT_EQ(map_codec_read(buf, 1, 10), 0x2AA);
}

}  // namespace
