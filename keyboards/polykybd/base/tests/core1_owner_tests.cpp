// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// core1_owner holds the rule every core1 bug of 2026-10-07 broke: core1 leaves
// PSM reset only when the last holder lets go, and nothing launches it while a
// hold is outstanding. The hardware seam is faked here, so each test can say
// exactly what the chip would have seen.
#include "gtest/gtest.h"

#include <string>
#include <vector>

extern "C" {
#include "core1_owner.h"
}

// --- fake hardware ----------------------------------------------------------

namespace {
struct FakeCore1 {
    bool                     forced_off    = false;
    bool                     launch_ok     = true;
    int                      service_runs  = 0;
    int                      custom_runs   = 0;
    int                      locks         = 0;
    std::vector<std::string> reports;
    // Runs inside a launch handshake, to model a hold landing on another thread.
    void (*during_launch)(void) = nullptr;
};
FakeCore1 g;

void entry_stub(void) {}
uint32_t  stack_stub[16];
} // namespace

extern "C" {
void core1_hw_lock(void) { g.locks++; }
void core1_hw_unlock(void) { g.locks--; }
void core1_hw_force_off(void) { g.forced_off = true; }
void core1_hw_release(void) { g.forced_off = false; }
bool core1_hw_launch_service(void) {
    if (g.during_launch) {
        auto f = g.during_launch;
        g.during_launch = nullptr;
        f();
    }
    // A core1 in reset never answers the handshake.
    if (g.forced_off || !g.launch_ok) return false;
    g.service_runs++;
    return true;
}
bool core1_hw_launch(void (*)(void), uint32_t *, size_t) {
    if (g.during_launch) {
        auto f = g.during_launch;
        g.during_launch = nullptr;
        f();
    }
    if (g.forced_off || !g.launch_ok) return false;
    g.custom_runs++;
    return true;
}
void core1_hw_report(const char *what) { g.reports.emplace_back(what); }
}

class Core1OwnerTest : public ::testing::Test {
  protected:
    void SetUp() override {
        while (core1_held()) core1_release();
        g = FakeCore1{};
    }
    void TearDown() override { EXPECT_EQ(g.locks, 0) << "lock left taken"; }
};

// --- holds ------------------------------------------------------------------

TEST_F(Core1OwnerTest, AHoldForcesCore1OffAndTheReleaseRelaunchesTheService) {
    core1_hold();
    EXPECT_TRUE(g.forced_off);
    EXPECT_TRUE(core1_held());
    core1_release();
    EXPECT_FALSE(g.forced_off);
    EXPECT_FALSE(core1_held());
    EXPECT_EQ(g.service_runs, 1);
}

// The latent bug the old lockout API had: a crash-record write during a flash
// erase ended its "lockout" by restarting core1 in the middle of the erase.
TEST_F(Core1OwnerTest, AnInnerReleaseDoesNotReleaseAnOuterHold) {
    core1_hold(); // the erase
    core1_hold(); // a crash record write during it
    core1_release();
    EXPECT_TRUE(g.forced_off) << "core1 left reset mid-erase";
    EXPECT_EQ(g.service_runs, 0);
    core1_release();
    EXPECT_FALSE(g.forced_off);
    EXPECT_EQ(g.service_runs, 1);
}

TEST_F(Core1OwnerTest, AReleaseWithoutAHoldDoesNothing) {
    core1_release();
    EXPECT_FALSE(core1_held());
    EXPECT_EQ(g.service_runs, 0);
}

TEST_F(Core1OwnerTest, ARelaunchThatTimesOutIsReported) {
    g.launch_ok = false;
    core1_hold();
    core1_release();
    EXPECT_FALSE(core1_held());
    ASSERT_EQ(g.reports.size(), 1u);
}

// --- core1_run (the Doom engine start) ----------------------------------------

TEST_F(Core1OwnerTest, RunLaunchesTheEntryWhenNothingHoldsCore1) {
    EXPECT_TRUE(core1_run(entry_stub, stack_stub, sizeof stack_stub));
    EXPECT_EQ(g.custom_runs, 1);
    EXPECT_EQ(core1_tenant(), CORE1_TENANT_CUSTOM);
}

// rig, 2026-10-07: the engine was launched from the slot being erased.
TEST_F(Core1OwnerTest, RunIsRefusedWhileAHoldIsOutstanding) {
    core1_hold();
    EXPECT_FALSE(core1_run(entry_stub, stack_stub, sizeof stack_stub));
    EXPECT_TRUE(g.forced_off) << "run released the hold";
    EXPECT_EQ(g.custom_runs, 0);
    core1_release();
}

TEST_F(Core1OwnerTest, AHoldThatLandsDuringTheLaunchFailsTheRunAndKeepsCore1Off) {
    g.during_launch = [] { core1_hold(); };
    EXPECT_FALSE(core1_run(entry_stub, stack_stub, sizeof stack_stub));
    EXPECT_TRUE(g.forced_off);
    EXPECT_EQ(g.custom_runs, 0);
    core1_release();
    EXPECT_EQ(core1_tenant(), CORE1_TENANT_SERVICE);
    EXPECT_EQ(g.service_runs, 1);
}

// --- core1_restore_service (the Doom engine stop) -------------------------------

TEST_F(Core1OwnerTest, RestoreRelaunchesTheServiceWhenNothingHoldsCore1) {
    ASSERT_TRUE(core1_run(entry_stub, stack_stub, sizeof stack_stub));
    EXPECT_TRUE(core1_restore_service());
    EXPECT_EQ(g.service_runs, 1);
    EXPECT_EQ(core1_tenant(), CORE1_TENANT_SERVICE);
}

// rig, 2026-10-07: the slave's engine stop relaunched core1 mid-erase.
TEST_F(Core1OwnerTest, RestoreLaunchesNothingWhileHeldAndTheReleaseDoes) {
    ASSERT_TRUE(core1_run(entry_stub, stack_stub, sizeof stack_stub));
    core1_hold();
    EXPECT_FALSE(core1_restore_service());
    EXPECT_TRUE(g.forced_off) << "restore released the hold";
    EXPECT_EQ(g.service_runs, 0);
    EXPECT_EQ(core1_tenant(), CORE1_TENANT_SERVICE);
    core1_release();
    EXPECT_EQ(g.service_runs, 1);
}

TEST_F(Core1OwnerTest, RestoreStopsRetryingOnceAHoldLands) {
    g.during_launch = [] { core1_hold(); };
    EXPECT_FALSE(core1_restore_service());
    EXPECT_TRUE(g.forced_off);
    EXPECT_EQ(g.service_runs, 0);
    core1_release();
    EXPECT_EQ(g.service_runs, 1);
}

TEST_F(Core1OwnerTest, RestoreGivesUpAfterThreeTimeouts) {
    g.launch_ok = false;
    EXPECT_FALSE(core1_restore_service());
    ASSERT_EQ(g.reports.size(), 1u);
    EXPECT_FALSE(core1_held());
}
