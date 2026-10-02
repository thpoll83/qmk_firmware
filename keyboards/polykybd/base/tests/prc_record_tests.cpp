// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Tests for base/prc_record.c — the cmd 41 record header that carries PRC-coded
// keycap images. The golden vectors are the polymod_prc module's: the host packed
// each one as a record as well (polyhost/util/prc_codec.py pack_record()).

#include "gtest/gtest.h"

extern "C" {
#include "prc_record.h"
#include "prc_table_v1.h"
}

#include "prc_vectors.h"

#include <cstring>

namespace {

TEST(PrcRecord, GoldenRecordsParseToTheHostsFields) {
    // The host packed these (polyhost/util/prc_codec.py pack_record()).
    int parsed = 0;
    for (const prc_vector_t &v : prc_vectors) {
        if (v.record_len == 0) continue;
        prc_record_t r;
        ASSERT_EQ(prc_parse_record(v.record, v.record_len, &r), v.record_len) << v.name;
        EXPECT_EQ(r.keycode, v.keycode) << v.name;
        EXPECT_EQ(r.modifier, v.modifier) << v.name;
        EXPECT_EQ(r.top, v.top) << v.name;
        EXPECT_EQ(r.left, v.left) << v.name;
        EXPECT_EQ(r.height, v.height) << v.name;
        EXPECT_EQ(r.width, v.width) << v.name;
        ASSERT_EQ(r.len, v.len) << v.name;
        EXPECT_EQ(0, memcmp(r.payload, v.payload, v.len)) << v.name;
        uint8_t out[PRC_FRAME_BYTES];
        ASSERT_TRUE(prc_decode_roi(out, r.top, r.left, r.height, r.width, r.payload, r.len, prc_table_v1));
        EXPECT_EQ(0, memcmp(out, v.overlay, PRC_FRAME_BYTES)) << v.name;
        parsed++;
    }
    EXPECT_GE(parsed, 7);
}

TEST(PrcRecord, TwoRecordsBackToBackThenZeroPadding) {
    // A report as the host builds it: records back to back, zero padded to 62.
    uint8_t report[62] = {0};
    const prc_vector_t &a = prc_vectors[0], &b = prc_vectors[2];
    memcpy(report, a.record, a.record_len);
    memcpy(report + a.record_len, b.record, b.record_len);
    prc_record_t r;
    uint8_t pos = 0, n;
    ASSERT_EQ(n = prc_parse_record(report + pos, sizeof report - pos, &r), a.record_len);
    EXPECT_EQ(r.keycode, a.keycode);
    pos += n;
    ASSERT_EQ(n = prc_parse_record(report + pos, sizeof report - pos, &r), b.record_len);
    EXPECT_EQ(r.keycode, b.keycode);
    pos += n;
    EXPECT_EQ(prc_parse_record(report + pos, sizeof report - pos, &r), 0);   // keycode 0 ends it
}

TEST(PrcRecord, MalformedRecordsEndTheList) {
    const prc_vector_t &v = prc_vectors[3];
    uint8_t buf[64];
    prc_record_t r;
    memcpy(buf, v.record, v.record_len);
    EXPECT_EQ(prc_parse_record(buf, PRC_RECORD_HDR - 1, &r), 0);          // no room for a header
    EXPECT_EQ(prc_parse_record(buf, v.record_len - 1, &r), 0);            // payload runs past the report
    buf[5] |= 0x01;                                                        // reserved bit
    EXPECT_EQ(prc_parse_record(buf, v.record_len, &r), 0);
    // top 39 with height 2: 41 > 40. keycode 4, mod 0, top 39, left 0, h-1 1, w-1 0, len 0.
    const uint8_t tall[6] = {0x04, 0x09, 0xC0, 0x02, 0x00, 0x00};
    EXPECT_EQ(prc_parse_record(tall, 6, &r), 0);
    // left 71 with width 2: 73 > 72.
    const uint8_t wide[6] = {0x04, 0x00, 0x23, 0x80, 0x04, 0x00};
    EXPECT_EQ(prc_parse_record(wide, 6, &r), 0);
    // The same boxes one pixel smaller are accepted.
    const uint8_t tall_ok[6] = {0x04, 0x09, 0xC0, 0x00, 0x00, 0x00};
    EXPECT_EQ(prc_parse_record(tall_ok, 6, &r), 6);
    EXPECT_EQ(r.top, 39); EXPECT_EQ(r.height, 1);
    const uint8_t wide_ok[6] = {0x04, 0x00, 0x23, 0x80, 0x00, 0x00};
    EXPECT_EQ(prc_parse_record(wide_ok, 6, &r), 6);
    EXPECT_EQ(r.left, 71); EXPECT_EQ(r.width, 1);
}

} // namespace
