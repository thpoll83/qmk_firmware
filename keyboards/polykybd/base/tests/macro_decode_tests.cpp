// Copyright 2026 Thomas Pollak
// SPDX-License-Identifier: GPL-2.0-or-later

#include "gtest/gtest.h"

extern "C" {
#include "macro_decode.h"
}

#include <cstring>
#include <string>
#include <vector>

namespace {

// The decoder reads through a callback so a test can back it with RAM instead of
// EEPROM. That indirection is the whole reason these tests can exist.
struct Buf {
    std::vector<uint8_t> bytes;
};

uint8_t rd(uint16_t offset, void *ctx) {
    return static_cast<Buf *>(ctx)->bytes[offset];
}

// Builds a body buffer the way the host would: macros back to back, each terminated,
// then zero-filled to `size` (which leaves the last byte NUL, i.e. "intact").
Buf make(const std::vector<std::string> &macros, size_t size = 64) {
    Buf b;
    for (const auto &m : macros) {
        b.bytes.insert(b.bytes.end(), m.begin(), m.end());
        b.bytes.push_back(0);
    }
    b.bytes.resize(size, 0);
    return b;
}

std::string tap(uint8_t kc) {
    return std::string{char(POLY_MACRO_PREFIX), char(POLY_MACRO_OP_TAP), char(kc)};
}
std::string down(uint8_t kc) {
    return std::string{char(POLY_MACRO_PREFIX), char(POLY_MACRO_OP_DOWN), char(kc)};
}
std::string up(uint8_t kc) {
    return std::string{char(POLY_MACRO_PREFIX), char(POLY_MACRO_OP_UP), char(kc)};
}
std::string delay(const std::string &digits) {
    return std::string{char(POLY_MACRO_PREFIX), char(POLY_MACRO_OP_DELAY)} + digits;
}

// Runs a whole macro and returns the steps, so a test can assert on the sequence
// rather than on one decode at a time.
std::vector<poly_macro_step_t> run(Buf &b, uint16_t start) {
    std::vector<poly_macro_step_t> out;
    uint16_t                       cursor = start;
    for (int guard = 0; guard < 512; ++guard) {
        poly_macro_step_t s = poly_macro_decode(rd, &b, cursor, (uint16_t)b.bytes.size());
        if (s.kind == POLY_MACRO_STEP_END) break;
        out.push_back(s);
        // A step that does not advance would spin the playback tick forever.
        EXPECT_GT(s.next, cursor);
        cursor = s.next;
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// Addressing

TEST(MacroFind, FirstMacroStartsAtZero) {
    Buf b = make({"ab", "cd"});
    EXPECT_EQ(poly_macro_find(rd, &b, 0, b.bytes.size()), 0);
}

TEST(MacroFind, SkipsOneTerminatorPerPrecedingMacro) {
    Buf b = make({"ab", "cd", "e"});
    EXPECT_EQ(poly_macro_find(rd, &b, 1, b.bytes.size()), 3); // after "ab\0"
    EXPECT_EQ(poly_macro_find(rd, &b, 2, b.bytes.size()), 6); // after "cd\0"
}

TEST(MacroFind, AnEmptyMacroStillOccupiesASlot) {
    Buf b = make({"", "x"});
    EXPECT_EQ(poly_macro_find(rd, &b, 0, b.bytes.size()), 0);
    EXPECT_EQ(poly_macro_find(rd, &b, 1, b.bytes.size()), 1);
    EXPECT_EQ(rd(0, &b), 0); // macro 0 is empty
    EXPECT_EQ(rd(1, &b), 'x');
}

TEST(MacroFind, IdPastTheEndReturnsEnd) {
    // Only the trailing zero fill is left, so every later id resolves to a NUL and
    // then, once the fill is exhausted, to `end`. It must never wrap or run off.
    Buf b = make({"a"}, 8);
    EXPECT_EQ(poly_macro_find(rd, &b, 200, b.bytes.size()), b.bytes.size());
}

// ---------------------------------------------------------------------------
// Budget

TEST(MacroUsed, CountsThroughTheLastTerminator) {
    Buf b = make({"abc"}, 64);
    EXPECT_EQ(poly_macro_used(rd, &b, b.bytes.size()), 4); // "abc\0"
}

TEST(MacroUsed, TrailingEmptyMacrosAreFree) {
    Buf a = make({"abc"}, 64);
    Buf c = make({"abc", "", "", ""}, 64);
    EXPECT_EQ(poly_macro_used(rd, &a, a.bytes.size()), poly_macro_used(rd, &c, c.bytes.size()));
}

TEST(MacroUsed, LeadingEmptyMacroStillCosts) {
    Buf b = make({"", "x"}, 64);
    EXPECT_EQ(poly_macro_used(rd, &b, b.bytes.size()), 3); // "\0x\0"
}

TEST(MacroUsed, EmptyBufferIsZero) {
    Buf b = make({}, 64);
    EXPECT_EQ(poly_macro_used(rd, &b, b.bytes.size()), 0);
}

// ---------------------------------------------------------------------------
// Mid-write guard

TEST(MacroIntact, LastByteNulMeansComplete) {
    Buf b = make({"abc"}, 64);
    EXPECT_TRUE(poly_macro_buffer_intact(rd, &b, b.bytes.size()));
}

TEST(MacroIntact, NonNulLastByteMeansAbortedWrite) {
    Buf b            = make({"abc"}, 64);
    b.bytes.back()   = 'Z';
    EXPECT_FALSE(poly_macro_buffer_intact(rd, &b, b.bytes.size()));
}

// ---------------------------------------------------------------------------
// Decoding

TEST(MacroDecode, PlainTextIsOneCharStepPerByte) {
    Buf  b     = make({"hi"});
    auto steps = run(b, 0);
    ASSERT_EQ(steps.size(), 2u);
    EXPECT_EQ(steps[0].kind, POLY_MACRO_STEP_CHAR);
    EXPECT_EQ(steps[0].code, 'h');
    EXPECT_EQ(steps[1].code, 'i');
}

TEST(MacroDecode, TapDownUpCarryTheirKeycode) {
    Buf  b     = make({tap(0x04) + down(0xE0) + up(0xE0)});
    auto steps = run(b, 0);
    ASSERT_EQ(steps.size(), 3u);
    EXPECT_EQ(steps[0].kind, POLY_MACRO_STEP_TAP);
    EXPECT_EQ(steps[0].code, 0x04);
    EXPECT_EQ(steps[1].kind, POLY_MACRO_STEP_DOWN);
    EXPECT_EQ(steps[1].code, 0xE0);
    EXPECT_EQ(steps[2].kind, POLY_MACRO_STEP_UP);
    EXPECT_EQ(steps[2].code, 0xE0);
}

TEST(MacroDecode, DelayReadsAsciiDigits) {
    Buf  b     = make({delay("250")});
    auto steps = run(b, 0);
    ASSERT_EQ(steps.size(), 1u);
    EXPECT_EQ(steps[0].kind, POLY_MACRO_STEP_DELAY);
    EXPECT_EQ(steps[0].ms, 250);
}

// The terminator of a delay is NOT consumed -- send_string re-reads it as the next
// step. Consuming it would silently swallow the character after every delay, which is
// the sort of thing that only shows up as "the macro drops a letter sometimes".
TEST(MacroDecode, TheByteEndingADelayIsNotEaten) {
    Buf  b     = make({delay("10") + "x"});
    auto steps = run(b, 0);
    ASSERT_EQ(steps.size(), 2u);
    EXPECT_EQ(steps[0].kind, POLY_MACRO_STEP_DELAY);
    EXPECT_EQ(steps[0].ms, 10);
    EXPECT_EQ(steps[1].kind, POLY_MACRO_STEP_CHAR);
    EXPECT_EQ(steps[1].code, 'x');
}

TEST(MacroDecode, DelayWithNoDigitsIsZeroNotAHang) {
    Buf  b     = make({delay("") + "x"});
    auto steps = run(b, 0);
    ASSERT_EQ(steps.size(), 2u);
    EXPECT_EQ(steps[0].kind, POLY_MACRO_STEP_DELAY);
    EXPECT_EQ(steps[0].ms, 0);
    EXPECT_GT(steps[0].next, 0);
}

// A wrapped delay reads as "no delay" and the macro races through a wait the author
// asked for, so the clamp is the safe direction.
TEST(MacroDecode, AbsurdDelayClampsRatherThanWraps) {
    Buf  b     = make({delay("999999999")});
    auto steps = run(b, 0);
    ASSERT_EQ(steps.size(), 1u);
    EXPECT_EQ(steps[0].ms, 0xFFFF);
}

TEST(MacroDecode, NulEndsTheMacro) {
    Buf               b = make({"a", "b"});
    poly_macro_step_t s = poly_macro_decode(rd, &b, 1, b.bytes.size()); // the terminator
    EXPECT_EQ(s.kind, POLY_MACRO_STEP_END);
}

TEST(MacroDecode, StopsAtTheBufferEnd) {
    Buf               b = make({"a"}, 1); // no room for anything
    poly_macro_step_t s = poly_macro_decode(rd, &b, 1, 1);
    EXPECT_EQ(s.kind, POLY_MACRO_STEP_END);
}

// An unknown op's following byte is not known to be an argument, so resuming past it
// would type whatever happened to be there. Stopping is the only safe reading.
TEST(MacroDecode, UnknownOpStopsRatherThanSkipping) {
    Buf b = make({std::string{char(POLY_MACRO_PREFIX), char(0x7F), 'X', 'Y'}});
    poly_macro_step_t s = poly_macro_decode(rd, &b, 0, b.bytes.size());
    EXPECT_EQ(s.kind, POLY_MACRO_STEP_END);
}

TEST(MacroDecode, TruncatedPrefixAtTheEndIsNotAStep) {
    Buf b;
    b.bytes = {POLY_MACRO_PREFIX};
    poly_macro_step_t s = poly_macro_decode(rd, &b, 0, b.bytes.size());
    EXPECT_EQ(s.kind, POLY_MACRO_STEP_END);
}

TEST(MacroDecode, TruncatedOpArgumentIsNotAStep) {
    Buf b;
    b.bytes = {POLY_MACRO_PREFIX, POLY_MACRO_OP_TAP}; // keycode byte missing
    poly_macro_step_t s = poly_macro_decode(rd, &b, 0, b.bytes.size());
    EXPECT_EQ(s.kind, POLY_MACRO_STEP_END);
}

// ---------------------------------------------------------------------------
// A realistic body, end to end

TEST(MacroDecode, ChordThenTextThenDelay) {
    // Ctrl down, 'c', Ctrl up, wait 50 ms, type "ok"
    Buf  b     = make({down(0xE0) + tap(0x06) + up(0xE0) + delay("50") + "ok"});
    auto steps = run(b, 0);
    ASSERT_EQ(steps.size(), 6u);
    EXPECT_EQ(steps[0].kind, POLY_MACRO_STEP_DOWN);
    EXPECT_EQ(steps[1].kind, POLY_MACRO_STEP_TAP);
    EXPECT_EQ(steps[2].kind, POLY_MACRO_STEP_UP);
    EXPECT_EQ(steps[3].kind, POLY_MACRO_STEP_DELAY);
    EXPECT_EQ(steps[3].ms, 50);
    EXPECT_EQ(steps[4].code, 'o');
    EXPECT_EQ(steps[5].code, 'k');
}

TEST(MacroDecode, SecondMacroDecodesIndependently) {
    Buf      b     = make({"first", "second"});
    uint16_t start = poly_macro_find(rd, &b, 1, b.bytes.size());
    auto     steps = run(b, start);
    std::string got;
    for (auto &s : steps) got.push_back(char(s.code));
    EXPECT_EQ(got, "second");
}
