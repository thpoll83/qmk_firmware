// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Tests for base/bitset.h, the packed bit array the focus ring and the menu
// cascade keep their per-panel state in. What matters is that a bit lands in
// byte i/8 at bit i%8, that put() touches exactly one bit in either direction,
// and that BITSET_BYTES rounds up.

#include "gtest/gtest.h"

extern "C" {
#include "bitset.h"
}

#include <cstring>

TEST(Bitset, BytesRoundsUp) {
    EXPECT_EQ(BITSET_BYTES(0), 0u);
    EXPECT_EQ(BITSET_BYTES(1), 1u);
    EXPECT_EQ(BITSET_BYTES(8), 1u);
    EXPECT_EQ(BITSET_BYTES(9), 2u);
    EXPECT_EQ(BITSET_BYTES(40), 5u);
}

TEST(Bitset, LayoutIsByteThenBit) {
    uint8_t m[BITSET_BYTES(40)] = {0};
    bitset_put(m, 0, true);
    bitset_put(m, 9, true);
    bitset_put(m, 39, true);
    EXPECT_EQ(m[0], 0x01);
    EXPECT_EQ(m[1], 0x02);
    EXPECT_EQ(m[4], 0x80);
    EXPECT_EQ(m[2] | m[3], 0);
}

TEST(Bitset, PutTouchesOnlyItsBit) {
    for (uint8_t i = 0; i < 40; ++i) {
        uint8_t set[5], clear[5];
        memset(set, 0x00, sizeof(set));
        memset(clear, 0xFF, sizeof(clear));
        bitset_put(set, i, true);
        bitset_put(clear, i, false);
        for (uint8_t j = 0; j < 40; ++j) {
            EXPECT_EQ(bitset_get(set, j), j == i) << "set i=" << int(i) << " j=" << int(j);
            EXPECT_EQ(bitset_get(clear, j), j != i) << "clear i=" << int(i) << " j=" << int(j);
        }
    }
}

TEST(Bitset, PutIsIdempotent) {
    uint8_t m[1] = {0};
    bitset_put(m, 3, true);
    bitset_put(m, 3, true);
    EXPECT_EQ(m[0], 0x08);
    bitset_put(m, 3, false);
    bitset_put(m, 3, false);
    EXPECT_EQ(m[0], 0x00);
}
