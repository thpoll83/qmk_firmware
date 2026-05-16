// Copyright 2024 PolyKybd contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Unit tests for keyboards/handwired/polykybd/base/crc32.c
//
// crc32_1byte implements the standard CRC-32/ISO-HDLC algorithm
// (also called CRC-32b, used in Ethernet, ZIP, PNG, etc.).
// Reference values were computed independently with Python's zlib.crc32().
// The function signature is:
//   uint32_t crc32_1byte(const void* data, uint16_t length, uint32_t previousCrc32)
// where previousCrc32 = 0 for a fresh computation.

#include "gtest/gtest.h"
#include <cstring>

extern "C" {
#include "crc32.h"
}

// ---------------------------------------------------------------------------
// Empty input
// ---------------------------------------------------------------------------

TEST(PolyKybdCrc32, EmptyInputReturnsCrc32OfEmpty) {
    // CRC-32 of zero bytes with initial value 0 is 0x00000000.
    EXPECT_EQ(crc32_1byte(nullptr, 0, 0x00000000u), 0x00000000u);
}

TEST(PolyKybdCrc32, EmptyInputWithNonZeroPrevCrcReturnsZero) {
    // Even with a "previous" CRC the result for zero bytes is the
    // finalised complement of ~previousCrc32 ^ 0xFFFFFFFF = previousCrc32.
    // Since length=0, the while loop never executes, so ~(~prev) = prev.
    uint32_t prev = 0x12345678u;
    EXPECT_EQ(crc32_1byte(nullptr, 0, prev), prev);
}

// ---------------------------------------------------------------------------
// Single-byte inputs — verified against zlib.crc32()
// ---------------------------------------------------------------------------

TEST(PolyKybdCrc32, SingleByteZero) {
    // zlib.crc32(b'\x00') == 0xD202EF8D
    const uint8_t data = 0x00;
    EXPECT_EQ(crc32_1byte(&data, 1, 0), 0xD202EF8Du);
}

TEST(PolyKybdCrc32, SingleByteFF) {
    // zlib.crc32(b'\xff') == 0xFF000000
    const uint8_t data = 0xFF;
    EXPECT_EQ(crc32_1byte(&data, 1, 0), 0xFF000000u);
}

TEST(PolyKybdCrc32, SingleByteA) {
    // zlib.crc32(b'a') == 0xE8B7BE43
    const uint8_t data = 'a';
    EXPECT_EQ(crc32_1byte(&data, 1, 0), 0xE8B7BE43u);
}

// ---------------------------------------------------------------------------
// Multi-byte well-known strings
// ---------------------------------------------------------------------------

TEST(PolyKybdCrc32, StringFoobar) {
    // zlib.crc32(b'foobar') == 0x9EF61F95
    const char* data = "foobar";
    EXPECT_EQ(crc32_1byte(data, (uint16_t)strlen(data), 0), 0x9EF61F95u);
}

TEST(PolyKybdCrc32, String123456789) {
    // The canonical CRC-32/ISO-HDLC check value for "123456789" is 0xCBF43926.
    const char* data = "123456789";
    EXPECT_EQ(crc32_1byte(data, (uint16_t)strlen(data), 0), 0xCBF43926u);
}

TEST(PolyKybdCrc32, StringHello) {
    // zlib.crc32(b'Hello') == 0xF7D18982
    const char* data = "Hello";
    EXPECT_EQ(crc32_1byte(data, (uint16_t)strlen(data), 0), 0xF7D18982u);
}

// ---------------------------------------------------------------------------
// Binary data
// ---------------------------------------------------------------------------

TEST(PolyKybdCrc32, AllZeroBytes4) {
    // zlib.crc32(b'\x00\x00\x00\x00') == 0x2144DF1C
    const uint8_t data[4] = {0, 0, 0, 0};
    EXPECT_EQ(crc32_1byte(data, 4, 0), 0x2144DF1Cu);
}

TEST(PolyKybdCrc32, AllFFBytes4) {
    // zlib.crc32(b'\xff\xff\xff\xff') == 0xFFFFFFFF
    const uint8_t data[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    EXPECT_EQ(crc32_1byte(data, 4, 0), 0xFFFFFFFFu);
}

TEST(PolyKybdCrc32, SequentialBytes) {
    // zlib.crc32(bytes(range(16))) == 0xCECEE288
    uint8_t data[16];
    for (int i = 0; i < 16; ++i) data[i] = (uint8_t)i;
    EXPECT_EQ(crc32_1byte(data, 16, 0), 0xCECEE288u);
}

// ---------------------------------------------------------------------------
// Chaining / incremental CRC
// ---------------------------------------------------------------------------

TEST(PolyKybdCrc32, IncrementalEqualsOneShot) {
    // CRC computed in two pieces must equal a single-pass CRC.
    const char* data = "Hello, World!";
    uint16_t    len  = (uint16_t)strlen(data);
    uint16_t    half = len / 2;

    uint32_t single = crc32_1byte(data, len, 0);
    uint32_t part1  = crc32_1byte(data, half, 0);
    uint32_t full   = crc32_1byte(data + half, (uint16_t)(len - half), part1);

    EXPECT_EQ(full, single);
}

TEST(PolyKybdCrc32, IncrementalByteAtATime) {
    const char* data = "123456789";
    uint16_t    len  = (uint16_t)strlen(data);
    uint32_t    crc  = 0;
    for (uint16_t i = 0; i < len; ++i) {
        crc = crc32_1byte(data + i, 1, crc);
    }
    EXPECT_EQ(crc, 0xCBF43926u);
}

// ---------------------------------------------------------------------------
// Boundary: length == 1 and length == 0 adjacently
// ---------------------------------------------------------------------------

TEST(PolyKybdCrc32, LengthZeroIsIdentityOnPreviousCrc) {
    const char*  data   = "test";
    uint32_t     crc    = crc32_1byte(data, (uint16_t)strlen(data), 0);
    uint32_t     noop   = crc32_1byte(data, 0, crc);  // zero bytes
    EXPECT_EQ(noop, crc);
}

// ---------------------------------------------------------------------------
// Determinism: same input always yields same output
// ---------------------------------------------------------------------------

TEST(PolyKybdCrc32, DeterministicOnSameInput) {
    const char* data = "PolyKybd firmware";
    uint32_t    r1   = crc32_1byte(data, (uint16_t)strlen(data), 0);
    uint32_t    r2   = crc32_1byte(data, (uint16_t)strlen(data), 0);
    EXPECT_EQ(r1, r2);
}

// ---------------------------------------------------------------------------
// Different inputs produce different CRCs (collision resistance spot-check)
// ---------------------------------------------------------------------------

TEST(PolyKybdCrc32, DifferentInputsProduceDifferentCrc) {
    const char* a = "abc";
    const char* b = "abd";
    uint32_t    ca = crc32_1byte(a, (uint16_t)strlen(a), 0);
    uint32_t    cb = crc32_1byte(b, (uint16_t)strlen(b), 0);
    EXPECT_NE(ca, cb);
}

// ---------------------------------------------------------------------------
// Regression: single flip in payload changes CRC
// ---------------------------------------------------------------------------

TEST(PolyKybdCrc32, SingleBitFlipChangesCrc) {
    uint8_t original[8] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04};
    uint8_t flipped[8];
    memcpy(flipped, original, sizeof(original));
    flipped[3] ^= 0x01;  // flip one bit

    uint32_t crc_orig    = crc32_1byte(original, 8, 0);
    uint32_t crc_flipped = crc32_1byte(flipped, 8, 0);
    EXPECT_NE(crc_orig, crc_flipped);
}

// ---------------------------------------------------------------------------
// Lookup table sanity: entry[0] must be 0x00000000, entry[1] must be 0x77073096
// (standard CRC-32/ISO-HDLC first two table entries)
// ---------------------------------------------------------------------------

extern uint32_t crc32Lookup[256];

TEST(PolyKybdCrc32, LookupTableEntry0IsZero) {
    EXPECT_EQ(crc32Lookup[0], 0x00000000u);
}

TEST(PolyKybdCrc32, LookupTableEntry1IsStandardValue) {
    EXPECT_EQ(crc32Lookup[1], 0x77073096u);
}

TEST(PolyKybdCrc32, LookupTableEntry255IsLastStandardValue) {
    EXPECT_EQ(crc32Lookup[255], 0x2D02EF8Du);
}

// ---------------------------------------------------------------------------
// Maximum uint16_t length (boundary): length = 65535
// This exercises the while(length--) counter roll-around behaviour — it
// should not overflow or crash.  We only check that the result is repeatable.
// ---------------------------------------------------------------------------

TEST(PolyKybdCrc32, MaxLengthIsRepeatable) {
    static uint8_t buf[65535];
    // Fill with a simple pattern so the test is deterministic.
    for (int i = 0; i < 65535; ++i) buf[i] = (uint8_t)(i & 0xFF);
    uint32_t r1 = crc32_1byte(buf, 65535, 0);
    uint32_t r2 = crc32_1byte(buf, 65535, 0);
    EXPECT_EQ(r1, r2);
}
