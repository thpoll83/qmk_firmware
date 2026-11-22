// Copyright 2026 Thomas Pollak
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Tests for base/host_unicode_mode.h: the unicode mode values of HID cmd 20.
//
// The host and QMK number the modes differently (macOS is 1 on the wire and 0 in
// QMK), so these tests pin every wire value to its QMK mode. They also pin that
// MacCompose (v38) is the macOS mode plus the F20 flag, and that the unused values
// are still refused.

#include "gtest/gtest.h"

extern "C" {
#include "host_unicode_mode.h"
}

namespace {

void expect_mode(uint8_t wire, uint8_t qmk_mode, bool mac_compose) {
    const host_unicode_mode_t m = host_unicode_mode_decode(wire);
    EXPECT_TRUE(m.ok) << "wire " << int(wire);
    EXPECT_EQ(m.qmk_mode, qmk_mode) << "wire " << int(wire);
    EXPECT_EQ(m.mac_compose, mac_compose) << "wire " << int(wire);
}

TEST(HostUnicodeMode, TheOldValuesKeepTheirQmkMode) {
    expect_mode(0, POLY_QMK_UNICODE_LINUX, false);
    expect_mode(1, POLY_QMK_UNICODE_MACOS, false);
    expect_mode(2, POLY_QMK_UNICODE_WINDOWS, false);
    expect_mode(3, POLY_QMK_UNICODE_WINCOMPOSE, false);
    expect_mode(4, POLY_QMK_UNICODE_BSD, false);
}

TEST(HostUnicodeMode, MacComposeIsTheMacosModeWithTheF20Flag) {
    expect_mode(6, POLY_QMK_UNICODE_MACOS, true);
}

TEST(HostUnicodeMode, UnusedValuesAreRefused) {
    // 5 is the host's InputMethod.Unknown, 7 is the first value after MacCompose.
    for (int wire : {5, 7, 0x7F, 0xFF}) {
        const host_unicode_mode_t m = host_unicode_mode_decode((uint8_t)wire);
        EXPECT_FALSE(m.ok) << "wire " << wire;
        EXPECT_FALSE(m.mac_compose) << "wire " << wire;
    }
}

}  // namespace
