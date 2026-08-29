// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Host-side tests for base/font_lookup.c — the glyph resolver and the
// display-list-aware bounding-box interpreter (kdisp_gfx_text_bbox_in).
//
// The interpreter's whole bug history is measurement arithmetic that nothing
// could reach through the SPI driver: the '!' substitution measuring op
// ARGUMENTS as glyphs, MOVE (\x0E) skipping no arguments so a coordinate byte
// that is also an op byte latched a font for the rest of the run
// (HINT_SZ_STOPSQ is (15,15) = HALF HALF), the SMALL halving rounding a
// negative offset toward zero instead of flooring, and the MID fallback using
// one baseline reference for the whole run instead of per glyph. Every one of
// those is pinned here against synthetic fonts whose metrics are written out
// by hand.

#include "gtest/gtest.h"

extern "C" {
#include "font_lookup.h"
}

#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Synthetic fonts. Bitmap contents are irrelevant to the bbox (it reads only
// metrics), so every font shares one dummy bitmap byte.

uint8_t g_dummy_bitmap[1] = {0};

struct TestFont {
    std::vector<GFXglyph> glyphs;
    GFXfont               font;

    TestFont(uint32_t first, uint32_t last, int8_t yadv) {
        glyphs.resize(last - first + 1, GFXglyph{0, 0, 0, 0, 0, 0});
        font.bitmap   = g_dummy_bitmap;
        font.glyph    = glyphs.data();
        font.first    = first;
        font.last     = last;
        font.yAdvance = yadv;
    }
    void set(uint32_t cp, int8_t w, int8_t h, int8_t adv, int8_t xo, int8_t yo) {
        glyphs[cp - font.first] = GFXglyph{0, w, h, adv, xo, yo};
    }
};

// The caller's pool font (stands in for the resident keycap face, yAdvance 40).
//   space: zero-extent, advance-only — what makes whitespace-only report zeros.
//   '!':   deliberately DISTINCT metrics so the missing-glyph substitution is
//          visible in the box, not just presumed.
//   'a':   the ordinary glyph most expectations are computed from.
//   'b':   ODD negative offsets — the floor-vs-truncate discriminator for SMALL.
TestFont make_base() {
    TestFont f(0x20, 0x7A, 40);
    f.set(' ', 0, 0, 9, 0, 0);
    f.set('!', 2, 12, 4, 0, -12);
    for (uint32_t cp = 'a'; cp <= 'z'; ++cp) f.set(cp, 6, 10, 8, 1, -10);
    f.set('b', 5, 9, 7, 3, -9);
    // 'w'/'h'/'x'/'y' keep the ordinary 'a' metrics (set in the loop above);
    // the truncated-op tests measure them as glyphs.
    return f;
}

// A second, TALLER face in the pool (stands in for a pack font, yAdvance 54):
// drawn through a multi-font array its glyphs shift by 54-40 = +14, through a
// single-font array by 0 — the documented baseline-align rule.
TestFont make_tall() {
    TestFont f(0xE000, 0xE00F, 54);
    for (uint32_t cp = 0xE000; cp <= 0xE00F; ++cp) f.set(cp, 20, 30, 22, 2, -28);
    return f;
}

// The HINT_MID face (stands in for the 19px _Mid_ UI face, ASCII-only).
TestFont make_mid() {
    TestFont f(0x20, 0x7E, 26);
    f.set(' ', 0, 0, 5, 0, 0);
    for (uint32_t cp = '!'; cp <= 0x7E; ++cp) f.set(cp, 4, 7, 5, 0, -7);
    return f;
}

// A font with a GAP record mid-range (the generated headers' padding for
// non-contiguous ranges) plus a second font really covering the gapped cp.
TestFont make_gappy() {
    TestFont f(0x100, 0x10F, 40);
    for (uint32_t cp = 0x100; cp <= 0x10F; ++cp) f.set(cp, 3, 5, 4, 0, -5);
    f.set(0x105, 0, 0, 0, 0, 0);   // gap: w == h == xAdvance == 0
    return f;
}

TestFont make_gap_filler() {
    TestFont f(0x100, 0x10F, 44);
    f.set(0x105, 7, 8, 9, 1, -8);
    return f;
}

struct Box {
    int8_t xmin, xmax, ymin, ymax;
    bool   operator==(const Box &o) const {
        return xmin == o.xmin && xmax == o.xmax && ymin == o.ymin && ymax == o.ymax;
    }
};

std::ostream &operator<<(std::ostream &os, const Box &b) {
    return os << "x[" << int(b.xmin) << ".." << int(b.xmax) << "] y[" << int(b.ymin) << ".." << int(b.ymax) << "]";
}

class FontBboxTest : public ::testing::Test {
   protected:
    TestFont base_ = make_base();
    TestFont tall_ = make_tall();
    TestFont mid_  = make_mid();

    const GFXfont *pool_[2]  = {&base_.font, &tall_.font};
    const GFXfont *midarr_[1] = {&mid_.font};

    Box measure(std::initializer_list<uint32_t> cps, const GFXfont *const *mid = nullptr, uint8_t mid_count = 0) {
        std::vector<uint32_t> text(cps);
        text.push_back(0);
        Box b{};
        kdisp_gfx_text_bbox_in(pool_, 2, mid, mid_count, text.data(), &b.xmin, &b.xmax, &b.ymin, &b.ymax);
        return b;
    }
    Box measure_mid(std::initializer_list<uint32_t> cps) {
        return measure(cps, midarr_, 1);
    }
};

// ---------------------------------------------------------------------------
// Plain glyph arithmetic

TEST_F(FontBboxTest, SingleGlyphBoxMatchesItsMetrics) {
    // 'a': xOffset 1 + width 6, yOffset -10 + height 10, own-font yadj 0.
    EXPECT_EQ(measure({'a'}), (Box{1, 6, -10, -1}));
}

TEST_F(FontBboxTest, AdvanceAccumulatesAcrossGlyphs) {
    // second 'a' starts at x = 8 (the advance), so its right edge is 8+1+6-1.
    EXPECT_EQ(measure({'a', 'a'}), (Box{1, 14, -10, -1}));
}

TEST_F(FontBboxTest, EmptyAndWhitespaceOnlyReportAllZero) {
    EXPECT_EQ(measure({}), (Box{0, 0, 0, 0}));
    // space is advance-only (w == h == 0), so nothing sets the box.
    EXPECT_EQ(measure({' ', ' '}), (Box{0, 0, 0, 0}));
}

TEST_F(FontBboxTest, LeadingSpaceAdvancesWithoutInk) {
    EXPECT_EQ(measure({' ', 'a'}), (Box{10, 15, -10, -1}));
}

TEST_F(FontBboxTest, MissingGlyphIsMeasuredAsBang) {
    // 0x4000 is in no pool font; the fallback substitutes '!' from fonts[0],
    // whose metrics are deliberately distinct from every letter's.
    EXPECT_EQ(measure({0x4000}), measure({'!'}));
    EXPECT_EQ(measure({'!'}), (Box{0, 1, -12, -1}));
}

// ---------------------------------------------------------------------------
// The per-glyph yAdvance baseline shift (the language-flag regression rule)

TEST_F(FontBboxTest, TallerFontShiftsByYAdvanceDifferenceInAMultiFontPool) {
    // tall glyph: yadj = 54-40 = +14, so top = 14-28 = -14, bottom = -14+30-1.
    EXPECT_EQ(measure({0xE000}), (Box{2, 21, -14, 15}));
}

TEST_F(FontBboxTest, SingleFontArrayZeroesTheBaselineShift) {
    const GFXfont *only_tall[1] = {&tall_.font};
    const uint32_t text[]       = {0xE000, 0};
    Box            b{};
    kdisp_gfx_text_bbox_in(only_tall, 1, nullptr, 0, text, &b.xmin, &b.xmax, &b.ymin, &b.ymax);
    // own font is fonts[0], adjustment 0: top = -28, bottom = 1.
    EXPECT_EQ(b, (Box{2, 21, -28, 1}));
}

TEST_F(FontBboxTest, MixedRunUnionsBothFontsBoxes) {
    // 'a' then the tall glyph at x = 8: x right = 8+2+20-1 = 29.
    EXPECT_EQ(measure({'a', 0xE000}), (Box{1, 29, -14, 15}));
}

// ---------------------------------------------------------------------------
// Cursor ops

TEST_F(FontBboxTest, VerticalNudgesMoveTheBaseline) {
    EXPECT_EQ(measure({0x05, 'a'}), (Box{1, 6, -8, 1}));            // down 2
    EXPECT_EQ(measure({'\f', 'a'}), measure({'a'}));                // up 2 saturates at 0
    EXPECT_EQ(measure({0x05, 0x05, '\f', 'a'}), (Box{1, 6, -8, 1})); // 4 down, 2 back up
}

TEST_F(FontBboxTest, HorizontalNudgesMoveTheCursor) {
    EXPECT_EQ(measure({0x06, 'a'}), (Box{3, 8, -10, -1}));   // right 2
    EXPECT_EQ(measure({'\b', 'a'}), measure({'a'}));         // back 2 saturates at 0
    EXPECT_EQ(measure({'a', '\b', 'a'}), (Box{1, 12, -10, -1})); // second at x=6
}

TEST_F(FontBboxTest, CarriageReturnRestartsXOnly) {
    EXPECT_EQ(measure({'a', 'a', '\r', 'a'}), (Box{1, 14, -10, -1}));
}

TEST_F(FontBboxTest, TabAddsA36pxStopSizedStep) {
    // \t is x += (x/36 + 1) * 36 — an ADDITIVE step, not a snap-to-stop: after
    // 'a' x = 8, the tab adds 36 and the next glyph starts at 44 (the draw's
    // rule, mirrored).
    EXPECT_EQ(measure({'a', '\t', 'a'}), (Box{1, 50, -10, -1}));
}

TEST_F(FontBboxTest, LineFeedStepsAFixed15AndKeepsX) {
    // \v from y=0 lands on 15: box = 15-10 .. 15-1.
    EXPECT_EQ(measure({'\v', 'a'}), (Box{1, 6, 5, 14}));
}

TEST_F(FontBboxTest, NewlineAdvancesByFontsZeroYAdvanceAndRestartsX) {
    // \n: y += 40, x = 0 — second 'a' spans y 30..39 at x 1..6.
    EXPECT_EQ(measure({'a', 'a', '\n', 'a'}), (Box{1, 14, -10, 39}));
}

TEST_F(FontBboxTest, ResetReturnsTheCursorToTheOrigin) {
    EXPECT_EQ(measure({'a', 0x05, 0x06, 0x18, 'a'}), measure({'a'}));
}

// ---------------------------------------------------------------------------
// Display-list op argument consumption. The rule: an op's argument codepoints
// are DATA, never glyphs and never ops — 13 of the 31 HINT_POS_*/HINT_SZ_*/
// MTB_* macros carry an argument byte that is also an op byte.

TEST_F(FontBboxTest, HalfAndThinConsumeTheirGlyphArgument) {
    // The composited glyph does not advance and is not measured.
    EXPECT_EQ(measure({0x0F, 'a'}), (Box{0, 0, 0, 0}));
    EXPECT_EQ(measure({0x11, 'a'}), (Box{0, 0, 0, 0}));
    // ...and a following real glyph measures from the unmoved cursor.
    EXPECT_EQ(measure({0x0F, 0xE000, 'a'}), measure({'a'}));
}

TEST_F(FontBboxTest, AnArgumentThatIsAnOpByteIsStillJustAnArgument) {
    // HINT_SZ_STOPSQ's shape: \x0F consumes the following \x0F as its argument,
    // so the 'a' after them is an ordinary glyph.
    EXPECT_EQ(measure({0x0F, 0x0F, 'a'}), measure({'a'}));
}

TEST_F(FontBboxTest, MoveConsumesBothCoordinates) {
    EXPECT_EQ(measure({0x0E, 'x', 'y', 'a'}), measure({'a'}));
}

TEST_F(FontBboxTest, MoveCoordinatesThatAreOpBytesDoNotLatchAFont) {
    // The 2026-08-26 regression class: a MOVE whose coordinates are \x16 bytes.
    // If the arguments leak into the switch, MID latches and 'a' measures in the
    // mid face (box x[0..3] y[-7..-1]) instead of the base face.
    EXPECT_EQ(measure_mid({0x0E, 0x16, 0x16, 'a'}), measure({'a'}));
}

TEST_F(FontBboxTest, RotFrameAndBadgeConsumeTheirArguments) {
    EXPECT_EQ(measure({0x15, 'r', 0xE000, 'a'}), measure({'a'}));       // ROT (angle, glyph)
    EXPECT_EQ(measure({0x12, 'w', 'h', 'a'}), measure({'a'}));          // FRAME (w, h)
    EXPECT_EQ(measure({0x13, 'w', 'h', 's', 'a'}), measure({'a'}));     // BADGE (w, h, style)
}

TEST_F(FontBboxTest, EraseIsAModeWithNoExtentAndNoArguments) {
    EXPECT_EQ(measure({0x14, 'a'}), measure({'a'}));
}

TEST_F(FontBboxTest, ATruncatedOpNeverSkipsPastTheTerminator) {
    // MOVE with only one byte before the NUL: the guard refuses the skip, the
    // stray byte measures as a glyph, and the walk stops at the terminator
    // instead of running past it.
    EXPECT_EQ(measure({0x0E, 'a'}), measure({'a'}));
    // BADGE one argument short: 'w' and 'h' measure as ordinary glyphs.
    EXPECT_EQ(measure({0x13, 'w', 'h'}), measure({'a', 'a'}));
}

// ---------------------------------------------------------------------------
// HINT_SMALL (\x10): halved extents/offsets with FLOOR rounding, halved
// (round-up) advance, unhalved baseline.

TEST_F(FontBboxTest, SmallHalvesExtentsOffsetsAndAdvance) {
    // 'a' small: gx = floor(1/2) = 0, gw = (6+1)/2 = 3, gy = floor(-10/2) = -5,
    // gh = (10+1)/2 = 5.
    EXPECT_EQ(measure({0x10, 'a'}), (Box{0, 2, -5, -1}));
    // advance halves round-up: second 'a' at x = (8+1)/2 = 4.
    EXPECT_EQ(measure({0x10, 'a', 'a'}), (Box{0, 6, -5, -1}));
}

TEST_F(FontBboxTest, SmallFloorsANegativeOffsetInsteadOfTruncating) {
    // 'b' has yOffset -9 / xOffset 3: floor gives gy -5 and gx 1; C truncation
    // would give -4 — the 1px-off-baseline bug the floor exists to prevent.
    EXPECT_EQ(measure({0x10, 'b'}), (Box{1, 3, -5, -1}));
}

TEST_F(FontBboxTest, SmallLatchesForTheRestOfTheRun) {
    // No back-to-full op exists, and \x18 resets only the cursor.
    EXPECT_EQ(measure({0x10, 'a', 0x18, 'a'}), (Box{0, 2, -5, -1}));
}

// ---------------------------------------------------------------------------
// HINT_MID (\x16): per-glyph face switch with per-glyph baseline reference.

TEST_F(FontBboxTest, MidMeasuresFromTheMidFaceWithItsOwnBaseline) {
    // mid 'a': w 4 h 7 xo 0 yo -7, and the baseline reference is the mid face
    // itself (yadj 0) — NOT fonts[0], which would shift it by 26-40 = -14.
    EXPECT_EQ(measure_mid({0x16, 'a'}), (Box{0, 3, -7, -1}));
}

TEST_F(FontBboxTest, MidFallsBackPerGlyphForCodepointsOutsideTheMidFace) {
    // 0xE000 is not ASCII: it falls back to the caller's pool and measures
    // exactly as it would without MID — including the fonts[0] baseline shift.
    EXPECT_EQ(measure_mid({0x16, 0xE000}), measure({0xE000}));
}

TEST_F(FontBboxTest, MidMixedRunAdvancesWithEachGlyphsOwnFace) {
    // mid 'a' advances 5, so the tall fallback glyph starts at x = 5.
    EXPECT_EQ(measure_mid({0x16, 'a', 0xE000}), (Box{0, 26, -14, 15}));
}

TEST_F(FontBboxTest, MidMissingEverywhereSubstitutesBangFromThePool) {
    // 0x4000 is neither in the mid face nor the pool: '!' from fonts[0].
    EXPECT_EQ(measure_mid({0x16, 0x4000}), measure({'!'}));
}

TEST_F(FontBboxTest, ANullMidPoolMakesEveryMidGlyphFallBack) {
    // The wrapper always passes the resident face, but the pure function
    // tolerates measuring with none: \x16 then changes nothing.
    EXPECT_EQ(measure({0x16, 'a'}, nullptr, 0), measure({'a'}));
}

// ---------------------------------------------------------------------------
// kdisp_gfx_glyph_font: the front-to-back resolver with gap skipping.

TEST(FontLookupTest, GapRecordFallsThroughToTheNextFont) {
    TestFont gappy  = make_gappy();
    TestFont filler = make_gap_filler();
    const GFXfont *fonts[2] = {&gappy.font, &filler.font};

    const GFXfont  *owner = nullptr;
    const GFXglyph *g     = kdisp_gfx_glyph_font(fonts, 2, 0x105, &owner);
    ASSERT_NE(g, nullptr);
    EXPECT_EQ(owner, &filler.font);
    EXPECT_EQ(g->width, 7);
}

TEST(FontLookupTest, FrontFontWinsWhenItReallyCoversTheCodepoint) {
    TestFont gappy  = make_gappy();
    TestFont filler = make_gap_filler();
    const GFXfont *fonts[2] = {&gappy.font, &filler.font};

    const GFXfont  *owner = nullptr;
    const GFXglyph *g     = kdisp_gfx_glyph_font(fonts, 2, 0x104, &owner);
    ASSERT_NE(g, nullptr);
    EXPECT_EQ(owner, &gappy.font);
    EXPECT_EQ(g->width, 3);
}

TEST(FontLookupTest, UncoveredCodepointReturnsNullAndNullFont) {
    TestFont gappy = make_gappy();
    const GFXfont *fonts[1] = {&gappy.font};

    const GFXfont *owner = &gappy.font;   // must be overwritten to NULL
    EXPECT_EQ(kdisp_gfx_glyph_font(fonts, 1, 0x1F0, &owner), nullptr);
    EXPECT_EQ(owner, nullptr);
    EXPECT_EQ(kdisp_gfx_glyph(fonts, 0, 0x104), nullptr);   // empty pool
}

TEST(FontLookupTest, HalfFloorFloorsNegativeValues) {
    EXPECT_EQ(glyph_half_floor(8), 4);
    EXPECT_EQ(glyph_half_floor(9), 4);
    EXPECT_EQ(glyph_half_floor(-8), -4);
    EXPECT_EQ(glyph_half_floor(-9), -5);   // truncation would give -4
    EXPECT_EQ(glyph_half_floor(0), 0);
    EXPECT_EQ(glyph_half_floor(-1), -1);
}

}  // namespace
