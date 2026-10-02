// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#include "gtest/gtest.h"

extern "C" {
#include "oled_i2c_diag.h"
}

#include <string>

namespace {

class OledI2cDiag : public ::testing::Test {
  protected:
    void SetUp() override { oled_i2c_diag_init(&d); }

    std::string Ok(uint32_t now, oled_i2c_kind kind = OLED_I2C_KIND_CMD) {
        size_t n = oled_i2c_diag_ok(&d, kind, now, buf, sizeof(buf));
        return std::string(buf, n);
    }

    std::string Fail(uint32_t now, bool retry_ok, oled_i2c_class cls = OLED_I2C_CLASS_NACK,
                     oled_i2c_kind kind = OLED_I2C_KIND_CMD) {
        oled_i2c_failure_t f{};
        f.kind     = kind;
        f.cls      = cls;
        f.flags    = 0;
        f.sda      = true;
        f.scl      = true;
        f.len      = 7;
        f.retried  = oled_i2c_diag_should_retry(&d, kind);
        f.retry_ok = f.retried && retry_ok;
        f.retry_cls = cls;
        size_t n   = oled_i2c_diag_fail(&d, &f, now, buf, sizeof(buf));
        return std::string(buf, n);
    }

    static bool Has(const std::string &s, const char *needle) { return s.find(needle) != std::string::npos; }

    oled_i2c_diag_t d;
    char            buf[320];
};

TEST_F(OledI2cDiag, GoodWritesPrintNothing) {
    EXPECT_EQ(Ok(100), "");
    EXPECT_EQ(Ok(200), "");
}

// The field case: one write fails, the retry lands, the panel never looks stuck.
TEST_F(OledI2cDiag, TransientFailurePrintsOneDetailLine) {
    Ok(1000);
    std::string s = Fail(1066, /*retry_ok=*/true);
    EXPECT_EQ(s,
              "oled_i2c: cmd write failed #1 (nack, flags=0x00, sda=1 scl=1, len=7, "
              "66 ms after the last good write) - retry ok\n");
    EXPECT_FALSE(d.stuck[OLED_I2C_KIND_CMD]);
    EXPECT_EQ(d.streak[OLED_I2C_KIND_CMD], 0u);
    EXPECT_EQ(d.retry_failed, 0u);
}

TEST_F(OledI2cDiag, FailureBeforeAnyGoodWriteSaysSo) {
    std::string s = Fail(5, true, OLED_I2C_CLASS_TIMEOUT);
    EXPECT_TRUE(Has(s, "(timeout, "));
    EXPECT_TRUE(Has(s, "no good write since boot)"));
}

TEST_F(OledI2cDiag, FailedRetryNamesItsOwnClass) {
    Ok(0);
    std::string s = Fail(10, /*retry_ok=*/false, OLED_I2C_CLASS_ARB_LOST);
    EXPECT_TRUE(Has(s, "retry failed (arb_lost)"));
    EXPECT_EQ(d.streak[OLED_I2C_KIND_CMD], 1u);
}

TEST_F(OledI2cDiag, ThreeLostWritesMarkStuckOnceAndStopRetrying) {
    Ok(0);
    Fail(10, false);
    Fail(20, false);
    EXPECT_TRUE(oled_i2c_diag_should_retry(&d, OLED_I2C_KIND_CMD));
    std::string s = Fail(30, false);
    EXPECT_TRUE(Has(s, "oled_i2c: status display not responding: 3 cmd writes in a row failed after a retry\n"));
    EXPECT_FALSE(oled_i2c_diag_should_retry(&d, OLED_I2C_KIND_CMD));
    // A fourth lost write does not repeat the stuck line.
    s = Fail(40, false);
    EXPECT_FALSE(Has(s, "not responding:"));
}

TEST_F(OledI2cDiag, NotRetriedWhenStuckIsSaidInTheDetailLine) {
    Ok(0);
    for (uint32_t t = 10; t <= 30; t += 10) Fail(t, false);
    d.printed_in_window = 0;   // make room in the window for one more detail line
    std::string s = Fail(40, true);   // the helper cannot retry: should_retry is false
    EXPECT_TRUE(Has(s, "not retried, display not responding"));
}

TEST_F(OledI2cDiag, RecoveryAfterStuckIsAnnouncedAndRetriesResume) {
    Ok(0);
    for (uint32_t t = 10; t <= 40; t += 10) Fail(t, false);
    std::string s = Ok(50);
    EXPECT_EQ(s, "oled_i2c: status display responding again after 4 failed cmd write(s)\n");
    EXPECT_TRUE(oled_i2c_diag_should_retry(&d, OLED_I2C_KIND_CMD));
    EXPECT_EQ(Ok(60), "");
}

// A retry that lands breaks a streak, so isolated glitches never add up to stuck.
TEST_F(OledI2cDiag, RetryOkResetsTheStreak) {
    Ok(0);
    Fail(10, false);
    Fail(20, false);
    Fail(30, true);
    Fail(40, false);
    EXPECT_FALSE(d.stuck[OLED_I2C_KIND_CMD]);
    EXPECT_EQ(d.streak[OLED_I2C_KIND_CMD], 1u);
}

// oled_render() sends a command, then the data, for every block. A panel that ACKs
// the command and fails the data must still reach the stuck state for DATA writes:
// with one shared streak each good command reset it, and every data write kept its
// two 100 ms attempts (CodeRabbit on #333).
TEST_F(OledI2cDiag, GoodCommandsDoNotResetADataStreak) {
    Ok(0);
    for (uint32_t t = 10; t <= 30; t += 10) {
        Ok(t, OLED_I2C_KIND_CMD);
        Fail(t + 1, false, OLED_I2C_CLASS_TIMEOUT, OLED_I2C_KIND_DATA);
    }
    EXPECT_TRUE(d.stuck[OLED_I2C_KIND_DATA]);
    EXPECT_FALSE(oled_i2c_diag_should_retry(&d, OLED_I2C_KIND_DATA));
    // Commands still land, so they keep their retry.
    EXPECT_FALSE(d.stuck[OLED_I2C_KIND_CMD]);
    EXPECT_TRUE(oled_i2c_diag_should_retry(&d, OLED_I2C_KIND_CMD));
}

TEST_F(OledI2cDiag, TheStuckLineNamesTheKind) {
    Ok(0);
    std::string s;
    for (uint32_t t = 10; t <= 30; t += 10) s = Fail(t, false, OLED_I2C_CLASS_NACK, OLED_I2C_KIND_DATA);
    EXPECT_TRUE(Has(s, "not responding: 3 data writes in a row failed after a retry\n"));
    EXPECT_EQ(Ok(40, OLED_I2C_KIND_CMD), "");   // a command landing does not end the data streak
    EXPECT_EQ(Ok(50, OLED_I2C_KIND_DATA),
              "oled_i2c: status display responding again after 3 failed data write(s)\n");
}

TEST_F(OledI2cDiag, DetailLinesAreRateLimitedAndSummarised) {
    Ok(0);
    int printed = 0;
    for (int i = 0; i < 10; ++i) {
        if (Has(Fail(100 + i, true), "write failed #")) printed++;
    }
    EXPECT_EQ(printed, (int)OLED_I2C_DIAG_LINES_PER_WINDOW);
    EXPECT_EQ(d.suppressed, 7u);
    // The next call after the window closes reports what was hidden.
    std::string s = Ok(OLED_I2C_DIAG_WINDOW_MS + 1);
    EXPECT_EQ(s, "oled_i2c: 7 more failed write(s) not printed in the last 10 s (10 since boot)\n");
    EXPECT_EQ(d.suppressed, 0u);
    EXPECT_TRUE(Has(Fail(OLED_I2C_DIAG_WINDOW_MS + 2, true), "write failed #11"));
}

TEST_F(OledI2cDiag, StuckLineIsNotRateLimited) {
    Ok(0);
    for (int i = 0; i < 3; ++i) Fail(10 + i, true);   // fill the window with transients
    Fail(20, false);
    Fail(21, false);
    std::string s = Fail(22, false);
    EXPECT_FALSE(Has(s, "write failed #"));
    EXPECT_TRUE(Has(s, "not responding:"));
}

TEST_F(OledI2cDiag, DataWritesAreLabelled) {
    oled_i2c_failure_t f{};
    f.kind    = OLED_I2C_KIND_DATA;
    f.cls     = OLED_I2C_CLASS_OVERRUN;
    f.flags   = 0x20;
    f.len     = 129;
    f.retried = true;
    f.retry_ok = true;
    size_t n  = oled_i2c_diag_fail(&d, &f, 0, buf, sizeof(buf));
    std::string s(buf, n);
    EXPECT_TRUE(Has(s, "oled_i2c: data write failed #1 (overrun, flags=0x20, sda=0 scl=0, len=129, "));
}

TEST_F(OledI2cDiag, TinyBufferIsTruncatedNotOverrun) {
    char small[16];
    oled_i2c_failure_t f{};
    f.retried = true;
    size_t n = oled_i2c_diag_fail(&d, &f, 0, small, sizeof(small));
    EXPECT_EQ(n, sizeof(small) - 1);
    EXPECT_EQ(small[sizeof(small) - 1], '\0');
}

TEST_F(OledI2cDiag, UptimeWrapKeepsTheGap) {
    Ok(0xFFFFFFF0u);
    std::string s = Fail(0x10u, true);
    EXPECT_TRUE(Has(s, "32 ms after the last good write"));
}

}  // namespace
