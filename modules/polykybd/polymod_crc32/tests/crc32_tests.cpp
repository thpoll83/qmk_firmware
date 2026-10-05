// Copyright 2026 Thomas Pollak
// SPDX-License-Identifier: GPL-2.0-or-later
//
// crc32_large() is the one CRC every staged artifact is checked with (the firmware
// image, the font-pack bundles, the icon library, the DOOM pack). Its whole job is
// to be indistinguishable from a one-shot CRC over a length crc32_1byte() cannot
// take, so the tests compare it against a bitwise reference at the lengths where a
// chunking mistake would show: around the 0x8000 chunk and the 0xFFFF length limit.

#include "gtest/gtest.h"

extern "C" {
#include "polymod_crc32.h"
}

#include <cstdint>
#include <vector>

namespace {

// Bitwise reflected CRC-32 (zlib), no table: an independent reference.
uint32_t ref_crc32(const uint8_t *p, size_t n) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) {
        crc ^= p[i];
        for (int b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

std::vector<uint8_t> pattern(size_t n) {
    std::vector<uint8_t> v(n);
    uint32_t x = 0x12345678u;
    for (auto &b : v) {
        x = x * 1103515245u + 12345u;
        b = (uint8_t)(x >> 16);
    }
    return v;
}

}  // namespace

TEST(Crc32Large, CheckValue) {
    const char *s = "123456789";
    EXPECT_EQ(crc32_large(s, 9, 0), 0xCBF43926u);
    EXPECT_EQ(crc32_1byte(s, 9, 0), 0xCBF43926u);
}

TEST(Crc32Large, EmptyIsSeed) {
    EXPECT_EQ(crc32_large(nullptr, 0, 0), 0u);
    EXPECT_EQ(crc32_large(nullptr, 0, 0xDEADBEEFu), 0xDEADBEEFu);
}

TEST(Crc32Large, MatchesReferenceAcrossChunkBoundaries) {
    const auto buf = pattern(300000);
    for (size_t n : {1u, 0x7FFFu, 0x8000u, 0x8001u, 0xFFFFu, 0x10000u, 0x10001u,
                     0x18000u, 230000u, 300000u}) {
        EXPECT_EQ(crc32_large(buf.data(), (uint32_t)n, 0), ref_crc32(buf.data(), n)) << "n=" << n;
    }
}

TEST(Crc32Large, ChainsLikeOneShot) {
    // Feeding the result back as the seed continues the same CRC, which is what
    // makes the chunked form exact.
    const auto buf = pattern(200000);
    const uint32_t head = crc32_large(buf.data(), 70000, 0);
    EXPECT_EQ(crc32_large(buf.data() + 70000, 130000, head), ref_crc32(buf.data(), 200000));
}
