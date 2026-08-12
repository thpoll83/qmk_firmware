// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Unit tests for the polymod_ltr559 driver. These drive the REAL driver against
// a mock LTR-559 on a mock I2C bus and the QMK test-platform timer, so they
// cover the parts that actually broke in the field or that a refactor could
// silently change: the "harmless when absent" bounded retry, the refusal to
// report a part that did not configure, the ALS byte order, the invalid-sample
// rule, and the growing-then-rolling average window.

#include "gtest/gtest.h"

#include "ltr559_mock_i2c.hpp"

extern "C" {
#include "polymod_ltr559.h"
#include "timer.h"

// The test platform's timer controls (platforms/test/timer.c) have no header —
// every test that drives the clock forward-declares them, as
// quantum/sequencer/tests does.
void set_time(uint32_t t);
void advance_time(uint32_t ms);
}

// Constants mirrored from the driver. Deliberately duplicated rather than
// exported: if someone changes one of these, a test failing is the point.
static constexpr uint8_t  kRegAlsContr    = 0x80;
static constexpr uint8_t  kRegPsContr     = 0x81;
static constexpr uint8_t  kRegPsNPulses   = 0x83;
static constexpr uint8_t  kRegAlsMeasRate = 0x85;
static constexpr uint8_t  kStatusAlsNew   = 1 << 2;
static constexpr uint8_t  kStatusPsNew    = 1 << 0;
static constexpr uint8_t  kStatusAlsBad   = 1 << 7;  // 1 = INVALID
static constexpr uint32_t kPollMs         = 100;
static constexpr uint32_t kRetryMs        = 1000;
static constexpr uint32_t kMaxRetries     = 30;
static constexpr uint32_t kAvgSamples     = 50;

class Ltr559Test : public ::testing::Test {
   protected:
    void SetUp() override {
        LtrMockI2C::Instance().reset();
        ltr559_reset_for_test();
        set_time(0);
    }

    LtrMockI2C& mock() {
        return LtrMockI2C::Instance();
    }

    // Delivers one ALS sample through a full poll cycle.
    void FeedAls(uint16_t ch0, uint16_t ch1, bool valid = true) {
        mock().set_als(ch0, ch1);
        mock().set_status(kStatusAlsNew | (valid ? 0 : kStatusAlsBad));
        advance_time(kPollMs);
        ltr559_task();
    }
};

// ── probe / configuration ───────────────────────────────────────────────────

TEST_F(Ltr559Test, ProbeSucceedsAndConfiguresThePart) {
    EXPECT_TRUE(ltr559_init());
    EXPECT_TRUE(ltr559_available());

    // The driver must bring the bus up itself — it is documented as working on a
    // board with no other I2C peripheral to have called i2c_init() for it.
    EXPECT_GE(mock().i2c_init_calls(), 1u);

    // Config values, and the order: the two ACTIVE bits (PS_CONTR, ALS_CONTR)
    // are written last, after the measurement-rate/pulse settings they govern.
    const std::vector<std::pair<uint8_t, uint8_t>> expected = {
        {kRegAlsMeasRate, 0x01},  // 100 ms integration, 100 ms repeat
        {kRegPsNPulses, 0x08},    // 8 LED pulses
        {kRegPsContr, 0x03},      // PS active
        {kRegAlsContr, 0x09},     // ALS active, gain 4x
    };
    EXPECT_EQ(mock().writes(), expected);
}

TEST_F(Ltr559Test, ProbeFailsWhenNothingIsOnTheBus) {
    mock().set_present(false);
    EXPECT_FALSE(ltr559_init());
    EXPECT_FALSE(ltr559_available());
    EXPECT_TRUE(mock().writes().empty());
}

TEST_F(Ltr559Test, ProbeFailsOnWrongPartId) {
    mock().set_ids(0x91, 0x05);
    EXPECT_FALSE(ltr559_init());
    EXPECT_FALSE(ltr559_available());
}

TEST_F(Ltr559Test, ProbeFailsOnWrongManufacturerId) {
    mock().set_ids(0x92, 0x04);
    EXPECT_FALSE(ltr559_init());
    EXPECT_FALSE(ltr559_available());
}

// The IDs matching only proves the part is there — if a config write then times
// out the part is left unconfigured, and reporting it present would feed garbage
// into the poll path. Guards the explicit comment in ltr559_probe().
TEST_F(Ltr559Test, ProbeFailsWhenConfigurationDoesNotLand) {
    mock().set_failing_write_reg(kRegAlsContr);  // the last of the four writes
    EXPECT_FALSE(ltr559_init());
    EXPECT_FALSE(ltr559_available());
}

// ── the "harmless when absent" contract ─────────────────────────────────────

// This is what makes the module safe to list unconditionally on a board where
// the sensor is an optional extra: it must stop touching the bus after a bounded
// number of retries instead of probing forever.
TEST_F(Ltr559Test, RetryIsBoundedWhenNoSensorIsFitted) {
    mock().set_present(false);
    EXPECT_FALSE(ltr559_init());
    const uint32_t after_init = mock().probe_attempts();
    EXPECT_EQ(after_init, 1u);

    for (uint32_t i = 0; i < kMaxRetries + 10; ++i) {
        advance_time(kRetryMs);
        ltr559_task();
    }

    EXPECT_EQ(mock().probe_attempts(), after_init + kMaxRetries);
    EXPECT_FALSE(ltr559_available());
}

TEST_F(Ltr559Test, RetryIsThrottledToOnePerSecond) {
    mock().set_present(false);
    ltr559_init();
    const uint32_t after_init = mock().probe_attempts();

    for (int i = 0; i < 9; ++i) {  // 900 ms total — under one retry interval
        advance_time(100);
        ltr559_task();
    }
    EXPECT_EQ(mock().probe_attempts(), after_init);

    advance_time(100);  // now at 1000 ms
    ltr559_task();
    EXPECT_EQ(mock().probe_attempts(), after_init + 1);
}

// A sensor that is slow to wake at boot (or fitted while powered) is still
// picked up — that is why the retry exists at all.
TEST_F(Ltr559Test, LateSensorIsPickedUpByTheRetry) {
    mock().set_present(false);
    EXPECT_FALSE(ltr559_init());

    advance_time(kRetryMs);
    ltr559_task();
    EXPECT_FALSE(ltr559_available());

    mock().set_present(true);
    advance_time(kRetryMs);
    ltr559_task();
    EXPECT_TRUE(ltr559_available());
}

// ── polling ─────────────────────────────────────────────────────────────────

TEST_F(Ltr559Test, PollIsThrottledToTheSensorsOwnRate) {
    ASSERT_TRUE(ltr559_init());
    const uint32_t base = mock().status_reads();

    ltr559_task();  // same millisecond as init
    advance_time(kPollMs - 1);
    ltr559_task();
    EXPECT_EQ(mock().status_reads(), base) << "polled faster than the part updates";

    advance_time(1);
    ltr559_task();
    EXPECT_EQ(mock().status_reads(), base + 1);
}

// ── ALS decode ──────────────────────────────────────────────────────────────

TEST_F(Ltr559Test, AlsChannelsAreDecodedInThePartsByteOrder) {
    ASSERT_TRUE(ltr559_init());
    FeedAls(/*ch0=*/0x1234, /*ch1=*/0xABCD);

    ltr559_reading_t r;
    ltr559_get_reading(&r);
    EXPECT_EQ(r.ch0, 0x1234);
    EXPECT_EQ(r.ch1, 0xABCD);
    EXPECT_TRUE(r.als_valid);
}

// The datasheet fit is piecewise on ch1/(ch0+ch1); each branch is exercised so a
// mis-ordered comparison can't slip through. Values are the integer result of
// the documented formula divided by the 4x gain.
TEST_F(Ltr559Test, LuxCoversEachBranchOfThePiecewiseFit) {
    ASSERT_TRUE(ltr559_init());
    ltr559_reading_t r;

    FeedAls(1000, 200);  // ratio 0.166 -> first branch
    ltr559_get_reading(&r);
    EXPECT_EQ(r.lux, 498);

    FeedAls(1000, 1000);  // ratio 0.500 -> second branch
    ltr559_get_reading(&r);
    EXPECT_EQ(r.lux, 580);

    FeedAls(400, 1000);  // ratio 0.714 -> third branch
    ltr559_get_reading(&r);
    EXPECT_EQ(r.lux, 88);

    FeedAls(100, 1000);  // ratio 0.909 -> out of range, reads as dark
    ltr559_get_reading(&r);
    EXPECT_EQ(r.lux, 0);
}

TEST_F(Ltr559Test, LuxIsZeroWhenBothChannelsAreZero) {
    ASSERT_TRUE(ltr559_init());
    FeedAls(0, 0);

    ltr559_reading_t r;
    ltr559_get_reading(&r);
    EXPECT_EQ(r.lux, 0);
}

// ── the rolling average that drives auto-brightness ─────────────────────────

// A brightness driver keys off "avg == 0" to know the sensor has not warmed up
// yet; without that guard the display dips to the near-off floor for the first
// ~1 s of every boot.
TEST_F(Ltr559Test, AverageStaysZeroUntilTheFirstValidSample) {
    ASSERT_TRUE(ltr559_init());
    EXPECT_EQ(ltr559_avg_lux(), 0);

    advance_time(kPollMs);
    mock().set_status(0);  // a poll with no fresh ALS data
    ltr559_task();
    EXPECT_EQ(ltr559_avg_lux(), 0);

    FeedAls(1000, 200);
    EXPECT_EQ(ltr559_avg_lux(), 498);
}

// The window GROWS to 5 s rather than waiting to fill, so the average is usable
// from the first sample.
TEST_F(Ltr559Test, AverageIsOverTheSamplesCollectedSoFar) {
    ASSERT_TRUE(ltr559_init());

    FeedAls(1000, 200);  // lux 498
    EXPECT_EQ(ltr559_avg_lux(), 498);

    FeedAls(500, 100);  // lux 249
    EXPECT_EQ(ltr559_avg_lux(), (498 + 249) / 2);
}

TEST_F(Ltr559Test, AverageRollsTheOldestSampleOutOnceTheWindowIsFull) {
    ASSERT_TRUE(ltr559_init());

    for (uint32_t i = 0; i < kAvgSamples; ++i) {
        FeedAls(1000, 200);  // lux 498
    }
    EXPECT_EQ(ltr559_avg_lux(), 498);

    FeedAls(500, 100);  // lux 249 displaces one 498
    EXPECT_EQ(ltr559_avg_lux(), (498 * (kAvgSamples - 1) + 249) / kAvgSamples);
}

// An out-of-range ALS reading must not skew the average that drives brightness:
// the last good lux is kept and nothing is pushed.
TEST_F(Ltr559Test, InvalidSampleIsFlaggedAndKeptOutOfTheAverage) {
    ASSERT_TRUE(ltr559_init());

    FeedAls(1000, 200);  // lux 498
    ASSERT_EQ(ltr559_avg_lux(), 498);

    FeedAls(1, 1, /*valid=*/false);

    ltr559_reading_t r;
    ltr559_get_reading(&r);
    EXPECT_FALSE(r.als_valid);
    EXPECT_EQ(r.lux, 498) << "last good lux must survive an invalid sample";
    EXPECT_EQ(ltr559_avg_lux(), 498);
}

// ── proximity decode ────────────────────────────────────────────────────────

TEST_F(Ltr559Test, ProximityIsDecodedAsElevenBits) {
    ASSERT_TRUE(ltr559_init());
    mock().set_prox(1234, /*saturated=*/false);
    mock().set_status(kStatusPsNew);
    advance_time(kPollMs);
    ltr559_task();

    EXPECT_EQ(ltr559_prox(), 1234);

    ltr559_reading_t r;
    ltr559_get_reading(&r);
    EXPECT_FALSE(r.prox_sat);
}

TEST_F(Ltr559Test, ProximitySaturationFlagIsSeparateFromTheValue) {
    ASSERT_TRUE(ltr559_init());
    mock().set_prox(2047, /*saturated=*/true);
    mock().set_status(kStatusPsNew);
    advance_time(kPollMs);
    ltr559_task();

    ltr559_reading_t r;
    ltr559_get_reading(&r);
    EXPECT_EQ(r.prox, 2047) << "the saturation bit must not leak into the value";
    EXPECT_TRUE(r.prox_sat);
}

// ── bus faults during normal operation ──────────────────────────────────────

TEST_F(Ltr559Test, TransientBusErrorLeavesTheLastReadingIntact) {
    ASSERT_TRUE(ltr559_init());
    FeedAls(1000, 200);
    ASSERT_EQ(ltr559_avg_lux(), 498);

    mock().set_reads_fail(true);
    advance_time(kPollMs);
    ltr559_task();

    ltr559_reading_t r;
    ltr559_get_reading(&r);
    EXPECT_EQ(r.lux, 498);
    EXPECT_EQ(ltr559_avg_lux(), 498);
    EXPECT_TRUE(ltr559_available()) << "a transient read error must not un-present the part";

    // ...and it recovers on the next poll.
    mock().set_reads_fail(false);
    FeedAls(500, 100);
    EXPECT_EQ(ltr559_avg_lux(), (498 + 249) / 2);
}
