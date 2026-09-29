// Copyright 2026 Thomas Pollak
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Tests for base/icon_lib.c: the PlyI overlay icon library (HID cmd 42).
//
// The bundle is file-controlled data on the unsigned resource transport, so
// most of these are refusals: every field the loader or the blit trusts is
// corrupted once and must be caught, never drawn.

#include "gtest/gtest.h"

// fontpack.h uses C11 _Static_assert; C++ spells it static_assert.
#define _Static_assert static_assert
extern "C" {
#include "icon_lib.h"
#include "fontpack.h"
#include "map_codec.h"
#include "polymod_crc32.h"
}

#include <cstring>
#include <vector>

namespace {

struct Icon {
    int8_t x, y, w, h;
    std::vector<bool> px;   // row-major, w*h
};

Icon box(int8_t x, int8_t y, int8_t w, int8_t h, bool filled = true) {
    Icon i{x, y, w, h, std::vector<bool>((size_t)w * h, false)};
    for (int r = 0; r < h; ++r)
        for (int c = 0; c < w; ++c)
            i.px[(size_t)r * w + c] = filled || r == 0 || c == 0 || r == h - 1 || c == w - 1;
    return i;
}

void put32(std::vector<uint8_t>& b, size_t at, uint32_t v) { memcpy(&b[at], &v, 4); }

void reseal(std::vector<uint8_t>& b) {
    uint32_t crc = crc32_1byte(b.data() + 32, (uint16_t)(b.size() - 32), 0);
    put32(b, 24, crc);
}

// Serialise `icons` as a PlyI with records of at most `per_record` icons.
std::vector<uint8_t> build(const std::vector<Icon>& icons, size_t per_record = 1000,
                           const char* magic = "PlyI", uint32_t version = 7) {
    size_t n_rec = icons.empty() ? 0 : (icons.size() + per_record - 1) / per_record;
    std::vector<uint8_t> out(sizeof(fontpack_header_t) + n_rec * sizeof(fontpack_font_t), 0);
    std::vector<fontpack_font_t> recs(n_rec);
    for (size_t r = 0; r < n_rec; ++r) {
        size_t lo = r * per_record, hi = std::min(icons.size(), lo + per_record);
        while (out.size() % 4) out.push_back(0);
        recs[r].glyph_off = (uint32_t)out.size();
        std::vector<uint8_t> bitmap;
        std::vector<GFXglyph> glyphs;
        for (size_t i = lo; i < hi; ++i) {
            const Icon& ic = icons[i];
            GFXglyph g{};
            g.bitmapOffset = (uint16_t)bitmap.size();
            g.width = ic.w; g.height = ic.h; g.xOffset = ic.x; g.yOffset = ic.y;
            glyphs.push_back(g);
            std::vector<uint8_t> bits(((size_t)ic.w * ic.h + 7) / 8, 0);
            for (size_t k = 0; k < ic.px.size(); ++k)
                if (ic.px[k]) bits[k >> 3] |= (uint8_t)(0x80u >> (k & 7));
            bitmap.insert(bitmap.end(), bits.begin(), bits.end());
        }
        const uint8_t* gp = reinterpret_cast<const uint8_t*>(glyphs.data());
        out.insert(out.end(), gp, gp + glyphs.size() * sizeof(GFXglyph));
        recs[r].bitmap_off = (uint32_t)out.size();
        out.insert(out.end(), bitmap.begin(), bitmap.end());
        recs[r].first = (uint32_t)lo;
        recs[r].last  = (uint32_t)hi - 1;
    }
    while (out.size() % 4) out.push_back(0);
    if (n_rec) memcpy(&out[sizeof(fontpack_header_t)], recs.data(), n_rec * sizeof(fontpack_font_t));
    fontpack_header_t h{};
    memcpy(h.magic, magic, 4);
    h.abi_version     = FONTPACK_ABI_VERSION;
    h.content_version = version;
    h.font_count      = (uint32_t)n_rec;
    h.font_table_off  = sizeof(fontpack_header_t);
    h.total_size      = (uint32_t)out.size();
    memcpy(out.data(), &h, sizeof(h));
    reseal(out);
    return out;
}

bool pixel(const uint8_t* frame, int x, int y) {
    int bit = y * 72 + x;
    return (frame[bit >> 3] >> (7 - (bit & 7))) & 1;
}

class IconLibTest : public ::testing::Test {
  protected:
    void TearDown() override { iconlib_unload(); }
};

TEST_F(IconLibTest, LoadsAndReportsVersionAndCount) {
    auto b = build({box(40, 2, 10, 8), box(34, 0, 38, 40, false)});
    uint16_t ver = 0;
    ASSERT_TRUE(iconlib_load_at(b.data(), 0, &ver));
    EXPECT_TRUE(iconlib_present());
    EXPECT_EQ(ver, 7);
    EXPECT_EQ(iconlib_count(), 2);
}

TEST_F(IconLibTest, BlitDrawsAtTheBakedOffsetAndClearsTheRest) {
    auto b = build({box(40, 2, 10, 8)});
    ASSERT_TRUE(iconlib_load_at(b.data(), 0, nullptr));
    uint8_t frame[360];
    memset(frame, 0xFF, sizeof(frame));        // stale image in the slot
    ASSERT_TRUE(iconlib_blit(0, frame));
    int set = 0;
    for (int y = 0; y < 40; ++y)
        for (int x = 0; x < 72; ++x) {
            bool in = x >= 40 && x < 50 && y >= 2 && y < 10;
            EXPECT_EQ(pixel(frame, x, y), in) << x << "," << y;
            set += pixel(frame, x, y);
        }
    EXPECT_EQ(set, 80);
}

TEST_F(IconLibTest, OddWidthRowsPackContinuously) {
    // 7x3: rows do not end on a byte boundary, which is where a per-row
    // padding bug would shift every row after the first.
    Icon ic{50, 10, 7, 3, std::vector<bool>(21, false)};
    ic.px[0] = ic.px[8] = ic.px[16] = ic.px[20] = true;   // (0,0) (1,1) (2,2) (6,2)
    auto b = build({ic});
    ASSERT_TRUE(iconlib_load_at(b.data(), 0, nullptr));
    uint8_t frame[360];
    ASSERT_TRUE(iconlib_blit(0, frame));
    EXPECT_TRUE(pixel(frame, 50, 10));
    EXPECT_TRUE(pixel(frame, 51, 11));
    EXPECT_TRUE(pixel(frame, 52, 12));
    EXPECT_TRUE(pixel(frame, 56, 12));
    EXPECT_FALSE(pixel(frame, 56, 11));
}

TEST_F(IconLibTest, IdsSpanSeveralRecords) {
    std::vector<Icon> icons;
    for (int i = 0; i < 5; ++i) icons.push_back(box((int8_t)(34 + i), 0, 1, 1));
    auto b = build(icons, 2);                   // records 0-1, 2-3, 4
    ASSERT_TRUE(iconlib_load_at(b.data(), 0, nullptr));
    EXPECT_EQ(iconlib_count(), 5);
    uint8_t frame[360];
    for (int i = 0; i < 5; ++i) {
        ASSERT_TRUE(iconlib_blit((uint16_t)i, frame)) << i;
        EXPECT_TRUE(pixel(frame, 34 + i, 0)) << i;
    }
    EXPECT_FALSE(iconlib_blit(5, frame));
}

TEST_F(IconLibTest, TheEmptyBundleIsTheWipeSentinel) {
    auto b = build({});
    ASSERT_TRUE(iconlib_load_at(b.data(), 0, nullptr));
    EXPECT_TRUE(iconlib_present());
    EXPECT_EQ(iconlib_count(), 0);
    uint8_t frame[360];
    EXPECT_FALSE(iconlib_blit(0, frame));
}

TEST_F(IconLibTest, AFontBundleIsNotAnIconBundle) {
    auto b = build({box(40, 2, 10, 8)}, 1000, "PlyF");
    EXPECT_FALSE(iconlib_load_at(b.data(), 0, nullptr));
    EXPECT_FALSE(iconlib_present());
}

TEST_F(IconLibTest, RefusesACorruptCrc) {
    auto b = build({box(40, 2, 10, 8)});
    b.back() ^= 1;
    EXPECT_FALSE(iconlib_load_at(b.data(), 0, nullptr));
}

TEST_F(IconLibTest, RefusesABundleLargerThanItsSlot) {
    auto b = build({box(40, 2, 10, 8)});
    EXPECT_FALSE(iconlib_load_at(b.data(), (uint32_t)b.size() - 4, nullptr));
    EXPECT_TRUE(iconlib_load_at(b.data(), (uint32_t)b.size(), nullptr));
}

TEST_F(IconLibTest, RefusesRecordsThatAreNotContiguousFromZero) {
    auto b = build({box(40, 2, 1, 1), box(41, 2, 1, 1)}, 1);
    fontpack_font_t r;
    memcpy(&r, &b[32 + sizeof(fontpack_font_t)], sizeof(r));
    r.first = r.last = 2;                       // a gap: ids 0, 2
    memcpy(&b[32 + sizeof(fontpack_font_t)], &r, sizeof(r));
    reseal(b);
    EXPECT_FALSE(iconlib_load_at(b.data(), 0, nullptr));
}

TEST_F(IconLibTest, RefusesAnIdRangeLongerThanItsGlyphArray) {
    auto b = build({box(40, 2, 1, 1)});
    fontpack_font_t r;
    memcpy(&r, &b[32], sizeof(r));
    r.last = 0xFFFE;
    memcpy(&b[32], &r, sizeof(r));
    reseal(b);
    EXPECT_FALSE(iconlib_load_at(b.data(), 0, nullptr));
}

// A glyph whose box leaves the keycap, or whose bitmap runs past the bundle,
// loads (the loader does not walk every glyph) but must never be drawn.
TEST_F(IconLibTest, BlitRefusesAGlyphOutsideTheFrame) {
    for (Icon ic : {box(70, 0, 3, 1), box(0, 38, 1, 3), box(-1, 0, 1, 1), box(0, 0, 0, 1)}) {
        auto b = build({ic, box(40, 2, 10, 8)});   // a sane second icon keeps the bitmap blob non-empty
        ASSERT_TRUE(iconlib_load_at(b.data(), 0, nullptr));
        uint8_t frame[360];
        memset(frame, 0xAB, sizeof(frame));
        EXPECT_FALSE(iconlib_blit(0, frame));
        EXPECT_EQ(frame[0], 0xAB) << "a refused blit must leave the slot untouched";
    }
}

TEST_F(IconLibTest, BlitRefusesABitmapPastTheEnd) {
    auto b = build({box(34, 0, 38, 40)});
    fontpack_font_t r;
    memcpy(&r, &b[32], sizeof(r));
    GFXglyph g;
    memcpy(&g, &b[r.glyph_off], sizeof(g));
    g.bitmapOffset = 0xFFF0;
    memcpy(&b[r.glyph_off], &g, sizeof(g));
    reseal(b);
    ASSERT_TRUE(iconlib_load_at(b.data(), 0, nullptr));
    uint8_t frame[360];
    EXPECT_FALSE(iconlib_blit(0, frame));
}

// ── cmd 42 pair list ─────────────────────────────────────────────────────────
uint8_t g_pool[4][360];
uint8_t* pool_slot(uint16_t s) { return g_pool[s]; }

std::vector<uint8_t> pairs(const std::vector<std::pair<uint16_t, uint16_t>>& ps, uint8_t width) {
    std::vector<uint8_t> buf(61, 0);
    uint16_t n = (uint16_t)(61 * 8 / width / 2);
    for (uint16_t p = 0; p < n; ++p) {
        auto pr = ps[std::min<size_t>(p, ps.size() - 1)];   // pad by repeating the last pair
        map_codec_write(buf.data(), (uint16_t)(2 * p), pr.first, width);
        map_codec_write(buf.data(), (uint16_t)(2 * p + 1), pr.second, width);
    }
    return buf;
}

TEST_F(IconLibTest, FillAppliesEveryPairAtEveryWidth) {
    auto b = build({box(34, 0, 1, 1), box(35, 0, 1, 1), box(36, 0, 1, 1)});
    ASSERT_TRUE(iconlib_load_at(b.data(), 0, nullptr));
    for (uint8_t w = 8; w <= 11; ++w) {
        memset(g_pool, 0, sizeof(g_pool));
        auto buf = pairs({{0, 2}, {3, 0}, {1, 1}}, w);
        EXPECT_EQ(iconlib_fill_pairs(buf.data(), 61, w, 4, pool_slot), ICONLIB_FILL_OK) << (int)w;
        EXPECT_TRUE(pixel(g_pool[0], 36, 0));
        EXPECT_TRUE(pixel(g_pool[3], 34, 0));
        EXPECT_TRUE(pixel(g_pool[1], 35, 0));
        EXPECT_FALSE(pixel(g_pool[2], 34, 0));
    }
}

TEST_F(IconLibTest, FillStopsAtTheFirstBadPairAndNamesIt) {
    auto b = build({box(34, 0, 1, 1)});
    ASSERT_TRUE(iconlib_load_at(b.data(), 0, nullptr));
    memset(g_pool, 0, sizeof(g_pool));
    auto buf = pairs({{0, 0}, {1, 9}, {2, 0}}, 9);     // id 9 does not exist
    EXPECT_EQ(iconlib_fill_pairs(buf.data(), 61, 9, 4, pool_slot), 1);
    EXPECT_TRUE(pixel(g_pool[0], 34, 0));
    EXPECT_FALSE(pixel(g_pool[2], 34, 0)) << "nothing after the failure is applied";

    buf = pairs({{0, 0}, {7, 0}}, 9);                  // slot 7 is outside a 4-slot pool
    EXPECT_EQ(iconlib_fill_pairs(buf.data(), 61, 9, 4, pool_slot), 1);
}

TEST_F(IconLibTest, FillWithNoLibraryFailsAtPairZero) {
    auto buf = pairs({{0, 0}}, 9);
    EXPECT_EQ(iconlib_fill_pairs(buf.data(), 61, 9, 4, pool_slot), 0);
}

TEST_F(IconLibTest, FillRefusesAWidthOutsideTheCodec) {
    auto b = build({box(34, 0, 1, 1)});
    ASSERT_TRUE(iconlib_load_at(b.data(), 0, nullptr));
    auto buf = pairs({{0, 0}}, 9);
    EXPECT_EQ(iconlib_fill_pairs(buf.data(), 61, 7, 4, pool_slot), 0);
    EXPECT_EQ(iconlib_fill_pairs(buf.data(), 61, 17, 4, pool_slot), 0);
}

}  // namespace
