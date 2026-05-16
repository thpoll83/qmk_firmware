// Copyright 2024 PolyKybd contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Unit tests for keyboards/handwired/polykybd/base/com.c
//
// These tests cover the eight pure-C flag-manipulation helpers declared in
// com.h.  All functions operate only on uint8_t values — no QMK or hardware
// dependencies.

#include "gtest/gtest.h"

extern "C" {
#include "com.h"
}

// ---------------------------------------------------------------------------
// test_flag
// ---------------------------------------------------------------------------

TEST(PolyKybdCom, TestFlagReturnsTrueWhenBitSet) {
    EXPECT_TRUE(test_flag(0xFF, 0x01));
    EXPECT_TRUE(test_flag(0xFF, 0x80));
    EXPECT_TRUE(test_flag(0xAA, 0x02));
}

TEST(PolyKybdCom, TestFlagReturnsFalseWhenBitClear) {
    EXPECT_FALSE(test_flag(0x00, 0x01));
    EXPECT_FALSE(test_flag(0xFE, 0x01));
    EXPECT_FALSE(test_flag(0x55, 0x02));
}

TEST(PolyKybdCom, TestFlagRequiresAllBitsInMask) {
    // test_flag returns true only when ALL bits of f are set in flags.
    EXPECT_TRUE(test_flag(0xFF, 0x03));
    EXPECT_FALSE(test_flag(0x01, 0x03));  // bit 1 is missing
    EXPECT_FALSE(test_flag(0x02, 0x03));  // bit 0 is missing
}

TEST(PolyKybdCom, TestFlagZeroMaskAlwaysTrue) {
    // A zero mask has all its bits (vacuously) set in any value.
    EXPECT_TRUE(test_flag(0x00, 0x00));
    EXPECT_TRUE(test_flag(0xFF, 0x00));
}

TEST(PolyKybdCom, TestFlagWithPolyFlagEnumerators) {
    uint8_t flags = (uint8_t)STATUS_DISP_ON | (uint8_t)RGB_ON;
    EXPECT_TRUE(test_flag(flags, STATUS_DISP_ON));
    EXPECT_TRUE(test_flag(flags, RGB_ON));
    EXPECT_FALSE(test_flag(flags, DBG_ON));
    EXPECT_FALSE(test_flag(flags, DISP_IDLE));
}

// ---------------------------------------------------------------------------
// flag_on
// ---------------------------------------------------------------------------

TEST(PolyKybdCom, FlagOnSetsBit) {
    EXPECT_EQ(flag_on(0x00, 0x01), 0x01u);
    EXPECT_EQ(flag_on(0x00, 0x80), 0x80u);
}

TEST(PolyKybdCom, FlagOnIsIdempotent) {
    // Setting an already-set bit leaves the value unchanged.
    EXPECT_EQ(flag_on(0xFF, 0x01), 0xFFu);
    EXPECT_EQ(flag_on(0x03, 0x03), 0x03u);
}

TEST(PolyKybdCom, FlagOnPreservesOtherBits) {
    EXPECT_EQ(flag_on(0xAA, 0x01), 0xABu);
    EXPECT_EQ(flag_on(0x55, 0x80), 0xD5u);
}

TEST(PolyKybdCom, FlagOnMultipleBits) {
    EXPECT_EQ(flag_on(0x00, 0x0F), 0x0Fu);
    EXPECT_EQ(flag_on(0x0F, 0xF0), 0xFFu);
}

// ---------------------------------------------------------------------------
// flag_off
// ---------------------------------------------------------------------------

TEST(PolyKybdCom, FlagOffClearsBit) {
    EXPECT_EQ(flag_off(0xFF, 0x01), 0xFEu);
    EXPECT_EQ(flag_off(0xFF, 0x80), 0x7Fu);
}

TEST(PolyKybdCom, FlagOffIsIdempotent) {
    // Clearing an already-clear bit leaves the value unchanged.
    EXPECT_EQ(flag_off(0x00, 0x01), 0x00u);
    EXPECT_EQ(flag_off(0xFE, 0x01), 0xFEu);
}

TEST(PolyKybdCom, FlagOffPreservesOtherBits) {
    EXPECT_EQ(flag_off(0xFF, 0xF0), 0x0Fu);
    EXPECT_EQ(flag_off(0xAB, 0x01), 0xAAu);
}

TEST(PolyKybdCom, FlagOffMultipleBits) {
    EXPECT_EQ(flag_off(0xFF, 0x0F), 0xF0u);
    EXPECT_EQ(flag_off(0xFF, 0xFF), 0x00u);
}

// ---------------------------------------------------------------------------
// set_flag
// ---------------------------------------------------------------------------

TEST(PolyKybdCom, SetFlagTrueEqualsFlagOn) {
    EXPECT_EQ(set_flag(0x00, 0x01, true), flag_on(0x00, 0x01));
    EXPECT_EQ(set_flag(0xAA, 0x55, true), flag_on(0xAA, 0x55));
}

TEST(PolyKybdCom, SetFlagFalseEqualsFlagOff) {
    EXPECT_EQ(set_flag(0xFF, 0x01, false), flag_off(0xFF, 0x01));
    EXPECT_EQ(set_flag(0xAA, 0x55, false), flag_off(0xAA, 0x55));
}

TEST(PolyKybdCom, SetFlagDoesNotAffectUnrelatedBits) {
    uint8_t result = set_flag(0b10101010, 0b00001111, true);
    EXPECT_EQ(result, 0b10101111u);
    result = set_flag(0b11111111, 0b00001111, false);
    EXPECT_EQ(result, 0b11110000u);
}

// ---------------------------------------------------------------------------
// toggle_flag
// ---------------------------------------------------------------------------

TEST(PolyKybdCom, ToggleFlagFlipsClearToSet) {
    EXPECT_EQ(toggle_flag(0x00, 0x01), 0x01u);
    EXPECT_EQ(toggle_flag(0x00, 0x80), 0x80u);
}

TEST(PolyKybdCom, ToggleFlagFlipsSetToClear) {
    EXPECT_EQ(toggle_flag(0xFF, 0x01), 0xFEu);
    EXPECT_EQ(toggle_flag(0xFF, 0x80), 0x7Fu);
}

TEST(PolyKybdCom, ToggleFlagTwiceIsIdentity) {
    uint8_t original = 0xA5;
    uint8_t f        = 0x0F;
    EXPECT_EQ(toggle_flag(toggle_flag(original, f), f), original);
}

TEST(PolyKybdCom, ToggleFlagPreservesOtherBits) {
    // Only the masked bit should change.
    EXPECT_EQ(toggle_flag(0xAA, 0x01), 0xABu);
    EXPECT_EQ(toggle_flag(0xAA, 0x02), 0xA8u);
}

// ---------------------------------------------------------------------------
// has_flag_changed
// ---------------------------------------------------------------------------

TEST(PolyKybdCom, HasFlagChangedReturnsTrueWhenBitFlips) {
    // 0 -> 1
    EXPECT_TRUE(has_flag_changed(0x00, 0x01, 0x01));
    // 1 -> 0
    EXPECT_TRUE(has_flag_changed(0x01, 0x00, 0x01));
}

TEST(PolyKybdCom, HasFlagChangedReturnsFalseWhenBitUnchanged) {
    EXPECT_FALSE(has_flag_changed(0x01, 0x01, 0x01));  // stays 1
    EXPECT_FALSE(has_flag_changed(0x00, 0x00, 0x01));  // stays 0
    EXPECT_FALSE(has_flag_changed(0xFF, 0xFF, 0x80));  // stays 1
}

TEST(PolyKybdCom, HasFlagChangedIgnoresUnmaskedBits) {
    // Other bits change but the masked bit stays the same.
    EXPECT_FALSE(has_flag_changed(0x01, 0xFF, 0x01));  // bit0 stays 1
    EXPECT_FALSE(has_flag_changed(0x00, 0xFE, 0x01));  // bit0 stays 0
}

TEST(PolyKybdCom, HasFlagChangedWithMultiBitMask) {
    // At least one bit in the mask changed.
    EXPECT_TRUE(has_flag_changed(0x00, 0x03, 0x03));
    EXPECT_TRUE(has_flag_changed(0x01, 0x02, 0x03));
    EXPECT_FALSE(has_flag_changed(0x03, 0x03, 0x03));
}

// ---------------------------------------------------------------------------
// flag_turned_off
// ---------------------------------------------------------------------------

TEST(PolyKybdCom, FlagTurnedOffReturnsTrueWhenBitGoesFromOneToZero) {
    EXPECT_TRUE(flag_turned_off(0x01, 0x00, 0x01));
    EXPECT_TRUE(flag_turned_off(0xFF, 0xFE, 0x01));
    EXPECT_TRUE(flag_turned_off(0x80, 0x00, 0x80));
}

TEST(PolyKybdCom, FlagTurnedOffReturnsFalseWhenBitStaysOne) {
    EXPECT_FALSE(flag_turned_off(0x01, 0x01, 0x01));
    EXPECT_FALSE(flag_turned_off(0xFF, 0xFF, 0x01));
}

TEST(PolyKybdCom, FlagTurnedOffReturnsFalseWhenBitStaysZero) {
    EXPECT_FALSE(flag_turned_off(0x00, 0x00, 0x01));
    EXPECT_FALSE(flag_turned_off(0xFE, 0xFE, 0x01));
}

TEST(PolyKybdCom, FlagTurnedOffReturnsFalseWhenBitGoesFromZeroToOne) {
    EXPECT_FALSE(flag_turned_off(0x00, 0x01, 0x01));
}

TEST(PolyKybdCom, FlagTurnedOffIgnoresUnmaskedBits) {
    // Bits outside the mask are irrelevant.
    EXPECT_TRUE(flag_turned_off(0xFF, 0xAA, 0x01));   // bit0: 1->0, rest noise
    EXPECT_FALSE(flag_turned_off(0xAA, 0xFE, 0x01));  // bit0: 0->0, rest noise
}

// ---------------------------------------------------------------------------
// flag_turned_on
// ---------------------------------------------------------------------------

TEST(PolyKybdCom, FlagTurnedOnReturnsTrueWhenBitGoesFromZeroToOne) {
    EXPECT_TRUE(flag_turned_on(0x00, 0x01, 0x01));
    EXPECT_TRUE(flag_turned_on(0xFE, 0xFF, 0x01));
    EXPECT_TRUE(flag_turned_on(0x00, 0x80, 0x80));
}

TEST(PolyKybdCom, FlagTurnedOnReturnsFalseWhenBitStaysZero) {
    EXPECT_FALSE(flag_turned_on(0x00, 0x00, 0x01));
    EXPECT_FALSE(flag_turned_on(0xFE, 0xFE, 0x01));
}

TEST(PolyKybdCom, FlagTurnedOnReturnsFalseWhenBitStaysOne) {
    EXPECT_FALSE(flag_turned_on(0x01, 0x01, 0x01));
    EXPECT_FALSE(flag_turned_on(0xFF, 0xFF, 0x80));
}

TEST(PolyKybdCom, FlagTurnedOnReturnsFalseWhenBitGoesFromOneToZero) {
    EXPECT_FALSE(flag_turned_on(0x01, 0x00, 0x01));
}

TEST(PolyKybdCom, FlagTurnedOnIgnoresUnmaskedBits) {
    EXPECT_TRUE(flag_turned_on(0xAA, 0xFF, 0x01));    // bit0: 0->1, rest noise
    EXPECT_FALSE(flag_turned_on(0xFF, 0xAA, 0x01));   // bit0: 1->0, not "turned on"
}

// ---------------------------------------------------------------------------
// Mutual-exclusivity: turned_off and turned_on are never both true
// ---------------------------------------------------------------------------

TEST(PolyKybdCom, TurnedOffAndTurnedOnAreMutuallyExclusive) {
    for (uint8_t a = 0; a < 256; ++a) {
        for (uint8_t b = 0; b < 256; ++b) {
            uint8_t f = 0x01;
            bool off = flag_turned_off((uint8_t)a, (uint8_t)b, f);
            bool on  = flag_turned_on ((uint8_t)a, (uint8_t)b, f);
            EXPECT_FALSE(off && on) << "flags1=" << (int)a << " flags2=" << (int)b;
            if (off || on) {
                // If either fired, has_flag_changed must agree.
                EXPECT_TRUE(has_flag_changed((uint8_t)a, (uint8_t)b, f));
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Overlay flag constants sanity checks
// ---------------------------------------------------------------------------

TEST(PolyKybdCom, OverlayActionFlagsContainsExpectedBits) {
    EXPECT_TRUE(test_flag(OVERLAY_ACTION_FLAGS, RESET_BUFFERS));
    EXPECT_TRUE(test_flag(OVERLAY_ACTION_FLAGS, USAGE_RESET));
    EXPECT_TRUE(test_flag(OVERLAY_ACTION_FLAGS, MAPPING_RESET));
    EXPECT_TRUE(test_flag(OVERLAY_ACTION_FLAGS, MAPPING_ALLSET));
    // Must NOT include MIRROR_OVERLAYS (that's in OVERLAY_SYNCED_STATE_FLAGS)
    EXPECT_FALSE(test_flag(OVERLAY_ACTION_FLAGS, MIRROR_OVERLAYS));
}

TEST(PolyKybdCom, OverlaySyncedStateFlagsContainsMirrorOverlays) {
    EXPECT_TRUE(test_flag(OVERLAY_SYNCED_STATE_FLAGS, MIRROR_OVERLAYS));
    // Should not contain action flags
    EXPECT_FALSE(test_flag(OVERLAY_SYNCED_STATE_FLAGS, RESET_BUFFERS));
    EXPECT_FALSE(test_flag(OVERLAY_SYNCED_STATE_FLAGS, USAGE_RESET));
}

// ---------------------------------------------------------------------------
// Regression: flag_off with full mask must yield zero
// ---------------------------------------------------------------------------

TEST(PolyKybdCom, FlagOffFullMaskYieldsZero) {
    EXPECT_EQ(flag_off(0xFF, 0xFF), 0x00u);
}

// ---------------------------------------------------------------------------
// Regression: toggle on 0x00 and 0xFF for every single-bit mask
// ---------------------------------------------------------------------------

TEST(PolyKybdCom, ToggleSingleBitMasksOnAllZero) {
    for (int shift = 0; shift < 8; ++shift) {
        uint8_t bit = (uint8_t)(1u << shift);
        EXPECT_EQ(toggle_flag(0x00, bit), bit) << "shift=" << shift;
    }
}

TEST(PolyKybdCom, ToggleSingleBitMasksOnAllOne) {
    for (int shift = 0; shift < 8; ++shift) {
        uint8_t bit = (uint8_t)(1u << shift);
        EXPECT_EQ(toggle_flag(0xFF, bit), (uint8_t)(0xFF & ~bit)) << "shift=" << shift;
    }
}