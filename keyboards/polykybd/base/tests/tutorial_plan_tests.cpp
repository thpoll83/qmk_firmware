// Copyright 2026 Thomas Pollak
// SPDX-License-Identifier: GPL-2.0-or-later

#include "gtest/gtest.h"

extern "C" {
#include "tutorial_plan.h"
}

#include <cstring>
#include <set>
#include <vector>

namespace {

constexpr uint8_t L(uint8_t i) { return TUT_SLOT(0, i); }
constexpr uint8_t R(uint8_t i) { return TUT_SLOT(1, i); }

// Run the clock forward to the end of the current phase and tick once.
void FinishPhase(tut_state_t *st, uint32_t *now, uint32_t dur) {
    *now += dur;
    EXPECT_TRUE(tut_tick(st, *now));
}

tut_state_t Start(uint32_t now, uint8_t a = L(3), uint8_t b = R(5), uint8_t c = L(9)) {
    const uint8_t slots[TUT_LETTERS] = {a, b, c};
    tut_state_t   st{};
    tut_init(&st, slots, now);
    return st;
}

// ---- phase machine --------------------------------------------------------

TEST(TutorialPhases, StartsBlankThenText) {
    uint32_t    now = 1000;
    tut_state_t st  = Start(now);
    EXPECT_EQ(st.phase, TUT_BLANK);

    // Not a millisecond early.
    now += TUT_BLANK_MS - 1;
    EXPECT_FALSE(tut_tick(&st, now));
    EXPECT_EQ(st.phase, TUT_BLANK);

    now += 1;
    EXPECT_TRUE(tut_tick(&st, now));
    EXPECT_EQ(st.phase, TUT_TEXT);
}

TEST(TutorialPhases, ReachesTheFirstLetterAndThenWaitsForever) {
    uint32_t    now = 0;
    tut_state_t st  = Start(now);
    FinishPhase(&st, &now, TUT_BLANK_MS);
    FinishPhase(&st, &now, TUT_TEXT_MS);
    EXPECT_EQ(st.phase, TUT_LETTER_IN);
    FinishPhase(&st, &now, TUT_LETTER_IN_MS);
    EXPECT_EQ(st.phase, TUT_LETTER_WAIT);

    // The whole point: no timeout. An hour later it is still asking.
    now += 3600u * 1000u;
    EXPECT_FALSE(tut_tick(&st, now));
    EXPECT_EQ(st.phase, TUT_LETTER_WAIT);
}

TEST(TutorialPhases, ThreeLettersThenDone) {
    uint32_t    now = 0;
    tut_state_t st  = Start(now);
    FinishPhase(&st, &now, TUT_BLANK_MS);
    FinishPhase(&st, &now, TUT_TEXT_MS);

    for (uint8_t i = 0; i < TUT_LETTERS; ++i) {
        EXPECT_EQ(st.phase, TUT_LETTER_IN);
        EXPECT_EQ(st.step, i);
        FinishPhase(&st, &now, TUT_LETTER_IN_MS);
        EXPECT_EQ(st.phase, TUT_LETTER_WAIT);
        EXPECT_TRUE(tut_press(&st, st.slots[i], now));
        EXPECT_EQ(st.phase, TUT_RIPPLE);
        FinishPhase(&st, &now, TUT_RIPPLE_MS);
        EXPECT_EQ(st.phase, TUT_GAP);
        FinishPhase(&st, &now, TUT_GAP_MS);
    }
    EXPECT_EQ(st.phase, TUT_DONE);
    EXPECT_FALSE(st.skipped);
}

TEST(TutorialPhases, DoneIsTerminal) {
    uint32_t    now = 0;
    tut_state_t st  = Start(now);
    tut_skip(&st, now);
    now += 10u * 60u * 1000u;
    EXPECT_FALSE(tut_tick(&st, now));
    EXPECT_EQ(st.phase, TUT_DONE);
}

// A phase must not stall across the 49.7-day timer wrap — the failure base/update.c
// had to be corrected for.
TEST(TutorialPhases, SurvivesTheTimerWrap) {
    uint32_t    now = 0xFFFFFF00u;
    tut_state_t st  = Start(now);
    now += TUT_BLANK_MS;                      // wraps past 0
    EXPECT_TRUE(tut_tick(&st, now));
    EXPECT_EQ(st.phase, TUT_TEXT);
}

// ---- presses --------------------------------------------------------------

TEST(TutorialPress, WrongKeyDoesNothingAtAll) {
    uint32_t    now = 0;
    tut_state_t st  = Start(now, L(3), R(5), L(9));
    FinishPhase(&st, &now, TUT_BLANK_MS);
    FinishPhase(&st, &now, TUT_TEXT_MS);
    FinishPhase(&st, &now, TUT_LETTER_IN_MS);

    const uint8_t seq = st.ripple_seq;
    EXPECT_FALSE(tut_press(&st, L(4), now));
    EXPECT_FALSE(tut_press(&st, R(3), now));   // same index, other half
    EXPECT_EQ(st.phase, TUT_LETTER_WAIT);      // still asking
    EXPECT_EQ(st.ripple_seq, seq);             // and no ripple was fired
}

TEST(TutorialPress, AcceptedDuringTheFadeIn) {
    uint32_t    now = 0;
    tut_state_t st  = Start(now);
    FinishPhase(&st, &now, TUT_BLANK_MS);
    FinishPhase(&st, &now, TUT_TEXT_MS);
    EXPECT_EQ(st.phase, TUT_LETTER_IN);
    EXPECT_TRUE(tut_press(&st, st.slots[0], now));
    EXPECT_EQ(st.phase, TUT_RIPPLE);
}

TEST(TutorialPress, IgnoredOutsideTheLetterPhases) {
    uint32_t    now = 0;
    tut_state_t st  = Start(now);
    EXPECT_EQ(st.phase, TUT_BLANK);
    EXPECT_FALSE(tut_press(&st, st.slots[0], now));
    EXPECT_EQ(st.phase, TUT_BLANK);
}

TEST(TutorialPress, BumpsTheRippleSequenceSoTheSlaveSeesAChange) {
    uint32_t    now = 0;
    tut_state_t st  = Start(now);
    FinishPhase(&st, &now, TUT_BLANK_MS);
    FinishPhase(&st, &now, TUT_TEXT_MS);
    const uint8_t before = st.ripple_seq;
    EXPECT_TRUE(tut_press(&st, st.slots[0], now));
    EXPECT_NE(st.ripple_seq, before);
    EXPECT_EQ(st.ripple_slot, st.slots[0]);
}

TEST(TutorialSkip, EndsFromAnyPhaseAndRecordsWhy) {
    uint32_t    now = 0;
    tut_state_t st  = Start(now);
    FinishPhase(&st, &now, TUT_BLANK_MS);
    tut_skip(&st, now);
    EXPECT_EQ(st.phase, TUT_DONE);
    EXPECT_TRUE(st.skipped);
}

TEST(TutorialSlot, CurrentSlotIsNoneOutsideTheLetterPhases) {
    uint32_t    now = 0;
    tut_state_t st  = Start(now);
    EXPECT_EQ(tut_current_slot(&st), TUT_SLOT_NONE);
    FinishPhase(&st, &now, TUT_BLANK_MS);
    FinishPhase(&st, &now, TUT_TEXT_MS);
    EXPECT_EQ(tut_current_slot(&st), st.slots[0]);
    EXPECT_TRUE(tut_press(&st, st.slots[0], now));
    EXPECT_EQ(tut_current_slot(&st), st.slots[0]);   // still lit while it settles out
    FinishPhase(&st, &now, TUT_RIPPLE_MS);
    EXPECT_EQ(tut_current_slot(&st), TUT_SLOT_NONE); // gap: nothing is being asked for
}

TEST(TutorialProgress, RunsZeroToFullAndSaturates) {
    uint32_t    now = 0;
    tut_state_t st  = Start(now);
    EXPECT_EQ(tut_phase_progress(&st, now), 0);
    EXPECT_EQ(tut_phase_progress(&st, now + TUT_BLANK_MS / 2), 127);
    EXPECT_EQ(tut_phase_progress(&st, now + TUT_BLANK_MS), 255);
    EXPECT_EQ(tut_phase_progress(&st, now + TUT_BLANK_MS * 4), 255);
}

TEST(TutorialProgress, AWaitingPhaseReportsFull) {
    uint32_t    now = 0;
    tut_state_t st  = Start(now);
    tut_skip(&st, now);
    EXPECT_EQ(tut_phase_progress(&st, now + 5), 255);   // no duration to be partway through
}

// ---- curves ---------------------------------------------------------------

TEST(TutorialCurves, FadeSpansTheFullContrastRange) {
    EXPECT_EQ(tut_fade_contrast(0), 0);
    EXPECT_EQ(tut_fade_contrast(255), 255);
}

TEST(TutorialCurves, FadeIsMonotonicAndEased) {
    uint8_t prev = 0;
    for (uint16_t p = 0; p <= 255; ++p) {
        const uint8_t v = tut_fade_contrast((uint8_t)p);
        EXPECT_GE(v, prev) << "at p=" << p;
        prev = v;
    }
    // Eased, not linear: the midpoint matches but the quarter points sit inside it.
    EXPECT_LT(tut_fade_contrast(64), 64);
    EXPECT_GT(tut_fade_contrast(191), 191);
}

TEST(TutorialCurves, RippleStartsAtTheFivePixelDisc) {
    EXPECT_EQ(tut_ripple_radius(0), 5);
}

TEST(TutorialCurves, RippleClearsTheFarCornerOfTheBoard) {
    // Board is 1673x563; the longest reach from any key is ~1765 board units. The
    // ripple must end PAST that or it visibly stops instead of leaving.
    EXPECT_GE(tut_ripple_radius(255), 1765);
}

TEST(TutorialCurves, RippleRadiusIsMonotonic) {
    uint16_t prev = 0;
    for (uint16_t p = 0; p <= 255; ++p) {
        const uint16_t r = tut_ripple_radius((uint8_t)p);
        EXPECT_GE(r, prev) << "at p=" << p;
        prev = r;
    }
}

// The ring must READ as a solid circle before it dissolves. The original curve faded
// from the very first frame, so on hardware the dissolve was never visible — it was
// already thinning before it had established itself.
TEST(TutorialCurves, RippleStaysSolidBeforeItDissolves) {
    for (uint16_t p = 0; p <= TUT_RIPPLE_SOLID_P; ++p) {
        EXPECT_EQ(tut_ripple_density((uint8_t)p), 255) << "at p=" << p;
    }
    EXPECT_LT(tut_ripple_density((uint8_t)(TUT_RIPPLE_SOLID_P + 1)), 255);
}

// A ripple you cannot watch is the defect this feature is most likely to regress into.
TEST(TutorialCurves, RippleIsSlowEnoughToFollow) {
    // ~1795 board units of travel; board units are about a display pixel, and a keycap
    // is 72 wide. Anything under ~50 ms per keycap width reads as a flash, not a wave.
    const uint32_t ms_per_keycap = (TUT_RIPPLE_MS * 72u) / 1795u;
    EXPECT_GE(ms_per_keycap, 50u) << "ripple crosses a keycap in " << ms_per_keycap << " ms";
}

TEST(TutorialCurves, RippleDensityFadesToNothing) {
    EXPECT_EQ(tut_ripple_density(0), 255);
    EXPECT_EQ(tut_ripple_density(255), 0);
    uint8_t prev = 255;
    for (uint16_t p = 0; p <= 255; ++p) {
        const uint8_t d = tut_ripple_density((uint8_t)p);
        EXPECT_LE(d, prev) << "at p=" << p;
        prev = d;
    }
}

// ---- letter selection -----------------------------------------------------

std::vector<uint8_t> BothHalves() {
    std::vector<uint8_t> c;
    for (uint8_t i = 0; i < 20; ++i) c.push_back(L(i));
    for (uint8_t i = 0; i < 20; ++i) c.push_back(R(i));
    return c;
}

TEST(TutorialChoose, PicksThreeDistinctKeys) {
    const auto    cand = BothHalves();
    uint8_t       out[TUT_LETTERS];
    EXPECT_EQ(tut_choose_slots(cand.data(), (uint8_t)cand.size(), 12345, out), TUT_LETTERS);
    std::set<uint8_t> uniq(out, out + TUT_LETTERS);
    EXPECT_EQ(uniq.size(), (size_t)TUT_LETTERS);
}

TEST(TutorialChoose, IsDeterministicForTheSameSeed) {
    const auto cand = BothHalves();
    uint8_t    a[TUT_LETTERS], b[TUT_LETTERS];
    tut_choose_slots(cand.data(), (uint8_t)cand.size(), 777, a);
    tut_choose_slots(cand.data(), (uint8_t)cand.size(), 777, b);
    EXPECT_EQ(memcmp(a, b, sizeof(a)), 0);
}

// The ripple crossing the split is what step 1 exists to show, so a same-half draw
// must be repaired rather than accepted.
TEST(TutorialChoose, AlwaysCoversBothHalves) {
    const auto cand = BothHalves();
    for (uint32_t seed = 0; seed < 300; ++seed) {
        uint8_t out[TUT_LETTERS];
        ASSERT_EQ(tut_choose_slots(cand.data(), (uint8_t)cand.size(), seed, out), TUT_LETTERS)
            << "seed " << seed;
        bool left = false, right = false;
        for (uint8_t i = 0; i < TUT_LETTERS; ++i) {
            if (TUT_SLOT_RIGHT(out[i])) right = true; else left = true;
        }
        EXPECT_TRUE(left && right) << "seed " << seed << " stayed on one half";
        std::set<uint8_t> uniq(out, out + TUT_LETTERS);
        EXPECT_EQ(uniq.size(), (size_t)TUT_LETTERS) << "seed " << seed << " repeated a key";
    }
}

TEST(TutorialChoose, SingleHalfPoolStillWorks) {
    std::vector<uint8_t> cand;
    for (uint8_t i = 0; i < 10; ++i) cand.push_back(L(i));
    uint8_t out[TUT_LETTERS];
    EXPECT_EQ(tut_choose_slots(cand.data(), (uint8_t)cand.size(), 9, out), TUT_LETTERS);
    for (uint8_t i = 0; i < TUT_LETTERS; ++i) EXPECT_FALSE(TUT_SLOT_RIGHT(out[i]));
}

TEST(TutorialChoose, TooFewCandidatesReportsShort) {
    const uint8_t cand[2] = {L(1), R(2)};
    uint8_t       out[TUT_LETTERS];
    EXPECT_LT(tut_choose_slots(cand, 2, 5, out), TUT_LETTERS);
}

TEST(TutorialChoose, EmptyPoolIsSafe) {
    uint8_t out[TUT_LETTERS];
    EXPECT_EQ(tut_choose_slots(nullptr, 0, 1, out), 0);
    EXPECT_EQ(out[0], TUT_SLOT_NONE);
}

}  // namespace
