// Copyright 2026 Thomas Pollak
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Tests for base/mode_byte.h — the one-byte "mode flag + known + 6-bit value"
// EEPROM encoding shared by pack/load_auto_brightness and pack/load_os_state
// (state.c). Before the extraction those were two hand-rolled copies of the
// same bit layout; these tests pin the layout once for both.

#include "gtest/gtest.h"

extern "C" {
#include "mode_byte.h"
}

namespace {

TEST(ModeByteTest, RoundTripsEveryCombination) {
    for (int flag = 0; flag <= 1; ++flag) {
        for (int known = 0; known <= 1; ++known) {
            for (uint16_t value = 0; value <= MODE_BYTE_VALUE; ++value) {
                uint8_t packed = mode_byte_pack(flag != 0, known != 0, (uint8_t)value);
                EXPECT_EQ(mode_byte_flag(packed), flag != 0);
                EXPECT_EQ(mode_byte_known(packed), known != 0);
                EXPECT_EQ(mode_byte_value(packed), value);
            }
        }
    }
}

TEST(ModeByteTest, AZeroedByteIsFlagOffUnknownValueZero) {
    // QMK's wear levelling hands an unwritten EEPROM byte back as ZERO, so 0
    // must decode to each setting's factory default: flag clear, nothing known.
    EXPECT_FALSE(mode_byte_flag(0));
    EXPECT_FALSE(mode_byte_known(0));
    EXPECT_EQ(mode_byte_value(0), 0);
}

TEST(ModeByteTest, ValueIsMaskedToSixBits) {
    // An out-of-range value must not bleed into the flag/known bits — that
    // would silently flip the MODE of the setting being saved.
    uint8_t packed = mode_byte_pack(false, false, 0xFF);
    EXPECT_FALSE(mode_byte_flag(packed));
    EXPECT_FALSE(mode_byte_known(packed));
    EXPECT_EQ(mode_byte_value(packed), MODE_BYTE_VALUE);
}

TEST(ModeByteTest, TheThreeFieldsAreDisjoint) {
    EXPECT_EQ(MODE_BYTE_FLAG & MODE_BYTE_KNOWN, 0u);
    EXPECT_EQ(MODE_BYTE_FLAG & MODE_BYTE_VALUE, 0u);
    EXPECT_EQ(MODE_BYTE_KNOWN & MODE_BYTE_VALUE, 0u);
    EXPECT_EQ(MODE_BYTE_FLAG | MODE_BYTE_KNOWN | MODE_BYTE_VALUE, 0xFFu);
}

TEST(ModeByteTest, LegacyBitPositionsAreStable) {
    // These bytes are already in field EEPROMs (auto_brightness since 2026-06,
    // os_state since protocol 7) — the positions are an on-flash ABI, not a
    // style choice. bit7 = flag, bit6 = known, bits0-5 = value.
    EXPECT_EQ(mode_byte_pack(true, false, 0), 0x80);
    EXPECT_EQ(mode_byte_pack(false, true, 0), 0x40);
    EXPECT_EQ(mode_byte_pack(true, true, 0x2A), 0xC0 | 0x2A);
}

}  // namespace
