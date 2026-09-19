// Copyright 2026 Thomas Pollak
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Tests for base/idle_timeout.h — the idle-timeout presets (HID cmd 40) and,
// mainly, their EEPROM encoding.
//
// The encoding is the part with a bug history in this repo, twice over: an
// unwritten EEPROM byte reads back as ZERO, not 0xFF (QMK's wear levelling
// normalises cleared bytes), which once made latin_assign read as "every key hosts
// 'a'" and forced idle_style to grow a whole sentinel byte because an explicit
// PULSE and "never chosen" were the same value. This field stores the enum biased
// by one so that collision cannot happen — and that is exactly the claim worth
// pinning, because getting it wrong changes the idle behaviour of every keyboard
// already in the field and nothing on the board would say so.

#include "gtest/gtest.h"

extern "C" {
#include "idle_timeout.h"
}

namespace {

TEST(IdleTimeoutEncoding, AnUnwrittenByteMeansTheBoardDefault) {
    // The whole point: 0 is what a byte no build ever wrote reads back as, and it
    // must resolve to the 2 minutes every board had before this setting existed.
    EXPECT_EQ(idle_timeout_unpack(IDLE_TIMEOUT_UNSET), POLY_DEFAULT_IDLE_TIMEOUT);
    EXPECT_EQ(idle_timeout_ms_of(idle_timeout_unpack(0)), 120000u);
}

TEST(IdleTimeoutEncoding, AnErasedByteAlsoMeansTheBoardDefault) {
    // The other direction a real EEPROM can hand back.
    EXPECT_EQ(idle_timeout_unpack(0xFF), POLY_DEFAULT_IDLE_TIMEOUT);
}

TEST(IdleTimeoutEncoding, EveryPresetRoundTrips) {
    for (uint8_t v = 0; v < IDLE_TIMEOUT_COUNT; ++v) {
        const uint8_t stored = idle_timeout_pack(v);
        EXPECT_NE(stored, IDLE_TIMEOUT_UNSET) << "preset " << int(v) << " stored as the unset value";
        EXPECT_EQ(idle_timeout_unpack(stored), v);
    }
}

TEST(IdleTimeoutEncoding, NoPresetEverStoresAsZero) {
    // The property that replaces idle_style's sentinel byte. Stated separately from
    // the round trip because it is the invariant, not a consequence of it: if any
    // valid choice could store as 0, "never chosen" becomes undecidable again.
    for (uint8_t v = 0; v < IDLE_TIMEOUT_COUNT; ++v) {
        EXPECT_GT(idle_timeout_pack(v), 0u);
    }
}

TEST(IdleTimeoutEncoding, GarbageAboveTheRangeFallsBackRatherThanWrappingIn) {
    // stored > IDLE_TIMEOUT_COUNT is out of range. The boundary matters: COUNT
    // itself is the LAST valid stored value (the biasing shifts the whole range up
    // by one), so an off-by-one in the guard would reject the longest preset.
    EXPECT_EQ(idle_timeout_unpack(IDLE_TIMEOUT_COUNT), IDLE_TIMEOUT_COUNT - 1);
    EXPECT_EQ(idle_timeout_unpack(IDLE_TIMEOUT_COUNT + 1), POLY_DEFAULT_IDLE_TIMEOUT);
    EXPECT_EQ(idle_timeout_unpack(200), POLY_DEFAULT_IDLE_TIMEOUT);
}

TEST(IdleTimeoutDurations, ThePresetsAreTheAdvertisedDurations) {
    EXPECT_EQ(idle_timeout_ms_of(IDLE_TIMEOUT_15S),   15000u);
    EXPECT_EQ(idle_timeout_ms_of(IDLE_TIMEOUT_30S),   30000u);
    EXPECT_EQ(idle_timeout_ms_of(IDLE_TIMEOUT_45S),   45000u);
    EXPECT_EQ(idle_timeout_ms_of(IDLE_TIMEOUT_1MIN),  60000u);
    EXPECT_EQ(idle_timeout_ms_of(IDLE_TIMEOUT_2MIN), 120000u);
    EXPECT_EQ(idle_timeout_ms_of(IDLE_TIMEOUT_5MIN), 300000u);
}

TEST(IdleTimeoutDurations, TheDefaultIsWhatFadeOutTimeAlwaysWas) {
    // FADE_OUT_TIME was 120000 for every board that ever shipped. If this moves,
    // every existing keyboard changes behaviour on a firmware update.
    EXPECT_EQ(idle_timeout_ms_of(POLY_DEFAULT_IDLE_TIMEOUT), 120000u);
    EXPECT_EQ(POLY_DEFAULT_IDLE_TIMEOUT_MS, 120000u);
}

TEST(IdleTimeoutDurations, NoPresetIsZeroAndTheyIncrease) {
    // A zero would idle the board on the pass it is read; the ordering is what lets
    // the host render the menu straight from the enum.
    uint32_t prev = 0;
    for (uint8_t v = 0; v < IDLE_TIMEOUT_COUNT; ++v) {
        const uint32_t ms = idle_timeout_ms_of(v);
        EXPECT_GT(ms, prev) << "preset " << int(v) << " is not longer than the one before";
        prev = ms;
    }
}

TEST(IdleTimeoutDurations, OutOfRangeAsksForADeadlineAndGetsTheDefaultNotZero) {
    // Every caller of this is a deadline. Returning 0 for an unknown value would
    // idle the board immediately and forever.
    EXPECT_EQ(idle_timeout_ms_of(IDLE_TIMEOUT_COUNT), POLY_DEFAULT_IDLE_TIMEOUT_MS);
    EXPECT_EQ(idle_timeout_ms_of(0xFF), POLY_DEFAULT_IDLE_TIMEOUT_MS);
}

}  // namespace
