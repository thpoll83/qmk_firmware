// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// When the slave's crash record stops reading "fresh" (base/crash_ack.h).
//
// The slave's FRESH bit is relative to the SLAVE's boot, so a master-only reboot
// used to pull an old slave crash as fresh again and the host alerted on it twice.
// What is pinned here: the ack retires exactly the record it names, never another;
// it is queued only by a host read of a present, fresh record; and the slave's own
// freshness (which the late-boot guard reads) is not what changes.
#include "gtest/gtest.h"

extern "C" {
#include "crash_ack.h"
}

namespace {

constexpr uint32_t kCrcA = 0x5EED0001u;
constexpr uint32_t kCrcB = 0x5EED0002u;

TEST(CrashAck, AFreshRecordReadsFreshUntilAcked) {
    crash_ack_t ack{};
    EXPECT_TRUE(crash_ack_slave_fresh(true, &ack, kCrcA));
    EXPECT_TRUE(crash_ack_apply(&ack, true, kCrcA, kCrcA));
    EXPECT_FALSE(crash_ack_slave_fresh(true, &ack, kCrcA));
}

TEST(CrashAck, AnAckIsNeverWhatMakesARecordFresh) {
    crash_ack_t ack{};
    EXPECT_FALSE(crash_ack_slave_fresh(false, &ack, kCrcA));
    crash_ack_apply(&ack, true, kCrcA, kCrcA);
    EXPECT_FALSE(crash_ack_slave_fresh(false, &ack, kCrcA));
}

TEST(CrashAck, AnAckForAnotherRecordChangesNothing) {
    // The slave rebooted into record B between the host's read of A and the ack.
    crash_ack_t ack{};
    EXPECT_FALSE(crash_ack_apply(&ack, true, kCrcB, kCrcA));
    EXPECT_FALSE(ack.acked);
    EXPECT_TRUE(crash_ack_slave_fresh(true, &ack, kCrcB));
}

TEST(CrashAck, AnAckWithNoRecordHeldChangesNothing) {
    crash_ack_t ack{};
    EXPECT_FALSE(crash_ack_apply(&ack, false, kCrcA, kCrcA));
    EXPECT_FALSE(ack.acked);
}

TEST(CrashAck, AnOldAckDoesNotRetireANewRecord) {
    // Acked A, then the slave's record became B without a reboot resetting the
    // ack (the archive was replaced). B must still read fresh.
    crash_ack_t ack{};
    crash_ack_apply(&ack, true, kCrcA, kCrcA);
    EXPECT_TRUE(crash_ack_slave_fresh(true, &ack, kCrcB));
}

TEST(CrashAck, OnlyAHostReadOfAPresentFreshRecordQueuesAnAck) {
    crash_ack_pending_t p{};
    EXPECT_FALSE(crash_ack_note_host_read(&p, false, true, kCrcA));
    EXPECT_FALSE(crash_ack_note_host_read(&p, true, false, kCrcA));
    EXPECT_FALSE(p.pending);
    EXPECT_TRUE(crash_ack_note_host_read(&p, true, true, kCrcA));
    EXPECT_TRUE(p.pending);
    EXPECT_EQ(p.crc, kCrcA);
}

TEST(CrashAck, TheQueuedAckRetiresTheRecordTheHostRead) {
    // End to end through both halves' helpers.
    crash_ack_pending_t master{};
    crash_ack_t         slave{};
    ASSERT_TRUE(crash_ack_note_host_read(&master, true, true, kCrcA));
    ASSERT_TRUE(crash_ack_apply(&slave, true, kCrcA, master.crc));
    EXPECT_FALSE(crash_ack_slave_fresh(true, &slave, kCrcA));
}

}  // namespace
