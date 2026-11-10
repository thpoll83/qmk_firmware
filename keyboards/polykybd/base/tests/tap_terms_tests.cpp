// Copyright 2026 Thomas Pollak
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Tests for base/tap_terms.h: the tapping-term and Flow Tap ranges (HID cmd 44)
// Auto Shift, and their EEPROM encoding.

#include "gtest/gtest.h"

extern "C" {
#include "tap_terms.h"
}

namespace {

TEST(TapTermsRange, TappingTermBoundaries) {
    EXPECT_FALSE(tapping_term_valid(90));
    EXPECT_TRUE(tapping_term_valid(100));
    EXPECT_TRUE(tapping_term_valid(200));
    EXPECT_TRUE(tapping_term_valid(500));
    EXPECT_FALSE(tapping_term_valid(510));
    EXPECT_FALSE(tapping_term_valid(0));
    EXPECT_FALSE(tapping_term_valid(TAP_TERM_KEEP));
}

TEST(TapTermsRange, FlowTapTermBoundaries) {
    EXPECT_TRUE(flow_tap_term_valid(0));   // off is a real choice
    EXPECT_TRUE(flow_tap_term_valid(150));
    EXPECT_TRUE(flow_tap_term_valid(300));
    EXPECT_FALSE(flow_tap_term_valid(310));
    EXPECT_FALSE(flow_tap_term_valid(TAP_TERM_KEEP));
}

TEST(TapTermsRange, OnlyTenMillisecondSteps) {
    EXPECT_FALSE(tapping_term_valid(205));
    EXPECT_FALSE(tapping_term_valid(101));
    EXPECT_FALSE(flow_tap_term_valid(5));
    EXPECT_FALSE(flow_tap_term_valid(299));
}

TEST(TapTermsEncoding, AnUnwrittenByteMeansTheDefault) {
    EXPECT_EQ(tapping_term_unpack(TAP_TERM_UNSET), POLY_DEFAULT_TAPPING_TERM_MS);
    EXPECT_EQ(flow_tap_term_unpack(TAP_TERM_UNSET), POLY_DEFAULT_FLOW_TAP_TERM_MS);
    EXPECT_EQ(POLY_DEFAULT_TAPPING_TERM_MS, 200u);
    EXPECT_EQ(POLY_DEFAULT_FLOW_TAP_TERM_MS, 150u);
}

TEST(TapTermsEncoding, AnErasedByteMeansTheDefault) {
    EXPECT_EQ(tapping_term_unpack(0xFF), POLY_DEFAULT_TAPPING_TERM_MS);
    EXPECT_EQ(flow_tap_term_unpack(0xFF), POLY_DEFAULT_FLOW_TAP_TERM_MS);
}

TEST(TapTermsEncoding, FlowTapOffIsNotTheUnsetByte) {
    // Without the bias, a stored "off" would read back as "never chosen".
    EXPECT_NE(tap_term_pack(0), TAP_TERM_UNSET);
    EXPECT_EQ(flow_tap_term_unpack(tap_term_pack(0)), 0u);
}

TEST(TapTermsEncoding, EveryValidValueRoundTrips) {
    for (uint16_t ms = TAPPING_TERM_MIN_MS; ms <= TAPPING_TERM_MAX_MS; ms += TAP_TERM_STEP_MS) {
        EXPECT_EQ(tapping_term_unpack(tap_term_pack(ms)), ms) << ms;
    }
    for (uint16_t ms = FLOW_TAP_TERM_MIN_MS; ms <= FLOW_TAP_TERM_MAX_MS; ms += TAP_TERM_STEP_MS) {
        EXPECT_EQ(flow_tap_term_unpack(tap_term_pack(ms)), ms) << ms;
    }
}

TEST(TapTermsEncoding, AnOutOfRangeByteMeansTheDefault) {
    // 90 ms is below the tapping-term range; 310 ms is above the Flow Tap range.
    EXPECT_EQ(tapping_term_unpack(tap_term_pack(90)), POLY_DEFAULT_TAPPING_TERM_MS);
    EXPECT_EQ(flow_tap_term_unpack(tap_term_pack(310)), POLY_DEFAULT_FLOW_TAP_TERM_MS);
}

TEST(TapTermsEncoding, TheLargestValueFitsOneByte) {
    EXPECT_EQ(tap_term_pack(TAPPING_TERM_MAX_MS), 51u);
}

TEST(TapTermsQuickTap, NeverAboveTheTappingTerm) {
    EXPECT_EQ(quick_tap_term_for(200, 100), 100u);
    EXPECT_EQ(quick_tap_term_for(200, 300), 200u);
    EXPECT_EQ(quick_tap_term_for(200, 200), 200u);
}

TEST(AutoShiftRange, Boundaries) {
    EXPECT_FALSE(auto_shift_ms_valid(90));
    EXPECT_TRUE(auto_shift_ms_valid(100));
    EXPECT_TRUE(auto_shift_ms_valid(POLY_DEFAULT_AUTO_SHIFT_MS));
    EXPECT_TRUE(auto_shift_ms_valid(500));
    EXPECT_FALSE(auto_shift_ms_valid(510));
    EXPECT_FALSE(auto_shift_ms_valid(175));   // QMK's default is off the grid
    EXPECT_FALSE(auto_shift_ms_valid(AUTO_SHIFT_MS_KEEP));
    EXPECT_FALSE(auto_shift_ms_valid(TAP_TERM_KEEP));
}

TEST(AutoShiftRange, StepStaysInRange) {
    EXPECT_EQ(auto_shift_ms_step(180, true), 190);
    EXPECT_EQ(auto_shift_ms_step(180, false), 170);
    EXPECT_EQ(auto_shift_ms_step(500, true), 500);
    EXPECT_EQ(auto_shift_ms_step(100, false), 100);
}

TEST(AutoShiftEncoding, UnsetIsOffAtTheDefault) {
    EXPECT_FALSE(auto_shift_unpack_on(0));
    EXPECT_EQ(auto_shift_unpack_ms(0), POLY_DEFAULT_AUTO_SHIFT_MS);
    // The switch bit alone, with no timeout, is not a real choice either.
    EXPECT_FALSE(auto_shift_unpack_on(AUTO_SHIFT_STORED_ON));
}

TEST(AutoShiftEncoding, RoundTripsEveryValue) {
    for (uint16_t ms = AUTO_SHIFT_MIN_MS; ms <= AUTO_SHIFT_MAX_MS; ms += TAP_TERM_STEP_MS) {
        for (int on = 0; on < 2; on++) {
            const uint8_t b = auto_shift_pack(on != 0, ms);
            EXPECT_NE(b, 0u);
            EXPECT_EQ(auto_shift_unpack_on(b), on != 0);
            EXPECT_EQ(auto_shift_unpack_ms(b), ms);
        }
    }
}

TEST(AutoShiftEncoding, OutOfRangeBytesGiveTheDefault) {
    // 0x7F decodes to 1270 ms, 0x09 to 90 ms: both off at the default.
    for (uint8_t b : {(uint8_t)0x7F, (uint8_t)0xFF, (uint8_t)0x09, (uint8_t)0x89}) {
        EXPECT_FALSE(auto_shift_unpack_on(b));
        EXPECT_EQ(auto_shift_unpack_ms(b), POLY_DEFAULT_AUTO_SHIFT_MS);
    }
}

} // namespace
