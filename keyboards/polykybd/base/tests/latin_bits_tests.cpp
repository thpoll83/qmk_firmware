// Copyright 2026 Thomas Pollak
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Tests for base/latin_bits.h: the 6-bit Intl pick and assignment fields. A field can
// cross a byte boundary (6 does not divide 8), and the last field of each array ends
// on its final byte, so these check every field with every value, that a write leaves
// the other fields alone, and that no access goes past the array's end.

#include <cstring>

#include "gtest/gtest.h"

extern "C" {
#include "latin_bits.h"
}

namespace {

// The sizes the EEPROM layout and the split sync depend on.
TEST(LatinBits, Sizes) {
    EXPECT_EQ(LATIN_TARGETS, 38);
    EXPECT_EQ(LATIN_PICK_FIELDS, 76);
    EXPECT_EQ(LATIN_PICK_BYTES, 57);
    EXPECT_EQ(LATIN_ASSIGN_BYTES, 29);
    EXPECT_EQ(LATIN_PICK_BASE_BYTES, 39);
    EXPECT_EQ(LATIN_PICK_EXT_BYTES, 18);
    EXPECT_EQ(LATIN_ASSIGN_BASE_BYTES, 20);
    EXPECT_EQ(LATIN_ASSIGN_EXT_BYTES, 9);
    // The pick split is a plain memcpy only because the letter fields end on a byte.
    EXPECT_EQ((LATIN_LETTER_TARGETS * 2 * LATIN_PICK_BITS) % 8, 0);
}

TEST(LatinBits, EveryPickFieldHoldsEveryValue) {
    for (int f = 0; f < LATIN_PICK_FIELDS; f++) {
        for (unsigned v = 0; v < LATIN_PICK_MAX; v++) {
            uint8_t ex[LATIN_PICK_BYTES];
            memset(ex, 0, sizeof ex);
            latin_pick_set(ex, (uint8_t)f, (uint8_t)v);
            ASSERT_EQ(latin_pick_get(ex, (uint8_t)f), v) << "field " << f;
        }
    }
}

TEST(LatinBits, AllPickFieldsCoexist) {
    uint8_t ex[LATIN_PICK_BYTES];
    memset(ex, 0, sizeof ex);
    for (int f = 0; f < LATIN_PICK_FIELDS; f++) latin_pick_set(ex, (uint8_t)f, (uint8_t)(f % LATIN_PICK_MAX));
    for (int f = 0; f < LATIN_PICK_FIELDS; f++) {
        EXPECT_EQ(latin_pick_get(ex, (uint8_t)f), f % LATIN_PICK_MAX) << "field " << f;
    }
}

TEST(LatinBits, AWriteLeavesTheOtherPickFieldsAlone) {
    for (int f = 0; f < LATIN_PICK_FIELDS; f++) {
        uint8_t ex[LATIN_PICK_BYTES];
        memset(ex, 0xFF, sizeof ex);
        latin_pick_set(ex, (uint8_t)f, 0);
        for (int i = 0; i < LATIN_PICK_FIELDS; i++) {
            ASSERT_EQ(latin_pick_get(ex, (uint8_t)i), i == f ? 0u : LATIN_PICK_MAX - 1u)
                << "set field " << f << ", read field " << i;
        }
    }
}

// The accessors must not touch the byte after the array: the last field ends exactly
// on the final byte, and a 16-bit window there would write one past the end.
TEST(LatinBits, TheLastFieldStaysInsideTheArray) {
    uint8_t buf[LATIN_PICK_BYTES + 1];
    memset(buf, 0, sizeof buf);
    buf[LATIN_PICK_BYTES] = 0xA5;
    latin_pick_set(buf, LATIN_PICK_FIELDS - 1, LATIN_PICK_MAX - 1);
    EXPECT_EQ(buf[LATIN_PICK_BYTES], 0xA5);
    EXPECT_EQ(latin_pick_get(buf, LATIN_PICK_FIELDS - 1), LATIN_PICK_MAX - 1);

    uint8_t as[LATIN_ASSIGN_BYTES + 1];
    memset(as, 0, sizeof as);
    as[LATIN_ASSIGN_BYTES] = 0x5A;
    latin_assign_set(as, LATIN_TARGETS - 1, 3);
    EXPECT_EQ(as[LATIN_ASSIGN_BYTES], 0x5A);
}

TEST(LatinBits, ThePickFieldsAreCaseInterleaved) {
    EXPECT_EQ(latin_pick_field(0, true), 0);
    EXPECT_EQ(latin_pick_field(0, false), 1);
    EXPECT_EQ(latin_pick_field(25, true), 50);
    EXPECT_EQ(latin_pick_field(37, false), 75);
}

TEST(LatinBits, AnUnassignedKeyFallsBackToItsOwnLetter) {
    uint8_t as[LATIN_ASSIGN_BYTES];
    memset(as, LATIN_ASSIGN_FILL, sizeof as);
    for (uint8_t slot = 0; slot < LATIN_TARGETS; slot++) {
        EXPECT_EQ(latin_bits_get(as, LATIN_ASSIGN_BYTES, slot), LATIN_ASSIGN_NONE) << "slot " << (int)slot;
        // A letter key keeps its own letter; a punctuation key has no row.
        EXPECT_EQ(latin_assign_get(as, slot), slot < LATIN_LETTER_TARGETS ? (int)slot : -1);
    }
    latin_assign_set(as, 30, 4);   // a punctuation key hosts E's row
    EXPECT_EQ(latin_assign_get(as, 30), 4);
    EXPECT_EQ(latin_assign_get(as, 29), -1);
    EXPECT_EQ(latin_assign_get(as, 31), -1);
}

// An old EEPROM reads the appended block as zeros (wear levelling), which must read as
// "every key hosts letter 0" here; the format byte, not the bits, says it was never
// written. This pins the value the load code has to gate on.
TEST(LatinBits, AZeroedAssignmentMapReadsLetterA) {
    uint8_t as[LATIN_ASSIGN_BYTES];
    memset(as, 0, sizeof as);
    EXPECT_EQ(latin_assign_get(as, 7), 0);
    EXPECT_EQ(latin_assign_get(as, 30), 0);
}

} // namespace
