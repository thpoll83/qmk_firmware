// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The idle style table decides what each style does when the board goes idle.
// These pin the decisions that used to be scattered `if (style == ...)` branches.
#include "gtest/gtest.h"

#include <cstring>

extern "C" {
#include "idle_style.h"
}

TEST(IdleStyleTest, EveryStyleHasANameAndTheNamesAreTheConsoleOnes) {
    EXPECT_STREQ(idle_style_desc(IDLE_STYLE_PULSE)->name, "pulse");
    EXPECT_STREQ(idle_style_desc(IDLE_STYLE_JITTER)->name, "jitter");
    EXPECT_STREQ(idle_style_desc(IDLE_STYLE_IDDQD)->name, "iddqd");
    // The rig's Eden test matches "style=eden" in the transition line.
    EXPECT_STREQ(idle_style_desc(IDLE_STYLE_EDEN)->name, "eden");
}

TEST(IdleStyleTest, AnUnknownStyleBehavesLikeThePulse) {
    EXPECT_EQ(idle_style_desc(IDLE_STYLE_COUNT), idle_style_desc(IDLE_STYLE_PULSE));
    EXPECT_EQ(idle_style_desc(0xFF), idle_style_desc(IDLE_STYLE_PULSE));
}

TEST(IdleStyleTest, EntryBehaviourPerStyle) {
    EXPECT_EQ(idle_style_desc(IDLE_STYLE_PULSE)->enter, IDLE_ENTER_PULSE);
    EXPECT_EQ(idle_style_desc(IDLE_STYLE_JITTER)->enter, IDLE_ENTER_PULSE);
    EXPECT_EQ(idle_style_desc(IDLE_STYLE_IDDQD)->enter, IDLE_ENTER_DOOM);
    EXPECT_EQ(idle_style_desc(IDLE_STYLE_EDEN)->enter, IDLE_ENTER_STEADY);
    EXPECT_EQ(idle_style_desc(IDLE_STYLE_EDEN)->steady_contrast, EDEN_IDLE_BRIGHTNESS);
}

TEST(IdleStyleTest, OnlyJitterRelocatesLegendsAndOnlyEdenOwnsTheKeycaps) {
    for (uint8_t s = 0; s < IDLE_STYLE_COUNT; ++s) {
        EXPECT_EQ(idle_style_desc(s)->jitter, s == IDLE_STYLE_JITTER) << unsigned(s);
        EXPECT_EQ(idle_style_desc(s)->owns_keycaps, s == IDLE_STYLE_EDEN) << unsigned(s);
    }
}

// A steady style must hold a visible contrast, or "idle" would look like "off".
TEST(IdleStyleTest, ASteadyStyleHoldsANonZeroContrast) {
    for (uint8_t s = 0; s < IDLE_STYLE_COUNT; ++s) {
        const idle_style_desc_t* d = idle_style_desc(s);
        if (d->enter == IDLE_ENTER_STEADY) EXPECT_GT(d->steady_contrast, 0) << d->name;
    }
}

// KC_IDLE_STYLE: the doom easter egg is never reached by the settings key.
TEST(IdleStyleTest, TheKeyCycleSkipsIddqd) {
    EXPECT_EQ(idle_style_next_in_cycle(IDLE_STYLE_PULSE), IDLE_STYLE_JITTER);
    EXPECT_EQ(idle_style_next_in_cycle(IDLE_STYLE_JITTER), IDLE_STYLE_EDEN);
    EXPECT_EQ(idle_style_next_in_cycle(IDLE_STYLE_EDEN), IDLE_STYLE_PULSE);
}

TEST(IdleStyleTest, ABoardOnIddqdCyclesOutOfIt) {
    EXPECT_EQ(idle_style_next_in_cycle(IDLE_STYLE_IDDQD), IDLE_STYLE_EDEN);
}

TEST(IdleStyleTest, AFullCycleVisitsEveryCycledStyleOnceAndNeverIddqd) {
    uint8_t s = IDLE_STYLE_PULSE;
    int     seen[IDLE_STYLE_COUNT] = {};
    for (int i = 0; i < 3; ++i) {
        s = idle_style_next_in_cycle(s);
        seen[s]++;
    }
    EXPECT_EQ(s, IDLE_STYLE_PULSE);
    EXPECT_EQ(seen[IDLE_STYLE_IDDQD], 0);
    EXPECT_EQ(seen[IDLE_STYLE_JITTER], 1);
    EXPECT_EQ(seen[IDLE_STYLE_EDEN], 1);
}

TEST(IdleStyleTest, AnOutOfRangeValueStillCyclesToAValidStyle) {
    EXPECT_LT(idle_style_next_in_cycle(0xFF), IDLE_STYLE_COUNT);
    EXPECT_NE(idle_style_next_in_cycle(0xFF), IDLE_STYLE_IDDQD);
}
