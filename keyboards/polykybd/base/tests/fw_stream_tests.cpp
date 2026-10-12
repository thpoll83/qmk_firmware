// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// fw_stream is the bookkeeping of one fw_staging stream: its stage, the deferred
// erase and the core1 hold. On the slave the split thread (BEGIN, COMMIT) preempts
// housekeeping's erase step wherever interrupts are on, so the fake below can run
// a "split thread" action at any chosen interrupt restore. The host's abort is a
// COMMIT, so a finalize landing mid-erase is the case these tests care most about.
#include "gtest/gtest.h"

#include <functional>
#include <vector>

extern "C" {
#include "fw_stream.h"
}

// --- fake hardware ----------------------------------------------------------

namespace {
struct FakeHw {
    int                   irq_off   = 0;  // nesting depth of irq_save
    int                   locks     = 0;
    int                   holds     = 0;  // core1 holds outstanding
    int                   takes     = 0;
    int                   releases  = 0;
    std::vector<uint32_t> erased;
    bool                  erased_with_irq_on = false;
    // Runs once, right after the Nth interrupt restore from now (1 = the next one):
    // the split thread preempting the main thread there.
    int                   preempt_at = 0;
    std::function<void()> preempt;
};
FakeHw g;
} // namespace

extern "C" {
uint32_t fw_stream_hw_irq_save(void) {
    return (uint32_t)g.irq_off++;
}
void fw_stream_hw_irq_restore(uint32_t saved) {
    g.irq_off = (int)saved;
    if (g.irq_off == 0 && g.preempt && --g.preempt_at == 0) {
        auto f = g.preempt;
        g.preempt = nullptr;
        f();
    }
}
void fw_stream_hw_erase_sector(uint32_t index) {
    if (g.irq_off == 0) g.erased_with_irq_on = true;
    g.erased.push_back(index);
}
void fw_stream_hw_lock(void) { g.locks++; }
void fw_stream_hw_unlock(void) { g.locks--; }
void fw_stream_hw_hold(void) {
    g.holds++;
    g.takes++;
}
void fw_stream_hw_release(void) {
    g.holds--;
    g.releases++;
}
}

class FwStreamTest : public ::testing::Test {
  protected:
    fw_stream_t s{};

    void SetUp() override {
        g = FakeHw{};
        fw_stream_init(&s);
    }
    void TearDown() override {
        EXPECT_EQ(g.locks, 0) << "lock left taken";
        EXPECT_EQ(g.irq_off, 0) << "interrupts left off";
        EXPECT_FALSE(g.erased_with_irq_on) << "a sector was erased with interrupts on";
        EXPECT_GE(g.holds, 0) << "core1 released more often than held";
    }
    // Run the erase step until it reports something other than a sector.
    fw_erase_step_t run_erase() {
        fw_erase_step_t r;
        int             guard = 0;
        while ((r = fw_stream_erase_step(&s, true)) == FW_ERASE_SECTOR && ++guard < 100) {
        }
        return r;
    }
    void preempt_at(int n, std::function<void()> f) {
        g.preempt_at = n;
        g.preempt    = std::move(f);
    }
};

// --- the plain stream -------------------------------------------------------

TEST_F(FwStreamTest, DeferredBeginErasesEverySectorThenReleasesItsHold) {
    fw_stream_begin_deferred(&s, 3);
    EXPECT_EQ(g.holds, 1);
    EXPECT_TRUE(fw_stream_erase_pending(&s));
    EXPECT_FALSE(fw_stream_accepts_chunk(&s));

    EXPECT_EQ(FW_ERASE_SECTOR, fw_stream_erase_step(&s, true));
    EXPECT_EQ(FW_ERASE_SECTOR, fw_stream_erase_step(&s, true));
    EXPECT_EQ(FW_ERASE_DONE, fw_stream_erase_step(&s, true));

    EXPECT_EQ(g.erased, (std::vector<uint32_t>{0, 1, 2}));
    EXPECT_EQ(g.holds, 0);
    EXPECT_EQ(s.stage, FW_STAGE_RECEIVING);
    EXPECT_TRUE(fw_stream_accepts_chunk(&s));
    EXPECT_EQ(FW_ERASE_IDLE, fw_stream_erase_step(&s, true));
}

TEST_F(FwStreamTest, TheRateLimitDelaysTheNextSector) {
    fw_stream_begin_deferred(&s, 2);
    EXPECT_EQ(FW_ERASE_WAIT, fw_stream_erase_step(&s, false));
    EXPECT_TRUE(g.erased.empty());
    EXPECT_EQ(FW_ERASE_SECTOR, fw_stream_erase_step(&s, true));
}

// BEGIN is re-sent for the same image (the re-erase of a dirty slot). A second
// hold with no second release would keep core1 down for good.
TEST_F(FwStreamTest, ARepeatedBeginTakesTheHoldOnce) {
    fw_stream_begin_deferred(&s, 2);
    fw_stream_begin_deferred(&s, 2);
    EXPECT_EQ(g.takes, 1);
    EXPECT_EQ(FW_ERASE_DONE, run_erase());
    EXPECT_EQ(g.holds, 0);
}

TEST_F(FwStreamTest, FinalizeAfterTheEraseEndsTheStreamCleanly) {
    fw_stream_begin_deferred(&s, 1);
    EXPECT_EQ(FW_ERASE_DONE, run_erase());
    fw_stream_finalize(&s);
    EXPECT_FALSE(fw_stream_active(&s));
    EXPECT_FALSE(s.erase_cut_short);
    EXPECT_EQ(g.releases, 1);
}

// --- the host's abort (a COMMIT) during the erase ---------------------------

TEST_F(FwStreamTest, FinalizeBetweenTwoErasePassesStopsTheErase) {
    fw_stream_begin_deferred(&s, 3);
    EXPECT_EQ(FW_ERASE_SECTOR, fw_stream_erase_step(&s, true));
    fw_stream_finalize(&s);

    EXPECT_FALSE(fw_stream_active(&s));
    EXPECT_TRUE(s.erase_cut_short);
    EXPECT_EQ(g.holds, 0) << "finalize must release core1";
    EXPECT_EQ(FW_ERASE_IDLE, fw_stream_erase_step(&s, true));
    EXPECT_EQ(g.erased, (std::vector<uint32_t>{0})) << "the erase went on after finalize";
    EXPECT_FALSE(fw_stream_accepts_chunk(&s)) << "chunk 0 would land on unerased sectors";
}

// The finalize lands right after the interrupts come back on from a middle sector.
TEST_F(FwStreamTest, FinalizeRightAfterAMiddleSectorStopsTheErase) {
    fw_stream_begin_deferred(&s, 3);
    preempt_at(1, [this] { fw_stream_finalize(&s); });
    EXPECT_EQ(FW_ERASE_SECTOR, fw_stream_erase_step(&s, true));
    EXPECT_EQ(FW_ERASE_IDLE, fw_stream_erase_step(&s, true));
    EXPECT_EQ(g.erased.size(), 1u);
    EXPECT_TRUE(s.erase_cut_short);
    EXPECT_EQ(g.releases, 1);
    EXPECT_FALSE(fw_stream_accepts_chunk(&s));
}

// Greptile's race on #364: finalize preempting the pass that erases the last
// sector must not have its IDLE overwritten by the ERASE_DONE transition, and the
// hold must be released exactly once.
TEST_F(FwStreamTest, FinalizeRightAfterTheLastSectorLeavesTheStreamEnded) {
    fw_stream_begin_deferred(&s, 2);
    EXPECT_EQ(FW_ERASE_SECTOR, fw_stream_erase_step(&s, true));
    preempt_at(1, [this] { fw_stream_finalize(&s); });
    const fw_erase_step_t r = fw_stream_erase_step(&s, true);

    EXPECT_FALSE(fw_stream_active(&s)) << "the slave would stay on the update screen";
    EXPECT_EQ(g.holds, 0);
    EXPECT_EQ(g.releases, 1) << "core1 released twice";
    // The erase completed inside the step, before the finalize, so the slot is
    // whole: the stream ends as a normal finalize.
    EXPECT_EQ(FW_ERASE_DONE, r);
    EXPECT_FALSE(s.erase_cut_short);
}

TEST_F(FwStreamTest, ANewBeginAfterACutShortEraseErasesAgainAndTakesChunks) {
    fw_stream_begin_deferred(&s, 3);
    EXPECT_EQ(FW_ERASE_SECTOR, fw_stream_erase_step(&s, true));
    fw_stream_finalize(&s);
    ASSERT_TRUE(s.erase_cut_short);

    g.erased.clear();
    fw_stream_begin_deferred(&s, 3);
    EXPECT_FALSE(s.erase_cut_short);
    EXPECT_FALSE(fw_stream_accepts_chunk(&s));
    EXPECT_EQ(FW_ERASE_DONE, run_erase());
    EXPECT_EQ(g.erased, (std::vector<uint32_t>{0, 1, 2}));
    EXPECT_TRUE(fw_stream_accepts_chunk(&s));
    EXPECT_EQ(g.holds, 0);
    EXPECT_EQ(g.takes, 2);
}

// --- a BEGIN for another image during the erase -----------------------------

// A BEGIN preempting right after a sector restarts the erase for its own image.
// The step used to advance the sector index after the interrupts came back on,
// which would skip the new erase's sector 0.
TEST_F(FwStreamTest, ABeginRightAfterASectorRestartsTheEraseFromSectorZero) {
    fw_stream_begin_deferred(&s, 3);
    EXPECT_EQ(FW_ERASE_SECTOR, fw_stream_erase_step(&s, true)); // old image, sector 0
    preempt_at(1, [this] { fw_stream_begin_deferred(&s, 2); });
    EXPECT_EQ(FW_ERASE_SECTOR, fw_stream_erase_step(&s, true)); // old image, sector 1

    g.erased.clear();
    EXPECT_EQ(FW_ERASE_DONE, run_erase());
    EXPECT_EQ(g.erased, (std::vector<uint32_t>{0, 1})) << "the new erase skipped a sector";
    EXPECT_EQ(g.takes, 1);
    EXPECT_EQ(g.holds, 0);
}

// The same BEGIN, landing right after the old erase's LAST sector, must not have
// its fresh erase marked complete by the old one.
TEST_F(FwStreamTest, ABeginRightAfterTheLastSectorStillErasesItsOwnImage) {
    fw_stream_begin_deferred(&s, 1);
    preempt_at(1, [this] { fw_stream_begin_deferred(&s, 2); });
    EXPECT_EQ(FW_ERASE_DONE, fw_stream_erase_step(&s, true)); // the old image is done

    // The old DONE released a hold the new BEGIN must still have: it took a fresh
    // one when it found the flag clear, or kept the old one.
    EXPECT_TRUE(fw_stream_erase_pending(&s)) << "the new erase was marked complete";
    EXPECT_EQ(g.holds, 1) << "core1 runs during the new erase";

    g.erased.clear();
    EXPECT_EQ(FW_ERASE_DONE, run_erase());
    EXPECT_EQ(g.erased, (std::vector<uint32_t>{0, 1}));
    EXPECT_EQ(g.holds, 0);
}

// --- behaviour kept on purpose ----------------------------------------------

// A refused BEGIN (size 0 or past the slot) stops a pending erase but leaves the
// stream open with its hold, as the two-flag original did. Finalize ends both.
TEST_F(FwStreamTest, ARefusedBeginStopsTheEraseButKeepsTheStreamAndItsHold) {
    fw_stream_begin_deferred(&s, 3);
    EXPECT_EQ(FW_ERASE_SECTOR, fw_stream_erase_step(&s, true));
    fw_stream_begin_refused(&s);
    EXPECT_FALSE(fw_stream_erase_pending(&s));
    EXPECT_TRUE(fw_stream_active(&s));
    EXPECT_EQ(g.holds, 1);
    EXPECT_EQ(FW_ERASE_IDLE, fw_stream_erase_step(&s, true));
    fw_stream_finalize(&s);
    EXPECT_EQ(g.holds, 0);
}
