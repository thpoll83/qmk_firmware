// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// FW-9: what happens when the DOOM engine pack's signature does not check out.
//
// The cryptography is Monocypher's and is not retested here. What IS tested is
// the policy around it, because that is where the interesting mistakes live: the
// unsigned/invalid asymmetry (getting it backwards hands an attacker a keypress
// on a tampered pack), the interactive/automatic axis (getting it wrong puts a
// dialog on 72 keycaps with nobody to answer), and the authorisation's binding to
// one pack (getting it wrong lets an accepted build authorise the next one).
#include "gtest/gtest.h"

extern "C" {
#include "doom/doom_pack_gate.h"
}

namespace {

constexpr uint32_t kCrcA = 0xDA8F72FAu;
constexpr uint32_t kCrcB = 0x1234ABCDu;

doom_pack_auth_t none() { return doom_pack_auth_t{false, 0}; }
doom_pack_auth_t accepted(uint32_t crc) { return doom_pack_auth_t{true, crc}; }

// ── A valid signature is unconditional ──────────────────────────────────────

TEST(DoomPackGate, ValidLoadsOnEitherEntry) {
    auto a = none();
    EXPECT_EQ(doom_pack_gate(DOOM_PACK_SIG_VALID, DOOM_PACK_ENTRY_INTERACTIVE, &a, kCrcA),
              DOOM_PACK_LOAD);
    EXPECT_EQ(doom_pack_gate(DOOM_PACK_SIG_VALID, DOOM_PACK_ENTRY_AUTOMATIC, &a, kCrcA),
              DOOM_PACK_LOAD);
}

TEST(DoomPackGate, ValidNeverPrompts) {
    // A release pack must never cost the user a keypress — the prompt exists for
    // developer builds, and a signed pack that asked would be a regression nobody
    // would notice until a release.
    auto a = none();
    EXPECT_NE(doom_pack_gate(DOOM_PACK_SIG_VALID, DOOM_PACK_ENTRY_INTERACTIVE, &a, kCrcA),
              DOOM_PACK_PROMPT);
}

// ── INVALID is never negotiable — the FW-2 rule, one artifact over ───────────

TEST(DoomPackGate, InvalidRefusedOnEveryPath) {
    auto none_ = none();
    auto auth  = accepted(kCrcA);
    for (auto entry : {DOOM_PACK_ENTRY_INTERACTIVE, DOOM_PACK_ENTRY_AUTOMATIC}) {
        EXPECT_EQ(doom_pack_gate(DOOM_PACK_SIG_INVALID, entry, &none_, kCrcA), DOOM_PACK_REFUSE);
        // …including when THIS pack's CRC was authorised. An authorisation says
        // "this developer build has no signature", never "this wrong signature is
        // fine" — they are different claims about the same bytes.
        EXPECT_EQ(doom_pack_gate(DOOM_PACK_SIG_INVALID, entry, &auth, kCrcA), DOOM_PACK_REFUSE);
    }
}

TEST(DoomPackGate, InvalidNeverPrompts) {
    auto a = none();
    EXPECT_NE(doom_pack_gate(DOOM_PACK_SIG_INVALID, DOOM_PACK_ENTRY_INTERACTIVE, &a, kCrcA),
              DOOM_PACK_PROMPT);
}

// ── UNSIGNED: the entry path decides ────────────────────────────────────────

TEST(DoomPackGate, UnsignedPromptsOnlyOnADeliberateEntry) {
    auto a = none();
    EXPECT_EQ(doom_pack_gate(DOOM_PACK_SIG_BLANK, DOOM_PACK_ENTRY_INTERACTIVE, &a, kCrcA),
              DOOM_PACK_PROMPT);
    // The idle screensaver has nobody to answer, so it keeps refusing outright —
    // the pre-FW-9-prompt behaviour, unchanged.
    EXPECT_EQ(doom_pack_gate(DOOM_PACK_SIG_BLANK, DOOM_PACK_ENTRY_AUTOMATIC, &a, kCrcA),
              DOOM_PACK_REFUSE);
}

TEST(DoomPackGate, AcceptedUnsignedPackLoadsOnBothEntries) {
    // Once the answer is given, the screensaver may run it too: the physical
    // decision was made, and re-asking at idle is the thing that cannot work.
    auto a = accepted(kCrcA);
    EXPECT_EQ(doom_pack_gate(DOOM_PACK_SIG_BLANK, DOOM_PACK_ENTRY_INTERACTIVE, &a, kCrcA),
              DOOM_PACK_LOAD);
    EXPECT_EQ(doom_pack_gate(DOOM_PACK_SIG_BLANK, DOOM_PACK_ENTRY_AUTOMATIC, &a, kCrcA),
              DOOM_PACK_LOAD);
}

TEST(DoomPackGate, AuthorisationIsBoundToTheOnePack) {
    // Accepting pack A must not carry over to pack B flashed on top of it — each
    // new developer build is a new decision, which costs one keypress.
    auto a = accepted(kCrcA);
    EXPECT_EQ(doom_pack_gate(DOOM_PACK_SIG_BLANK, DOOM_PACK_ENTRY_INTERACTIVE, &a, kCrcB),
              DOOM_PACK_PROMPT);
    EXPECT_EQ(doom_pack_gate(DOOM_PACK_SIG_BLANK, DOOM_PACK_ENTRY_AUTOMATIC, &a, kCrcB),
              DOOM_PACK_REFUSE);
}

TEST(DoomPackGate, NoAuthStructIsTreatedAsNoAnswer) {
    EXPECT_FALSE(doom_pack_authorised(nullptr, kCrcA));
    EXPECT_EQ(doom_pack_gate(DOOM_PACK_SIG_BLANK, DOOM_PACK_ENTRY_AUTOMATIC, nullptr, kCrcA),
              DOOM_PACK_REFUSE);
}

TEST(DoomPackGate, AnInvalidFlagWithAMatchingCrcStillCountsAsNoAnswer) {
    // valid=false is the thing that matters; a stale CRC left in the struct must
    // not authorise anything on its own.
    doom_pack_auth_t stale{false, kCrcA};
    EXPECT_FALSE(doom_pack_authorised(&stale, kCrcA));
}

// ── Classification: what "no signature" looks like in flash ─────────────────

TEST(DoomPackGate, ClassifyValidWhenTheCheckPassed) {
    const uint8_t sig[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    EXPECT_EQ(doom_pack_classify(true, sig, sizeof sig), DOOM_PACK_SIG_VALID);
}

TEST(DoomPackGate, ErasedFlashAndZeroFillBothReadAsUnsigned) {
    // Erased flash reads 0xFF; a pack that simply stops at image_size leaves
    // whatever the slot held, commonly zeros. Both mean "never signed".
    const uint8_t erased[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    const uint8_t zeros[8]  = {0};
    const uint8_t mixed[8]  = {0xFF, 0x00, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0x00};
    EXPECT_EQ(doom_pack_classify(false, erased, sizeof erased), DOOM_PACK_SIG_BLANK);
    EXPECT_EQ(doom_pack_classify(false, zeros, sizeof zeros), DOOM_PACK_SIG_BLANK);
    EXPECT_EQ(doom_pack_classify(false, mixed, sizeof mixed), DOOM_PACK_SIG_BLANK);
}

TEST(DoomPackGate, OneRealByteMakesItInvalidRatherThanUnsigned) {
    // The distinction is load-bearing, not cosmetic: BLANK may prompt, INVALID
    // never may. A single non-fill byte anywhere in the trailer is a signature
    // that was written and does not check out.
    for (size_t i = 0; i < 8; i++) {
        uint8_t sig[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        sig[i]         = 0x42;
        EXPECT_EQ(doom_pack_classify(false, sig, sizeof sig), DOOM_PACK_SIG_INVALID)
            << "byte " << i << " should have made it INVALID";
    }
}

// ── The cheap path: a blank trailer must be decidable without crypto ────────

TEST(DoomPackGate, BlankTrailerIsRecognisedWithoutTheVerifier) {
    // This is a performance contract, not a cosmetic split. The loader runs the
    // Ed25519 check ONLY when this returns false, because a SHA-512 over ~210 KB
    // on the slave costs it the window in which it must answer split
    // transactions — which on hardware read as "the slave rebooted".
    const uint8_t erased[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    const uint8_t zeros[8]  = {0};
    EXPECT_TRUE(doom_pack_trailer_is_blank(erased, sizeof erased));
    EXPECT_TRUE(doom_pack_trailer_is_blank(zeros, sizeof zeros));
}

TEST(DoomPackGate, AWrittenTrailerStillGoesToTheVerifier) {
    const uint8_t written[8] = {0xFF, 0xFF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    EXPECT_FALSE(doom_pack_trailer_is_blank(written, sizeof written));
}

TEST(DoomPackGate, ClassifyAgreesWithTheCheapTestOnEveryFailedCheck) {
    // The two must not drift: whatever the cheap test calls blank is what
    // classify() must call BLANK when the verifier said no.
    const uint8_t cases[][4] = {
        {0xFF, 0xFF, 0xFF, 0xFF}, {0x00, 0x00, 0x00, 0x00},
        {0x00, 0xFF, 0x00, 0xFF}, {0x00, 0xFF, 0x7F, 0xFF},
        {0x01, 0x00, 0x00, 0x00}, {0xFF, 0xFF, 0xFF, 0xFE},
    };
    for (const auto &c : cases) {
        const bool blank = doom_pack_trailer_is_blank(c, 4);
        EXPECT_EQ(doom_pack_classify(false, c, 4),
                  blank ? DOOM_PACK_SIG_BLANK : DOOM_PACK_SIG_INVALID);
    }
}

// ── The end-to-end shape, as the loader sees it ─────────────────────────────

TEST(DoomPackGate, TheRefusedPackDeliveredThisSessionWouldNowPrompt) {
    // The regression this feature exists for: build_pack.sh does not sign, so a
    // locally built .plyx carries no trailer at all. It reached hardware, was
    // refused, and said nothing about how to proceed.
    const uint8_t no_trailer[64] = {0xFF};  // rest zero-initialised — still all fill
    const auto    sig = doom_pack_classify(false, no_trailer, sizeof no_trailer);
    ASSERT_EQ(sig, DOOM_PACK_SIG_BLANK);
    auto a = none();
    EXPECT_EQ(doom_pack_gate(sig, DOOM_PACK_ENTRY_INTERACTIVE, &a, kCrcA), DOOM_PACK_PROMPT);
}


// ── The refusal latch's key (the loader's, modelled here) ───────────────────
//
// doom_pack_load() caches one refusal so a retry does not re-walk ~210 KB of CRC
// on every housekeeping pass. The cache is only sound while its key carries every
// input the gate reads — and `entry` is one of them, because the same unsigned
// pack is REFUSED automatically and PROMPTED interactively. These pin the gate
// half of that; the latch itself lives in doom_pack_load.c, which needs flash.

TEST(DoomPackGate, TheSamePackGivesOppositeVerdictsOnTheTwoEntries) {
    // The property the latch key has to respect: entry alone flips the answer, so
    // a cache keyed on (crc, auth) would serve one entry's verdict to the other.
    // That is what silently disarmed the prompt for a whole boot — the idle
    // screensaver refused first, and the deliberate KC_IDDQD press then never
    // reached this function.
    auto a = none();
    EXPECT_NE(doom_pack_gate(DOOM_PACK_SIG_BLANK, DOOM_PACK_ENTRY_AUTOMATIC, &a, kCrcA),
              doom_pack_gate(DOOM_PACK_SIG_BLANK, DOOM_PACK_ENTRY_INTERACTIVE, &a, kCrcA));
}

TEST(DoomPackGate, EntryIsIrrelevantOnceTheAnswerIsIn) {
    // …and the converse, which is why the auth is in the key too: with an
    // authorisation for this pack both entries agree, so a latch from before the
    // answer must not outlive it.
    auto a = accepted(kCrcA);
    EXPECT_EQ(doom_pack_gate(DOOM_PACK_SIG_BLANK, DOOM_PACK_ENTRY_AUTOMATIC, &a, kCrcA),
              doom_pack_gate(DOOM_PACK_SIG_BLANK, DOOM_PACK_ENTRY_INTERACTIVE, &a, kCrcA));
}

// ── The SLAVE's delegated authorisation ─────────────────────────────────────
//
// The slave never sees the keypress; it receives poly_sync_t.doom_pack_auth_crc,
// the image_crc the master accepted. These model what doom_pack_load() builds
// from it, so the binding is pinned by a test rather than by the call site alone.

// What the slave constructs: the master's accepted CRC, honoured only for the
// pack THIS half is holding.
static doom_pack_auth_t slave_auth(uint32_t synced_crc, uint32_t local_crc) {
    doom_pack_auth_t a;
    a.valid     = (synced_crc != 0u && synced_crc == local_crc);
    a.image_crc = local_crc;
    return a;
}

TEST(DoomPackGate, SlaveRunsTheAcceptedPackWhenBothHalvesHoldIt) {
    auto a = slave_auth(kCrcA, kCrcA);
    EXPECT_EQ(doom_pack_gate(DOOM_PACK_SIG_BLANK, DOOM_PACK_ENTRY_AUTOMATIC, &a, kCrcA),
              DOOM_PACK_LOAD);
}

TEST(DoomPackGate, SlaveRefusesADIFFERENTPackFromTheOneAccepted) {
    // The case a bare "something was accepted" boolean got wrong. One host command
    // writes both halves, but it can land on the master and fail on the slave with
    // nothing downstream reporting it — so accepting pack B on the keycaps must not
    // run the slave's leftover pack A, which nobody accepted.
    auto a = slave_auth(kCrcB, kCrcA);
    EXPECT_FALSE(a.valid);
    EXPECT_EQ(doom_pack_gate(DOOM_PACK_SIG_BLANK, DOOM_PACK_ENTRY_AUTOMATIC, &a, kCrcA),
              DOOM_PACK_REFUSE);
}

TEST(DoomPackGate, SlaveRefusesWhenNothingWasAccepted) {
    auto a = slave_auth(0u, kCrcA);
    EXPECT_FALSE(a.valid);
    EXPECT_EQ(doom_pack_gate(DOOM_PACK_SIG_BLANK, DOOM_PACK_ENTRY_AUTOMATIC, &a, kCrcA),
              DOOM_PACK_REFUSE);
}

TEST(DoomPackGate, AZeroCrcPackFailsClosedRatherThanOpen) {
    // 0 is the "nothing accepted" encoding, so a pack whose image_crc is genuinely
    // 0 reads as unaccepted and is refused. Deliberate: of the two ways to be
    // wrong about a 1-in-4-billion collision, refusing is the survivable one.
    auto a = slave_auth(0u, 0u);
    EXPECT_FALSE(a.valid);
    EXPECT_EQ(doom_pack_gate(DOOM_PACK_SIG_BLANK, DOOM_PACK_ENTRY_AUTOMATIC, &a, 0u),
              DOOM_PACK_REFUSE);
}

TEST(DoomPackGate, ADelegatedAuthorisationStillDoesNotExcuseAWrongSignature) {
    // The delegation widens exactly one verdict — "unsigned developer build".
    // INVALID is still judged locally on each half.
    auto a = slave_auth(kCrcA, kCrcA);
    EXPECT_EQ(doom_pack_gate(DOOM_PACK_SIG_INVALID, DOOM_PACK_ENTRY_AUTOMATIC, &a, kCrcA),
              DOOM_PACK_REFUSE);
    EXPECT_EQ(doom_pack_gate(DOOM_PACK_SIG_INVALID, DOOM_PACK_ENTRY_INTERACTIVE, &a, kCrcA),
              DOOM_PACK_REFUSE);
}

}  // namespace
