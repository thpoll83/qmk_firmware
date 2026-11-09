// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Tests for base/emoji_dict.c: the emoji search dictionary loader and search.
//
// The bundles are built here by the Python builder (emoji/emoji_dict.py) from
// the committed sources, and the expected results come from its reference
// search (`vectors`), so the C search is checked against the reference on every
// query in VECTOR_QUERIES, for en and de. The rest are refusals: the bundle is
// file-controlled data on the unsigned resource transport.

#include "gtest/gtest.h"

#define _Static_assert static_assert
extern "C" {
#include "emoji_dict.h"
#include "fontpack.h"
#include "polymod_crc32.h"
}

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

namespace {

const char *kTool = "keyboards/polykybd/emoji/emoji_dict.py";

std::string run(const std::string &cmd) {
    std::string out;
    FILE       *p = popen(cmd.c_str(), "r");
    if (!p) return out;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), p)) > 0) out.append(buf, n);
    pclose(p);
    return out;
}

std::vector<uint8_t> build(const std::string &lang) {
    std::string path = "/tmp/polykybd_emoji_dict_" + lang + ".plyd";
    run(std::string("python3 ") + kTool + " build " + lang + " -o " + path + " > /dev/null");
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

std::vector<uint32_t> hex_list(const std::string &s) {
    std::vector<uint32_t> v;
    std::stringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        if (!tok.empty()) v.push_back((uint32_t)std::stoul(tok, nullptr, 16));
    }
    return v;
}

struct Vector {
    std::vector<uint32_t> query;
    bool typo;
    std::vector<uint32_t> results;
};

std::vector<Vector> vectors(const std::string &lang) {
    std::string path = "/tmp/polykybd_emoji_dict_" + lang + ".plyd";
    std::stringstream out(run(std::string("python3 ") + kTool + " vectors " + path));
    std::vector<Vector> v;
    std::string line;
    while (std::getline(out, line)) {
        std::stringstream ls(line);
        std::string q, t, r;
        std::getline(ls, q, '\t');
        std::getline(ls, t, '\t');
        std::getline(ls, r, '\t');
        v.push_back({hex_list(q), t == "1", hex_list(r)});
    }
    return v;
}

std::string show(const std::vector<uint32_t> &q) {
    std::string s;
    for (uint32_t c : q) s += (c < 0x80) ? (char)c : '?';
    return s;
}

// content_version = (number in dict_src/dictionaries.tsv << 8) | revision.
void expect_matches_reference(const std::string &lang, uint16_t content_version) {
    std::vector<uint8_t> blob = build(lang);
    ASSERT_GT(blob.size(), 1000u) << "python3 build of " << lang << " failed";
    uint16_t ver = 0;
    ASSERT_TRUE(emjdict_load_at(blob.data(), 0x20000, &ver));
    EXPECT_EQ(ver, content_version);
    ASSERT_TRUE(emjdict_present());
    std::vector<Vector> vs = vectors(lang);
    ASSERT_GE(vs.size(), 10u);
    for (const Vector &v : vs) {
        std::vector<uint32_t> got(v.results.size() + 4);
        bool typo = false;
        uint16_t total = emjdict_search(v.query.data(), (uint8_t)v.query.size(), 0, got.data(),
                                        (uint8_t)std::min<size_t>(got.size(), 255), &typo);
        got.resize(std::min<size_t>(total, got.size()));
        EXPECT_EQ(total, v.results.size()) << lang << " '" << show(v.query) << "'";
        size_t n = std::min<size_t>(v.results.size(), 255);
        EXPECT_EQ(std::vector<uint32_t>(got.begin(), got.begin() + std::min(n, got.size())),
                  std::vector<uint32_t>(v.results.begin(), v.results.begin() + n))
            << lang << " '" << show(v.query) << "'";
        EXPECT_EQ(typo, v.typo) << lang << " '" << show(v.query) << "'";
    }
    emjdict_unload();
}

TEST(EmojiDict, EnglishMatchesReference) { expect_matches_reference("en", (0 << 8) | 1); }
TEST(EmojiDict, GermanMatchesReference) { expect_matches_reference("de", (1 << 8) | 1); }

TEST(EmojiDict, PagingSkipsResults) {
    std::vector<uint8_t> blob = build("en");
    ASSERT_TRUE(emjdict_load_at(blob.data(), 0x20000, nullptr));
    const uint32_t q[] = {'h', 'e', 'a', 'r', 't'};
    uint32_t all[64], page[12];
    uint16_t total = emjdict_search(q, 5, 0, all, 64, nullptr);
    ASSERT_GT(total, 24u);
    EXPECT_EQ(emjdict_search(q, 5, 12, page, 12, nullptr), total);
    EXPECT_EQ(0, memcmp(page, all + 12, sizeof(page)));
    emjdict_unload();
}

TEST(EmojiDict, WipeSentinelLoadsEmpty) {
    fontpack_header_t h{};
    memcpy(h.magic, "PlyF", 4);
    h.abi_version     = FONTPACK_ABI_VERSION;
    h.content_version = 5;
    h.font_table_off  = sizeof(h);
    h.total_size      = sizeof(h);
    uint16_t ver      = 0;
    EXPECT_TRUE(emjdict_load_at((const uint8_t *)&h, 0x20000, &ver));
    EXPECT_EQ(ver, 5);
    EXPECT_FALSE(emjdict_present());
    const uint32_t q[] = {'a'};
    EXPECT_EQ(emjdict_search(q, 1, 0, nullptr, 0, nullptr), 0);
}

TEST(EmojiDict, Refusals) {
    std::vector<uint8_t> good = build("en");
    ASSERT_GT(good.size(), 1000u);
    auto load = [](std::vector<uint8_t> b, uint32_t cap = 0x20000) {
        return emjdict_load_at(b.data(), cap, nullptr);
    };
    auto refix_crc = [](std::vector<uint8_t> &b) {
        uint32_t c = crc32_large(b.data() + 32, (uint32_t)b.size() - 32, 0);
        memcpy(b.data() + 28, &c, 4);
    };
    EXPECT_TRUE(load(good));
    EXPECT_FALSE(load(good, (uint32_t)good.size() - 4)) << "past the slot";

    std::vector<uint8_t> b = good;
    b[100] ^= 1;
    EXPECT_FALSE(load(b)) << "CRC";

    b = good;
    b[4] = 1;                                  // abi
    EXPECT_FALSE(load(b)) << "ABI";

    b = good;
    b[6] = 0;                                  // flags: not FLAG_DATA
    EXPECT_FALSE(load(b)) << "flags";

    b = good;
    memcpy(b.data() + 32, "EmjX", 4);
    refix_crc(b);
    EXPECT_FALSE(load(b)) << "dict magic";

    b = good;
    uint16_t big = EMJDICT_MAX_CPS + 1;
    memcpy(b.data() + 32 + 8, &big, 2);        // cp_count
    refix_crc(b);
    EXPECT_FALSE(load(b)) << "cp_count";

    b = good;
    uint32_t far = (uint32_t)b.size();
    memcpy(b.data() + 32 + 16, &far, 4);       // words_off at the end
    refix_crc(b);
    EXPECT_FALSE(load(b)) << "words_off";

    b = good;
    uint32_t words_off;
    memcpy(&words_off, b.data() + 32 + 16, 4);
    uint16_t back = 0xFFFF;
    memcpy(b.data() + words_off + 4, &back, 2); // word 1's string past the strings
    refix_crc(b);
    EXPECT_FALSE(load(b)) << "word string offset";
    emjdict_unload();
}

}  // namespace
