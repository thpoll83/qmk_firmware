// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The agent status light's fade curve. Header-only, so this suite is just the tests.
//
// What it pins, and why each half matters: IDLE and ATTENTION are STANDING states an
// agent can sit in for hours, so they go out after a minute; WORKING is bounded by the
// work and must NOT, because it is the one state where a glance at the board has to
// answer "is it still going?". Getting that backwards leaves either a keyboard lit up
// all night or a busy agent with no light at all.

#include "gtest/gtest.h"

extern "C" {
#include "ai_light.h"
}

static const uint32_t HOLD = AI_LIGHT_HOLD_MS;
static const uint32_t FADE = AI_LIGHT_FADE_MS;

TEST(AiLight, OffIsNeverLit) {
    EXPECT_EQ(0, ai_light_scale(AI_LIGHT_OFF, 0));
    EXPECT_EQ(0, ai_light_scale(AI_LIGHT_OFF, HOLD * 10));
    EXPECT_FALSE(ai_light_wants_led(AI_LIGHT_OFF, 0));
}

TEST(AiLight, AnOutOfRangeStateIsNotLit) {
    // The range is CLOSED (state.h), so an unknown value is a bug, not a colour this
    // build happens to lack -- painting *something* for it would hide that.
    EXPECT_EQ(0, ai_light_scale(AI_LIGHT_COUNT, 0));
    EXPECT_EQ(0, ai_light_scale(200, 0));
    EXPECT_EQ(0, ai_light_scale(255, 0));
}

TEST(AiLight, WorkingNeverFades) {
    // The whole asymmetry in one test. A day of it is still full brightness.
    EXPECT_EQ(255, ai_light_scale(AI_LIGHT_WORKING, 0));
    EXPECT_EQ(255, ai_light_scale(AI_LIGHT_WORKING, HOLD));
    EXPECT_EQ(255, ai_light_scale(AI_LIGHT_WORKING, HOLD + FADE + 1));
    EXPECT_EQ(255, ai_light_scale(AI_LIGHT_WORKING, 86400000u));
    EXPECT_TRUE(ai_light_wants_led(AI_LIGHT_WORKING, 86400000u));
}

TEST(AiLight, IdleAndAttentionHoldFullBrightnessForTheWholeMinute) {
    for (uint8_t st : {AI_LIGHT_IDLE, AI_LIGHT_ATTENTION}) {
        EXPECT_EQ(255, ai_light_scale(st, 0)) << (int)st;
        EXPECT_EQ(255, ai_light_scale(st, HOLD / 2)) << (int)st;
        EXPECT_EQ(255, ai_light_scale(st, HOLD - 1)) << (int)st;
    }
}

TEST(AiLight, IdleAndAttentionAreOutAfterTheFade) {
    for (uint8_t st : {AI_LIGHT_IDLE, AI_LIGHT_ATTENTION}) {
        EXPECT_EQ(0, ai_light_scale(st, HOLD + FADE)) << (int)st;
        EXPECT_EQ(0, ai_light_scale(st, HOLD + FADE + 1)) << (int)st;
        EXPECT_FALSE(ai_light_wants_led(st, HOLD + FADE)) << (int)st;
    }
}

TEST(AiLight, ItStaysOutForEveryLaterElapsed) {
    // ⚠️ NOT redundant with the test above, and the difference is a real bug shape: a
    // ramp with no clamp underflows once `into` passes the window, so the light comes
    // back on -- at a wrong brightness, hours later, on a keyboard whose owner went
    // home. Checking one millisecond past the end misses it entirely (the arithmetic
    // is still ~0 there); this is the mutation that escaped the first version.
    for (uint8_t st : {AI_LIGHT_IDLE, AI_LIGHT_ATTENTION}) {
        for (uint32_t t : {HOLD + FADE * 2, HOLD * 2, 600000u, 3600000u, 86400000u,
                           0xFFFFFFFFu}) {
            EXPECT_EQ(0, ai_light_scale(st, t)) << (int)st << " @ " << t;
        }
    }
}

TEST(AiLight, TheRampIsMonotonicAndActuallyRamps) {
    // A "fade" that jumps 255 -> 0 in one step is a switch. Walk the whole window and
    // require it to descend, and to spend real time in the middle.
    uint8_t  prev = 255;
    uint32_t mid  = 0;
    for (uint32_t t = HOLD; t <= HOLD + FADE; t += 25) {
        const uint8_t v = ai_light_scale(AI_LIGHT_IDLE, t);
        EXPECT_LE(v, prev) << "brightness rose at " << t;
        if (v > 20 && v < 235) mid++;
        prev = v;
    }
    EXPECT_GT(mid, 50u) << "the ramp is too abrupt to read as a fade";
}

TEST(AiLight, TheHoldIS_A_MINUTE) {
    // Pinned as a LITERAL on purpose. Every other test here expresses its times in
    // terms of HOLD, so shrinking the constant moves both sides of the comparison and
    // the whole suite still passes -- which is exactly what happened when this was
    // mutated to one second. "A minute" is the requirement, not an implementation
    // detail, so something has to state the number.
    EXPECT_EQ(60000u, AI_LIGHT_HOLD_MS);
    EXPECT_EQ(255, ai_light_scale(AI_LIGHT_IDLE, 59999u));
    EXPECT_GT(255, ai_light_scale(AI_LIGHT_IDLE, 61500u));
    EXPECT_EQ(0, ai_light_scale(AI_LIGHT_IDLE, 63000u));
}

TEST(AiLight, TheFadeStartsAtTheMinuteNotBefore) {
    // The boundary the user actually named. One millisecond either side of it.
    EXPECT_EQ(255, ai_light_scale(AI_LIGHT_IDLE, HOLD - 1));
    EXPECT_GT(255, ai_light_scale(AI_LIGHT_IDLE, HOLD + FADE / 2));
    EXPECT_TRUE(ai_light_wants_led(AI_LIGHT_IDLE, HOLD));
}

TEST(AiLight, ApplyScalesAChannelAndBothEndsAreExact) {
    EXPECT_EQ(48, ai_light_apply(48, 255));   // full scale changes nothing
    EXPECT_EQ(0, ai_light_apply(48, 0));
    EXPECT_EQ(0, ai_light_apply(0, 255));
    EXPECT_EQ(255, ai_light_apply(255, 255));
}

TEST(AiLight, ApplyRoundsToNearestSoADimChannelKeepsItsLastStep) {
    // IDLE's green is 14, so the fade has only 14 steps to spend. Truncation drops the
    // last two of them at once, which reads as the light snapping off at the end.
    EXPECT_EQ(7, ai_light_apply(14, 128));    // 14*128/255 = 7.03
    EXPECT_EQ(1, ai_light_apply(14, 12));     // 0.65 -> 1, not 0
    EXPECT_EQ(0, ai_light_apply(14, 2));      // 0.11 -> 0
}

TEST(AiLight, ApplyNeverOverflowsForAnyChannelAndScale) {
    for (uint16_t c = 0; c <= 255; c++) {
        for (uint16_t s = 0; s <= 255; s += 5) {
            const uint8_t got = ai_light_apply((uint8_t)c, (uint8_t)s);
            EXPECT_LE(got, c) << c << "/" << s;   // scaling can only dim
        }
    }
}

TEST(AiLight, WantsLedAgreesWithScaleEverywhere) {
    // The borrow that switches a disabled RGB matrix ON asks wants_led, and the paint
    // asks scale. If they disagreed, the matrix would stay lit to display black.
    for (uint8_t st = 0; st < AI_LIGHT_COUNT + 2; st++) {
        for (uint32_t t : {0u, HOLD - 1, HOLD, HOLD + FADE / 2, HOLD + FADE, 999999u}) {
            EXPECT_EQ(ai_light_scale(st, t) != 0, ai_light_wants_led(st, t))
                << (int)st << " @ " << t;
        }
    }
}
