// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Tests for base/glyph_place_op.h, the placement op byte of HID cmd 30 (v26).

#include "gtest/gtest.h"

extern "C" {
#include "glyph_place_op.h"
}

namespace {

constexpr uint8_t kCount = 2;   // GLYPH_PLACE_COUNT: centre, top-left

TEST(GlyphPlaceOpTest, ZeroKeepsThePlacementSoAZeroPaddedReportChangesNothing) {
    uint8_t place = 0x55;
    EXPECT_EQ(glyph_place_op_decode(0, kCount, &place), GLYPH_PLACE_OP_IS_KEEP);
    EXPECT_EQ(place, 0x55);   // untouched
}

TEST(GlyphPlaceOpTest, OneIsCentreAndTwoIsTopLeft) {
    uint8_t place = 0x55;
    EXPECT_EQ(glyph_place_op_decode(GLYPH_PLACE_OP_CENTRE, kCount, &place), GLYPH_PLACE_OP_IS_SET);
    EXPECT_EQ(place, 0);
    EXPECT_EQ(glyph_place_op_decode(GLYPH_PLACE_OP_TOP_LEFT, kCount, &place), GLYPH_PLACE_OP_IS_SET);
    EXPECT_EQ(place, 1);
}

TEST(GlyphPlaceOpTest, EveryOpPastTheLastPlacementIsRefused) {
    for (int op = kCount + 1; op <= 0xFF; ++op) {
        uint8_t place = 0x55;
        EXPECT_EQ(glyph_place_op_decode((uint8_t)op, kCount, &place), GLYPH_PLACE_OP_IS_INVALID) << op;
        EXPECT_EQ(place, 0x55) << op;
    }
}

}  // namespace
