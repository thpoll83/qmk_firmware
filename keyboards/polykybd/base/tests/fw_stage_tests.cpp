// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// fw_stage replaced two flags (s_fw_up_active + s_erase_pending) with one stage.
// These tests pin every transition, including the two that keep the old flags'
// behaviour on purpose.
#include "gtest/gtest.h"

extern "C" {
#include "fw_stage.h"
}

namespace {
const fw_stage_t kAll[] = {FW_STAGE_IDLE, FW_STAGE_ERASING, FW_STAGE_RECEIVING};
} // namespace

TEST(FwStage, SyncBeginReceivesFromAnyStage) {
    for (fw_stage_t s : kAll) {
        EXPECT_EQ(FW_STAGE_RECEIVING, fw_stage_next(s, FW_EV_BEGIN_SYNC));
    }
}

TEST(FwStage, DeferredBeginErasesFromAnyStage) {
    for (fw_stage_t s : kAll) {
        EXPECT_EQ(FW_STAGE_ERASING, fw_stage_next(s, FW_EV_BEGIN_DEFERRED));
    }
}

TEST(FwStage, EraseDoneOpensTheSlotForChunks) {
    EXPECT_EQ(FW_STAGE_RECEIVING, fw_stage_next(FW_STAGE_ERASING, FW_EV_ERASE_DONE));
}

TEST(FwStage, EraseDoneOutsideAnEraseChangesNothing) {
    EXPECT_EQ(FW_STAGE_IDLE, fw_stage_next(FW_STAGE_IDLE, FW_EV_ERASE_DONE));
    EXPECT_EQ(FW_STAGE_RECEIVING, fw_stage_next(FW_STAGE_RECEIVING, FW_EV_ERASE_DONE));
}

TEST(FwStage, FinalizeEndsTheStreamFromAnyStage) {
    for (fw_stage_t s : kAll) {
        EXPECT_EQ(FW_STAGE_IDLE, fw_stage_next(s, FW_EV_FINALIZED));
    }
}

// Old behaviour, kept: a refused BEGIN cleared erase_pending and left
// fw_up_active alone.
TEST(FwStage, RefusedBeginFromIdleStaysIdle) {
    EXPECT_EQ(FW_STAGE_IDLE, fw_stage_next(FW_STAGE_IDLE, FW_EV_BEGIN_REFUSED));
}

TEST(FwStage, RefusedBeginStopsAPendingEraseButNotTheStream) {
    EXPECT_EQ(FW_STAGE_RECEIVING, fw_stage_next(FW_STAGE_ERASING, FW_EV_BEGIN_REFUSED));
}

TEST(FwStage, RefusedBeginLeavesAReceivingStreamOpen) {
    EXPECT_EQ(FW_STAGE_RECEIVING, fw_stage_next(FW_STAGE_RECEIVING, FW_EV_BEGIN_REFUSED));
}

// The full deferred stream as the slave runs it.
TEST(FwStage, DeferredStreamRoundTrip) {
    fw_stage_t s = FW_STAGE_IDLE;
    s = fw_stage_next(s, FW_EV_BEGIN_DEFERRED);
    EXPECT_EQ(FW_STAGE_ERASING, s);
    s = fw_stage_next(s, FW_EV_ERASE_DONE);
    EXPECT_EQ(FW_STAGE_RECEIVING, s);
    s = fw_stage_next(s, FW_EV_FINALIZED);
    EXPECT_EQ(FW_STAGE_IDLE, s);
}

TEST(FwStage, ChunksWaitForTheErase) {
    EXPECT_FALSE(fw_stage_accepts_chunk(FW_STAGE_ERASING, false));
    EXPECT_TRUE(fw_stage_accepts_chunk(FW_STAGE_RECEIVING, false));
}

// A COMMIT that lands mid-erase stops the erase and ends the stream. The slot is
// partly erased, so chunks must stay refused until a new BEGIN erases it again.
TEST(FwStage, ChunksAreRefusedAfterAFinalizeCutTheEraseShort) {
    fw_stage_t s = fw_stage_next(FW_STAGE_IDLE, FW_EV_BEGIN_DEFERRED);
    s = fw_stage_next(s, FW_EV_FINALIZED);
    ASSERT_EQ(FW_STAGE_IDLE, s);
    EXPECT_FALSE(fw_stage_accepts_chunk(s, true));
    s = fw_stage_next(s, FW_EV_BEGIN_SYNC);
    EXPECT_TRUE(fw_stage_accepts_chunk(s, false));
}
