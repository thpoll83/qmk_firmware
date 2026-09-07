// Copyright 2026 Thomas Pollak
// SPDX-License-Identifier: GPL-2.0-or-later

#include "gtest/gtest.h"

extern "C" {
#include "macro_decode.h"
#include "macro_record.h"
}

#include <string>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Fixtures

// The splice reads and writes through callbacks so a test can back it with RAM
// instead of EEPROM -- the same indirection that makes the decoder testable.
struct Region {
    std::vector<uint8_t> bytes;
};

uint8_t rd(uint16_t offset, void *ctx) {
    return static_cast<Region *>(ctx)->bytes[offset];
}
void wr(uint16_t offset, uint8_t value, void *ctx) {
    static_cast<Region *>(ctx)->bytes[offset] = value;
}

// Macros back to back, each terminated, then zero-filled -- which leaves the last byte
// NUL, i.e. "intact", exactly as a real region reads.
Region make(const std::vector<std::string> &macros, size_t size = 64) {
    Region r;
    for (const auto &m : macros) {
        r.bytes.insert(r.bytes.end(), m.begin(), m.end());
        r.bytes.push_back(0);
    }
    r.bytes.resize(size, 0);
    return r;
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

// Reads macro `id` back out of a region as raw bytes, so a test can compare against a
// string built from the helpers above.
std::string macro_at(Region &r, uint8_t id) {
    const uint16_t end   = (uint16_t)r.bytes.size();
    uint16_t       start = poly_macro_find(rd, &r, id, end);
    std::string    out;
    for (uint16_t i = start; i < end && r.bytes[i] != 0; i++) {
        out.push_back((char)r.bytes[i]);
    }
    return out;
}

// The encoder's staging buffer, plus a clock the test moves by hand.
struct Rec {
    poly_macro_rec_t     st{};
    std::vector<uint8_t> buf;

    explicit Rec(uint16_t cap = 192) : buf(cap, 0) {
        poly_macro_rec_begin(&st, buf.data(), cap);
    }
    bool        key(uint8_t code, bool pressed, uint32_t ms) { return poly_macro_rec_key(&st, code, pressed, ms); }
    bool        tapk(uint8_t code, uint32_t ms, uint32_t hold = 20) { return key(code, true, ms) && key(code, false, ms + hold); }
    uint16_t    finish() { return poly_macro_rec_finish(&st); }
    std::string body() const { return std::string(buf.begin(), buf.begin() + st.len); }
};

// Walks a body through the real decoder. Every encoder test ends here rather than
// comparing bytes only: the property that matters is that what was written decodes
// back to the steps that were recorded.
uint8_t body_read(uint16_t offset, void *ctx) {
    return static_cast<const std::string *>(ctx)->at(offset)  & 0xFF;
}
std::vector<poly_macro_step_t> decode_all(const std::string &body) {
    std::vector<poly_macro_step_t> out;
    uint16_t                       cursor = 0;
    const uint16_t                 end    = (uint16_t)body.size();
    for (int guard = 0; guard < 1024; ++guard) {
        poly_macro_step_t s = poly_macro_decode(body_read, (void *)&body, cursor, end);
        if (s.kind == POLY_MACRO_STEP_END) break;
        EXPECT_GT(s.next, cursor) << "a step that does not advance would spin playback";
        out.push_back(s);
        cursor = s.next;
    }
    return out;
}

constexpr uint8_t KC_A_ = 0x04, KC_B_ = 0x05, KC_C_ = 0x06, KC_LSFT_ = 0xE1, KC_LCTL_ = 0xE0;

// ---------------------------------------------------------------------------
// Encoder

TEST(MacroRecord, TheFirstEventCarriesNoLeadingDelay) {
    Rec r;
    r.key(KC_A_, true, 100000);   // a clock that has been running for a while
    EXPECT_EQ(r.body(), down(KC_A_));
}

TEST(MacroRecord, APressAndReleaseCollapsesToOneTap) {
    Rec r;
    r.tapk(KC_A_, 1000);
    EXPECT_EQ(r.body(), tap(KC_A_));
    EXPECT_EQ(r.st.len, 3u);
}

TEST(MacroRecord, AHoldLongerThanTheTapWindowKeepsDownAndUp) {
    Rec r;
    r.key(KC_A_, true, 1000);
    r.key(KC_A_, false, 1000 + POLY_MACRO_REC_TAP_MS + 1);
    // Holding a key so the host auto-repeats is a real thing to record; collapsing it
    // would silently drop the hold.
    const auto steps = decode_all(r.body());
    ASSERT_EQ(steps.size(), 3u);
    EXPECT_EQ(steps[0].kind, POLY_MACRO_STEP_DOWN);
    EXPECT_EQ(steps[1].kind, POLY_MACRO_STEP_DELAY);
    EXPECT_EQ(steps[2].kind, POLY_MACRO_STEP_UP);
}

TEST(MacroRecord, AnInterveningKeyPreventsTheCollapse) {
    Rec r;
    r.key(KC_LSFT_, true, 1000);
    r.tapk(KC_A_, 1010);
    r.key(KC_LSFT_, false, 1040);
    // Shift is held across another key, so it must stay DOWN..UP; only the inner A is
    // a tap.
    EXPECT_EQ(r.body(), down(KC_LSFT_) + tap(KC_A_) + up(KC_LSFT_));
}

TEST(MacroRecord, OverlappingPressesDoNotCollapse) {
    // Rolling one key onto the next: A is still down when B goes down, so neither
    // release sees its own DOWN as the last step and both must stay DOWN..UP.
    Rec r;
    r.key(KC_A_, true, 0);
    r.key(KC_B_, true, 10);
    r.key(KC_A_, false, 20);
    r.key(KC_B_, false, 30);
    EXPECT_EQ(r.body(), down(KC_A_) + down(KC_B_) + up(KC_A_) + up(KC_B_));
}

TEST(MacroRecord, AReleaseWithNoRecordedPressIsJustAnUp) {
    // Recording can start while a key is already held: the shim reports deltas, so the
    // first thing seen for that key is its release.
    Rec r;
    r.key(KC_LSFT_, false, 1000);
    EXPECT_EQ(r.body(), up(KC_LSFT_));
}

TEST(MacroRecord, ShortGapsAreNotEncoded) {
    Rec r;
    r.tapk(KC_A_, 1000);
    r.tapk(KC_B_, 1000 + POLY_MACRO_REC_GAP_MS - 1);
    EXPECT_EQ(r.body(), tap(KC_A_) + tap(KC_B_));
}

TEST(MacroRecord, ALongGapBecomesADelayStep) {
    Rec r;
    r.tapk(KC_A_, 1000, 0);
    r.tapk(KC_B_, 1400, 0);   // 400 ms after A's release
    EXPECT_EQ(r.body(), tap(KC_A_) + delay("400") + tap(KC_B_));
}

TEST(MacroRecord, TheGapIsRoundedToTenMilliseconds) {
    Rec r1;
    r1.tapk(KC_A_, 0, 0);
    r1.tapk(KC_B_, 214, 0);
    EXPECT_EQ(r1.body(), tap(KC_A_) + delay("210") + tap(KC_B_));

    Rec r2;
    r2.tapk(KC_A_, 0, 0);
    r2.tapk(KC_B_, 216, 0);
    EXPECT_EQ(r2.body(), tap(KC_A_) + delay("220") + tap(KC_B_));
}

TEST(MacroRecord, AVeryLongPauseIsCapped) {
    Rec r;
    r.tapk(KC_A_, 0, 0);
    r.tapk(KC_B_, 60u * 1000u, 0);   // a minute of thinking
    EXPECT_EQ(r.body(), tap(KC_A_) + delay(std::to_string(POLY_MACRO_REC_GAP_MAX_MS)) + tap(KC_B_));
}

TEST(MacroRecord, TheClockWrapIsHandledModularly) {
    // timer_read32() wraps every 49.7 days. The gap is a modular subtraction, so a
    // recording that straddles the wrap must not see four billion milliseconds and
    // encode a maximum delay in the middle of someone's macro.
    Rec r;
    r.tapk(KC_A_, 0xFFFFFFC0u, 0);
    r.tapk(KC_B_, 0x00000004u, 0);   // 68 ms later, across the wrap
    EXPECT_EQ(r.body(), tap(KC_A_) + tap(KC_B_));
}

TEST(MacroRecord, TheHoldTimeOfATapIsNotEncoded) {
    // The collapse happens before the gap is measured, so a tap's own hold never
    // becomes a delay sitting between it and what came before.
    Rec r;
    r.tapk(KC_A_, 0, 0);
    r.tapk(KC_B_, 50, 300);   // held 300 ms, still inside the tap window
    EXPECT_EQ(r.body(), tap(KC_A_) + tap(KC_B_));
}

TEST(MacroRecord, RunningOutOfRoomStopsAtAStepBoundary) {
    Rec r(64);
    for (int i = 0; i < 200 && !poly_macro_rec_full(&r.st); i++) {
        r.tapk(KC_A_, (uint32_t)(i * 40));   // past the 20 ms hold, so time only moves forward
    }
    ASSERT_TRUE(poly_macro_rec_full(&r.st));
    // Every byte decodes: a truncated step would leave a trailing prefix the decoder
    // reports as END, losing bytes.
    const auto steps = decode_all(r.body());
    ASSERT_FALSE(steps.empty());
    EXPECT_EQ(steps.back().next, r.st.len);
    for (const auto &s : steps) {
        EXPECT_EQ(s.kind, POLY_MACRO_STEP_TAP);
    }
}

TEST(MacroRecord, ADelayIsNeverWrittenWithoutTheStepItPrecedes) {
    // Fill to just inside the reserve, then offer an event that needs a delay too.
    // The pair is space-checked together, so the refusal must leave no dangling wait.
    Rec r(64);
    uint32_t t = 0;
    while (!poly_macro_rec_full(&r.st)) {
        t += POLY_MACRO_REC_GAP_MS + 50;   // every step preceded by a delay
        r.tapk(KC_A_, t);
    }
    const auto steps = decode_all(r.body());
    ASSERT_FALSE(steps.empty());
    EXPECT_NE(steps.back().kind, POLY_MACRO_STEP_DELAY);
    EXPECT_EQ(steps.back().next, r.st.len);
}

TEST(MacroRecord, FinishClosesAKeyLeftDown) {
    Rec r;
    r.key(KC_LCTL_, true, 0);
    r.tapk(KC_C_, 10);
    // The user stopped recording without letting go of Ctrl.
    r.finish();
    EXPECT_EQ(r.body(), down(KC_LCTL_) + tap(KC_C_) + up(KC_LCTL_));
}

TEST(MacroRecord, FinishReleasesInReversePressOrder) {
    Rec r;
    r.key(KC_LCTL_, true, 0);
    r.key(KC_LSFT_, true, 10);
    r.finish();
    // A hand lets go of the last modifier first.
    EXPECT_EQ(r.body(), down(KC_LCTL_) + down(KC_LSFT_) + up(KC_LSFT_) + up(KC_LCTL_));
}

TEST(MacroRecord, FinishAddsNothingWhenEverythingWasReleased) {
    Rec r;
    r.tapk(KC_A_, 0);
    const std::string before = r.body();
    r.finish();
    EXPECT_EQ(r.body(), before);
}

TEST(MacroRecord, FinishIsIdempotent) {
    Rec r;
    r.key(KC_LSFT_, true, 0);
    r.finish();
    const std::string once = r.body();
    r.finish();
    EXPECT_EQ(r.body(), once);
}

TEST(MacroRecord, FinishAddsNoTrailingDelay) {
    Rec r;
    r.key(KC_LSFT_, true, 0);
    r.st.last_ms = 0;
    r.finish();
    const auto steps = decode_all(r.body());
    ASSERT_EQ(steps.size(), 2u);
    EXPECT_EQ(steps[1].kind, POLY_MACRO_STEP_UP);
}

TEST(MacroRecord, TheReserveAlwaysLeavesRoomToCloseEveryHeldKey) {
    // Hold as many keys as the flush tracks, then fill the rest of the buffer. The
    // reserve is what makes this a clean stop rather than a body that holds keys down
    // on the host after every replay.
    Rec r(96);
    uint32_t t = 0;
    for (uint8_t i = 0; i < POLY_MACRO_REC_HELD_MAX; i++) {
        ASSERT_TRUE(r.key((uint8_t)(0x10 + i), true, t)) << "held key " << (int)i;
        t += 10;
    }
    while (!poly_macro_rec_full(&r.st)) {
        t += 40;
        r.tapk(KC_A_, t);
    }
    const uint16_t len = r.finish();
    EXPECT_LE(len, r.buf.size());

    // Every DOWN in the finished body has a matching UP.
    int open = 0;
    for (const auto &s : decode_all(r.body())) {
        if (s.kind == POLY_MACRO_STEP_DOWN) open++;
        if (s.kind == POLY_MACRO_STEP_UP) open--;
    }
    EXPECT_EQ(open, 0);
}

// ---------------------------------------------------------------------------
// Splice

// Runs a whole splice at a given chunk size, so a test can prove the chunking makes no
// difference to the result.
poly_macro_splice_result_t splice(Region &r, uint8_t id, const std::string &body, uint16_t budget = 4096) {
    poly_macro_commit_t c{};
    const auto          res = poly_macro_commit_begin(&c, rd, wr, &r, id,
                                                      (const uint8_t *)body.data(),
                                                      (uint16_t)body.size(),
                                                      (uint16_t)r.bytes.size());
    if (res != POLY_MACRO_SPLICE_OK) return res;
    int guard = 0;
    while (poly_macro_commit_step(&c, budget)) {
        if (++guard > 10000) {
            ADD_FAILURE() << "commit never finished";
            break;
        }
    }
    EXPECT_TRUE(poly_macro_commit_done(&c));
    return res;
}

TEST(MacroSplice, ReplacingWithTheSameSizeLeavesTheNeighboursAlone) {
    Region r = make({tap(KC_A_), tap(KC_B_), tap(KC_C_)});
    ASSERT_EQ(splice(r, 1, tap(KC_C_)), POLY_MACRO_SPLICE_OK);
    EXPECT_EQ(macro_at(r, 0), tap(KC_A_));
    EXPECT_EQ(macro_at(r, 1), tap(KC_C_));
    EXPECT_EQ(macro_at(r, 2), tap(KC_C_));
}

TEST(MacroSplice, GrowingShiftsTheTailUp) {
    Region r = make({tap(KC_A_), tap(KC_B_), tap(KC_C_)});
    const std::string bigger = tap(KC_A_) + tap(KC_B_) + tap(KC_C_);
    ASSERT_EQ(splice(r, 1, bigger), POLY_MACRO_SPLICE_OK);
    EXPECT_EQ(macro_at(r, 0), tap(KC_A_));
    EXPECT_EQ(macro_at(r, 1), bigger);
    EXPECT_EQ(macro_at(r, 2), tap(KC_C_));
}

TEST(MacroSplice, GrowingByLessThanTheTailLengthStillShiftsCorrectly) {
    // The case that actually pins the copy DIRECTION. When the shift is smaller than
    // the tail, source and destination overlap, and only a top-down copy survives --
    // a forwards one smears the first byte over the rest. GrowingShiftsTheTailUp above
    // grows by more than its tail, so both directions happen to work there.
    Region r = make({tap(KC_A_), tap(KC_B_), tap(KC_C_) + tap(KC_C_) + tap(KC_C_)}, 96);
    ASSERT_EQ(splice(r, 1, tap(KC_B_) + std::string(1, 'z')), POLY_MACRO_SPLICE_OK);
    EXPECT_EQ(macro_at(r, 0), tap(KC_A_));
    EXPECT_EQ(macro_at(r, 1), tap(KC_B_) + std::string(1, 'z'));
    EXPECT_EQ(macro_at(r, 2), tap(KC_C_) + tap(KC_C_) + tap(KC_C_));
}

TEST(MacroSplice, ShrinkingShiftsTheTailDown) {
    Region r = make({tap(KC_A_), tap(KC_A_) + tap(KC_B_) + tap(KC_C_), tap(KC_C_)});
    ASSERT_EQ(splice(r, 1, tap(KC_B_)), POLY_MACRO_SPLICE_OK);
    EXPECT_EQ(macro_at(r, 0), tap(KC_A_));
    EXPECT_EQ(macro_at(r, 1), tap(KC_B_));
    EXPECT_EQ(macro_at(r, 2), tap(KC_C_));
}

TEST(MacroSplice, ShrinkingLeavesNoPhantomMacroBehind) {
    // The bytes a shrink vacates sit past the last terminator, so left as-is they read
    // as further macros -- which is a keypress typing a fragment of the old body.
    Region r     = make({tap(KC_A_), tap(KC_A_) + tap(KC_B_) + tap(KC_C_), tap(KC_C_)});
    const auto before_used = poly_macro_used(rd, &r, (uint16_t)r.bytes.size());
    ASSERT_EQ(splice(r, 1, tap(KC_B_)), POLY_MACRO_SPLICE_OK);
    const auto after_used = poly_macro_used(rd, &r, (uint16_t)r.bytes.size());
    EXPECT_LT(after_used, before_used);
    EXPECT_EQ(macro_at(r, 3), "");
    EXPECT_EQ(macro_at(r, 4), "");
}

TEST(MacroSplice, WritesIntoTheFirstSlot) {
    Region r = make({tap(KC_A_), tap(KC_B_)});
    ASSERT_EQ(splice(r, 0, tap(KC_C_) + tap(KC_C_)), POLY_MACRO_SPLICE_OK);
    EXPECT_EQ(macro_at(r, 0), tap(KC_C_) + tap(KC_C_));
    EXPECT_EQ(macro_at(r, 1), tap(KC_B_));
}

TEST(MacroSplice, WritesIntoAnEmptySlotPastTheLastMacro) {
    // Trailing empty slots are bare NULs indistinguishable from the zero fill, so slot
    // 5 exists in an all-zero region and a splice into it must just work.
    Region r = make({tap(KC_A_)});
    ASSERT_EQ(splice(r, 5, tap(KC_B_)), POLY_MACRO_SPLICE_OK);
    EXPECT_EQ(macro_at(r, 0), tap(KC_A_));
    EXPECT_EQ(macro_at(r, 1), "");
    EXPECT_EQ(macro_at(r, 5), tap(KC_B_));
}

TEST(MacroSplice, ClearingAMacroToEmptyIsJustAZeroLengthBody) {
    Region r = make({tap(KC_A_), tap(KC_B_), tap(KC_C_)});
    ASSERT_EQ(splice(r, 1, ""), POLY_MACRO_SPLICE_OK);
    EXPECT_EQ(macro_at(r, 0), tap(KC_A_));
    EXPECT_EQ(macro_at(r, 1), "");
    EXPECT_EQ(macro_at(r, 2), tap(KC_C_));
}

TEST(MacroSplice, RefusesABodyThatDoesNotFitAndWritesNothing) {
    Region       r      = make({tap(KC_A_), tap(KC_B_)}, 32);
    const auto   before = r.bytes;
    std::string  huge(40, 'x');
    poly_macro_commit_t c{};
    EXPECT_EQ(poly_macro_commit_begin(&c, rd, wr, &r, 1, (const uint8_t *)huge.data(),
                                      (uint16_t)huge.size(), (uint16_t)r.bytes.size()),
              POLY_MACRO_SPLICE_NO_ROOM);
    EXPECT_EQ(r.bytes, before) << "a refusal must not half-apply";
}

TEST(MacroSplice, TheLastByteIsOffLimitsToABody) {
    // Exactly the boundary: a body that would put its terminator on the final byte is
    // refused, because that byte IS the intact marker -- writing it would leave the
    // whole buffer unplayable. One byte shorter must be accepted.
    constexpr uint16_t kSize = 32;
    Region             big   = make({}, kSize);
    poly_macro_commit_t c{};
    const std::string  exact(kSize - 1, 'x');   // body + terminator = kSize
    EXPECT_EQ(poly_macro_commit_begin(&c, rd, wr, &big, 0, (const uint8_t *)exact.data(),
                                      (uint16_t)exact.size(), kSize),
              POLY_MACRO_SPLICE_NO_ROOM);

    Region            fits = make({}, kSize);
    const std::string just(kSize - 2, 'x');
    ASSERT_EQ(splice(fits, 0, just), POLY_MACRO_SPLICE_OK);
    EXPECT_EQ(macro_at(fits, 0), just);
    EXPECT_TRUE(poly_macro_buffer_intact(rd, &fits, kSize));
}

TEST(MacroSplice, RefusesAnIdTheRegionCannotHold) {
    Region              r = make({tap(KC_A_)}, 8);
    poly_macro_commit_t c{};
    EXPECT_EQ(poly_macro_commit_begin(&c, rd, wr, &r, 200, nullptr, 0,
                                      (uint16_t)r.bytes.size()),
              POLY_MACRO_SPLICE_BAD_ID);
}

TEST(MacroSplice, LeavesTheLastByteNulSoTheBufferReadsIntact) {
    Region r = make({tap(KC_A_), tap(KC_B_)});
    ASSERT_EQ(splice(r, 1, tap(KC_C_) + tap(KC_C_)), POLY_MACRO_SPLICE_OK);
    EXPECT_TRUE(poly_macro_buffer_intact(rd, &r, (uint16_t)r.bytes.size()));
}

TEST(MacroSplice, TheBufferReadsNotIntactForTheWholeSplice) {
    // A power cut mid-shift must leave something poly_macro_start() refuses. Otherwise
    // the tail of a former macro is promoted into a macro of its own, and a keypress
    // types a fragment of whatever it used to hold.
    Region              r = make({tap(KC_A_), tap(KC_B_), tap(KC_C_)});
    const uint16_t      end = (uint16_t)r.bytes.size();
    poly_macro_commit_t c{};
    ASSERT_EQ(poly_macro_commit_begin(&c, rd, wr, &r, 1,
                                      (const uint8_t *)tap(KC_C_).data(), 3, end),
              POLY_MACRO_SPLICE_OK);

    bool ever_intact_midway = false;
    int  steps              = 0;
    while (poly_macro_commit_step(&c, 1)) {
        ASSERT_LT(++steps, 10000);
        if (poly_macro_buffer_intact(rd, &r, end)) ever_intact_midway = true;
    }
    EXPECT_FALSE(ever_intact_midway);
    EXPECT_TRUE(poly_macro_buffer_intact(rd, &r, end));
    EXPECT_GT(steps, 1) << "a one-shot commit cannot be chunked off the main loop";
}

TEST(MacroSplice, TheChunkSizeDoesNotChangeTheResult) {
    const std::string body = tap(KC_C_) + down(KC_LSFT_) + tap(KC_A_) + up(KC_LSFT_);
    Region            one  = make({tap(KC_A_), tap(KC_B_), tap(KC_C_)});
    Region            all  = make({tap(KC_A_), tap(KC_B_), tap(KC_C_)});
    ASSERT_EQ(splice(one, 1, body, 1), POLY_MACRO_SPLICE_OK);
    ASSERT_EQ(splice(all, 1, body, 4096), POLY_MACRO_SPLICE_OK);
    EXPECT_EQ(one.bytes, all.bytes);
}

TEST(MacroSplice, ARecordedBodyRoundTripsThroughTheSpliceAndTheDecoder) {
    // The end-to-end shape: capture, close, splice, read back, decode.
    Rec rec;
    rec.key(KC_LCTL_, true, 0);
    rec.tapk(KC_C_, 20);
    rec.key(KC_LCTL_, false, 60);
    rec.tapk(KC_A_, 400);
    rec.finish();

    Region r = make({tap(KC_A_), tap(KC_B_)}, 128);
    ASSERT_EQ(splice(r, 1, rec.body()), POLY_MACRO_SPLICE_OK);
    EXPECT_EQ(macro_at(r, 1), rec.body());

    const auto steps = decode_all(macro_at(r, 1));
    ASSERT_EQ(steps.size(), 5u);
    EXPECT_EQ(steps[0].kind, POLY_MACRO_STEP_DOWN);
    EXPECT_EQ(steps[0].code, KC_LCTL_);
    EXPECT_EQ(steps[1].kind, POLY_MACRO_STEP_TAP);
    EXPECT_EQ(steps[2].kind, POLY_MACRO_STEP_UP);
    EXPECT_EQ(steps[3].kind, POLY_MACRO_STEP_DELAY);
    EXPECT_EQ(steps[4].kind, POLY_MACRO_STEP_TAP);
    EXPECT_EQ(steps[4].code, KC_A_);
}

}  // namespace
