// Copyright 2026 Thomas Pollak
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Tests for base/tap_terms.h: the tapping-term and Flow Tap ranges (HID cmd 44)
// and their EEPROM encoding.

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

} // namespace
