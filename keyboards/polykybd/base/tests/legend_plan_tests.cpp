// Copyright 2026 Thomas Pollak
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Tests for base/legend_plan.c — the keycap MAIN-legend size planner (HID cmd 34).
//
// The planner's two shipped hazards are pinned here off-hardware:
//   - the cursor-op handling (the five zero-argument nudges are DROPPED, every
//     other display-list op bails) — getting this wrong silently halved the
//     AZERTY number row once, and the rig cannot see it;
//   - the origin/baseline clamping, whose whole job is keeping a 43 px glyph or
//     a tall accent stack on the 72×40 panel.

#include "gtest/gtest.h"

extern "C" {
#include "legend_plan.h"
}

#include <set>
#include <vector>

namespace {

// The split72/split42 visible window the firmware binds: columns 28..99, rows 0..39.
constexpr int8_t kWinX0 = 28;
constexpr int8_t kWinX1 = 99;
constexpr int8_t kWinY1 = 39;

// Fake font world: a set of drawable codepoints plus a deterministic measurer —
// each glyph 10 px wide advancing from x 0, inking ymin..ymax as configured.
struct FakeFonts {
    std::set<uint32_t> glyphs;
    int8_t             ymin = -20;  // above the baseline, like a capital
    int8_t             ymax = 0;
};

bool fake_has_glyph(uint32_t cp, void* ctx) {
    return static_cast<FakeFonts*>(ctx)->glyphs.count(cp) != 0;
}

void fake_bbox(const uint32_t* text, int8_t* xmin, int8_t* xmax, int8_t* ymin, int8_t* ymax, void* ctx) {
    auto* f = static_cast<FakeFonts*>(ctx);
    int   n = 0;
    while (text[n] != 0) n++;
    *xmin = 0;
    *xmax = static_cast<int8_t>(n > 0 ? n * 10 - 1 : 0);
    *ymin = f->ymin;
    *ymax = f->ymax;
}

legend_plan_env_t env_for(FakeFonts* f) {
    legend_plan_env_t env{};
    env.has_glyph = fake_has_glyph;
    env.bbox      = fake_bbox;
    env.ctx       = f;
    env.win_x0    = kWinX0;
    env.win_x1    = kWinX1;
    env.win_y1    = kWinY1;
    return env;
}

// A fake world where both bigger tiers can draw 'a'..'z'.
FakeFonts latin_world() {
    FakeFonts f;
    for (uint32_t c = 'a'; c <= 'z'; ++c) {
        f.glyphs.insert(0xF0000u + c);
        f.glyphs.insert(0xF3000u + c);
    }
    return f;
}

TEST(LegendPlanRemapTest, SmallSizeAlwaysFallsBack) {
    FakeFonts f   = latin_world();
    auto      env = env_for(&f);
    const uint32_t text[] = {'a', 0};
    uint32_t       out[GLYPH_SIZE_MAX_LEN + 1];
    EXPECT_FALSE(legend_plan_remap(&env, LEGEND_PLAN_SIZE_S, text, out, GLYPH_SIZE_MAX_LEN + 1));
}

TEST(LegendPlanRemapTest, UnknownSizeFallsBack) {
    // The closed-range contract: a size the firmware does not know renders small
    // (cmd 34 NACKs it on the wire; this is the render-side belt to that brace).
    FakeFonts f   = latin_world();
    auto      env = env_for(&f);
    const uint32_t text[] = {'a', 0};
    uint32_t       out[GLYPH_SIZE_MAX_LEN + 1];
    EXPECT_FALSE(legend_plan_remap(&env, LEGEND_PLAN_SIZE_COUNT, text, out, GLYPH_SIZE_MAX_LEN + 1));
    EXPECT_FALSE(legend_plan_remap(&env, 0xFF, text, out, GLYPH_SIZE_MAX_LEN + 1));
    EXPECT_FALSE(legend_plan_remap(&env, LEGEND_PLAN_SIZE_M, nullptr, out, GLYPH_SIZE_MAX_LEN + 1));
}

TEST(LegendPlanRemapTest, RelocationAddsExactlyTheTierBase) {
    // Pin the PUA plane-15 bases: fonts.yaml's latinbig `offset:` values are
    // 0xF0000 (M) and 0xF3000 (L), and the two must stay identical or every
    // legend silently renders the wrong tier's glyphs.
    FakeFonts f   = latin_world();
    auto      env = env_for(&f);
    const uint32_t text[] = {'a', 'b', 0};
    uint32_t       out[GLYPH_SIZE_MAX_LEN + 1];
    ASSERT_TRUE(legend_plan_remap(&env, LEGEND_PLAN_SIZE_M, text, out, GLYPH_SIZE_MAX_LEN + 1));
    EXPECT_EQ(out[0], 0xF0000u + 'a');
    EXPECT_EQ(out[1], 0xF0000u + 'b');
    EXPECT_EQ(out[2], 0u);
    ASSERT_TRUE(legend_plan_remap(&env, LEGEND_PLAN_SIZE_L, text, out, GLYPH_SIZE_MAX_LEN + 1));
    EXPECT_EQ(out[0], 0xF3000u + 'a');
    EXPECT_EQ(out[1], 0xF3000u + 'b');
}

TEST(LegendPlanRemapTest, OneMissingGlyphFailsTheWholeLegend) {
    // All-or-nothing: a partial hit would mix two faces (and two baselines) in
    // one legend.
    FakeFonts f = latin_world();
    f.glyphs.erase(0xF0000u + 'b');
    auto env = env_for(&f);
    const uint32_t text[] = {'a', 'b', 0};
    uint32_t       out[GLYPH_SIZE_MAX_LEN + 1];
    EXPECT_FALSE(legend_plan_remap(&env, LEGEND_PLAN_SIZE_M, text, out, GLYPH_SIZE_MAX_LEN + 1));
    // The same legend still relocates at the tier that has every glyph.
    EXPECT_TRUE(legend_plan_remap(&env, LEGEND_PLAN_SIZE_L, text, out, GLYPH_SIZE_MAX_LEN + 1));
}

TEST(LegendPlanRemapTest, TheFiveCursorNudgesAreDroppedNotRelocated) {
    // The AZERTY case: `é è ç à` are spelled `\f\f <letter>` (0x0C 0x0C <cp>).
    // Dropping the nudges — never relocating them as if they were glyphs — is
    // what lets those keys scale at all.
    FakeFonts f   = latin_world();
    auto      env = env_for(&f);
    for (uint32_t op : {0x05u, 0x06u, 0x08u, 0x0Bu, 0x0Cu}) {
        const uint32_t text[] = {op, op, 'e', 0};
        uint32_t       out[GLYPH_SIZE_MAX_LEN + 1];
        ASSERT_TRUE(legend_plan_remap(&env, LEGEND_PLAN_SIZE_M, text, out, GLYPH_SIZE_MAX_LEN + 1))
            << "op 0x" << std::hex << op;
        EXPECT_EQ(out[0], 0xF0000u + 'e');
        EXPECT_EQ(out[1], 0u);
    }
}

TEST(LegendPlanRemapTest, EveryOtherDisplayListOpBails) {
    // HINT_MOVE/HINT_FRAME consume the two codepoints after them, HINT_HALF /
    // HINT_SMALL / HINT_MID rescale or re-font what follows — relocating any of
    // that as glyph data would draw garbage, so the whole legend stays small.
    FakeFonts f   = latin_world();
    auto      env = env_for(&f);
    for (uint32_t op : {0x01u, 0x0Eu, 0x0Fu, 0x10u, 0x12u, 0x13u, 0x16u, 0x1Fu}) {
        const uint32_t text[] = {op, 'e', 0};
        uint32_t       out[GLYPH_SIZE_MAX_LEN + 1];
        EXPECT_FALSE(legend_plan_remap(&env, LEGEND_PLAN_SIZE_M, text, out, GLYPH_SIZE_MAX_LEN + 1))
            << "op 0x" << std::hex << op;
    }
}

TEST(LegendPlanRemapTest, OpsAloneDrawNothing) {
    FakeFonts f   = latin_world();
    auto      env = env_for(&f);
    const uint32_t text[] = {0x0C, 0x0C, 0};
    uint32_t       out[GLYPH_SIZE_MAX_LEN + 1];
    EXPECT_FALSE(legend_plan_remap(&env, LEGEND_PLAN_SIZE_M, text, out, GLYPH_SIZE_MAX_LEN + 1));
}

TEST(LegendPlanRemapTest, ATooLongLegendFallsBack) {
    FakeFonts f   = latin_world();
    auto      env = env_for(&f);
    // The buffer is deliberately LARGER than GLYPH_SIZE_MAX_LEN + 1, so this pins
    // the glyph-count contract itself, not the caller's buffer bound: a five-glyph
    // legend must fall back even when the output has room for it.
    std::vector<uint32_t> text(GLYPH_SIZE_MAX_LEN, 'a');
    text.push_back(0);
    uint32_t out[GLYPH_SIZE_MAX_LEN + 4];
    EXPECT_TRUE(legend_plan_remap(&env, LEGEND_PLAN_SIZE_M, text.data(), out, GLYPH_SIZE_MAX_LEN + 4));
    text.back() = 'a';
    text.push_back(0);
    EXPECT_FALSE(legend_plan_remap(&env, LEGEND_PLAN_SIZE_M, text.data(), out, GLYPH_SIZE_MAX_LEN + 4));
}

TEST(LegendPlanRemapTest, ATightBufferStillBounds) {
    FakeFonts f   = latin_world();
    auto      env = env_for(&f);
    // The out_cap half of the guard: a legend within the glyph-count contract
    // must still be refused when the caller's buffer cannot hold it + terminator.
    std::vector<uint32_t> text(2, 'a');
    text.push_back(0);
    uint32_t out[GLYPH_SIZE_MAX_LEN + 1];
    EXPECT_FALSE(legend_plan_remap(&env, LEGEND_PLAN_SIZE_M, text.data(), out, 2));
    EXPECT_TRUE(legend_plan_remap(&env, LEGEND_PLAN_SIZE_M, text.data(), out, 3));
}

TEST(LegendPlanMainTest, BigUsesTheTiersNominalBaseline) {
    // One 10 px glyph at x 40 needs no clamp: x stays put and y is the tier's
    // nominal baseline (M 25, L 28 — chosen so cap height matches the small face).
    FakeFonts f   = latin_world();
    auto      env = env_for(&f);
    const uint32_t text[] = {'a', 0};
    uint32_t       scratch[GLYPH_SIZE_MAX_LEN + 1];
    main_legend_t  plan{};
    legend_plan_main(&env, LEGEND_PLAN_SIZE_M, text, 40, 23, scratch, GLYPH_SIZE_MAX_LEN + 1, &plan);
    EXPECT_TRUE(plan.big);
    EXPECT_EQ(plan.text, scratch);
    EXPECT_EQ(plan.x, 40);
    EXPECT_EQ(plan.y, 25);
    EXPECT_EQ(plan.ink_min, 40);
    EXPECT_EQ(plan.ink_max, 49);
    legend_plan_main(&env, LEGEND_PLAN_SIZE_L, text, 40, 23, scratch, GLYPH_SIZE_MAX_LEN + 1, &plan);
    EXPECT_EQ(plan.y, 28);
}

TEST(LegendPlanMainTest, HorizontalClampKeepsTheInkInTheWindow) {
    FakeFonts f   = latin_world();
    auto      env = env_for(&f);
    // Three glyphs = ink 0..29 relative to the origin. Placed at x 90 the right
    // edge would ink to 119; the clamp pulls it back so the last pixel is win_x1.
    const uint32_t text[] = {'a', 'b', 'c', 0};
    uint32_t       scratch[GLYPH_SIZE_MAX_LEN + 1];
    main_legend_t  plan{};
    legend_plan_main(&env, LEGEND_PLAN_SIZE_M, text, 90, 23, scratch, GLYPH_SIZE_MAX_LEN + 1, &plan);
    EXPECT_EQ(plan.ink_max, kWinX1);
    EXPECT_EQ(plan.x, kWinX1 - 29);
    // Placed left of the window the left clamp wins.
    legend_plan_main(&env, LEGEND_PLAN_SIZE_M, text, 10, 23, scratch, GLYPH_SIZE_MAX_LEN + 1, &plan);
    EXPECT_EQ(plan.ink_min, kWinX0);
    EXPECT_EQ(plan.x, kWinX0);
}

TEST(LegendPlanMainTest, VerticalClampShiftsTallAccentsAndDeepDescenders) {
    FakeFonts f = latin_world();
    // A tall accent stack: 30 px of ink above the baseline. At the M nominal
    // baseline 25 the top row would be -5, so the baseline shifts down to 30.
    f.ymin = -30;
    f.ymax = 0;
    auto env = env_for(&f);
    const uint32_t text[] = {'a', 0};
    uint32_t       scratch[GLYPH_SIZE_MAX_LEN + 1];
    main_legend_t  plan{};
    legend_plan_main(&env, LEGEND_PLAN_SIZE_M, text, 40, 23, scratch, GLYPH_SIZE_MAX_LEN + 1, &plan);
    EXPECT_EQ(plan.y, 30);
    // A deep descender: 15 px below the L nominal baseline 28 overshoots row 39,
    // so the baseline lifts to 24.
    f.ymin = -20;
    f.ymax = 15;
    legend_plan_main(&env, LEGEND_PLAN_SIZE_L, text, 40, 23, scratch, GLYPH_SIZE_MAX_LEN + 1, &plan);
    EXPECT_EQ(plan.y, kWinY1 - 15);
}

TEST(LegendPlanMainTest, SmallPathKeepsTheLanguagesOwnOrigin) {
    // No bigger face (S, or nothing relocatable): the per-language origin is taken
    // verbatim — the planner must not clamp or re-place what the small face has
    // always drawn — and the ink extents still come from the measurement.
    FakeFonts f   = latin_world();
    auto      env = env_for(&f);
    const uint32_t text[] = {'a', 'b', 0};
    uint32_t       scratch[GLYPH_SIZE_MAX_LEN + 1];
    main_legend_t  plan{};
    legend_plan_main(&env, LEGEND_PLAN_SIZE_S, text, 33, 23, scratch, GLYPH_SIZE_MAX_LEN + 1, &plan);
    EXPECT_FALSE(plan.big);
    EXPECT_EQ(plan.text, text);
    EXPECT_EQ(plan.x, 33);
    EXPECT_EQ(plan.y, 23);
    EXPECT_EQ(plan.ink_min, 33);
    EXPECT_EQ(plan.ink_max, 33 + 19);
    // A legend the tiers cannot draw (missing glyph) takes the same small path.
    FakeFonts empty;
    auto      env2 = env_for(&empty);
    legend_plan_main(&env2, LEGEND_PLAN_SIZE_M, text, 33, 23, scratch, GLYPH_SIZE_MAX_LEN + 1, &plan);
    EXPECT_FALSE(plan.big);
    EXPECT_EQ(plan.text, text);
}

// ── legend_plan_idle_travel: the idle anti-burn-in travel range ──────────────
//
// The failure this closes is silent by construction: a legend with no free space
// simply did not move, on the exact keycaps (the 40 px icons) where a frozen
// legend burns in. The window here is 72x40 at x 28, so the numbers below are the
// firmware's own.
constexpr uint8_t kOver = 3;   // IDLE_TRAVEL_OVERHANG_PX

struct Travel { int8_t xlo, xhi, ylo, yhi; };

Travel travel_of(const legend_plan_env_t& env, int8_t xmin, int8_t xmax, int8_t ymin, int8_t ymax,
                 uint8_t overhang = kOver) {
    Travel t{};
    legend_plan_idle_travel(&env, xmin, xmax, ymin, ymax, overhang, &t.xlo, &t.xhi, &t.ylo, &t.yhi);
    return t;
}

TEST(LegendPlanIdleTravel, RoomySlimGlyphIsUntouchedByTheOverhang) {
    FakeFonts f;
    auto      env = env_for(&f);
    // A 4x10 "i" sitting at the window's top-left: 68 px of width and 29 of height
    // to roam, all of it on-screen. Nothing may push it off an edge — 3 px west
    // would delete its stem.
    const Travel t = travel_of(env, 28, 31, 0, 9);
    EXPECT_EQ(t.xlo, 0);
    EXPECT_EQ(t.xhi, 68);
    EXPECT_EQ(t.ylo, 0);
    EXPECT_EQ(t.yhi, 30);
}

TEST(LegendPlanIdleTravel, FullHeightIconGetsItsTravelFromTheSouthOnly) {
    FakeFonts f;
    auto      env = env_for(&f);
    // The 40x40 globe (U+1F310): zero vertical slack, so it could not move a row.
    // North is not on offer — row -1 is outside the scratch buffer — so the whole
    // shortfall is spent hanging off the BOTTOM.
    const Travel t = travel_of(env, 30, 69, 0, 39);
    EXPECT_EQ(t.ylo, 0);
    EXPECT_EQ(t.yhi, kOver);
    // Its width is untouched: 32 px of real slack, no borrowing.
    EXPECT_EQ(t.xlo, -2);
    EXPECT_EQ(t.xhi, 30);
}

TEST(LegendPlanIdleTravel, AlmostFullHeightBorrowsOnlyTheShortfall) {
    FakeFonts f;
    auto      env = env_for(&f);
    // 39 px tall (the 😀 / 🔧 case): 1 px of its own, so it borrows 2 more, not 3.
    const Travel t = travel_of(env, 40, 59, 0, 38);
    EXPECT_EQ(t.ylo, 0);
    EXPECT_EQ(t.yhi, 3);
    EXPECT_EQ(t.yhi - t.ylo, kOver);
}

TEST(LegendPlanIdleTravel, FullWidthGlyphSplitsItsBorrowingAcrossBothSides) {
    FakeFonts f;
    auto      env = env_for(&f);
    // Horizontally BOTH edges have buffer behind them, so the shortfall is split
    // rather than hung off one side — half as much clipped ink on either edge.
    const Travel t = travel_of(env, 28, 99, 10, 20);
    EXPECT_EQ(t.xlo, -1);
    EXPECT_EQ(t.xhi, 2);
    EXPECT_EQ(t.xhi - t.xlo, kOver);
}

TEST(LegendPlanIdleTravel, OverSizeLegendIsLeftWhereTheClampPutIt) {
    FakeFonts f;
    auto      env = env_for(&f);
    // he-IL's 43 px nikud, already clamped south-edge-wins so its top is off-panel.
    // Widening cannot buy travel here, only move the clip to the other edge, so the
    // range stays empty (lo > hi) and roll_idle_offset() leaves the axis at 0.
    const Travel t = travel_of(env, 40, 59, -3, 39);
    EXPECT_GT(t.ylo, t.yhi);
}

TEST(LegendPlanIdleTravel, ZeroOverhangReproducesTheOldFullyOnScreenRule) {
    FakeFonts f;
    auto      env = env_for(&f);
    const Travel t = travel_of(env, 30, 69, 0, 39, 0);
    EXPECT_EQ(t.ylo, 0);
    EXPECT_EQ(t.yhi, 0);
    EXPECT_EQ(t.xlo, -2);
    EXPECT_EQ(t.xhi, 30);
}

TEST(LegendPlanIdleTravel, TravelNeverCarriesInkOutsideTheScratchBuffer) {
    FakeFonts f;
    auto      env = env_for(&f);
    // The contract poly_keymap.c's _Static_asserts rest on: whatever the box, the
    // offset range keeps the ink inside [win_x0-overhang, win_x1+overhang] x
    // [0, win_y1+overhang] — the slack the buffer actually has.
    for (int xmin = 20; xmin <= 100; ++xmin) {
        for (int w = 1; w <= 80; ++w) {
            const int xmax = xmin + w - 1;
            if (xmax > 110) continue;
            for (int ymin = -6; ymin <= 39; ++ymin) {
                for (int h = 1; h <= 46; ++h) {
                    const int ymax = ymin + h - 1;
                    if (ymax > 45) continue;
                    const Travel t = travel_of(env, (int8_t)xmin, (int8_t)xmax, (int8_t)ymin, (int8_t)ymax);
                    if (t.xhi >= t.xlo) {
                        EXPECT_GE(xmin + t.xlo, kWinX0 - kOver);
                        EXPECT_LE(xmax + t.xhi, kWinX1 + kOver);
                    }
                    if (t.yhi >= t.ylo) {
                        EXPECT_GE(ymin + t.ylo, 0);            // never north of row 0
                        EXPECT_LE(ymax + t.yhi, kWinY1 + kOver);
                    }
                }
            }
        }
    }
}

}  // namespace
