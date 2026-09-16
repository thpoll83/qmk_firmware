// Copyright 2026 Thomas Pollak
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Tests for base/status_brightness.h — the map that puts the I2C status OLED on
// the SAME brightness scale as the per-keycap SPI panels. The two ends of the
// map are the load-bearing part: FULL_BRIGHT must land exactly on
// OLED_BRIGHTNESS (so the top of the scale keeps the look it has always had),
// and DISP_OFF must be the only input that yields 0 (every other level has to
// stay lit, because the status panel is what a user reads to find the
// brightness control in the first place).

#include "gtest/gtest.h"

extern "C" {
#include "status_brightness.h"
}

namespace {

TEST(StatusBrightnessTest, FullBrightKeepsTodaysFixedValue) {
    // The whole point of anchoring the top of the range: a board at full
    // brightness must look byte-identical to the fixed OLED_BRIGHTNESS the
    // panel used before it had a control at all.
    EXPECT_EQ(poly_status_brightness(FULL_BRIGHT), OLED_BRIGHTNESS);
}

TEST(StatusBrightnessTest, OffIsTheOnlyInputThatGoesDark) {
    EXPECT_EQ(poly_status_brightness(DISP_OFF), 0);
    for (uint16_t c = 1; c <= FULL_BRIGHT; ++c) {
        // Anchored on 1, NOT on POLY_STATUS_MIN_BRIGHT: comparing the map against
        // its own floor passes trivially when the floor itself is set to 0, which
        // is precisely the regression this test exists to catch.
        EXPECT_GE(poly_status_brightness((uint8_t)c), 1)
            << "contrast " << c << " must stay lit, not fade to black";
        EXPECT_GE(poly_status_brightness((uint8_t)c), POLY_STATUS_MIN_BRIGHT)
            << "contrast " << c << " must not fall below the configured floor";
    }
}

TEST(StatusBrightnessTest, LowestLitLevelIsTheFloor) {
    EXPECT_EQ(poly_status_brightness(1), POLY_STATUS_MIN_BRIGHT);
}

TEST(StatusBrightnessTest, IsMonotonic) {
    // A brightness key must never make the panel darker than the level below it.
    for (uint16_t c = 1; c < FULL_BRIGHT; ++c) {
        EXPECT_LE(poly_status_brightness((uint8_t)c), poly_status_brightness((uint8_t)(c + 1)))
            << "contrast " << c << " -> " << (c + 1);
    }
}

TEST(StatusBrightnessTest, ClampsAboveFullBright) {
    // hid_com.c range-checks cmd 13, but the map is also fed get_active_brightness(),
    // whose host-auto value has travelled over the wire. Saturate rather than wrap:
    // the naive expression would overflow past the register's 0..255 meaning.
    for (uint16_t c = FULL_BRIGHT; c <= 255; ++c) {
        EXPECT_EQ(poly_status_brightness((uint8_t)c), OLED_BRIGHTNESS) << "contrast " << c;
    }
}

TEST(StatusBrightnessTest, IdleLevelStillDisplays) {
    // Idle hands the panel to oled_render_logos() and its HARDWARE scroll keeps
    // running, so the idle level is a dim contrast register, NOT oled_off().
    // 0 is the faintest an SSD1306 shows while still displaying.
    EXPECT_LT(POLY_STATUS_IDLE_BRIGHT, POLY_STATUS_MIN_BRIGHT);
}

TEST(StatusBrightnessTest, MidScaleIsProportional) {
    // Spot-check the middle so a rounding change can't quietly re-shape the curve:
    // half the keycap scale should be about half the panel's range above the floor.
    const int expected = POLY_STATUS_MIN_BRIGHT +
                         ((FULL_BRIGHT / 2 - 1) * (OLED_BRIGHTNESS - POLY_STATUS_MIN_BRIGHT) +
                          (FULL_BRIGHT - 1) / 2) / (FULL_BRIGHT - 1);
    EXPECT_EQ(poly_status_brightness(FULL_BRIGHT / 2), expected);
}

}  // namespace
