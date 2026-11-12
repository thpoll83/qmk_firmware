// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Tests for base/attn_guide.c: parsing cmd 47 op 2 (GUIDE) and picking the step for
// the active layer.

#include <vector>

#include "gtest/gtest.h"

extern "C" {
#include "attn_guide.h"
}

namespace {

constexpr uint8_t kRows = 10, kCols = 8, kLayers = 12, kPMin = 23, kPMax = 100;

// FN on the base layer 4 at (2,0), then the MIDI key on the Fn layer 5 at (8,7);
// the target is the MIDI layer 8.
std::vector<uint8_t> fn_then_midi() {
    std::vector<uint8_t> d(64, 0);
    d[1] = 47;
    d[2] = ATTN_GUIDE_OP;
    d[ATTN_GUIDE_IDX_COUNT]  = 12;
    d[ATTN_GUIDE_IDX_PERIOD] = 25;
    d[ATTN_GUIDE_IDX_TARGET] = 8;
    d[ATTN_GUIDE_IDX_NSTEPS] = 2;
    const uint8_t steps[] = {2, 0, 4, 8, 7, 5};
    for (unsigned i = 0; i < sizeof steps; ++i) d[ATTN_GUIDE_IDX_STEP0 + i] = steps[i];
    return d;
}

bool parse(const std::vector<uint8_t> &d, attn_guide_t *g, uint8_t length = 64) {
    return attn_guide_parse(d.data(), length, kRows, kCols, kLayers, kPMin, kPMax, g);
}

TEST(AttnGuide, ParsesTwoSteps) {
    attn_guide_t g;
    ASSERT_TRUE(parse(fn_then_midi(), &g));
    EXPECT_EQ(g.count, 12);
    EXPECT_EQ(g.period, 25);
    EXPECT_EQ(g.target, 8);
    ASSERT_EQ(g.nsteps, 2);
    EXPECT_EQ(g.step[0].row, 2);
    EXPECT_EQ(g.step[0].col, 0);
    EXPECT_EQ(g.step[0].layer, 4);
    EXPECT_EQ(g.step[1].row, 8);
    EXPECT_EQ(g.step[1].col, 7);
    EXPECT_EQ(g.step[1].layer, 5);
}

TEST(AttnGuide, SelectFollowsTheActiveLayer) {
    attn_guide_t g;
    ASSERT_TRUE(parse(fn_then_midi(), &g));
    EXPECT_EQ(attn_guide_select(&g, 4), 0);                 // base: ring FN
    EXPECT_EQ(attn_guide_select(&g, 5), 1);                 // FN held: ring MIDI
    EXPECT_EQ(attn_guide_select(&g, 4), 0);                 // FN let go: back
    EXPECT_EQ(attn_guide_select(&g, 6), ATTN_GUIDE_IDLE);   // another layer: pause
    EXPECT_EQ(attn_guide_select(&g, 8), ATTN_GUIDE_DONE);   // the MIDI layer
}

TEST(AttnGuide, OneStepIsAllowed) {
    auto d = fn_then_midi();
    d[ATTN_GUIDE_IDX_NSTEPS] = 1;
    attn_guide_t g;
    ASSERT_TRUE(parse(d, &g));
    EXPECT_EQ(g.nsteps, 1);
    EXPECT_EQ(attn_guide_select(&g, 5), ATTN_GUIDE_IDLE);
}

TEST(AttnGuide, FourStepsFitTheReport) {
    auto d = fn_then_midi();
    d[ATTN_GUIDE_IDX_NSTEPS] = 4;
    const uint8_t more[] = {0, 0, 9, 1, 1, 10};
    for (unsigned i = 0; i < sizeof more; ++i) d[ATTN_GUIDE_IDX_STEP0 + 6 + i] = more[i];
    attn_guide_t g;
    ASSERT_TRUE(parse(d, &g, ATTN_GUIDE_IDX_STEP0 + 12));
    EXPECT_EQ(attn_guide_select(&g, 10), 3);
}

TEST(AttnGuide, RefusesBadFields) {
    attn_guide_t g;
    g.nsteps = 99;   // must survive every refusal
    struct Case { unsigned idx; uint8_t value; };
    const Case bad[] = {
        {ATTN_GUIDE_IDX_PERIOD, kPMin - 1},
        {ATTN_GUIDE_IDX_PERIOD, kPMax + 1},
        {ATTN_GUIDE_IDX_TARGET, kLayers},
        {ATTN_GUIDE_IDX_NSTEPS, 0},
        {ATTN_GUIDE_IDX_NSTEPS, ATTN_GUIDE_MAX_STEPS + 1},
        {ATTN_GUIDE_IDX_STEP0 + 0, kRows},           // row
        {ATTN_GUIDE_IDX_STEP0 + 1, kCols},           // col
        {ATTN_GUIDE_IDX_STEP0 + 2, kLayers},         // layer
        {ATTN_GUIDE_IDX_STEP0 + 5, 4},               // two steps on one layer
        {ATTN_GUIDE_IDX_STEP0 + 5, 8},               // a step on the target layer
    };
    for (const auto &c : bad) {
        auto d = fn_then_midi();
        d[c.idx] = c.value;
        EXPECT_FALSE(parse(d, &g)) << "byte " << c.idx << " = " << int(c.value);
        EXPECT_EQ(g.nsteps, 99);
    }
}

TEST(AttnGuide, RefusesAShortReport) {
    attn_guide_t g;
    auto d = fn_then_midi();
    EXPECT_FALSE(parse(d, &g, ATTN_GUIDE_IDX_STEP0 + 5));   // second step cut
    EXPECT_FALSE(parse(d, &g, ATTN_GUIDE_IDX_NSTEPS));      // no step count
    EXPECT_TRUE(parse(d, &g, ATTN_GUIDE_IDX_STEP0 + 6));
    EXPECT_FALSE(attn_guide_parse(nullptr, 64, kRows, kCols, kLayers, kPMin, kPMax, &g));
}

}  // namespace
