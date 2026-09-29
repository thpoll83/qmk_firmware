// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Tests for base/ctx_codec.c — the decoder for context-coded overlays (cmd 41).
//
// The decoder has to reproduce the host's encoder bit for bit, and nothing on the
// rig can tell a wrong pixel from a right one. So the core test decodes golden
// vectors the HOST generated (PolyKybdHost tools/gen_ctx_vectors.py, which also
// pins them host-side) and compares all 360 bytes. A mismatch here means host and
// firmware no longer speak the same format.

#include "gtest/gtest.h"

extern "C" {
#include "ctx_codec.h"
#include "ctx_table.h"
}

#include "ctx_codec_vectors.h"

#include <cstring>

namespace {

TEST(CtxCodec, GoldenVectorsDecodeToTheHostsOverlay) {
    for (const ctx_vector_t &v : ctx_vectors) {
        uint8_t out[CTX_FRAME_BYTES];
        memset(out, 0xA5, sizeof out);   // the decoder must clear what it does not draw
        ASSERT_TRUE(ctx_decode_roi(out, v.top, v.left, v.height, v.width, v.payload, v.len, ctx_table_v1))
            << v.name;
        EXPECT_EQ(0, memcmp(out, v.overlay, CTX_FRAME_BYTES)) << v.name;
    }
}

TEST(CtxCodec, TableIsTheFrozenV1) {
    // The table is part of the format: the host encodes with the same bytes.
    // Byte sum and a few spot values catch an accidental regeneration; they are
    // taken from PolyKybdHost polyhost/res/ctx_table_v1.bin.
    EXPECT_EQ(CTX_TABLE_ID, 1);
    uint32_t sum = 0;
    for (int i = 0; i < 1024; i++) {
        ASSERT_GE(ctx_table_v1[i], 1) << i;
        sum += ctx_table_v1[i];
    }
    EXPECT_EQ(sum, 135654u);
    EXPECT_EQ(ctx_table_v1[0], 252);
    EXPECT_EQ(ctx_table_v1[511], 177);
    EXPECT_EQ(ctx_table_v1[1023], 13);
}

TEST(CtxCodec, BoxOutsideTheFrameIsRefusedAndLeavesTheOverlay) {
    uint8_t out[CTX_FRAME_BYTES];
    const uint8_t payload[4] = {0};
    memset(out, 0x5A, sizeof out);
    EXPECT_FALSE(ctx_decode_roi(out, 0, 0, 0, 5, payload, 4, ctx_table_v1));    // empty
    EXPECT_FALSE(ctx_decode_roi(out, 0, 0, 5, 0, payload, 4, ctx_table_v1));
    EXPECT_FALSE(ctx_decode_roi(out, 30, 0, 11, 5, payload, 4, ctx_table_v1));  // 30+11 > 40
    EXPECT_FALSE(ctx_decode_roi(out, 0, 70, 1, 3, payload, 4, ctx_table_v1));   // 70+3 > 72
    EXPECT_FALSE(ctx_decode_roi(out, 255, 255, 255, 255, payload, 4, ctx_table_v1));
    for (uint8_t b : out) ASSERT_EQ(b, 0x5A);
}

TEST(CtxCodec, FullFrameBoxIsAccepted) {
    uint8_t out[CTX_FRAME_BYTES];
    const uint8_t payload[1] = {0};
    EXPECT_TRUE(ctx_decode_roi(out, 0, 0, 40, 72, payload, 1, ctx_table_v1));
}

TEST(CtxCodec, ReadingPastThePayloadIsSafe) {
    // A truncated payload decodes to *something* but must not read past `len`:
    // the reader returns 0 bytes, which the host relies on when it trims zeros.
    uint8_t out[CTX_FRAME_BYTES];
    const ctx_vector_t &v = ctx_vectors[sizeof(ctx_vectors) / sizeof(ctx_vectors[0]) - 1];
    ASSERT_GT(v.len, 2);
    EXPECT_TRUE(ctx_decode_roi(out, v.top, v.left, v.height, v.width, v.payload, 2, ctx_table_v1));
}

TEST(CtxRecord, GoldenRecordsParseToTheHostsFields) {
    // The host packed these (polyhost/util/ctx_codec.py pack_record()).
    int parsed = 0;
    for (const ctx_vector_t &v : ctx_vectors) {
        if (v.record_len == 0) continue;
        ctx_record_t r;
        ASSERT_EQ(ctx_parse_record(v.record, v.record_len, &r), v.record_len) << v.name;
        EXPECT_EQ(r.keycode, v.keycode) << v.name;
        EXPECT_EQ(r.modifier, v.modifier) << v.name;
        EXPECT_EQ(r.top, v.top) << v.name;
        EXPECT_EQ(r.left, v.left) << v.name;
        EXPECT_EQ(r.height, v.height) << v.name;
        EXPECT_EQ(r.width, v.width) << v.name;
        ASSERT_EQ(r.len, v.len) << v.name;
        EXPECT_EQ(0, memcmp(r.payload, v.payload, v.len)) << v.name;
        uint8_t out[CTX_FRAME_BYTES];
        ASSERT_TRUE(ctx_decode_roi(out, r.top, r.left, r.height, r.width, r.payload, r.len, ctx_table_v1));
        EXPECT_EQ(0, memcmp(out, v.overlay, CTX_FRAME_BYTES)) << v.name;
        parsed++;
    }
    EXPECT_GE(parsed, 7);
}

TEST(CtxRecord, TwoRecordsBackToBackThenZeroPadding) {
    // A report as the host builds it: records back to back, zero padded to 62.
    uint8_t report[62] = {0};
    const ctx_vector_t &a = ctx_vectors[0], &b = ctx_vectors[2];
    memcpy(report, a.record, a.record_len);
    memcpy(report + a.record_len, b.record, b.record_len);
    ctx_record_t r;
    uint8_t pos = 0, n;
    ASSERT_EQ(n = ctx_parse_record(report + pos, sizeof report - pos, &r), a.record_len);
    EXPECT_EQ(r.keycode, a.keycode);
    pos += n;
    ASSERT_EQ(n = ctx_parse_record(report + pos, sizeof report - pos, &r), b.record_len);
    EXPECT_EQ(r.keycode, b.keycode);
    pos += n;
    EXPECT_EQ(ctx_parse_record(report + pos, sizeof report - pos, &r), 0);   // keycode 0 ends it
}

TEST(CtxRecord, MalformedRecordsEndTheList) {
    const ctx_vector_t &v = ctx_vectors[3];
    uint8_t buf[64];
    ctx_record_t r;
    memcpy(buf, v.record, v.record_len);
    EXPECT_EQ(ctx_parse_record(buf, CTX_RECORD_HDR - 1, &r), 0);          // no room for a header
    EXPECT_EQ(ctx_parse_record(buf, v.record_len - 1, &r), 0);            // payload runs past the report
    buf[5] |= 0x01;                                                        // reserved bit
    EXPECT_EQ(ctx_parse_record(buf, v.record_len, &r), 0);
    // top 39 with height 2: 41 > 40. keycode 4, mod 0, top 39, left 0, h-1 1, w-1 0, len 0.
    const uint8_t tall[6] = {0x04, 0x09, 0xC0, 0x02, 0x00, 0x00};
    EXPECT_EQ(ctx_parse_record(tall, 6, &r), 0);
    // left 71 with width 2: 73 > 72.
    const uint8_t wide[6] = {0x04, 0x00, 0x23, 0x80, 0x04, 0x00};
    EXPECT_EQ(ctx_parse_record(wide, 6, &r), 0);
    // The same boxes one pixel smaller are accepted.
    const uint8_t tall_ok[6] = {0x04, 0x09, 0xC0, 0x00, 0x00, 0x00};
    EXPECT_EQ(ctx_parse_record(tall_ok, 6, &r), 6);
    EXPECT_EQ(r.top, 39); EXPECT_EQ(r.height, 1);
    const uint8_t wide_ok[6] = {0x04, 0x00, 0x23, 0x80, 0x00, 0x00};
    EXPECT_EQ(ctx_parse_record(wide_ok, 6, &r), 6);
    EXPECT_EQ(r.left, 71); EXPECT_EQ(r.width, 1);
}

} // namespace
