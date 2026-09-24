// Copyright 2026 Thomas Pollak
// SPDX-License-Identifier: GPL-2.0-or-later

#include "gtest/gtest.h"

extern "C" {
#include "tutorial_plan.h"
}

#include <cstring>
#include <cmath>
#include <map>
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

// The two Shift keys chapter 2 points at, deliberately NOT any of the letter slots —
// the chapter refuses a shift it is not pointing at, so a fixture that shared a slot
// would pass for the wrong reason.
constexpr uint8_t SHIFT_L = L(32);
constexpr uint8_t SHIFT_R = R(32);

tut_state_t Start(uint32_t now, uint8_t a = L(3), uint8_t b = R(5), uint8_t c = L(9),
                  uint8_t sl = SHIFT_L, uint8_t sr = SHIFT_R) {
    const uint8_t slots[TUT_LETTERS]       = {a, b, c};
    const uint8_t shifts[TUT_SHIFT_STAGES] = {sl, sr};
    tut_state_t   st{};
    tut_init(&st, slots, shifts, now);
    return st;
}

// Clear the chapter-2 stage the ring is currently pointing at: hold, sweep, dwell.
void ClearShiftStage(tut_state_t *st, uint32_t *now) {
    const uint8_t slot = tut_point_slot(st);
    EXPECT_TRUE(tut_hold(st, TUT_HOLD_SHIFT, true, slot, *now));
    FinishPhase(st, now, TUT_SWEEP_MS);
    FinishPhase(st, now, TUT_SHIFT_HELD_MS);
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

TEST(TutorialPhases, ThreeLettersThenChapterTwo) {
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
    // The last letter hands over to chapter 2 rather than ending the tutorial.
    EXPECT_EQ(st.phase, TUT_REVEAL);
    EXPECT_FALSE(st.skipped);
}

// ---- chapter 2: shift -----------------------------------------------------

// Run the three letters and stop on the doorstep of chapter 2.
static tut_state_t AtChapterTwo(uint32_t *now) {
    tut_state_t st = Start(*now);
    FinishPhase(&st, now, TUT_BLANK_MS);
    FinishPhase(&st, now, TUT_TEXT_MS);
    for (uint8_t i = 0; i < TUT_LETTERS; ++i) {
        FinishPhase(&st, now, TUT_LETTER_IN_MS);
        tut_press(&st, st.slots[i], *now);
        FinishPhase(&st, now, TUT_RIPPLE_MS);
        FinishPhase(&st, now, TUT_GAP_MS);
    }
    return st;
}

TEST(TutorialShift, RevealWaitsForTheUser) {
    uint32_t    now = 0;
    tut_state_t st  = AtChapterTwo(&now);
    EXPECT_EQ(st.phase, TUT_REVEAL);
    FinishPhase(&st, &now, TUT_REVEAL_MS);
    EXPECT_EQ(st.phase, TUT_SHIFT_WAIT);
    // No timeout: only Shift leaves it, however long that takes. ⚠️ tut_tick() may well
    // return TRUE here — the pointing ring re-fires on its own period — so what is
    // asserted is the PHASE, not the return. Asserting the return would have to be
    // changed the moment the ring moved, which is the opposite of what this pins.
    now += 10u * 60u * 1000u;
    tut_tick(&st, now);
    EXPECT_EQ(st.phase, TUT_SHIFT_WAIT);
}

TEST(TutorialShift, PressStartsTheSweep) {
    uint32_t    now = 0;
    tut_state_t st  = AtChapterTwo(&now);
    FinishPhase(&st, &now, TUT_REVEAL_MS);
    EXPECT_TRUE(tut_hold(&st, TUT_HOLD_SHIFT, true, SHIFT_L, now));
    EXPECT_EQ(st.phase, TUT_SHIFT_SWEEP);
    EXPECT_TRUE(st.hold_on);
}

// ⚠️ NEITHER EDGE RAISES A RING, and the opposite used to be asserted here. The ring in
// this chapter POINTS at a key you have not found; once you are holding it, a ripple
// bursting out from under your own finger answers a question you have just answered.
// (Chapter 1 is the other way round — there the ring IS the confirmation, which
// TutorialPress.BumpsTheRippleSequenceSoTheSlaveSeesAChange pins.)
TEST(TutorialShift, HoldingAndReleasingRaiseNoRing) {
    uint32_t    now = 0;
    tut_state_t st  = AtChapterTwo(&now);
    FinishPhase(&st, &now, TUT_REVEAL_MS);
    // Take the pointing ring out of the picture first, so what is measured is the edge.
    tut_tick(&st, now);
    const uint8_t seq = st.ripple_seq;

    EXPECT_TRUE(tut_hold(&st, TUT_HOLD_SHIFT, true, SHIFT_L, now));
    EXPECT_EQ(st.ripple_seq, seq) << "the press raises no ring";

    EXPECT_TRUE(tut_hold(&st, TUT_HOLD_SHIFT, false, SHIFT_L, now));
    EXPECT_FALSE(st.hold_on);
    EXPECT_EQ(st.ripple_seq, seq) << "and neither does the release";

    // …and the phase still has to reach the slave, which is what the `true` returns arm.
    EXPECT_EQ(st.phase, TUT_SHIFT_SWEEP);
}

// ⚠️ A repeated edge in the same direction is still ignored — QMK can re-deliver one,
// and each one would otherwise be a state change pushed over the link for nothing.
TEST(TutorialShift, ARepeatedEdgeInTheSameDirectionIsIgnored) {
    uint32_t    now = 0;
    tut_state_t st  = AtChapterTwo(&now);
    FinishPhase(&st, &now, TUT_REVEAL_MS);
    tut_hold(&st, TUT_HOLD_SHIFT, true, SHIFT_L, now);
    EXPECT_TRUE(st.hold_on);
    EXPECT_FALSE(tut_hold(&st, TUT_HOLD_SHIFT, true, SHIFT_L, now));
}

// The dwell is on the CLOCK, so someone who taps Shift and lets go still sees the
// board change and change back instead of being told they did it wrong.
// Chapter 2 now hands over to chapter 3 rather than finishing the tutorial.
TEST(TutorialShift, ATapStillFinishesTheChapter) {
    uint32_t    now = 0;
    tut_state_t st  = AtChapterTwo(&now);
    FinishPhase(&st, &now, TUT_REVEAL_MS);
    tut_hold(&st, TUT_HOLD_SHIFT, true, SHIFT_L, now);
    tut_hold(&st, TUT_HOLD_SHIFT, false, SHIFT_L, now + 30u);   // let go almost at once
    FinishPhase(&st, &now, TUT_SWEEP_MS);
    EXPECT_EQ(st.phase, TUT_SHIFT_HELD);
    FinishPhase(&st, &now, TUT_SHIFT_HELD_MS);
    // The chapter runs once per hand, so a tap finishes the FIRST stage and the second
    // one opens.
    EXPECT_EQ(st.phase, TUT_SHIFT_AGAIN);
    ClearShiftStage(&st, &now);
    // ⚠️ The LAYER chapter is postponed: Shift hands over to the board reveal, not to
    // TUT_LAYER_WAIT. This is the one assertion that pins that; the layer chapter is
    // still implemented and still tested below, entered directly.
    EXPECT_EQ(st.phase, TUT_BOARD_REVEAL);
    EXPECT_FALSE(st.skipped);
}

// ---- chapter 2 runs ONCE PER HAND ----------------------------------------

TEST(TutorialShift, PointsAtTheLeftHandFirstAndThenTheRight) {
    uint32_t    now = 0;
    tut_state_t st  = AtChapterTwo(&now);
    FinishPhase(&st, &now, TUT_REVEAL_MS);
    ASSERT_EQ(st.phase, TUT_SHIFT_WAIT);
    EXPECT_EQ(tut_point_slot(&st), SHIFT_L);

    ClearShiftStage(&st, &now);
    EXPECT_EQ(st.phase, TUT_SHIFT_AGAIN);
    EXPECT_EQ(tut_point_slot(&st), SHIFT_R) << "the second stage asks for the OTHER hand";

    ClearShiftStage(&st, &now);
    EXPECT_EQ(st.phase, TUT_BOARD_REVEAL);
}

// ⚠️ The whole reason the second stage exists: both stages must not be clearable with
// the same hand. A shift the ring is not pointing at does nothing at all — the same
// silence a wrong letter gets in chapter 1.
TEST(TutorialShift, TheShiftNotBeingPointedAtIsRefused) {
    uint32_t    now = 0;
    tut_state_t st  = AtChapterTwo(&now);
    FinishPhase(&st, &now, TUT_REVEAL_MS);
    EXPECT_FALSE(tut_hold(&st, TUT_HOLD_SHIFT, true, SHIFT_R, now));
    EXPECT_EQ(st.phase, TUT_SHIFT_WAIT);
    EXPECT_FALSE(st.hold_on) << "a refused key is not held either";

    ClearShiftStage(&st, &now);
    ASSERT_EQ(st.phase, TUT_SHIFT_AGAIN);
    EXPECT_FALSE(tut_hold(&st, TUT_HOLD_SHIFT, true, SHIFT_L, now))
        << "and the left one no longer counts once the ring has moved";
    EXPECT_EQ(st.phase, TUT_SHIFT_AGAIN);
}

// A board with only one Shift must not strand the user on a stage nothing can clear.
TEST(TutorialShift, OneShiftBoardFinishesAfterASingleStage) {
    uint32_t    now = 0;
    tut_state_t st  = Start(now, L(3), R(5), L(9), SHIFT_L, TUT_SLOT_NONE);
    // Straight to chapter 2 rather than through three letters: the letters are not what
    // this is about.
    st.phase       = TUT_SHIFT_WAIT;
    st.phase_start = now;
    ClearShiftStage(&st, &now);
    EXPECT_EQ(st.phase, TUT_BOARD_REVEAL);
    // With no right Shift the reveal starts from the left one.
    EXPECT_EQ(st.ripple_slot, SHIFT_L);
}

// ⚠️ The pointing ring RE-FIRES while the chapter waits. Chapter 1's ring is struck by
// the press, so one shot is right; this one has to keep saying "this key, here" to
// someone who has not found it yet, and a single 2.2 s splash is gone before a
// first-time user looks up from the status line.
TEST(TutorialShift, ThePointingRingRepeatsWhileItWaits) {
    uint32_t    now = 0;
    tut_state_t st  = AtChapterTwo(&now);
    FinishPhase(&st, &now, TUT_REVEAL_MS);
    const uint8_t on_entry = st.ripple_seq;

    // AT ONCE on entering the wait — three silent seconds at the moment the user is
    // asked to find a key would be the pointer arriving after it was needed.
    EXPECT_TRUE(tut_tick(&st, now));
    EXPECT_NE(st.ripple_seq, on_entry);
    EXPECT_EQ(st.ripple_slot, SHIFT_L) << "and it points at the key being asked for";
    EXPECT_EQ(st.phase, TUT_SHIFT_WAIT) << "pointing is not a phase change";

    const uint8_t seq = st.ripple_seq;
    now += TUT_POINT_PERIOD_MS - 1;
    EXPECT_FALSE(tut_tick(&st, now)) << "not a millisecond early";
    EXPECT_EQ(st.ripple_seq, seq);

    now += 1;
    EXPECT_TRUE(tut_tick(&st, now));
    EXPECT_NE(st.ripple_seq, seq);

    // …and again, for as long as it waits.
    const uint8_t seq2 = st.ripple_seq;
    now += TUT_POINT_PERIOD_MS;
    EXPECT_TRUE(tut_tick(&st, now));
    EXPECT_NE(st.ripple_seq, seq2);
}

// Only the two waits point. Chapter 1's ring is a confirmation and chapter 3 has no
// pointer, so a ring fired there would be an unexplained flash on an unrelated key.
TEST(TutorialShift, NothingElsePoints) {
    uint32_t    now = 0;
    tut_state_t st  = Start(now);
    EXPECT_EQ(tut_point_slot(&st), TUT_SLOT_NONE);
    FinishPhase(&st, &now, TUT_BLANK_MS);
    FinishPhase(&st, &now, TUT_TEXT_MS);
    EXPECT_EQ(tut_point_slot(&st), TUT_SLOT_NONE) << "chapter 1 confirms, it does not point";
    FinishPhase(&st, &now, TUT_LETTER_IN_MS);
    ASSERT_EQ(st.phase, TUT_LETTER_WAIT);
    EXPECT_EQ(tut_point_slot(&st), TUT_SLOT_NONE);

    // A letter wait has no duration either, and must not acquire a repeating ring.
    const uint8_t seq = st.ripple_seq;
    now += 10u * TUT_POINT_PERIOD_MS;
    EXPECT_FALSE(tut_tick(&st, now));
    EXPECT_EQ(st.ripple_seq, seq);
}

// ---- chapter 3: a layer --------------------------------------------------

// Stop on the doorstep of chapter 3.
//
// ⚠️ Entered DIRECTLY, because the chapter-2 -> chapter-3 transition is postponed (see
// ThreeLettersThenChapterTwo). The chapter itself is fully implemented, so it stays
// fully tested — restoring the one transition must not also mean rewriting its tests.
static tut_state_t AtChapterThree(uint32_t *now) {
    tut_state_t st = AtChapterTwo(now);
    FinishPhase(&st, now, TUT_REVEAL_MS);
    tut_hold(&st, TUT_HOLD_SHIFT, true, SHIFT_L, *now);
    FinishPhase(&st, now, TUT_SWEEP_MS);
    // (chapter 2's second stage is skipped: this fixture is about chapter 3)
    st.phase       = TUT_LAYER_WAIT;
    st.phase_start = *now;
    st.hold_on     = false;      // what the postponed boundary does
    return st;
}

TEST(TutorialLayer, ChapterThreeRunsThroughToTheNotationAndDone) {
    uint32_t    now = 0;
    tut_state_t st  = AtChapterThree(&now);
    EXPECT_EQ(st.phase, TUT_LAYER_WAIT);
    EXPECT_TRUE(tut_hold(&st, TUT_HOLD_LAYER, true, TUT_SLOT(0, 11), now));
    EXPECT_EQ(st.phase, TUT_LAYER_SWEEP);
    FinishPhase(&st, &now, TUT_SWEEP_MS);
    EXPECT_EQ(st.phase, TUT_LAYER_HELD);
    FinishPhase(&st, &now, TUT_LAYER_HELD_MS);
    EXPECT_EQ(st.phase, TUT_NOTATION);
    FinishPhase(&st, &now, TUT_NOTATION_MS);
    EXPECT_EQ(st.phase, TUT_DONE);
    EXPECT_FALSE(st.skipped);
}

// ⚠️ Shift STILL being physically down as chapter 2 ends must not read as "the layer
// key is already held" — that would swallow chapter 3's first press and strand the
// tutorial on a screen saying "Now hold Fn" that no press can leave.
TEST(TutorialLayer, AStillHeldShiftDoesNotSwallowTheFirstLayerPress) {
    uint32_t    now = 0;
    tut_state_t st  = AtChapterThree(&now);   // reaches LAYER_WAIT with Shift never released
    EXPECT_FALSE(st.hold_on) << "the chapter boundary must clear the held flag";
    EXPECT_TRUE(tut_hold(&st, TUT_HOLD_LAYER, true, TUT_SLOT(0, 11), now));
    EXPECT_EQ(st.phase, TUT_LAYER_SWEEP);
}

// Each chapter answers to ITS OWN key. A stray Shift in chapter 3 (or a stray layer
// key in chapter 2) does nothing, exactly as a wrong letter does in chapter 1.
TEST(TutorialLayer, TheWrongKindOfKeyDoesNothingInEitherChapter) {
    uint32_t    now = 0;
    tut_state_t st  = AtChapterTwo(&now);
    FinishPhase(&st, &now, TUT_REVEAL_MS);
    ASSERT_EQ(st.phase, TUT_SHIFT_WAIT);
    EXPECT_FALSE(tut_hold(&st, TUT_HOLD_LAYER, true, TUT_SLOT(0, 11), now));
    EXPECT_EQ(st.phase, TUT_SHIFT_WAIT);

    uint32_t    now3 = 0;
    tut_state_t st3  = AtChapterThree(&now3);
    ASSERT_EQ(st3.phase, TUT_LAYER_WAIT);
    EXPECT_FALSE(tut_hold(&st3, TUT_HOLD_SHIFT, true, SHIFT_L, now3));
    EXPECT_EQ(st3.phase, TUT_LAYER_WAIT);
}

TEST(TutorialLayer, LayerWaitHasNoTimeout) {
    uint32_t    now = 0;
    tut_state_t st  = AtChapterThree(&now);
    now += 10u * 60u * 1000u;
    EXPECT_FALSE(tut_tick(&st, now));
    EXPECT_EQ(st.phase, TUT_LAYER_WAIT);
}

TEST(TutorialLayer, SkipStillEndsItFromChapterThree) {
    uint32_t    now = 0;
    tut_state_t st  = AtChapterThree(&now);
    tut_skip(&st, now);
    EXPECT_EQ(st.phase, TUT_DONE);
    EXPECT_TRUE(st.skipped);
}

// ⚠️ The phase predicates, pinned as exact SETS. They decide whether the tutorial OWNS
// the displays or merely annotates a normally-rendering board, so a new phase nobody
// classified would either render nothing or let two writers fight over every keycap.
//
// The contract NOW: nothing is exclusive — the ripple became a general focus service
// that composites over the board's own legends, which was the last thing the normal
// renderer could not express. EXCLUSIVE is kept as a seam for a future chapter that
// genuinely needs the panels; this test is what makes reintroducing one a deliberate,
// visible act rather than a side effect.
TEST(TutorialLayer, ThePhasePredicatesCoverExactlyTheRightPhases) {
    for (uint8_t p = 0; p <= TUT_DONE; ++p) {
        EXPECT_FALSE(tut_phase_is_exclusive(p))
            << "phase " << (int)p << " claims the panels; see the note above";
        EXPECT_EQ(tut_phase_is_intro(p), p != TUT_DONE) << "phase " << (int)p;
        EXPECT_EQ(tut_phase_is_wave(p), p == TUT_RIPPLE || p == TUT_BOARD_REVEAL)
            << "phase " << (int)p;
    }
}

// The chapter-1 phases are ordinary intro phases now, which is the whole point of the
// focus service: no special mode, just a lit set of one key.
TEST(TutorialLayer, ChapterOneIsAnOrdinaryIntroChapter) {
    for (uint8_t p : {TUT_BLANK, TUT_TEXT, TUT_LETTER_IN, TUT_LETTER_WAIT,
                      TUT_RIPPLE, TUT_GAP}) {
        EXPECT_TRUE(tut_phase_is_intro(p)) << "phase " << (int)p;
        EXPECT_FALSE(tut_phase_is_exclusive(p)) << "phase " << (int)p;
    }
}

// An edge inside the chapter re-runs the front WITHOUT restarting the phase clock —
// otherwise drumming on Shift would make the chapter never end.
TEST(TutorialShift, TappingDoesNotExtendTheChapter) {
    uint32_t    now = 0;
    tut_state_t st  = AtChapterTwo(&now);
    FinishPhase(&st, &now, TUT_REVEAL_MS);
    tut_hold(&st, TUT_HOLD_SHIFT, true, SHIFT_L, now);
    FinishPhase(&st, &now, TUT_SWEEP_MS);
    EXPECT_EQ(st.phase, TUT_SHIFT_HELD);
    const uint32_t started = st.phase_start;
    for (int i = 0; i < 8; ++i) {
        now += 50;
        tut_hold(&st, TUT_HOLD_SHIFT, (i % 2) == 0, TUT_SLOT(0, 3), now);
        EXPECT_EQ(st.phase, TUT_SHIFT_HELD);
        EXPECT_EQ(st.phase_start, started) << "an edge restarted the dwell";
    }
}

// ⚠️ EVERY chapter-1 phase, not just the first. A version that checked only
// TUT_LETTER_IN survived a mutation adding TUT_LETTER_WAIT to tut_shift()'s accepting
// cases — and LETTER_WAIT is precisely where a user would fumble Shift, since it is the
// phase that waits indefinitely. Accepting it there jumps to the sweep and silently
// eats the remaining letters.
TEST(TutorialShift, ShiftDoesNothingAnywhereInChapterOne) {
    uint32_t    now = 0;
    tut_state_t st  = Start(now);
    const uint8_t chapter_one[] = {TUT_BLANK, TUT_TEXT, TUT_LETTER_IN,
                                   TUT_LETTER_WAIT, TUT_RIPPLE, TUT_GAP};
    for (uint8_t want : chapter_one) {
        // Walk to `want`, pressing the asked-for letter when the wait needs one.
        for (int guard = 0; guard < 64 && st.phase != want; ++guard) {
            if (st.phase == TUT_LETTER_WAIT) {
                tut_press(&st, st.slots[st.step], now);
            } else {
                // Longer than any timed phase, and tut_tick() makes at most one
                // transition per call, so this steps exactly one phase at a time.
                now += 5000; tut_tick(&st, now);
            }
        }
        ASSERT_EQ(st.phase, want);
        EXPECT_FALSE(tut_hold(&st, TUT_HOLD_SHIFT, true, SHIFT_L, now)) << "phase " << (int)want;
        EXPECT_FALSE(tut_hold(&st, TUT_HOLD_SHIFT, false, SHIFT_L, now)) << "phase " << (int)want;
        EXPECT_EQ(st.phase, want) << "shift moved the phase";
        EXPECT_FALSE(st.hold_on);
    }
}

TEST(TutorialShift, SkipStillEndsItFromChapterTwo) {
    uint32_t    now = 0;
    tut_state_t st  = AtChapterTwo(&now);
    FinishPhase(&st, &now, TUT_REVEAL_MS);
    tut_skip(&st, now);
    EXPECT_EQ(st.phase, TUT_DONE);
    EXPECT_TRUE(st.skipped);
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

// A keycap is 72 x 40 board units, so "one key width" is 72 — the unit the hardware
// feedback is given in ("dissolve in a distance of ~3 keys").
constexpr uint16_t KEY_W = 72;

TEST(TutorialCurves, RippleStartsAsASolidDot) {
    EXPECT_EQ(tut_ripple_radius(0), TUT_RIPPLE_R0);
    EXPECT_EQ(tut_ripple_density(0), 255);
}

// ⚠️ The ripple used to run to ~1800 units, PAST the far corner, so it left the board
// rather than fading. It now dies of its own dissolve, which is what "fades out" means
// and is why there is no longer a clears-the-corner test.
TEST(TutorialCurves, RippleFadesOutInsteadOfLeavingTheBoard) {
    EXPECT_EQ(tut_ripple_radius(255), TUT_RIPPLE_MAX_R);
    EXPECT_EQ(tut_ripple_density(255), 0);
    EXPECT_LT(TUT_RIPPLE_MAX_R, 10 * KEY_W) << "this is a local splash, not a board sweep";
}

// ⚠️ A 4x4 ordered dither needs a band several pixels across before losing half its
// pixels reads as THINNING. On the original 5-unit ring it read as a dotted line, which
// is part of why three rounds of curve tuning never produced a visible dissolve.
// ---- the ring is ROUND ----------------------------------------------------
// The octagonal minimax distance this replaced was off by up to 3.95 %, which at a
// 235-unit reach is ~9 units — "not round, more like 8 or 10 corners" on hardware. A
// distance test cannot catch that on the axes, where the approximation is exact; it has
// to be swept over ANGLE, which is what these do.

// Walk outward along one ray and return the last offset the band still covers.
static uint32_t OuterEdgeAlong(const tut_ring_t &b, double theta) {
    uint32_t last = 0;
    for (uint32_t d = 0; d < 2000; ++d) {
        const int32_t dx = (int32_t)llround(cos(theta) * (double)d);
        const int32_t dy = (int32_t)llround(sin(theta) * (double)d);
        if (tut_ring_hit(&b, dx, dy)) last = d;
    }
    return last;
}

TEST(TutorialRing, IsRoundAtEveryAngle) {
    for (uint16_t r : {32u, 90u, 150u, (unsigned)TUT_RIPPLE_MAX_R}) {
        const tut_ring_t b = tut_ring_bounds(r, TUT_RING_W);
        for (int deg = 0; deg < 360; ++deg) {
            const uint32_t edge = OuterEdgeAlong(b, deg * M_PI / 180.0);
            // One unit of slack for the rounding of the ray's own integer step.
            EXPECT_NEAR((double)edge, (double)r, 1.0)
                << "r=" << r << " angle=" << deg << " edge=" << edge;
        }
    }
}

// ⚠️ 22.5 degrees, NOT 45. This test was first written as "the diagonal reaches as far
// as the axis", which SURVIVED a mutation that put the old octagon back — because a
// MINIMAX approximation is tuned to make 0 and 45 degrees agree, and hides its whole
// error between them, at the octagon's vertex. The obvious two angles to compare are
// precisely the two that cannot show the bug. The sweep above catches it either way;
// this keeps the cheap targeted version pointed at a wavelength it can actually see.
TEST(TutorialRing, TheOctagonVertexAngleReachesAsFarAsTheAxis) {
    const tut_ring_t b = tut_ring_bounds(TUT_RIPPLE_MAX_R, TUT_RING_W);
    const uint32_t axis   = OuterEdgeAlong(b, 0.0);
    const uint32_t vertex = OuterEdgeAlong(b, M_PI / 8.0);
    EXPECT_NEAR((double)vertex, (double)axis, 1.0);
}

TEST(TutorialRing, TheHoleIsRoundToo) {
    const tut_ring_t b = tut_ring_bounds(TUT_RIPPLE_MAX_R, TUT_RING_W);
    for (int deg = 0; deg < 360; ++deg) {
        const double th = deg * M_PI / 180.0;
        uint32_t first = 0;
        for (uint32_t d = 0; d < 2000; ++d) {
            if (tut_ring_hit(&b, (int32_t)llround(cos(th) * (double)d),
                                 (int32_t)llround(sin(th) * (double)d))) { first = d; break; }
        }
        EXPECT_NEAR((double)first, (double)(TUT_RIPPLE_MAX_R - TUT_RING_W), 1.0)
            << "angle=" << deg;
    }
}

// Below its own thickness the band is a solid DISC, which is what makes the strike read
// as a dot rather than as a ring that has to grow out of nothing.
TEST(TutorialRing, StartsAsASolidDisc) {
    const tut_ring_t b = tut_ring_bounds(TUT_RIPPLE_R0, TUT_RING_W);
    EXPECT_EQ(b.inner2, 0u);
    EXPECT_TRUE(tut_ring_hit(&b, 0, 0));
}

// ⚠️ Expressed as a FRACTION of the reach, not in key widths — this bound has now been
// re-derived twice because the ripple keeps being dialled smaller and an absolute floor
// in key widths fights that. What actually has to hold is scale-free: MOST of the wave's
// travel is spent dissolving, so the ring reads as eroding rather than as a hard edge
// that happens to stop. A loose absolute floor keeps it from collapsing to nothing.
TEST(TutorialCurves, MostOfTheTravelIsSpentDissolving) {
    const uint16_t span   = TUT_RIPPLE_MAX_R - TUT_RIPPLE_SOLID_R;
    const uint16_t travel = TUT_RIPPLE_MAX_R - TUT_RIPPLE_R0;
    EXPECT_GE(span * 100u / travel, 50u) << "the solid core has eaten the dissolve";
    EXPECT_LE(span * 100u / travel, 95u) << "there is no solid core left to erode from";
    EXPECT_GE(span, KEY_W) << "a dissolve shorter than one key cannot be seen crossing one";
}

// ---- the repaint front must CROSS THE BOARD -------------------------------
// ⚠️ The guard that was missing. The chapter-2 front was driven from the letter
// ripple's radius (max 205), which is ~12 % of the board's 1673-unit width — so a Shift
// press repainted only the keys within ~2.8 key widths and left the rest stale. Nothing
// failed, because nothing asserted that the front reaches anything.
constexpr uint16_t BOARD_W = 1673;   // SA_BOARD_W
constexpr uint16_t BOARD_H = 563;    // SA_BOARD_H

TEST(TutorialSweep, ReachesEveryKeyFromEveryKey) {
    // Worst case: opposite corners. The front starts at ANY key, so it must clear the
    // full diagonal, not half of it.
    const double diag = std::sqrt((double)BOARD_W * BOARD_W + (double)BOARD_H * BOARD_H);
    EXPECT_GE((double)TUT_SWEEP_MAX_R, diag)
        << "a key in the far corner would never repaint";
}

TEST(TutorialSweep, IsMuchBiggerThanTheDecorativeRipple) {
    // The two radii are for different jobs and must not be quietly re-merged: the
    // ripple is a splash, the sweep is a full-board repaint front.
    EXPECT_GT(TUT_SWEEP_MAX_R, (uint16_t)(TUT_RIPPLE_MAX_R * 4));
}

TEST(TutorialSweep, StartsAtZeroAndEndsAtFullReach) {
    EXPECT_EQ(tut_sweep_radius(0), 0u);
    EXPECT_EQ(tut_sweep_radius(255), TUT_SWEEP_MAX_R);
}

TEST(TutorialSweep, IsMonotonic) {
    for (uint16_t p = 1; p <= 255; ++p) {
        EXPECT_GE(tut_sweep_radius((uint8_t)p), tut_sweep_radius((uint8_t)(p - 1)));
    }
}

// A modifier has to feel immediate. 2.2 s is a fine life for a decorative splash and
// far too slow for "hold Shift and watch".
TEST(TutorialSweep, CrossesTheBoardQuicklyEnoughToFeelLikeAModifier) {
    EXPECT_LE(TUT_SWEEP_MS, 1000u);
    EXPECT_GE(TUT_SWEEP_MS, 200u) << "instant is a flicker, not a sweep";
    // ⚠️ Eased OUT, and the bound has to be tight enough to tell that from linear —
    // "* 2 >= max" passes for a linear ramp by exactly one unit, and survived a
    // mutation that made it linear. Ease-out is what makes the far side of the board
    // catch up in the first third, i.e. what makes a modifier feel immediate rather
    // than like a slow wipe; linear is the "laggy" feel this round is fixing.
    // Measured: the eased curve is at ~75 % of full reach by halfway, linear at 50 %.
    EXPECT_GE(tut_sweep_radius(128) * 100u, (uint32_t)TUT_SWEEP_MAX_R * 65u);
}

// ---- the ring tapers ------------------------------------------------------
TEST(TutorialRing, IsThickestAtTheStrike) {
    EXPECT_EQ(tut_ring_width(0), TUT_RING_W0);
    EXPECT_EQ(tut_ring_width(255), TUT_RING_W);
    EXPECT_GT(TUT_RING_W0, TUT_RING_W);
}

TEST(TutorialRing, ThicknessNeverGrows) {
    for (uint16_t p = 1; p <= 255; ++p) {
        EXPECT_LE(tut_ring_width((uint8_t)p), tut_ring_width((uint8_t)(p - 1)));
    }
}

// Both ends must stay thick enough for a dither to READ as a fading band rather than a
// dotted line — the reason the ring was widened in the first place.
TEST(TutorialRing, StaysThickEnoughToShowADitherThroughout) {
    for (uint16_t p = 0; p <= 255; ++p) {
        EXPECT_GE(tut_ring_width((uint8_t)p), 8u);
    }
}

TEST(TutorialCurves, RippleStaysSolidBeforeItDissolves) {
    for (uint16_t p = 0; p <= 255; ++p) {
        if (tut_ripple_radius((uint8_t)p) > TUT_RIPPLE_SOLID_R) break;
        EXPECT_EQ(tut_ripple_density((uint8_t)p), 255) << "at p=" << p;
    }
    bool dissolved = false;
    for (uint16_t p = 0; p <= 255 && !dissolved; ++p) dissolved = tut_ripple_density((uint8_t)p) < 255;
    EXPECT_TRUE(dissolved);
}

// ⚠️ THE regression, stated as the defect rather than as the implementation: the ring
// must have visibly shed ink WHILE IT IS STILL NEAR THE STRUCK KEY. Every earlier
// curve passed its own monotonicity checks and still failed on hardware because the
// thinning happened too far out to see. Asserted against the RADIUS, which is what the
// eye judges — a re-tune may move any constant as long as this holds.
TEST(TutorialCurves, TheRingHasVisiblyThinnedWithinACoupleOfKeyWidths) {
    uint16_t at_1key = 255, at_2key = 255;
    for (uint16_t p = 0; p <= 255; ++p) {
        const uint16_t r = tut_ripple_radius((uint8_t)p);
        if (r <= TUT_RIPPLE_SOLID_R + KEY_W)     at_1key = tut_ripple_density((uint8_t)p);
        if (r <= TUT_RIPPLE_SOLID_R + 2 * KEY_W) at_2key = tut_ripple_density((uint8_t)p);
    }
    EXPECT_LE(at_1key, 160) << "still " << at_1key << "/255 one key past the solid stretch";
    EXPECT_LE(at_2key, 80) << "still " << at_2key << "/255 two keys past the solid stretch";
}

// Quadratic, not linear: a linear ramp is exactly 128 at the halfway radius and still
// reads as a solid ring there. Shedding fastest early is what makes the dissolve
// legible at all.
TEST(TutorialCurves, ShedsPixelsFastestJustAfterTheSolidStretch) {
    const uint16_t mid_r = (uint16_t)(TUT_RIPPLE_SOLID_R + (TUT_RIPPLE_MAX_R - TUT_RIPPLE_SOLID_R) / 2);
    uint16_t at_mid = 255;
    for (uint16_t p = 0; p <= 255; ++p) {
        if (tut_ripple_radius((uint8_t)p) <= mid_r) at_mid = tut_ripple_density((uint8_t)p);
    }
    EXPECT_LT(at_mid, 100) << "halfway out it is " << at_mid << "/255 — that is a linear ramp";
}

// ⚠️ Black-box statement of the fix: the ink left depends on WHERE the wave is, on
// nothing else. Three rounds of bugs all came from a density that answered to the
// clock while the radius answered to an eased curve, so the two could disagree.
TEST(TutorialCurves, DensityIsAFunctionOfTheRadiusAlone) {
    std::map<uint16_t, uint16_t> seen;
    for (uint16_t p = 0; p <= 255; ++p) {
        const uint16_t r = tut_ripple_radius((uint8_t)p);
        const uint16_t d = tut_ripple_density((uint8_t)p);
        auto it = seen.find(r);
        if (it == seen.end()) seen[r] = d;
        else EXPECT_EQ(it->second, d) << "radius " << r << " gives two densities";
    }
}

TEST(TutorialCurves, DensityFallsAsTheRadiusGrows) {
    uint16_t prev_r = 0, prev_d = 256;
    for (uint16_t p = 0; p <= 255; ++p) {
        const uint16_t r = tut_ripple_radius((uint8_t)p);
        const uint16_t d = tut_ripple_density((uint8_t)p);
        EXPECT_GE(r, prev_r) << "at p=" << p;
        EXPECT_LE(d, prev_d) << "at p=" << p;
        prev_r = r;
        prev_d = d;
    }
}

TEST(TutorialCurves, TravelSpansTheWholeRange) {
    EXPECT_EQ(tut_ripple_travel(0), 0);
    EXPECT_EQ(tut_ripple_travel(255), 255);
}

// A ripple you cannot watch is the defect this feature is most likely to regress into.
TEST(TutorialCurves, RippleIsSlowEnoughToFollow) {
    const uint32_t ms_per_key = (TUT_RIPPLE_MS * KEY_W) / (TUT_RIPPLE_MAX_R - TUT_RIPPLE_R0);
    EXPECT_GE(ms_per_key, 150u) << "ripple crosses a key width in " << ms_per_key << " ms";
}

// ---- the dither field -----------------------------------------------------
// ⚠️ "Too uniform" is the property that has regressed by eye twice, so it is asserted
// numerically here rather than left to a hardware round.

// The old field was a 4x4 ordered matrix indexed by the local keycap pixel: exactly
// periodic in BOTH axes at 4, and identical on every key. That regularity is what the
// eye reads as a screen door closing.
TEST(TutorialDither, HasNoShortPeriodInEitherAxis) {
    for (int period : {1, 2, 4, 8, 16}) {
        int same = 0, total = 0;
        for (int16_t y = -40; y < 40; ++y) {
            for (int16_t x = -40; x < 40; ++x) {
                if (tut_dither(x, y) == tut_dither((int16_t)(x + period), y)) same++;
                if (tut_dither(x, y) == tut_dither(x, (int16_t)(y + period))) same++;
                total += 2;
            }
        }
        // Two independent bytes collide 1/256 of the time; a periodic field collides
        // every time. Anything under a few percent is "no period at this step".
        EXPECT_LT(same * 100, total * 3) << "period " << period << ": " << same << "/" << total
                                         << " positions repeat — the field is regular";
    }
}

// ⚠️ A byte-level period test is NOT enough, and mutation-testing is what showed it:
// dropping the hash's avalanche step leaves ((x*93) ^ (y*159)) & 0xFF, whose BYTES have
// no short period and whose distribution is perfectly uniform — it passes every test
// above — while its bit 0 is (x&1)^(y&1), a literal checkerboard. Structure in a bit
// plane is structure on the panel, so each plane is checked on its own.
TEST(TutorialDither, NoBitPlaneIsPeriodic) {
    for (int bit = 0; bit < 8; ++bit) {
        for (int period : {1, 2, 4, 8}) {
            int same = 0, total = 0;
            for (int16_t y = -40; y < 40; ++y) {
                for (int16_t x = -40; x < 40; ++x) {
                    const int a = (tut_dither(x, y) >> bit) & 1;
                    EXPECT_TRUE(a == 0 || a == 1);
                    same += (a == ((tut_dither((int16_t)(x + period), y) >> bit) & 1)) ? 1 : 0;
                    same += (a == ((tut_dither(x, (int16_t)(y + period)) >> bit) & 1)) ? 1 : 0;
                    total += 2;
                }
            }
            // Two independent bits agree half the time; a periodic plane agrees always.
            EXPECT_LT(same * 100, total * 60)
                << "bit " << bit << " period " << period << ": " << same << "/" << total
                << " agree — that plane is structured";
        }
    }
}

// Unbiased: thresholding at T must light about T/256 of the area, or the ring's density
// curve would not mean what it says.
TEST(TutorialDither, ThresholdLightsTheRequestedFraction) {
    for (int t : {32, 64, 128, 192, 224}) {
        int lit = 0, total = 0;
        for (int16_t y = -60; y < 60; ++y) {
            for (int16_t x = -60; x < 60; ++x) {
                if (t > tut_dither(x, y)) lit++;
                total++;
            }
        }
        const int want = t * total / 256;
        EXPECT_NEAR(lit, want, total / 40) << "at threshold " << t;
    }
}

TEST(TutorialDither, UsesTheWholeByteRange) {
    std::set<uint8_t> seen;
    for (int16_t y = -30; y < 30; ++y)
        for (int16_t x = -30; x < 30; ++x) seen.insert(tut_dither(x, y));
    EXPECT_EQ(seen.size(), 256u);
}

// Static in board space: a pixel the ring has already dropped must stay dropped as the
// wave passes. A field that varied per frame would sparkle instead of eroding — which
// is the reason the original chose an ordered matrix in the first place.
TEST(TutorialDither, IsStableForAGivenPosition) {
    for (int16_t y = -20; y < 20; ++y)
        for (int16_t x = -20; x < 20; ++x)
            EXPECT_EQ(tut_dither(x, y), tut_dither(x, y));
}

std::vector<uint8_t> BothHalves() {
    std::vector<uint8_t> c;
    for (uint8_t i = 0; i < 20; ++i) c.push_back(L(i));
    for (uint8_t i = 0; i < 20; ++i) c.push_back(R(i));
    return c;
}

// ---- the cross-half ripple clock ------------------------------------------

TEST(TutorialClock, EncodeRoundTripsWithinOneQuantum) {
    for (uint32_t ms = 0; ms <= TUT_RIPPLE_MS; ms += 7) {
        const uint32_t back = tut_elapsed_decode(tut_elapsed_encode(ms));
        EXPECT_LE(back, ms) << "at ms=" << ms;                 // never AHEAD of the master
        EXPECT_LT(ms - back, TUT_SYNC_TICK_MS) << "at ms=" << ms;
    }
}

// The whole ripple must be expressible, or the slave would snap back to a stale point
// on every re-push rather than tracking the master.
TEST(TutorialClock, TheWholeRippleFitsTheOneByteField) {
    EXPECT_LT(tut_elapsed_encode(TUT_RIPPLE_MS), 255u);
}

// ⚠️ Saturating, not wrapping. A value past the end must read as "the very end"; a
// wrap would read as "just started" and restart the slave's wave at the origin.
TEST(TutorialClock, SaturatesInsteadOfWrapping) {
    EXPECT_EQ(tut_elapsed_encode(255u * TUT_SYNC_TICK_MS), 255);
    EXPECT_EQ(tut_elapsed_encode(1000000u), 255);
    EXPECT_GE(tut_elapsed_decode(tut_elapsed_encode(1000000u)), TUT_RIPPLE_MS);
}

TEST(TutorialClock, IsMonotonic) {
    uint8_t prev = 0;
    for (uint32_t ms = 0; ms < 8000; ms += 3) {
        const uint8_t e = tut_elapsed_encode(ms);
        EXPECT_GE(e, prev) << "at ms=" << ms;
        prev = e;
    }
}

// The quantum has to be invisible: rounding may not cost more than a rendered frame,
// or the alignment it buys is spent on its own error.
TEST(TutorialClock, QuantumIsUnderOneRenderedFrame) {
    EXPECT_LE(TUT_SYNC_TICK_MS, 16u);
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

// ---- the split sync word's flag byte --------------------------------------
//
// The ARMED-without-ACTIVE case is the whole reason this classifier exists: the
// master carries ARMED for the entire length of its own Eden and pushes it every
// pass, so a half whose Eden finished first receives those packets WHILE running
// the tutorial. Reading one as a stop tore that half down and stamped the
// boot-intro marker, so its first-run tutorial was lost for good.

TEST(TutorialSyncWord, ActiveNeverStops) {
    EXPECT_FALSE(tut_sync_word_stops(TUT_SYNC_ACTIVE));
    EXPECT_FALSE(tut_sync_word_stops(TUT_SYNC_ACTIVE | TUT_SYNC_ARMED));
    // The step rides in the same byte and must not change the verdict.
    for (uint8_t step = 0; step < 4; ++step) {
        const uint8_t w = (uint8_t)(TUT_SYNC_ACTIVE | (step << TUT_SYNC_STEP_SHIFT));
        EXPECT_FALSE(tut_sync_word_stops(w)) << "step " << (int)step;
    }
}

TEST(TutorialSyncWord, ArmedWithoutActiveIsNotYetStartedNotStop) {
    EXPECT_FALSE(tut_sync_word_stops(TUT_SYNC_ARMED));
    for (uint8_t step = 0; step < 4; ++step) {
        const uint8_t w = (uint8_t)(TUT_SYNC_ARMED | (step << TUT_SYNC_STEP_SHIFT));
        EXPECT_FALSE(tut_sync_word_stops(w)) << "step " << (int)step;
    }
}

TEST(TutorialSyncWord, NeitherFlagStops) {
    EXPECT_TRUE(tut_sync_word_stops(0));
    // The retired word the master writes at teardown is all-zero, and a stray step
    // left in it must still read as a stop.
    for (uint8_t step = 0; step < 4; ++step) {
        EXPECT_TRUE(tut_sync_word_stops((uint8_t)(step << TUT_SYNC_STEP_SHIFT)));
    }
}

// ---- chapter 3: the board reveal, languages and scripts -----------------------

// Run chapters 1 and 2 and stop at the start of the board reveal.
static tut_state_t AtChapterThree3(uint32_t *now, uint8_t lang_slot, uint8_t n_preview) {
    tut_state_t st = AtChapterTwo(now);
    tut_set_chapter3(&st, lang_slot, n_preview);
    FinishPhase(&st, now, TUT_REVEAL_MS);
    ClearShiftStage(&st, now);
    ClearShiftStage(&st, now);
    EXPECT_EQ(st.phase, TUT_BOARD_REVEAL);
    return st;
}

constexpr uint8_t LANG_KEY = R(33);

// The reveal is a WAVE: it must bump the ripple sequence, or the slave never starts its
// half of it, and it must start from the Shift that was just held.
TEST(TutorialBoard, RevealStartsFromTheLastShiftAndBumpsTheSequence) {
    // The sequence at the end of chapter 1 (one bump per letter). Chapter 2 bumps it
    // only for its pointing rings, so compare against a run that stops just before the
    // reveal rather than against a constant.
    uint32_t    t0     = 0;
    tut_state_t before = AtChapterTwo(&t0);
    tut_set_chapter3(&before, LANG_KEY, 3);
    FinishPhase(&before, &t0, TUT_REVEAL_MS);
    ClearShiftStage(&before, &t0);
    before.shift_stage = 1;
    ASSERT_EQ(before.phase, TUT_SHIFT_AGAIN);
    const uint8_t seq_before_last_stage = before.ripple_seq;
    ClearShiftStage(&before, &t0);
    ASSERT_EQ(before.phase, TUT_BOARD_REVEAL);
    EXPECT_NE(before.ripple_seq, seq_before_last_stage) << "the slave would never start";
    EXPECT_EQ(before.ripple_slot, SHIFT_R);
    uint32_t    now = 0;
    tut_state_t st  = AtChapterThree3(&now, LANG_KEY, 3);
    EXPECT_EQ(st.ripple_slot, SHIFT_R);
    EXPECT_TRUE(tut_phase_is_wave(st.phase));
}

TEST(TutorialBoard, RunsRevealShowLanguagesPointFinaleDone) {
    uint32_t    now = 0;
    tut_state_t st  = AtChapterThree3(&now, LANG_KEY, 3);
    FinishPhase(&st, &now, TUT_BOARD_REVEAL_MS);
    EXPECT_EQ(st.phase, TUT_BOARD_SHOW);
    FinishPhase(&st, &now, TUT_BOARD_SHOW_MS);
    EXPECT_EQ(st.phase, TUT_LANG_INTRO);
    EXPECT_EQ(tut_preview_index(&st), -1) << "no item is live before the first one";
    FinishPhase(&st, &now, TUT_LANG_INTRO_MS);
    for (int16_t i = 0; i < 3; ++i) {
        EXPECT_EQ(st.phase, TUT_LANG_SHOW);
        EXPECT_EQ(tut_preview_index(&st), i);
        FinishPhase(&st, &now, TUT_LANG_ITEM_MS);
    }
    EXPECT_EQ(st.phase, TUT_LANG_POINT);
    EXPECT_EQ(tut_preview_index(&st), -1) << "the last item must not stay applied";
    EXPECT_EQ(tut_point_slot(&st), LANG_KEY);
    FinishPhase(&st, &now, TUT_LANG_POINT_MS);
    EXPECT_EQ(st.phase, TUT_FINALE);
    FinishPhase(&st, &now, TUT_FINALE_MS);
    EXPECT_EQ(st.phase, TUT_DONE);
    EXPECT_FALSE(st.skipped);
}

// A board with no font pack can render no language but its own and no script at all:
// the reveal is then the whole chapter, and nothing points at a Lang key it will not
// have explained.
TEST(TutorialBoard, NoPreviewItemsGoesStraightToTheFinale) {
    uint32_t    now = 0;
    tut_state_t st  = AtChapterThree3(&now, LANG_KEY, 0);
    FinishPhase(&st, &now, TUT_BOARD_REVEAL_MS);
    FinishPhase(&st, &now, TUT_BOARD_SHOW_MS);
    EXPECT_EQ(st.phase, TUT_LANG_POINT);
}

TEST(TutorialBoard, NoLangKeySkipsThePointer) {
    uint32_t    now = 0;
    tut_state_t st  = AtChapterThree3(&now, TUT_SLOT_NONE, 1);
    FinishPhase(&st, &now, TUT_BOARD_REVEAL_MS);
    FinishPhase(&st, &now, TUT_BOARD_SHOW_MS);
    FinishPhase(&st, &now, TUT_LANG_INTRO_MS);
    FinishPhase(&st, &now, TUT_LANG_ITEM_MS);
    EXPECT_EQ(st.phase, TUT_FINALE);
}

// The pointer at the Lang key fires at once, like the Shift pointers.
TEST(TutorialBoard, LangPointFiresTheRingImmediately) {
    uint32_t    now = 0;
    tut_state_t st  = AtChapterThree3(&now, LANG_KEY, 1);
    FinishPhase(&st, &now, TUT_BOARD_REVEAL_MS);
    FinishPhase(&st, &now, TUT_BOARD_SHOW_MS);
    FinishPhase(&st, &now, TUT_LANG_INTRO_MS);
    FinishPhase(&st, &now, TUT_LANG_ITEM_MS);
    ASSERT_EQ(st.phase, TUT_LANG_POINT);
    const uint8_t seq = st.ripple_seq;
    tut_tick(&st, now + 1u);
    EXPECT_NE(st.ripple_seq, seq);
    EXPECT_EQ(st.ripple_slot, LANG_KEY);
}

TEST(TutorialBoard, PreviewCountIsCapped) {
    tut_state_t st{};
    tut_init(&st, nullptr, nullptr, 0);
    tut_set_chapter3(&st, TUT_SLOT_NONE, 200);
    EXPECT_EQ(st.n_preview, TUT_PREVIEW_MAX);
}

TEST(TutorialBoard, SkipEndsItMidPreview) {
    uint32_t    now = 0;
    tut_state_t st  = AtChapterThree3(&now, LANG_KEY, 3);
    FinishPhase(&st, &now, TUT_BOARD_REVEAL_MS);
    FinishPhase(&st, &now, TUT_BOARD_SHOW_MS);
    FinishPhase(&st, &now, TUT_LANG_INTRO_MS);
    ASSERT_EQ(tut_preview_index(&st), 0);
    tut_skip(&st, now);
    EXPECT_EQ(st.phase, TUT_DONE);
    EXPECT_EQ(tut_preview_index(&st), -1) << "a skip must drop the preview too";
}

// Only the reveal decides key by key; every later chapter-3 phase shows the whole board.
TEST(TutorialBoard, ShowsAllCoversExactlyThePostRevealPhases) {
    for (uint8_t p = 0; p <= TUT_DONE; ++p) {
        const bool want = p >= TUT_BOARD_SHOW && p <= TUT_FINALE;
        EXPECT_EQ(tut_phase_shows_all(p), want) << "phase " << (int)p;
    }
}

TEST(TutorialBoard, ChapterCountFollowsThePhases) {
    EXPECT_EQ(tut_chapter_of(TUT_BLANK), 1u);
    EXPECT_EQ(tut_chapter_of(TUT_GAP), 1u);
    EXPECT_EQ(tut_chapter_of(TUT_REVEAL), 2u);
    EXPECT_EQ(tut_chapter_of(TUT_SHIFT_AGAIN), 2u);
    EXPECT_EQ(tut_chapter_of(TUT_BOARD_REVEAL), 3u);
    EXPECT_EQ(tut_chapter_of(TUT_FINALE), 3u);
    EXPECT_LE(tut_chapter_of(TUT_FINALE), TUT_CHAPTERS);
}

}  // namespace
