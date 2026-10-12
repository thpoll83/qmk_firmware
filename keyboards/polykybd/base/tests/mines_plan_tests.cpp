// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#include "gtest/gtest.h"

extern "C" {
#include "mines_plan.h"
}

#include <cstdlib>
#include <cstring>

namespace {

// The two shapes the firmware uses (mines_mode.c).
const mines_geom_t kSplit72 = {14, 4, 9};
const mines_geom_t kSplit42 = {12, 3, 6};

int Count(const uint8_t *mask, const mines_geom_t &g) {
    int n = 0;
    for (int i = 0; i < g.w * g.h; ++i) n += mines_bit(mask, (uint8_t)i);
    return n;
}

bool Near(const mines_geom_t &g, int a, int b) {
    const int dx = a % g.w - b % g.w, dy = a / g.w - b / g.w;
    return dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1;
}

void SetMine(mines_board_t &b, int cell) {
    b.mine[cell >> 3] |= (uint8_t)(1u << (cell & 7));
}

// A board with mines at `cells`, already placed, so the first reveal does not move them.
mines_board_t Fixed(std::initializer_list<int> cells) {
    mines_board_t b;
    mines_reset(&b);
    for (int c : cells) SetMine(b, c);
    b.state |= MINES_PLACED;
    return b;
}

TEST(MinesPlan, BoardFitsTheSyncStruct) {
    EXPECT_EQ(sizeof(mines_board_t), 24u);
    EXPECT_LE(kSplit72.w * kSplit72.h, MINES_MAX_CELLS);
    EXPECT_LE(kSplit42.w * kSplit42.h, MINES_MAX_CELLS);
}

TEST(MinesPlan, ResetKeepsOnlyTheActiveBit) {
    mines_board_t b;
    std::memset(&b, 0xA5, sizeof(b));
    b.state = MINES_ACTIVE | MINES_LOST | MINES_PLACED;
    mines_reset(&b);
    EXPECT_EQ(b.state, MINES_ACTIVE);
    EXPECT_EQ(b.boom, MINES_NO_CELL);
    EXPECT_EQ(Count(b.mine, kSplit72) + Count(b.open, kSplit72) + Count(b.flag, kSplit72), 0);
}

TEST(MinesPlan, FirstRevealPlacesExactlyTheMinesAwayFromTheTap) {
    for (const mines_geom_t *g : {&kSplit72, &kSplit42}) {
        const int n = g->w * g->h;
        for (int first = 0; first < n; ++first) {
            for (uint32_t seed : {0u, 1u, 0xDEADBEEFu, (uint32_t)first * 7919u}) {
                mines_board_t b;
                mines_reset(&b);
                mines_reveal(g, &b, (uint8_t)first, seed);
                ASSERT_EQ(Count(b.mine, *g), g->mines) << "first=" << first << " seed=" << seed;
                for (int i = 0; i < n; ++i) {
                    if (Near(*g, first, i)) ASSERT_FALSE(mines_bit(b.mine, (uint8_t)i));
                }
                ASSERT_FALSE(b.state & MINES_LOST);
                // Its neighbourhood is mine-free, so the tap opens an area.
                ASSERT_EQ(mines_neighbour_count(g, &b, (uint8_t)first), 0);
                ASSERT_GE(Count(b.open, *g), 4);
            }
        }
    }
}

TEST(MinesPlan, PlacementDependsOnTheSeed) {
    mines_board_t a, b;
    mines_reset(&a);
    mines_reset(&b);
    mines_reveal(&kSplit72, &a, 0, 1);
    mines_reveal(&kSplit72, &b, 0, 2);
    EXPECT_NE(std::memcmp(a.mine, b.mine, sizeof(a.mine)), 0);
}

TEST(MinesPlan, NeighbourCountSeesAllEightAndNotAcrossRows) {
    // 14 x 4. Cell 15 is (1,1); its neighbours are 0,1,2,14,16,28,29,30.
    mines_board_t b = Fixed({0, 1, 2, 14, 16, 28, 29, 30});
    EXPECT_EQ(mines_neighbour_count(&kSplit72, &b, 15), 8);
    // Cell 13 is the end of row 0; 14 is the start of row 1 and must not count.
    mines_board_t c = Fixed({14});
    EXPECT_EQ(mines_neighbour_count(&kSplit72, &c, 13), 0);
    EXPECT_EQ(mines_neighbour_count(&kSplit72, &c, 0), 1);
    // Two rows or two columns away is not a neighbour.
    mines_board_t d = Fixed({43, 17});
    EXPECT_EQ(mines_neighbour_count(&kSplit72, &d, 15), 0);
}

TEST(MinesPlan, RevealOfANumberOpensOnlyThatCell) {
    mines_board_t b = Fixed({0});
    mines_reveal(&kSplit72, &b, 1, 0);
    EXPECT_EQ(Count(b.open, kSplit72), 1);
    EXPECT_EQ(mines_cell_show(&kSplit72, &b, 1), MINES_SHOW_0 + 1);
}

TEST(MinesPlan, RevealOfAZeroFloodsToTheNumbers) {
    // One mine in the corner: everything else opens from the far corner, and that wins.
    mines_board_t b = Fixed({0});
    const mines_geom_t g = {14, 4, 1};
    mines_reveal(&g, &b, 55, 0);
    EXPECT_EQ(Count(b.open, g), 55);
    EXPECT_TRUE(b.state & MINES_WON);
    EXPECT_EQ(mines_cell_show(&g, &b, 0), MINES_SHOW_FLAG);   // the win flags the mines
}

TEST(MinesPlan, OpeningAMineLosesAndShowsTheBoard) {
    mines_board_t b = Fixed({5, 20});
    mines_toggle_flag(&kSplit72, &b, 33);   // wrong flag
    mines_reveal(&kSplit72, &b, 5, 0);
    EXPECT_TRUE(b.state & MINES_LOST);
    EXPECT_EQ(b.boom, 5);
    EXPECT_EQ(mines_cell_show(&kSplit72, &b, 5), MINES_SHOW_BOOM);
    EXPECT_EQ(mines_cell_show(&kSplit72, &b, 20), MINES_SHOW_MINE);
    EXPECT_EQ(mines_cell_show(&kSplit72, &b, 33), MINES_SHOW_WRONG_FLAG);
    // Nothing moves after the game is over.
    mines_board_t before = b;
    mines_reveal(&kSplit72, &b, 40, 0);
    mines_toggle_flag(&kSplit72, &b, 41);
    EXPECT_EQ(std::memcmp(&before, &b, sizeof(b)), 0);
}

TEST(MinesPlan, FlagsBlockRevealAndCountDown) {
    mines_board_t b = Fixed({5});
    mines_toggle_flag(&kSplit72, &b, 5);
    EXPECT_EQ(mines_flags_left(&kSplit72, &b), 8);
    mines_reveal(&kSplit72, &b, 5, 0);
    EXPECT_FALSE(b.state & MINES_LOST);
    EXPECT_EQ(mines_cell_show(&kSplit72, &b, 5), MINES_SHOW_FLAG);
    mines_toggle_flag(&kSplit72, &b, 5);
    EXPECT_EQ(mines_cell_show(&kSplit72, &b, 5), MINES_SHOW_HIDDEN);
    // An open cell takes no flag; the counter floors at 0.
    mines_reveal(&kSplit72, &b, 6, 0);
    mines_toggle_flag(&kSplit72, &b, 6);
    EXPECT_FALSE(mines_bit(b.flag, 6));
    const mines_geom_t one = {14, 4, 1};
    mines_toggle_flag(&one, &b, 40);
    mines_toggle_flag(&one, &b, 41);
    EXPECT_EQ(mines_flags_left(&one, &b), 0);
}

TEST(MinesPlan, ChordOpensTheRestOnlyWhenTheFlagsMatch) {
    // Mine at 0; cell 15 sees one mine. Open 15, then chord it.
    mines_board_t b = Fixed({0, 50});
    mines_reveal(&kSplit72, &b, 15, 0);
    ASSERT_EQ(Count(b.open, kSplit72), 1);
    mines_chord(&kSplit72, &b, 15);   // no flag yet: nothing
    EXPECT_EQ(Count(b.open, kSplit72), 1);
    mines_toggle_flag(&kSplit72, &b, 0);
    mines_chord(&kSplit72, &b, 15);
    EXPECT_GT(Count(b.open, kSplit72), 1);
    EXPECT_FALSE(b.state & MINES_LOST);
}

TEST(MinesPlan, ChordWithTooManyFlagsDoesNothing) {
    mines_board_t b = Fixed({0, 50});
    mines_reveal(&kSplit72, &b, 15, 0);
    mines_toggle_flag(&kSplit72, &b, 0);
    mines_toggle_flag(&kSplit72, &b, 2);
    mines_chord(&kSplit72, &b, 15);
    EXPECT_EQ(Count(b.open, kSplit72), 1);
    EXPECT_FALSE(b.state & MINES_LOST);
}

TEST(MinesPlan, ChordWithAWrongFlagLoses) {
    mines_board_t b = Fixed({0, 50});
    mines_reveal(&kSplit72, &b, 15, 0);
    mines_toggle_flag(&kSplit72, &b, 2);   // the mine is at 0
    mines_chord(&kSplit72, &b, 15);
    EXPECT_TRUE(b.state & MINES_LOST);
    EXPECT_EQ(b.boom, 0);
}

TEST(MinesPlan, RevealOrderSpreadsOutwardFromTheTap) {
    // Pending: the whole board but the origin (cell 15 = (1,1)). Each next cell is no
    // farther than the one before, and the first ring comes in index order.
    uint8_t pending[MINES_MASK_BYTES] = {};
    for (int i = 0; i < 56; ++i) pending[i >> 3] |= (uint8_t)(1u << (i & 7));
    pending[15 >> 3] &= (uint8_t)~(1u << (15 & 7));
    const int first_ring[] = {0, 1, 2, 14, 16, 28, 29, 30};
    int prev_d = 0;
    for (int k = 0; k < 55; ++k) {
        const uint8_t c = mines_next_pending(&kSplit72, pending, 15);
        ASSERT_NE(c, MINES_NO_CELL);
        ASSERT_TRUE(mines_bit(pending, c));
        const int dx = std::abs(c % 14 - 1), dy = std::abs(c / 14 - 1);
        const int d = dx > dy ? dx : dy;
        ASSERT_GE(d, prev_d) << "step " << k;
        if (k < 8) ASSERT_EQ(c, first_ring[k]);
        prev_d = d;
        pending[c >> 3] &= (uint8_t)~(1u << (c & 7));
    }
    EXPECT_EQ(mines_next_pending(&kSplit72, pending, 15), MINES_NO_CELL);
}

TEST(MinesPlan, CheckWinWaitsForEverySafeCell) {
    mines_board_t b = Fixed({0});
    const mines_geom_t g = {14, 4, 1};
    for (int i = 1; i < 55; ++i) b.open[i >> 3] |= (uint8_t)(1u << (i & 7));
    mines_check_win(&g, &b);
    EXPECT_FALSE(b.state & MINES_WON);
    b.open[55 >> 3] |= (uint8_t)(1u << (55 & 7));
    mines_check_win(&g, &b);
    EXPECT_TRUE(b.state & MINES_WON);
    EXPECT_TRUE(mines_bit(b.flag, 0));
    // A lost board never turns into a win.
    mines_board_t l = Fixed({0});
    l.state |= MINES_LOST;
    for (int i = 1; i < 56; ++i) l.open[i >> 3] |= (uint8_t)(1u << (i & 7));
    mines_check_win(&g, &l);
    EXPECT_FALSE(l.state & MINES_WON);
}

TEST(MinesPlan, OutOfRangeCellsAreIgnored) {
    mines_board_t b;
    mines_reset(&b);
    mines_reveal(&kSplit42, &b, 36, 0);
    mines_reveal(&kSplit42, &b, MINES_NO_CELL, 0);
    EXPECT_EQ(b.state & MINES_PLACED, 0);
}

}  // namespace
