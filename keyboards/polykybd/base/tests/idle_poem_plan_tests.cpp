// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#include "gtest/gtest.h"

extern "C" {
#include "idle_poem_plan.h"
}

#include <set>

namespace {

const char *kShort = "ab\ncd";   // 5 characters, one line break

uint32_t TypeMs(const char *p) { return idle_poem_type_ms(p); }

idle_poem_frame_t At(const char *p, uint32_t u) {
    idle_poem_frame_t f{};
    EXPECT_TRUE(idle_poem_frame(p, u, &f)) << "u=" << u;
    return f;
}

// ---- the texts ----------------------------------------------------------------

TEST(IdlePoemTexts, AreAsciiTheFaceCovers) {
    ASSERT_GE(idle_poem_count, 2u);
    for (uint8_t p = 0; p < idle_poem_count; ++p)
        for (const char *c = idle_poems[p]; *c; ++c)
            EXPECT_TRUE(*c == '\n' || (*c >= 0x20 && *c <= 0x7E)) << "poem " << int(p) << " has 0x" << std::hex << int(*c);
}

TEST(IdlePoemTexts, NeitherStartNorEndOnALineBreak) {
    for (uint8_t p = 0; p < idle_poem_count; ++p) {
        const uint8_t n = idle_poem_len(idle_poems[p]);
        ASSERT_GT(n, 0u);
        EXPECT_NE(idle_poems[p][0], '\n');
        EXPECT_NE(idle_poems[p][n - 1], '\n');
    }
}

// ---- the timeline ---------------------------------------------------------------

TEST(IdlePoemTimeline, TypingTimeCountsEveryKeyAndEachLineWait) {
    EXPECT_EQ(TypeMs(kShort), 5u * IDLE_POEM_KEY_MS + IDLE_POEM_LINE_MS);
    EXPECT_EQ(TypeMs(""), 0u);
}

TEST(IdlePoemTimeline, LeadShowsOnlyABlinkingCursor) {
    EXPECT_EQ(At(kShort, 0).typed, 0u);
    EXPECT_TRUE(At(kShort, 0).cursor);
    EXPECT_FALSE(At(kShort, IDLE_POEM_BLINK_MS).cursor);
}

TEST(IdlePoemTimeline, OneKeyPerKeystroke) {
    const uint32_t t0 = IDLE_POEM_LEAD_MS;
    EXPECT_EQ(At(kShort, t0).typed, 1u);                          // the first key lands at once
    EXPECT_EQ(At(kShort, t0 + IDLE_POEM_KEY_MS - 1).typed, 1u);
    EXPECT_EQ(At(kShort, t0 + IDLE_POEM_KEY_MS).typed, 2u);
    EXPECT_TRUE(At(kShort, t0 + IDLE_POEM_KEY_MS).cursor);        // steady while typing
}

TEST(IdlePoemTimeline, ALineBreakWaitsWithTheCursorBlinking) {
    // "ab\n" is typed by 2 * KEY; the '\n' keystroke then waits KEY + LINE.
    const uint32_t nl = IDLE_POEM_LEAD_MS + 2u * IDLE_POEM_KEY_MS;
    EXPECT_EQ(At(kShort, nl).typed, 3u);
    EXPECT_EQ(At(kShort, nl + IDLE_POEM_KEY_MS + IDLE_POEM_LINE_MS - 1).typed, 3u);
    EXPECT_EQ(At(kShort, nl + IDLE_POEM_KEY_MS + IDLE_POEM_LINE_MS).typed, 4u);
    // During the wait the cursor blinks: lit, then dark one half-period later.
    EXPECT_TRUE(At(kShort, nl + IDLE_POEM_KEY_MS).cursor);
    EXPECT_FALSE(At(kShort, nl + IDLE_POEM_KEY_MS + IDLE_POEM_BLINK_MS).cursor);
}

TEST(IdlePoemTimeline, HoldsTheWholePoemThenBackspacesFromTheEnd) {
    const uint32_t hold = IDLE_POEM_LEAD_MS + TypeMs(kShort);
    EXPECT_EQ(At(kShort, hold).typed, 5u);
    EXPECT_EQ(At(kShort, hold + IDLE_POEM_HOLD_MS - 1).typed, 5u);
    const uint32_t del = hold + IDLE_POEM_HOLD_MS;
    EXPECT_EQ(At(kShort, del).typed, 4u);                         // the first press takes one at once
    EXPECT_TRUE(At(kShort, del).cursor);
    EXPECT_EQ(At(kShort, del + IDLE_POEM_BS_MS).typed, 3u);
    EXPECT_EQ(At(kShort, del + 4u * IDLE_POEM_BS_MS).typed, 0u);  // the last press empties it
}

TEST(IdlePoemTimeline, EmptyCursorThenNothing) {
    const uint32_t empty = IDLE_POEM_LEAD_MS + TypeMs(kShort) + IDLE_POEM_HOLD_MS + 5u * IDLE_POEM_BS_MS;
    EXPECT_EQ(At(kShort, empty).typed, 0u);
    EXPECT_TRUE(At(kShort, empty).cursor);
    idle_poem_frame_t f{};
    EXPECT_FALSE(idle_poem_frame(kShort, empty + IDLE_POEM_EMPTY_MS, &f));
}

TEST(IdlePoemTimeline, NeverMoreCharactersThanThePoemHas) {
    for (uint8_t p = 0; p < idle_poem_count; ++p) {
        const uint8_t n = idle_poem_len(idle_poems[p]);
        idle_poem_frame_t f{};
        for (uint32_t u = 0; idle_poem_frame(idle_poems[p], u, &f); u += 25) ASSERT_LE(f.typed, n);
    }
}

TEST(IdlePoemTimeline, EveryPoemEndsInsideThePhase) {
    const uint32_t phase = idle_poem_phase_ms();
    for (uint8_t p = 0; p < idle_poem_count; ++p) {
        idle_poem_frame_t f{};
        EXPECT_TRUE(idle_poem_frame(idle_poems[p], 0, &f));
        EXPECT_FALSE(idle_poem_frame(idle_poems[p], phase, &f)) << "poem " << int(p) << " overruns the phase";
    }
}

// ---- the pick ---------------------------------------------------------------------

TEST(IdlePoemPick, NeverTheSamePoemTwiceInARow) {
    for (uint16_t s = 0; s < 200; ++s)
        for (uint32_t c = 1; c < 40; ++c) ASSERT_NE(idle_poem_pick(s, c), idle_poem_pick(s, c - 1)) << "seed " << s;
}

TEST(IdlePoemPick, AlwaysInRangeAndReachesEveryPoem) {
    std::set<uint8_t> seen;
    for (uint16_t s = 0; s < 50; ++s)
        for (uint32_t c = 0; c < 20; ++c) {
            const uint8_t p = idle_poem_pick(s, c);
            ASSERT_LT(p, idle_poem_count);
            seen.insert(p);
        }
    EXPECT_EQ(seen.size(), idle_poem_count);
}

TEST(IdlePoemPick, SeedsStartOnDifferentPoems) {
    std::set<uint8_t> first;
    for (uint16_t s = 0; s < 50; ++s) first.insert(idle_poem_pick(s, 0));
    EXPECT_EQ(first.size(), idle_poem_count);
}

TEST(IdlePoemPick, IsAPureFunctionOfSeedAndCycle) {
    EXPECT_EQ(idle_poem_pick(7, 11), idle_poem_pick(7, 11));
}

}  // namespace
