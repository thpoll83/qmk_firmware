// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// core1_suspend decides, tick by tick, when a USB suspend holds core1 and whether the
// hold found a stall. suspend_power_down_kb() calls it every ~17 ms, so the rules that
// matter are: hold once, never call a few ms of in-flight work a stall, and release
// at wake only what this suspend held.
#include "gtest/gtest.h"

extern "C" {
#include "core1_suspend.h"
}

namespace {
constexpr uint32_t TICK = 17;
}

TEST(Core1Suspend, IdleCoreIsHeldAtTheFirstTick) {
    core1_suspend_t s = {};
    EXPECT_EQ(core1_suspend_step(&s, true, false, 1000), CORE1_SUSPEND_HOLD);
    EXPECT_TRUE(s.held);
}

TEST(Core1Suspend, HoldsOncePerSuspend) {
    core1_suspend_t s = {};
    ASSERT_EQ(core1_suspend_step(&s, true, false, 1000), CORE1_SUSPEND_HOLD);
    for (uint32_t t = 1000 + TICK; t < 5000; t += TICK) {
        EXPECT_EQ(core1_suspend_step(&s, true, false, t), CORE1_SUSPEND_NONE);
        EXPECT_EQ(core1_suspend_step(&s, true, true, t), CORE1_SUSPEND_NONE);
    }
}

TEST(Core1Suspend, InFlightWorkThatFinishesIsNotAStall) {
    // Eden jobs take ~4.3 ms each and two can be queued at the suspend.
    core1_suspend_t s = {};
    EXPECT_EQ(core1_suspend_step(&s, true, true, 1000), CORE1_SUSPEND_NONE);
    EXPECT_EQ(core1_suspend_step(&s, true, false, 1000 + TICK), CORE1_SUSPEND_HOLD);
    EXPECT_EQ(s.waited, 0u);
}

TEST(Core1Suspend, WorkOwedForTheBudgetIsAStall) {
    core1_suspend_t s = {};
    uint32_t t = 1000;
    for (; t - 1000 < CORE1_SUSPEND_STALL_MS; t += TICK) {
        ASSERT_EQ(core1_suspend_step(&s, true, true, t), CORE1_SUSPEND_NONE) << "at +" << (t - 1000);
    }
    EXPECT_EQ(core1_suspend_step(&s, true, true, t), CORE1_SUSPEND_HOLD_STALLED);
    EXPECT_TRUE(s.held);
    EXPECT_GE(s.waited, CORE1_SUSPEND_STALL_MS);
    EXPECT_LT(s.waited, CORE1_SUSPEND_STALL_MS + TICK);
}

TEST(Core1Suspend, StallIsMeasuredAcrossATimerWrap) {
    core1_suspend_t s = {};
    const uint32_t start = UINT32_MAX - 100;
    ASSERT_EQ(core1_suspend_step(&s, true, true, start), CORE1_SUSPEND_NONE);
    EXPECT_EQ(core1_suspend_step(&s, true, true, start + 200), CORE1_SUSPEND_NONE);
    EXPECT_EQ(core1_suspend_step(&s, true, true, start + CORE1_SUSPEND_STALL_MS), CORE1_SUSPEND_HOLD_STALLED);
}

TEST(Core1Suspend, IneligibleTicksNeverHoldAndRestartTheWait) {
    // A flash hold or DOOM owning core1 makes it busy on purpose.
    core1_suspend_t s = {};
    ASSERT_EQ(core1_suspend_step(&s, true, true, 1000), CORE1_SUSPEND_NONE);
    for (uint32_t t = 1000 + TICK; t < 3000; t += TICK) {
        ASSERT_EQ(core1_suspend_step(&s, false, true, t), CORE1_SUSPEND_NONE);
    }
    EXPECT_FALSE(s.held);
    // Eligible again: the budget starts over instead of counting the ineligible time.
    EXPECT_EQ(core1_suspend_step(&s, true, true, 3000), CORE1_SUSPEND_NONE);
    EXPECT_EQ(core1_suspend_step(&s, true, true, 3000 + CORE1_SUSPEND_STALL_MS - 1), CORE1_SUSPEND_NONE);
    EXPECT_EQ(core1_suspend_step(&s, true, true, 3000 + CORE1_SUSPEND_STALL_MS), CORE1_SUSPEND_HOLD_STALLED);
}

TEST(Core1Suspend, WakeReleasesOnlyWhatThisSuspendHeld) {
    core1_suspend_t s = {};
    EXPECT_FALSE(core1_resume_step(&s));                 // no suspend at all (a ltr559 or host wake)
    ASSERT_EQ(core1_suspend_step(&s, false, false, 1000), CORE1_SUSPEND_NONE);
    EXPECT_FALSE(core1_resume_step(&s));                 // suspended, but never eligible
    ASSERT_EQ(core1_suspend_step(&s, true, false, 2000), CORE1_SUSPEND_HOLD);
    EXPECT_TRUE(core1_resume_step(&s));
    EXPECT_FALSE(core1_resume_step(&s));                 // a second wake call releases nothing
}

TEST(Core1Suspend, WakeDuringTheWaitReleasesNothingAndForgetsTheWait) {
    core1_suspend_t s = {};
    ASSERT_EQ(core1_suspend_step(&s, true, true, 1000), CORE1_SUSPEND_NONE);
    EXPECT_FALSE(core1_resume_step(&s));
    // The next suspend measures from its own start, not from the old one.
    EXPECT_EQ(core1_suspend_step(&s, true, true, 9000), CORE1_SUSPEND_NONE);
    EXPECT_EQ(core1_suspend_step(&s, true, true, 9000 + CORE1_SUSPEND_STALL_MS - 1), CORE1_SUSPEND_NONE);
}

TEST(Core1Suspend, EverySuspendHoldsAgain) {
    core1_suspend_t s = {};
    for (int round = 0; round < 3; ++round) {
        const uint32_t t = 1000u + 10000u * (uint32_t)round;
        ASSERT_EQ(core1_suspend_step(&s, true, false, t), CORE1_SUSPEND_HOLD) << round;
        ASSERT_TRUE(core1_resume_step(&s)) << round;
    }
}
