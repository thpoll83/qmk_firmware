// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The KC_IME held-key table. Each case is one of the review findings on #355 that
// once left Right Alt down (or dropped one the user was holding), replayed against
// a fake matrix and a fake host report that counts what reached the host.

#include <set>
#include <utility>

#include "gtest/gtest.h"

extern "C" {
#include "ime_held_slots.h"
}

namespace {

constexpr uint8_t RALT = 0xE6;
constexpr uint8_t NUBS = 0x64;

// The keyboard as the table sees it: which keys are down, and what the report holds.
// register/unregister follow QMK's semantics for the report: a usage is in it or not,
// with no count of holders.
struct Fake {
    std::set<std::pair<uint8_t, uint8_t>> down;
    std::set<uint8_t>                     report;
    int                                   taps = 0;
};
Fake* g;

bool key_down(uint8_t r, uint8_t c) { return g->down.count({r, c}) != 0; }
bool in_report(uint8_t u) { return g->report.count(u) != 0; }
void press(uint8_t u) { g->report.insert(u); }
void release(uint8_t u) { g->report.erase(u); }
void tap(uint8_t) { ++g->taps; }

const ime_slot_io_t kIo = {key_down, in_report, press, release, tap};

class ImeHeldSlots : public ::testing::Test {
   protected:
    void SetUp() override {
        g = &f;
        t = ime_slots_t{};
    }
    // A real key event: QMK updates the matrix first, then delivers the event,
    // and process_record_user runs forget_cleared before anything else.
    void Down(uint8_t r, uint8_t c, uint8_t usage) {
        f.down.insert({r, c});
        ime_slots_forget_cleared(&t, &kIo);
        ime_slots_press(&t, r, c, usage, &kIo);
    }
    void Up(uint8_t r, uint8_t c) {
        f.down.erase({r, c});
        ime_slots_forget_cleared(&t, &kIo);
        ime_slots_release(&t, r, c, &kIo);
    }
    bool Held(uint8_t u) { return f.report.count(u) != 0; }

    Fake        f;
    ime_slots_t t;
};

TEST_F(ImeHeldSlots, PressHoldsAndReleaseLetsGo) {
    Down(1, 1, RALT);
    EXPECT_TRUE(Held(RALT));
    Up(1, 1);
    EXPECT_FALSE(Held(RALT));
}

TEST_F(ImeHeldSlots, ReleaseLetsGoOfWhatThePressRegistered) {
    // The language changes while the key is down: the release must still undo RALT.
    Down(1, 1, RALT);
    Up(1, 1);
    EXPECT_TRUE(f.report.empty());
}

TEST_F(ImeHeldSlots, TwoKeysWithDifferentUsagesEachReleaseTheirOwn) {
    // Hold one KC_IME on en-US (RALT), switch to a NUBS language, hold a second.
    Down(1, 1, RALT);
    Down(2, 2, NUBS);
    Up(1, 1);
    EXPECT_FALSE(Held(RALT));
    EXPECT_TRUE(Held(NUBS));
    Up(2, 2);
    EXPECT_TRUE(f.report.empty());
}

TEST_F(ImeHeldSlots, OverlappingHoldsOfTheSameUsageKeepItUntilTheLast) {
    Down(1, 1, RALT);
    Down(2, 2, RALT);
    Up(1, 1);
    EXPECT_TRUE(Held(RALT));   // the other finger is still on it
    Up(2, 2);
    EXPECT_FALSE(Held(RALT));
}

TEST_F(ImeHeldSlots, OverflowPressDoesNotStealAHeldUsage) {
    for (uint8_t i = 0; i < IME_HELD_SLOTS; ++i) Down(1, i, RALT);
    Down(3, 3, RALT);           // a fifth key: no slot left
    EXPECT_TRUE(Held(RALT));
    EXPECT_EQ(f.taps, 0);       // a tap would have released it under four fingers
}

TEST_F(ImeHeldSlots, OverflowPressOfANewUsageIsATap) {
    for (uint8_t i = 0; i < IME_HELD_SLOTS; ++i) Down(1, i, RALT);
    Down(3, 3, NUBS);
    EXPECT_EQ(f.taps, 1);
    EXPECT_FALSE(Held(NUBS));   // never registered, so nothing can stick
}

TEST_F(ImeHeldSlots, SwallowedReleaseAfterClearKeyboardLeavesNothingStuck) {
    // Hold KC_IME, the macro picker opens and clears the report, the release is
    // swallowed by the picker; then the same key is pressed and released again.
    Down(1, 1, RALT);
    f.report.clear();                 // clear_keyboard()
    f.down.erase({1, 1});             // the release the picker swallowed
    ime_slots_forget_cleared(&t, &kIo);
    Down(1, 1, RALT);
    Up(1, 1);
    EXPECT_FALSE(Held(RALT));
}

TEST_F(ImeHeldSlots, StaleSlotDoesNotDropANewerOrdinaryRightAlt) {
    // The picker clears a KC_IME hold and swallows its release. The user then holds
    // the ordinary KC_RALT and presses KC_IME on a NUBS language: the stale slot
    // must not release the RALT the user is holding.
    Down(1, 1, RALT);
    f.report.clear();                 // clear_keyboard()
    f.down.erase({1, 1});             // swallowed release
    f.down.insert({4, 4});            // ordinary KC_RALT, not a KC_IME key:
    ime_slots_forget_cleared(&t, &kIo);  // process_record_user runs before QMK...
    f.report.insert(RALT);            // ...registers it
    Down(2, 2, NUBS);
    EXPECT_TRUE(Held(RALT));
}

TEST_F(ImeHeldSlots, ReleaseQueuedInTheSameScanIsNotLost) {
    // Key A releases and key B presses in one scan; QMK updates the matrix for both,
    // then delivers B's press before A's release (row/column order).
    Down(5, 5, RALT);                 // A, on en-US
    f.down.erase({5, 5});             // A's release is in the matrix...
    f.down.insert({1, 1});            // ...as is B's press
    ime_slots_forget_cleared(&t, &kIo);
    ime_slots_press(&t, 1, 1, NUBS, &kIo);  // B, after a language switch
    ime_slots_forget_cleared(&t, &kIo);
    ime_slots_release(&t, 5, 5, &kIo);       // A's queued release
    EXPECT_FALSE(Held(RALT));
    EXPECT_TRUE(Held(NUBS));
}

TEST_F(ImeHeldSlots, ADeadSlotDoesNotKeepAUsageHeld) {
    // Two KC_IME keys hold RALT; one release is lost without clearing the report.
    // When the other key lets go, nobody is left holding RALT: it must go.
    Down(1, 1, RALT);
    Down(2, 2, RALT);
    f.down.erase({1, 1});             // released, but the event never arrives
    Up(2, 2);
    EXPECT_FALSE(Held(RALT));
}

TEST_F(ImeHeldSlots, DeadSlotsDoNotBlockANewHold) {
    // Every slot left behind by a lost release: a new press must still get a slot
    // (a real hold), not fall back to the overflow tap.
    for (uint8_t i = 0; i < IME_HELD_SLOTS; ++i) Down(1, i, RALT);
    for (uint8_t i = 0; i < IME_HELD_SLOTS; ++i) f.down.erase({1, i});
    Down(3, 3, NUBS);
    EXPECT_EQ(f.taps, 0);
    EXPECT_TRUE(Held(NUBS));
    EXPECT_FALSE(Held(RALT));         // and the dead slots' RALT was let go
}

TEST_F(ImeHeldSlots, ReleaseOfAKeyWithNoSlotDoesNothing) {
    f.report.insert(RALT);            // somebody else's
    Up(7, 7);
    EXPECT_TRUE(Held(RALT));
}

} // namespace
