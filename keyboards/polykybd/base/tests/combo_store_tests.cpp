// Copyright 2026 Thomas Pollak
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Tests for base/combo_store.h: record validation, the wire encoding and the
// tap-dance decision (HID cmd 48).

#include "gtest/gtest.h"

#include <cstring>

extern "C" {
#include "combo_store.h"
}

namespace {

constexpr uint16_t A = 0x04, B = 0x05, C = 0x06, ESC = 0x29, CAPS = 0x39;
constexpr uint16_t MO1 = 0x5221, TG2 = 0x5262, TO3 = 0x5203;
constexpr uint16_t LSFT_A = 0x0204;           // S(KC_A): basic + modifier
constexpr uint16_t MT_LSFT_A = 0x2204;        // LSFT_T(KC_A): mod-tap, not supported
constexpr uint16_t MACRO0 = 0x7700;

TEST(ComboRecord, EmptyIsValidAndInactive) {
    poly_combo_rec_t r = {};
    EXPECT_TRUE(poly_combo_rec_valid(&r));
    EXPECT_FALSE(poly_combo_rec_active(&r));
}

TEST(ComboRecord, TwoToFourPackedKeysWithAResult) {
    poly_combo_rec_t two = {{A, B, 0, 0}, ESC};
    poly_combo_rec_t four = {{A, B, C, LSFT_A}, MACRO0};   // any result keycode
    EXPECT_TRUE(poly_combo_rec_valid(&two));
    EXPECT_TRUE(poly_combo_rec_active(&two));
    EXPECT_EQ(poly_combo_key_count(&four), 4);
    EXPECT_TRUE(poly_combo_rec_active(&four));
}

TEST(ComboRecord, MalformedRecordsAreRefused) {
    poly_combo_rec_t one_key   = {{A, 0, 0, 0}, ESC};
    poly_combo_rec_t gap       = {{A, 0, B, 0}, ESC};
    poly_combo_rec_t no_result = {{A, B, 0, 0}, 0};
    EXPECT_FALSE(poly_combo_rec_valid(&one_key));
    EXPECT_FALSE(poly_combo_rec_valid(&gap));
    EXPECT_EQ(poly_combo_key_count(&gap), 0);
    EXPECT_FALSE(poly_combo_rec_valid(&no_result));
}

TEST(ComboRecord, TheSameKeyTwiceIsRefused) {
    // QMK maps both presses to one trigger index, so it could never complete.
    poly_combo_rec_t twice     = {{A, A, 0, 0}, ESC};
    poly_combo_rec_t twice_far = {{A, B, ESC, B}, ESC};
    EXPECT_FALSE(poly_combo_rec_valid(&twice));
    EXPECT_FALSE(poly_combo_rec_valid(&twice_far));
    EXPECT_FALSE(poly_combo_rec_active(&twice));
}

TEST(ComboRecord, WireBytesAreLittleEndian) {
    poly_combo_rec_t r = {{A, MO1, 0, 0}, 0x1234};
    uint8_t wire[POLY_COMBO_REC_BYTES];
    poly_combo_words_put(wire, (const uint16_t *)&r);
    const uint8_t want[POLY_COMBO_REC_BYTES] = {0x04, 0, 0x21, 0x52, 0, 0, 0, 0, 0x34, 0x12};
    EXPECT_EQ(0, memcmp(wire, want, sizeof(want)));
    poly_combo_rec_t back = {};
    poly_combo_words_get(wire, (uint16_t *)&back);
    EXPECT_EQ(0, memcmp(&back, &r, sizeof(r)));
}

TEST(TapDanceRecord, SupportedKeycodes) {
    for (uint16_t kc : {(uint16_t)0, A, LSFT_A, MO1, TG2, TO3}) {
        EXPECT_TRUE(poly_td_keycode_supported(kc)) << kc;
    }
    for (uint16_t kc : {MT_LSFT_A, MACRO0, (uint16_t)0x5240 /* DF(0) */, (uint16_t)0x7E00}) {
        EXPECT_FALSE(poly_td_keycode_supported(kc)) << kc;
    }
}

TEST(TapDanceRecord, TermIsZeroOrTheTappingTermRange) {
    EXPECT_TRUE(poly_td_term_valid(0));
    EXPECT_TRUE(poly_td_term_valid(100));
    EXPECT_TRUE(poly_td_term_valid(500));
    EXPECT_FALSE(poly_td_term_valid(90));
    EXPECT_FALSE(poly_td_term_valid(510));
    EXPECT_FALSE(poly_td_term_valid(205));
    poly_tap_dance_rec_t bad = {A, MT_LSFT_A, 0, 0, 0};
    poly_tap_dance_rec_t ok  = {ESC, MO1, CAPS, 0, 250};
    EXPECT_FALSE(poly_td_rec_valid(&bad));
    EXPECT_TRUE(poly_td_rec_valid(&ok));
}

TEST(TapDancePlan, AllFourActions) {
    poly_tap_dance_rec_t r = {ESC, MO1, CAPS, TG2, 0};
    poly_td_plan_t p = poly_td_plan(&r, 1, false);
    EXPECT_EQ(p.tap_kc, ESC); EXPECT_EQ(p.taps, 1); EXPECT_EQ(p.hold_kc, 0);
    p = poly_td_plan(&r, 1, true);
    EXPECT_EQ(p.taps, 0); EXPECT_EQ(p.hold_kc, MO1);
    p = poly_td_plan(&r, 2, false);
    EXPECT_EQ(p.tap_kc, CAPS); EXPECT_EQ(p.taps, 1); EXPECT_EQ(p.hold_kc, 0);
    p = poly_td_plan(&r, 2, true);
    EXPECT_EQ(p.taps, 0); EXPECT_EQ(p.hold_kc, TG2);
}

TEST(TapDancePlan, UnsetActionsFallBackToOnTap) {
    poly_tap_dance_rec_t r = {A, 0, 0, 0, 0};
    poly_td_plan_t p = poly_td_plan(&r, 1, true);
    EXPECT_EQ(p.hold_kc, A);
    p = poly_td_plan(&r, 2, false);
    EXPECT_EQ(p.tap_kc, A); EXPECT_EQ(p.taps, 2); EXPECT_EQ(p.hold_kc, 0);
    p = poly_td_plan(&r, 2, true);
    EXPECT_EQ(p.tap_kc, A); EXPECT_EQ(p.taps, 1); EXPECT_EQ(p.hold_kc, A);
    p = poly_td_plan(&r, 3, false);
    EXPECT_EQ(p.taps, 3);
}

TEST(TapDancePlan, EmptyRecordDoesNothing) {
    poly_tap_dance_rec_t r = {};
    for (uint8_t n = 0; n < 4; n++) {
        for (bool held : {false, true}) {
            poly_td_plan_t p = poly_td_plan(&r, n, held);
            EXPECT_EQ(p.taps, 0);
            EXPECT_EQ(p.hold_kc, 0);
        }
    }
}

} // namespace
