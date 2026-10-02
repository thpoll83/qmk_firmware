// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Tests for polymod_prc.c — the PRC image decoder.
//
// The decoder has to reproduce the host's encoder bit for bit, and nothing on the
// rig can tell a wrong pixel from a right one. So the core test decodes golden
// vectors the HOST generated (PolyKybdHost tools/gen_prc_vectors.py, which also
// pins them host-side) and compares all 360 bytes. A mismatch here means host and
// firmware no longer speak the same format.

#include "gtest/gtest.h"

extern "C" {
#include "polymod_prc.h"
#include "prc_table_v1.h"
}

#include "prc_vectors.h"

#include <cstring>

namespace {

TEST(PrcCodec, GoldenVectorsDecodeToTheHostsOverlay) {
    for (const prc_vector_t &v : prc_vectors) {
        uint8_t out[PRC_FRAME_BYTES];
        memset(out, 0xA5, sizeof out); // the decoder must clear what it does not draw
        ASSERT_TRUE(prc_decode_roi(out, v.top, v.left, v.height, v.width, v.payload, v.len, prc_table_v1)) << v.name;
        EXPECT_EQ(0, memcmp(out, v.overlay, PRC_FRAME_BYTES)) << v.name;
    }
}

TEST(PrcCodec, TableIsTheFrozenV1) {
    // The table is part of the format: the host encodes with the same bytes.
    // Byte sum and a few spot values catch an accidental regeneration; they are
    // taken from PolyKybdHost polyhost/res/prc_table_v1.bin.
    EXPECT_EQ(PRC_TABLE_ID, 1);
    uint32_t sum = 0;
    for (int i = 0; i < 1024; i++) {
        ASSERT_GE(prc_table_v1[i], 1) << i;
        sum += prc_table_v1[i];
    }
    EXPECT_EQ(sum, 135654u);
    EXPECT_EQ(prc_table_v1[0], 252);
    EXPECT_EQ(prc_table_v1[511], 177);
    EXPECT_EQ(prc_table_v1[1023], 13);
}

TEST(PrcCodec, BoxOutsideTheFrameIsRefusedAndLeavesTheOverlay) {
    uint8_t       out[PRC_FRAME_BYTES];
    const uint8_t payload[4] = {0};
    memset(out, 0x5A, sizeof out);
    EXPECT_FALSE(prc_decode_roi(out, 0, 0, 0, 5, payload, 4, prc_table_v1)); // empty
    EXPECT_FALSE(prc_decode_roi(out, 0, 0, 5, 0, payload, 4, prc_table_v1));
    EXPECT_FALSE(prc_decode_roi(out, 30, 0, 11, 5, payload, 4, prc_table_v1)); // 30+11 > 40
    EXPECT_FALSE(prc_decode_roi(out, 0, 70, 1, 3, payload, 4, prc_table_v1));  // 70+3 > 72
    EXPECT_FALSE(prc_decode_roi(out, 255, 255, 255, 255, payload, 4, prc_table_v1));
    for (uint8_t b : out)
        ASSERT_EQ(b, 0x5A);
}

TEST(PrcCodec, FullFrameBoxIsAccepted) {
    uint8_t       out[PRC_FRAME_BYTES];
    const uint8_t payload[1] = {0};
    EXPECT_TRUE(prc_decode_roi(out, 0, 0, 40, 72, payload, 1, prc_table_v1));
}

TEST(PrcCodec, ReadingPastThePayloadIsSafe) {
    // A truncated payload decodes to *something* but must not read past `len`:
    // the reader returns 0 bytes, which the host relies on when it trims zeros.
    uint8_t             out[PRC_FRAME_BYTES];
    const prc_vector_t &v = prc_vectors[sizeof(prc_vectors) / sizeof(prc_vectors[0]) - 1];
    ASSERT_GT(v.len, 2);
    EXPECT_TRUE(prc_decode_roi(out, v.top, v.left, v.height, v.width, v.payload, 2, prc_table_v1));
}

TEST(PrcCodec, AnyFrameSizeDecodesTheSamePixels) {
    // prc_decode_roi_in() with 72x40 is prc_decode_roi(); with a larger frame the
    // same ROI must land on the same pixels, rows now 80 wide.
    constexpr uint8_t W = 80, H = 48;
    for (const prc_vector_t &v : prc_vectors) {
        uint8_t same[PRC_FRAME_BYTES];
        ASSERT_TRUE(prc_decode_roi_in(same, PRC_FRAME_W, PRC_FRAME_H, v.top, v.left, v.height, v.width, v.payload, v.len, prc_table_v1)) << v.name;
        EXPECT_EQ(0, memcmp(same, v.overlay, PRC_FRAME_BYTES)) << v.name;

        uint8_t big[W * H / 8 + 1];
        memset(big, 0xA5, sizeof big);
        ASSERT_TRUE(prc_decode_roi_in(big, W, H, v.top, v.left, v.height, v.width, v.payload, v.len, prc_table_v1)) << v.name;
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                int want = 0;
                if (y < PRC_FRAME_H && x < PRC_FRAME_W) {
                    int b = y * PRC_FRAME_W + x;
                    want  = (v.overlay[b >> 3] >> (7 - (b & 7))) & 1;
                }
                int b = y * W + x;
                ASSERT_EQ((big[b >> 3] >> (7 - (b & 7))) & 1, want) << v.name << " at " << y << "," << x;
            }
        }
        EXPECT_EQ(big[W * H / 8], 0xA5) << "wrote past the frame";
    }
}

TEST(PrcCodec, BoxOutsideACustomFrameIsRefused) {
    uint8_t       out[16 * 8 / 8];
    const uint8_t payload[4] = {0};
    memset(out, 0x5A, sizeof out);
    EXPECT_FALSE(prc_decode_roi_in(out, 16, 8, 0, 0, 9, 1, payload, 4, prc_table_v1));  // 9 > 8 rows
    EXPECT_FALSE(prc_decode_roi_in(out, 16, 8, 0, 15, 1, 2, payload, 4, prc_table_v1)); // 15+2 > 16
    for (uint8_t b : out)
        ASSERT_EQ(b, 0x5A);
    EXPECT_TRUE(prc_decode_roi_in(out, 16, 8, 0, 0, 8, 16, payload, 4, prc_table_v1));
}

} // namespace
