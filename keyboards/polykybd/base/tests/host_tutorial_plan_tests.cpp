// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Tests for base/host_tutorial_plan.c: cmd 49 (HOST TUTORIAL) parsing, matching and
// the watchdog.

#include <cstring>
#include <string>
#include <vector>

#include "gtest/gtest.h"

extern "C" {
#include "host_tutorial_plan.h"
}

namespace {

constexpr uint8_t kRows = 10, kCols = 8, kLayers = 13;

std::vector<uint8_t> report(uint8_t op) {
    std::vector<uint8_t> d(64, 0);
    d[0] = 'P';
    d[1] = HOST_TUT_CMD;
    d[2] = op;
    return d;
}

std::vector<uint8_t> text(const std::string &a, const std::string &b) {
    auto d = report(HOST_TUT_OP_TEXT);
    size_t p = HOST_TUT_IDX_ARG;
    d[p++] = (uint8_t)a.size();
    memcpy(&d[p], a.data(), a.size());
    p += a.size();
    d[p++] = (uint8_t)b.size();
    memcpy(&d[p], b.data(), b.size());
    return d;
}

std::string line(const host_tut_text_t &t, int i) {
    return std::string(host_tut_text_line(&t, (uint8_t)i), t.len[i]);
}

// ---- TEXT ----------------------------------------------------------------------

TEST(HostTutText, TwoLines) {
    auto d = text("Hold Fn", "then tap");
    host_tut_text_t t;
    ASSERT_TRUE(host_tut_parse_text(d.data(), 64, &t));
    EXPECT_EQ(line(t, 0), "Hold Fn");
    EXPECT_EQ(line(t, 1), "then tap");
    EXPECT_FALSE(host_tut_text_empty(&t));
}

TEST(HostTutText, EmptyClears) {
    auto d = text("", "");
    host_tut_text_t t;
    ASSERT_TRUE(host_tut_parse_text(d.data(), 64, &t));
    EXPECT_TRUE(host_tut_text_empty(&t));
}

TEST(HostTutText, TheWholeReportFitsOneLine) {
    // 3 header bytes + 2 length bytes + 59 bytes of text = 64.
    std::string longest(HOST_TUT_TEXT_BYTES, 'i');
    auto d = text(longest, "");
    ASSERT_EQ(d.size(), 64u);
    host_tut_text_t t;
    ASSERT_TRUE(host_tut_parse_text(d.data(), 64, &t));
    EXPECT_EQ(t.len[0], HOST_TUT_TEXT_BYTES);
    // One more byte of text leaves no room for the second length byte.
    d[HOST_TUT_IDX_ARG] = HOST_TUT_TEXT_BYTES + 1;
    EXPECT_FALSE(host_tut_parse_text(d.data(), 64, &t));
}

TEST(HostTutText, Refusals) {
    host_tut_text_t t;
    memset(&t, 0x55, sizeof t);
    host_tut_text_t before = t;
    auto bad = text("Gr\xC3\xB6", "x");       // UTF-8: outside the status face
    EXPECT_FALSE(host_tut_parse_text(bad.data(), 64, &t));
    bad = text("tab\there", "");
    EXPECT_FALSE(host_tut_parse_text(bad.data(), 64, &t));
    bad = text("~", "");
    bad[HOST_TUT_IDX_ARG + 1] = 0x7F;          // DEL
    EXPECT_FALSE(host_tut_parse_text(bad.data(), 64, &t));
    auto d = report(HOST_TUT_OP_TEXT);
    d[HOST_TUT_IDX_ARG] = 62;                  // runs past the report
    EXPECT_FALSE(host_tut_parse_text(d.data(), 64, &t));
    auto shortr = text("abc", "de");
    EXPECT_FALSE(host_tut_parse_text(shortr.data(), 9, &t));   // second line cut off
    EXPECT_EQ(0, memcmp(&t, &before, sizeof t));               // *out untouched
}

TEST(HostTutText, AShortReportWithBothLines) {
    auto d = text("abc", "de");
    host_tut_text_t t;
    EXPECT_TRUE(host_tut_parse_text(d.data(), 3 + 1 + 3 + 1 + 2, &t));
    EXPECT_EQ(line(t, 1), "de");
}

// ---- SPOTLIGHT -----------------------------------------------------------------

TEST(HostTutSpot, MaskBits) {
    auto d = report(HOST_TUT_OP_SPOTLIGHT);
    d[HOST_TUT_IDX_ARG] = HOST_TUT_SPOT_FLAG_ON;
    // (2, 0) = bit 16, (9, 7) = bit 79.
    d[HOST_TUT_IDX_ARG + 1 + 2] = 0x01;
    d[HOST_TUT_IDX_ARG + 1 + 9] = 0x80;
    bool    on = false;
    uint8_t mask[HOST_TUT_MASK_BYTES];
    ASSERT_TRUE(host_tut_parse_spotlight(d.data(), 64, kRows, kCols, &on, mask));
    EXPECT_TRUE(on);
    EXPECT_TRUE(host_tut_mask_has(mask, kCols, 2, 0));
    EXPECT_TRUE(host_tut_mask_has(mask, kCols, 9, 7));
    EXPECT_FALSE(host_tut_mask_has(mask, kCols, 2, 1));
    EXPECT_FALSE(host_tut_mask_has(mask, kCols, 0, 8));     // col out of range
    EXPECT_FALSE(host_tut_mask_has(mask, kCols, 10, 0));    // past the mask
}

TEST(HostTutSpot, OffAndRefusals) {
    auto d = report(HOST_TUT_OP_SPOTLIGHT);
    bool    on = true;
    uint8_t mask[HOST_TUT_MASK_BYTES];
    ASSERT_TRUE(host_tut_parse_spotlight(d.data(), 64, kRows, kCols, &on, mask));
    EXPECT_FALSE(on);
    d[HOST_TUT_IDX_ARG] = 0x02;                             // unknown flag
    EXPECT_FALSE(host_tut_parse_spotlight(d.data(), 64, kRows, kCols, &on, mask));
    d[HOST_TUT_IDX_ARG] = HOST_TUT_SPOT_FLAG_ON;
    // split42 (8 x 6 = 48 bits): bit 48 is past the matrix.
    d[HOST_TUT_IDX_ARG + 1 + 6] = 0x01;
    EXPECT_FALSE(host_tut_parse_spotlight(d.data(), 64, 8, 6, &on, mask));
    EXPECT_TRUE(host_tut_parse_spotlight(d.data(), 64, kRows, kCols, &on, mask));
    EXPECT_FALSE(host_tut_parse_spotlight(d.data(), HOST_TUT_IDX_ARG + HOST_TUT_MASK_BYTES,
                                          kRows, kCols, &on, mask));   // one byte short
}

// ---- ARM and matching ----------------------------------------------------------

std::vector<uint8_t> arm(std::vector<host_tut_cond_t> conds) {
    auto d = report(HOST_TUT_OP_ARM);
    d[HOST_TUT_IDX_ARG] = (uint8_t)conds.size();
    for (size_t i = 0; i < conds.size(); ++i) {
        d[HOST_TUT_IDX_ARG + 1 + 3 * i]     = conds[i].kind;
        d[HOST_TUT_IDX_ARG + 1 + 3 * i + 1] = conds[i].a;
        d[HOST_TUT_IDX_ARG + 1 + 3 * i + 2] = conds[i].b;
    }
    return d;
}

TEST(HostTutArm, ParseAndMatch) {
    auto d = arm({{HOST_TUT_COND_KEY, 2, 0},
                  {HOST_TUT_COND_LAYER, 8, 0},
                  {HOST_TUT_COND_KEYCODE, 0x08, 0x7E}});   // KC_BASE 0x7E08
    host_tut_conds_t c;
    ASSERT_TRUE(host_tut_parse_arm(d.data(), 64, kRows, kCols, kLayers, &c));
    EXPECT_EQ(c.count, 3);
    EXPECT_EQ(c.matched, 0);
    host_tut_note_key(&c, 2, 0, 0x5225, false);          // a release does not count
    EXPECT_EQ(c.matched, 0);
    host_tut_note_key(&c, 2, 0, 0x5225, true);
    EXPECT_EQ(c.matched, 0x01);
    host_tut_note_layers(&c, (1u << 4) | (1u << 5));
    EXPECT_EQ(c.matched, 0x01);
    host_tut_note_layers(&c, 1u << 8);
    EXPECT_EQ(c.matched, 0x03);
    host_tut_note_layers(&c, 1u << 4);                   // sticky: leaving does not clear
    EXPECT_EQ(c.matched, 0x03);
    host_tut_note_key(&c, 9, 0, 0x7E08, true);
    EXPECT_EQ(c.matched, 0x07);
}

TEST(HostTutArm, ZeroConditions) {
    auto d = arm({});
    host_tut_conds_t c;
    ASSERT_TRUE(host_tut_parse_arm(d.data(), 64, kRows, kCols, kLayers, &c));
    EXPECT_EQ(c.count, 0);
    host_tut_note_key(&c, 0, 0, 0, true);
    host_tut_note_layers(&c, 0xFFFFFFFFu);
    EXPECT_EQ(c.matched, 0);
}

TEST(HostTutArm, Refusals) {
    host_tut_conds_t c;
    memset(&c, 0x55, sizeof c);
    host_tut_conds_t before = c;
    const std::vector<std::vector<uint8_t>> bad = {
        arm({{HOST_TUT_COND_KEY, kRows, 0}}),
        arm({{HOST_TUT_COND_KEY, 0, kCols}}),
        arm({{HOST_TUT_COND_LAYER, kLayers, 0}}),
        arm({{HOST_TUT_COND_LAYER, 1, 1}}),
        arm({{0, 0, 0}}),
        arm({{4, 0, 0}}),
        arm({{1, 0, 0}, {1, 0, 0}, {1, 0, 0}, {1, 0, 0}, {1, 0, 0}}),   // 5 > max
    };
    for (const auto &d : bad) {
        EXPECT_FALSE(host_tut_parse_arm(d.data(), 64, kRows, kCols, kLayers, &c));
    }
    auto d = arm({{HOST_TUT_COND_KEY, 1, 1}, {HOST_TUT_COND_KEY, 1, 2}});
    EXPECT_FALSE(host_tut_parse_arm(d.data(), HOST_TUT_IDX_ARG + 1 + 5, kRows, kCols, kLayers, &c));
    EXPECT_EQ(0, memcmp(&c, &before, sizeof c));
}

// ---- the VERIFY checksum -------------------------------------------------------

TEST(HostTutCrc, SameStateSameCrcAndEveryFieldCounts) {
    host_tut_text_t a, b;
    auto d = text("Hold Fn", "now");
    ASSERT_TRUE(host_tut_parse_text(d.data(), 64, &a));
    ASSERT_TRUE(host_tut_parse_text(d.data(), 64, &b));
    b.buf[HOST_TUT_TEXT_BYTES - 1] = 'x';            // past the used bytes: ignored
    uint8_t mask[HOST_TUT_MASK_BYTES] = {0};
    mask[2] = 0x01;
    const uint32_t base = host_tut_state_crc(&a, true, mask);
    EXPECT_EQ(base, host_tut_state_crc(&b, true, mask));
    EXPECT_NE(base, host_tut_state_crc(&a, false, mask));          // spotlight flag
    uint8_t other[HOST_TUT_MASK_BYTES] = {0};
    other[2] = 0x02;
    EXPECT_NE(base, host_tut_state_crc(&a, true, other));          // mask
    auto e = text("Hold Fn", "nov");
    ASSERT_TRUE(host_tut_parse_text(e.data(), 64, &b));
    EXPECT_NE(base, host_tut_state_crc(&b, true, mask));           // text
    auto f = text("Hold Fnn", "ow");                                // same bytes, other split
    ASSERT_TRUE(host_tut_parse_text(f.data(), 64, &b));
    EXPECT_NE(base, host_tut_state_crc(&b, true, mask));
    host_tut_text_t empty;
    memset(&empty, 0, sizeof empty);
    uint8_t zero[HOST_TUT_MASK_BYTES] = {0};
    // A fresh slave (all zero) never matches a master with text or a spotlight.
    EXPECT_NE(host_tut_state_crc(&empty, false, zero), base);
}

// ---- watchdog and reveal -------------------------------------------------------

TEST(HostTutWatchdog, TenSecondsWrapSafe) {
    EXPECT_FALSE(host_tut_expired(1000, 1000));
    EXPECT_FALSE(host_tut_expired(1000, 1000 + HOST_TUT_WATCHDOG_MS - 1));
    EXPECT_TRUE(host_tut_expired(1000, 1000 + HOST_TUT_WATCHDOG_MS));
    EXPECT_FALSE(host_tut_expired(0xFFFFFF00u, 0x00000100u));         // 512 ms across the wrap
    EXPECT_TRUE(host_tut_expired(0xFFFFFF00u, HOST_TUT_WATCHDOG_MS));
}

TEST(HostTutReveal, Progress) {
    EXPECT_EQ(host_tut_reveal_progress(0, 2400), 0);
    EXPECT_EQ(host_tut_reveal_progress(1200, 2400), 127);
    EXPECT_EQ(host_tut_reveal_progress(2400, 2400), 255);
    EXPECT_EQ(host_tut_reveal_progress(99999, 2400), 255);
    EXPECT_EQ(host_tut_reveal_progress(5, 0), 255);
}

}  // namespace
