// Copyright 2026 Thomas Pollak
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Tests for base/update.c — the idle-activity timestamp and overlay-burst
// coalescing counters. This module has real bug history and had no tests: the
// timestamp was once a signed int32 whose sign bit doubled as the "tracking
// off" sentinel, so idle silently stopped working for the ~25 days of uptime
// after timer_read32() set bit 31, and the host "start idle" backdate
// underflowed in the first two minutes after boot. Both regressions are pinned
// here against the mock clock (platforms/test/timer.c).

#include "gtest/gtest.h"

extern "C" {
#include "update.h"
// The mock clock has no header — the pattern every timer-driving QMK test uses.
void set_time(uint32_t t);
void advance_time(uint32_t ms);
}

namespace {

class IdleUpdateTest : public ::testing::Test {
   protected:
    void SetUp() override {
        set_time(0);
        // The module's statics persist across tests in one binary; put them in
        // a known state instead of relying on test order.
        update_performed();
        clear_overlay_pending();
    }
};

TEST_F(IdleUpdateTest, UpdatePerformedEnablesTrackingAndZeroesElapsed) {
    set_time(5000);
    update_performed();
    EXPECT_TRUE(is_idle_tracking());
    EXPECT_EQ(get_time_since_last_update(), 0u);
    advance_time(1234);
    EXPECT_EQ(get_time_since_last_update(), 1234u);
}

TEST_F(IdleUpdateTest, DisableIdleTrackingTurnsTheGateOff) {
    disable_idle_tracking();
    EXPECT_FALSE(is_idle_tracking());
    update_performed();
    EXPECT_TRUE(is_idle_tracking());
}

TEST_F(IdleUpdateTest, LegacyNegativeSetDisablesAndNonNegativeReenables) {
    set_last_update(-1);
    EXPECT_FALSE(is_idle_tracking());
    set_last_update(1000);
    EXPECT_TRUE(is_idle_tracking());
    EXPECT_EQ(get_last_update(), 1000u);
}

TEST_F(IdleUpdateTest, TrackingSurvivesTheSignBitOfTheTimer) {
    // The 24.86-day regression: uptime past 2^31 ms must not disable idle or
    // corrupt the elapsed arithmetic.
    set_time(0x80000000u + 5000u);
    update_performed();
    EXPECT_TRUE(is_idle_tracking());
    EXPECT_EQ(get_time_since_last_update(), 0u);
    advance_time(250);
    EXPECT_EQ(get_time_since_last_update(), 250u);
}

TEST_F(IdleUpdateTest, ElapsedIsCorrectAcrossTheTimerWrap) {
    set_time(0xFFFFFF00u);
    update_performed();
    advance_time(0x200);  // wraps past 2^32
    EXPECT_EQ(get_time_since_last_update(), 0x200u);
}

TEST_F(IdleUpdateTest, BackdateNearBootDoesNotUnderflowToJustActive) {
    // The host "start idle" regression: backdating by FADE_OUT_TIME (120 s)
    // when uptime is only 1 s must read as "idle for 120 s", not "just active".
    set_time(1000);
    backdate_last_update(120000);
    EXPECT_TRUE(is_idle_tracking());
    EXPECT_EQ(get_time_since_last_update(), 120000u);
}

TEST_F(IdleUpdateTest, BackdateReenablesTracking) {
    disable_idle_tracking();
    backdate_last_update(1);
    EXPECT_TRUE(is_idle_tracking());
}

TEST_F(IdleUpdateTest, OverlayActivityElapsedAndPendingCount) {
    set_time(10000);
    note_overlay_activity();
    note_overlay_activity();
    EXPECT_EQ(overlay_pending_count(), 2u);
    EXPECT_EQ(overlay_activity_elapsed(), 0u);
    advance_time(75);
    EXPECT_EQ(overlay_activity_elapsed(), 75u);
    clear_overlay_pending();
    EXPECT_EQ(overlay_pending_count(), 0u);
}

TEST_F(IdleUpdateTest, OverlayPendingSaturatesInsteadOfWrapping) {
    // A stream longer than 65535 reports must keep reading as "a lot", not
    // wrap back to a small number that defeats the flush threshold.
    for (uint32_t i = 0; i < 70000u; ++i) {
        note_overlay_activity();
    }
    EXPECT_EQ(overlay_pending_count(), 0xFFFFu);
}

TEST_F(IdleUpdateTest, RefreshModeRoundTrips) {
    set_disp_refresh(DONE_ALL);
    EXPECT_EQ(get_refresh_mode(), DONE_ALL);
    request_disp_refresh();
    EXPECT_EQ(get_refresh_mode(), ALL_AT_ONCE);
    set_disp_refresh(START_FIRST_HALF);
    EXPECT_EQ(get_refresh_mode(), START_FIRST_HALF);
}

}  // namespace
