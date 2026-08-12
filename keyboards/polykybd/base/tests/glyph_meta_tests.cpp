// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#include "gtest/gtest.h"

extern "C" {
#include "glyph_meta.h"
}

#include <vector>

namespace {

// A synthetic column-native glyph so the decode is checked against a bitmap whose
// intended shape is written out by hand rather than taken from a generated font.
//
// 3 wide x 10 tall => col_bytes 2, so each column is two page-bytes and the glyph
// straddles a page boundary at y==8 — which is the case a row-major reader gets
// wrong silently.
//
//   x:      0 1 2
//   y=0     # . #
//   y=1     . # .
//   y=7     # . .        <- last row of page 0
//   y=8     . . #        <- first row of page 1
//   y=9     # # #
constexpr uint8_t kBitmapPad = 4;   // a non-zero bitmapOffset, as in a real font
uint8_t           g_bitmap[kBitmapPad + 3 * 2];

void build_bitmap() {
    for (auto &b : g_bitmap) b = 0;
    auto set = [](uint8_t x, uint8_t y) {
        g_bitmap[kBitmapPad + x * 2 + (y >> 3)] |= static_cast<uint8_t>(1u << (y & 7));
    };
    set(0, 0); set(2, 0);
    set(1, 1);
    set(0, 7);
    set(2, 8);
    set(0, 9); set(1, 9); set(2, 9);
}

bool expected_lit(uint16_t x, uint16_t y) {
    if (y == 0) return x == 0 || x == 2;
    if (y == 1) return x == 1;
    if (y == 7) return x == 0;
    if (y == 8) return x == 2;
    if (y == 9) return true;
    return false;
}

// Negative offsets are the norm, not an edge case: every text glyph sits above the
// baseline (yOffset < 0) and the icons are -15/-16.
constexpr GFXglyph kGlyph = {
    /* bitmapOffset */ kBitmapPad,
    /* width        */ 3,
    /* height       */ 10,
    /* xAdvance     */ 5,
    /* xOffset      */ -2,
    /* yOffset      */ -8,
};

class GlyphMeta : public ::testing::Test {
  protected:
    void SetUp() override { build_bitmap(); }
};

TEST_F(GlyphMeta, UnsignedMetricsReadBack) {
    EXPECT_EQ(glyph_width(&kGlyph), 3);
    EXPECT_EQ(glyph_height(&kGlyph), 10);
    EXPECT_EQ(glyph_x_advance(&kGlyph), 5);
    EXPECT_EQ(glyph_bitmap_offset(&kGlyph), kBitmapPad);
}

// The regression this header exists for. Reading these through the raw
// pgm_read_byte() zero-extends: yOffset -8 becomes 248, and the glyph plots off the
// bottom of the panel where the plotter silently clips it. That shipped once in
// split42's portrait status OLED (PR #149) while disp_array.c's copy was correct —
// two copies of the same decode, one of them wrong.
TEST_F(GlyphMeta, OffsetsAreSignedNotZeroExtended) {
    EXPECT_EQ(glyph_x_offset(&kGlyph), -2);
    EXPECT_EQ(glyph_y_offset(&kGlyph), -8);
    // Spelled out, because "it compiles" is not the same as "it sign-extends":
    EXPECT_LT(static_cast<int>(glyph_y_offset(&kGlyph)), 0);
    EXPECT_NE(static_cast<int>(glyph_y_offset(&kGlyph)), 248);
}

TEST_F(GlyphMeta, ColBytesRoundsUpToWholePages) {
    EXPECT_EQ(glyph_col_bytes(0), 0);
    EXPECT_EQ(glyph_col_bytes(1), 1);
    EXPECT_EQ(glyph_col_bytes(8), 1);
    EXPECT_EQ(glyph_col_bytes(9), 2);
    EXPECT_EQ(glyph_col_bytes(10), 2);
    EXPECT_EQ(glyph_col_bytes(16), 2);
    EXPECT_EQ(glyph_col_bytes(40), 5);    // the 72x40 keycap panel
    EXPECT_EQ(glyph_col_bytes(255), 32);  // no overflow at the type's limit
}

// glyph_col_bytes takes uint8_t where the open-coded version shifted a signed
// int16_t, so the compiler emits a logical rather than arithmetic shift. The two
// agree over the whole reachable domain — a height is a pgm_read_byte, i.e. 0..255 —
// and this pins that rather than leaving it as an assertion in a commit message.
TEST_F(GlyphMeta, ColBytesMatchesTheOldSignedExpressionOverTheWholeDomain) {
    for (int h = 0; h <= 255; ++h) {
        const uint8_t was = static_cast<uint8_t>((static_cast<int16_t>(h) + 7) >> 3);
        EXPECT_EQ(glyph_col_bytes(static_cast<uint8_t>(h)), was) << "height " << h;
    }
}

TEST_F(GlyphMeta, ColumnNativeDecodeMatchesTheDrawnShape) {
    const uint8_t cb = glyph_col_bytes(glyph_height(&kGlyph));
    ASSERT_EQ(cb, 2);
    for (uint16_t y = 0; y < glyph_height(&kGlyph); ++y) {
        for (uint16_t x = 0; x < glyph_width(&kGlyph); ++x) {
            EXPECT_EQ(glyph_pixel_lit(g_bitmap, kBitmapPad, cb, x, y), expected_lit(x, y))
                << "at (" << x << "," << y << ")";
        }
    }
}

// The row-invariant form is what the blit loops actually use (folding it into the
// per-pixel call cost three functions 18/8/7 instructions when the compiler stopped
// hoisting). It must agree with the direct form everywhere.
TEST_F(GlyphMeta, RowFormAgreesWithTheDirectForm) {
    const uint8_t cb = glyph_col_bytes(glyph_height(&kGlyph));
    for (uint16_t y = 0; y < glyph_height(&kGlyph); ++y) {
        const uint16_t base = glyph_row_base(kBitmapPad, y);
        const uint8_t  mask = glyph_row_mask(y);
        for (uint16_t x = 0; x < glyph_width(&kGlyph); ++x) {
            EXPECT_EQ(glyph_row_pixel_lit(g_bitmap, base, mask, cb, x),
                      glyph_pixel_lit(g_bitmap, kBitmapPad, cb, x, y))
                << "at (" << x << "," << y << ")";
        }
    }
}

// A row-major reader of the same bytes produces a different picture. This is the
// failure the format comment warns about — it does not crash or mis-size, it just
// draws noise — so it is worth having a test that says the two are NOT the same.
TEST_F(GlyphMeta, RowMajorReadIsNotEquivalent) {
    const uint8_t cb = glyph_col_bytes(glyph_height(&kGlyph));
    const uint8_t w  = glyph_width(&kGlyph);
    int disagreements = 0;
    for (uint16_t y = 0; y < glyph_height(&kGlyph); ++y) {
        for (uint16_t x = 0; x < w; ++x) {
            const uint16_t bit      = static_cast<uint16_t>(y * w + x);
            const bool     rowmajor = (g_bitmap[kBitmapPad + (bit >> 3)] & (0x80u >> (bit & 7))) != 0;
            if (rowmajor != glyph_pixel_lit(g_bitmap, kBitmapPad, cb, x, y)) ++disagreements;
        }
    }
    EXPECT_GT(disagreements, 0)
        << "column-native and row-major agreed everywhere — the fixture is too symmetric to "
           "distinguish them, which makes the other tests weaker than they look";
}

}  // namespace
