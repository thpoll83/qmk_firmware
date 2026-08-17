// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

// Unit tests for the flash-staging COMMIT decision layer.
//
// Why these exist: the host side has had this contract under test for a while
// (PolyKybdHost tests/device/hid_fontpack_test.py — classify_commit_reply, the
// retry/no-retry behaviour per status). But those tests encode the firmware's reply
// bytes as FIXTURES, so they can only catch the HOST misreading a status. They can
// never catch this end EMITTING the wrong one — and "emitting the wrong one" is
// exactly the bug that shipped: a dropped split-link ACK reported as a CRC mismatch,
// which sent a field diagnosis the wrong way for two rounds (2026-08-17).
//
// So the same contract is now pinned from both ends.

#include "gtest/gtest.h"

extern "C" {
#include "sync_ack.h"
#include "fw_staging.h"
#include "fw_up_verdict.h"
#include "hid_fontpack.h"
#include "polymod_crc32.h"
}

namespace {

// Every ack value the split link can put in poly_sync_reply_t.
const uint8_t kAllAcks[] = {SYNC_ACK, SYNC_ACK_SIG, SYNC_CRC32_ERR, SYNC_NACK_REFUSED};

int popcount8(uint8_t v) {
    int n = 0;
    for (int i = 0; i < 8; i++) n += (v >> i) & 1;
    return n;
}

int hamming(uint8_t a, uint8_t b) { return popcount8((uint8_t)(a ^ b)); }

fw_staging_status_t crc_stamped(fw_staging_status_t s, uint32_t *out_crc) {
    *out_crc = crc32_1byte((const uint8_t *)&s, sizeof(s), 0);
    return s;
}

}  // namespace

// ---------------------------------------------------------------------------
// The ack vocabulary itself (base/sync_ack.h)
// ---------------------------------------------------------------------------

TEST(SyncAckTest, SyncSucceededAcceptsExactlyTheTwoAcks) {
    EXPECT_TRUE(sync_succeeded(SYNC_ACK));
    EXPECT_TRUE(sync_succeeded(SYNC_ACK_SIG));
    EXPECT_FALSE(sync_succeeded(SYNC_CRC32_ERR));
    EXPECT_FALSE(sync_succeeded(SYNC_NACK_REFUSED));
}

// The helper must be a WHITELIST, not a blacklist of known failures. That property
// is what let SYNC_NACK_REFUSED be introduced without touching any of the 14
// existing "not ACK == failure" call sites: a new value is a failure the moment it
// exists. Sweeping all 256 bytes is what distinguishes the two implementations.
TEST(SyncAckTest, SyncSucceededIsFailClosedAcrossEveryByte) {
    for (int v = 0; v <= 0xFF; v++) {
        uint8_t ack      = (uint8_t)v;
        bool    expected = (ack == SYNC_ACK || ack == SYNC_ACK_SIG);
        EXPECT_EQ(sync_succeeded(ack), expected) << "ack=0x" << std::hex << v;
    }
}

// The reply carries NO CRC, so the ack values are deliberately spaced to survive a
// flipped bit. This is documented in sync_ack.h; the test makes it enforceable, so a
// fifth value cannot silently degrade the tolerance for the whole set.
TEST(SyncAckTest, AckValuesStayHammingSpaced) {
    const size_t n = sizeof(kAllAcks) / sizeof(kAllAcks[0]);
    for (size_t i = 0; i < n; i++) {
        for (size_t j = i + 1; j < n; j++) {
            EXPECT_GE(hamming(kAllAcks[i], kAllAcks[j]), 4)
                << "0x" << std::hex << (int)kAllAcks[i] << " vs 0x" << (int)kAllAcks[j]
                << " — a new ack value must keep the min pairwise distance at 4";
        }
    }
}

// 0x00 and 0xFF are what a stuck-low or floating line reads as, so they must never
// be a meaningful ack however well-spaced they are.
TEST(SyncAckTest, NoAckValueIsAStuckLineReading) {
    for (uint8_t ack : kAllAcks) {
        EXPECT_NE(ack, 0x00);
        EXPECT_NE(ack, 0xFF);
    }
}

// ---------------------------------------------------------------------------
// The COMMIT-failure classification (base/fw_up_verdict.c)
// ---------------------------------------------------------------------------

TEST(FwUpVerdictTest, OnlyARefusalAckIsSelfDescribing) {
    EXPECT_TRUE(fw_up_commit_ack_is_self_describing(SYNC_NACK_REFUSED));
    // SYNC_CRC32_ERR means three different things, so it settles nothing — this is
    // what makes the caller spend a STATUS probe.
    EXPECT_FALSE(fw_up_commit_ack_is_self_describing(SYNC_CRC32_ERR));
    EXPECT_FALSE(fw_up_commit_ack_is_self_describing(SYNC_ACK));
}

// An explicit refusal must not be second-guessed by the probe: a snapshot can be
// stale, corrupt or unobtainable, and none of that makes the refusal we already
// received less true.
TEST(FwUpVerdictTest, ExplicitRefusalWinsOverEveryProbeResult) {
    EXPECT_EQ(fw_up_classify_commit_failure(SYNC_NACK_REFUSED, true, SYNC_NACK_REFUSED),
              FW_UP_COMMIT_REFUSED);
    EXPECT_EQ(fw_up_classify_commit_failure(SYNC_NACK_REFUSED, true, SYNC_ACK),
              FW_UP_COMMIT_REFUSED);
    EXPECT_EQ(fw_up_classify_commit_failure(SYNC_NACK_REFUSED, true, 0), FW_UP_COMMIT_REFUSED);
    EXPECT_EQ(fw_up_classify_commit_failure(SYNC_NACK_REFUSED, false, 0), FW_UP_COMMIT_REFUSED);
}

// The case the whole last_commit_ack field exists for: the slave answered "no", that
// answer was lost on the wire, and only its recorded verdict can still say so.
TEST(FwUpVerdictTest, LostRefusalAckIsRecoveredFromTheRecordedVerdict) {
    EXPECT_EQ(fw_up_classify_commit_failure(SYNC_CRC32_ERR, true, SYNC_NACK_REFUSED),
              FW_UP_COMMIT_REFUSED);
}

// The expensive mistake to avoid. The verdict is cleared at the start of every
// staging attempt, so 0 means "the slave's COMMIT handler never ran this attempt" —
// a link failure, and retrying COMMIT is free. Calling this a refusal costs a full
// re-stream of the pack over the very link that just dropped a frame.
TEST(FwUpVerdictTest, ClearedVerdictIsARetryableLinkFailure) {
    EXPECT_EQ(fw_up_classify_commit_failure(SYNC_CRC32_ERR, true, 0), FW_UP_COMMIT_LINK_FAILURE);
}

// A SUCCESS whose ack was lost in transit. fw_up_active is cleared by finalize on
// both success and refusal, which is precisely why it is not an input here.
TEST(FwUpVerdictTest, LostSuccessAckIsARetryableLinkFailure) {
    EXPECT_EQ(fw_up_classify_commit_failure(SYNC_CRC32_ERR, true, SYNC_ACK),
              FW_UP_COMMIT_LINK_FAILURE);
}

// !status_ok must make the recorded value unreadable, not merely unlikely — the
// snapshot is zeroed/garbage when the RPC or its CRC failed, so trusting a byte out
// of it would invent a verdict.
TEST(FwUpVerdictTest, AFailedProbeIgnoresTheRecordedVerdictEntirely) {
    EXPECT_EQ(fw_up_classify_commit_failure(SYNC_CRC32_ERR, false, SYNC_NACK_REFUSED),
              FW_UP_COMMIT_LINK_FAILURE);
    EXPECT_EQ(fw_up_classify_commit_failure(SYNC_CRC32_ERR, false, 0), FW_UP_COMMIT_LINK_FAILURE);
}

// Backward compatibility: a slave too old to record a verdict answers with the field
// as 0, which must degrade to the conservative reading rather than to a refusal.
TEST(FwUpVerdictTest, ASlaveThatRecordsNoVerdictDegradesToLinkFailure) {
    fw_staging_status_t old_slave = {};   // every field zero, as an older reply reads
    EXPECT_EQ(fw_up_classify_commit_failure(SYNC_CRC32_ERR, true, old_slave.last_commit_ack),
              FW_UP_COMMIT_LINK_FAILURE);
}

// ---------------------------------------------------------------------------
// The STATUS snapshot integrity check
// ---------------------------------------------------------------------------

TEST(FwUpStatusCrcTest, AValidSnapshotPasses) {
    fw_staging_status_t s = {};
    s.fw_up_active        = true;
    s.next_offset         = 38632;
    s.last_commit_ack     = SYNC_NACK_REFUSED;
    uint32_t crc          = 0;
    s                     = crc_stamped(s, &crc);
    EXPECT_TRUE(fw_up_status_crc_ok(&s, crc));
}

// A snapshot decides whether a flash landed, so a corrupt one must be refused rather
// than believed — it rides the same un-CRC'd link as everything else.
TEST(FwUpStatusCrcTest, AFlippedBitInTheSnapshotFails) {
    fw_staging_status_t s = {};
    s.next_offset         = 38632;
    s.last_commit_ack     = SYNC_ACK;
    uint32_t crc          = 0;
    s                     = crc_stamped(s, &crc);

    s.last_commit_ack = SYNC_NACK_REFUSED;   // the one field the decision reads
    EXPECT_FALSE(fw_up_status_crc_ok(&s, crc));
}

TEST(FwUpStatusCrcTest, AWrongClaimedCrcFails) {
    fw_staging_status_t s   = {};
    uint32_t            crc = 0;
    s                       = crc_stamped(s, &crc);
    EXPECT_FALSE(fw_up_status_crc_ok(&s, crc ^ 1u));
}

TEST(FwUpStatusCrcTest, ANullSnapshotFails) {
    EXPECT_FALSE(fw_up_status_crc_ok(nullptr, 0));
}

// ---------------------------------------------------------------------------
// The font-pack COMMIT status byte (the host-visible half of the contract)
// ---------------------------------------------------------------------------

TEST(FontpackCommitStatusTest, BothHalvesOkIsDot) {
    EXPECT_EQ(fontpack_commit_status(true, true, false), FONTPACK_COMMIT_OK);
}

// A master rejection outranks everything, including a perfectly healthy slave: there
// is no point telling the host to retry a half whose own copy is bad.
TEST(FontpackCommitStatusTest, MasterRejectionIsRejectedRegardlessOfTheSlave) {
    EXPECT_EQ(fontpack_commit_status(false, true, false), FONTPACK_COMMIT_REJECTED);
    EXPECT_EQ(fontpack_commit_status(false, false, false), FONTPACK_COMMIT_REJECTED);
    EXPECT_EQ(fontpack_commit_status(false, false, true), FONTPACK_COMMIT_REJECTED);
}

TEST(FontpackCommitStatusTest, SlaveRefusalIsRejectedNotUnconfirmed) {
    EXPECT_EQ(fontpack_commit_status(true, false, true), FONTPACK_COMMIT_REJECTED);
}

// The status that started all of this: the master's copy is live and only the
// acknowledgement was lost, so the host must retry COMMIT rather than re-stream.
TEST(FontpackCommitStatusTest, LostSlaveAckIsNoSlave) {
    EXPECT_EQ(fontpack_commit_status(true, false, false), FONTPACK_COMMIT_NO_SLAVE);
}

// Collapsing these back into one failure byte is the regression this guards: it is
// what made a perfect pack read as corrupt.
TEST(FontpackCommitStatusTest, TheThreeStatusesAreDistinct) {
    EXPECT_NE(FONTPACK_COMMIT_OK, FONTPACK_COMMIT_REJECTED);
    EXPECT_NE(FONTPACK_COMMIT_OK, FONTPACK_COMMIT_NO_SLAVE);
    EXPECT_NE(FONTPACK_COMMIT_REJECTED, FONTPACK_COMMIT_NO_SLAVE);
    // '!' is the legacy collapsed value; a new status must not reuse it, since a new
    // host maps it to "unspecified".
    EXPECT_NE(FONTPACK_COMMIT_REJECTED, '!');
    EXPECT_NE(FONTPACK_COMMIT_NO_SLAVE, '!');
}

// A status letter that is a hex digit cannot be written next to a \xNN escape:
// "P\x52C" is the single escape \x52C, not three bytes. The reply is built from
// constants now, so this is a guard against anyone going back to a literal.
TEST(FontpackCommitStatusTest, StatusBytesAreSafeInAStringLiteral) {
    for (uint8_t st : {(uint8_t)FONTPACK_COMMIT_OK, (uint8_t)FONTPACK_COMMIT_REJECTED,
                       (uint8_t)FONTPACK_COMMIT_NO_SLAVE}) {
        EXPECT_GE(st, 0x21) << "must be printable in a log";
        EXPECT_LE(st, 0x7E);
        bool hex_digit = (st >= '0' && st <= '9') || (st >= 'a' && st <= 'f') ||
                         (st >= 'A' && st <= 'F');
        EXPECT_FALSE(hex_digit) << "0x" << std::hex << (int)st
                                << " would merge into a preceding \\xNN escape";
    }
}
