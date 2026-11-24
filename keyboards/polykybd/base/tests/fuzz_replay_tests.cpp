// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Replays the fuzz corpus (dev/fuzz/corpus/<target>/) and a fixed-seed set of
// mutated inputs through the fuzz entry points (dev/fuzz/poly_fuzz.h), so a
// normal `make test:polykybd_fuzz_replay` checks the invariants the fuzzers
// check. This build has no sanitizers: a memory error shows up only in the
// libFuzzer build (dev/fuzz/README.md). A crash input a fuzzer saves belongs in
// the corpus, which this suite then replays.

#include "gtest/gtest.h"

// fontpack.h uses C11 _Static_assert; C++ spells it static_assert.
#define _Static_assert static_assert

extern "C" {
#include "poly_fuzz.h"
#include "fontpack.h"
#include "polymod_crc32.h"
}

#include <dirent.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

typedef int (*entry_fn)(const uint8_t *, size_t);

struct Target {
    const char *name;
    entry_fn    fn;
    size_t      min_seeds;
};

const Target kTargets[] = {
    {"overlay_roi", poly_fuzz_overlay_roi, 6},
    {"prc_report", poly_fuzz_prc_report, 9},
    {"fontpack", poly_fuzz_fontpack, 4},
    {"iconlib", poly_fuzz_iconlib, 3},
    {"emjdict", poly_fuzz_emjdict, 3},
};

// make test runs from the repository root.
const std::string kCorpus = "keyboards/polykybd/dev/fuzz/corpus/";

std::vector<std::vector<uint8_t>> load_corpus(const char *target) {
    std::vector<std::vector<uint8_t>> out;
    std::string dir = kCorpus + target;
    DIR        *d   = opendir(dir.c_str());
    if (!d) return out;
    std::vector<std::string> names;
    while (struct dirent *e = readdir(d)) {
        if (e->d_name[0] != '.') names.push_back(e->d_name);
    }
    closedir(d);
    std::sort(names.begin(), names.end());
    for (const std::string &n : names) {
        FILE *f = fopen((dir + "/" + n).c_str(), "rb");
        if (!f) continue;
        std::vector<uint8_t> buf;
        uint8_t              tmp[4096];
        size_t               r;
        while ((r = fread(tmp, 1, sizeof tmp, f)) > 0) buf.insert(buf.end(), tmp, tmp + r);
        fclose(f);
        out.push_back(buf);
    }
    return out;
}

// xorshift32: the same inputs on every run and every machine.
struct Rng {
    uint32_t s;
    uint32_t next() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    uint32_t below(uint32_t n) { return n ? next() % n : 0; }
};

// One mutation of the kind libFuzzer makes: flip bits, set bytes to edge values,
// truncate, or append random bytes. Big inputs (dictionaries) get their changes
// in the first 4 KB, where the headers and tables are.
std::vector<uint8_t> mutate(const std::vector<uint8_t> &seed, Rng &rng) {
    std::vector<uint8_t> v     = seed;
    const uint32_t       edits = 1 + rng.below(8);
    for (uint32_t k = 0; k < edits; ++k) {
        const size_t span = v.size() < 4096 ? v.size() : 4096;
        switch (rng.below(5)) {
            case 0:
                if (span) v[rng.below(span)] ^= (uint8_t)(1u << rng.below(8));
                break;
            case 1: {
                static const uint8_t edge[] = {0x00, 0x01, 0x7F, 0x80, 0xFE, 0xFF};
                if (span) v[rng.below(span)] = edge[rng.below(sizeof edge)];
                break;
            }
            case 2:
                if (span) v[rng.below(span)] = (uint8_t)rng.next();
                break;
            case 3:
                if (!v.empty()) v.resize(rng.below((uint32_t)v.size()));
                break;
            default:
                for (uint32_t n = 1 + rng.below(16); n; --n) v.push_back((uint8_t)rng.next());
                break;
        }
    }
    return v;
}

const int kRandomPerTarget = 2000;

TEST(FuzzReplay, CorpusPassesEveryTarget) {
    for (const Target &t : kTargets) {
        auto corpus = load_corpus(t.name);
        ASSERT_GE(corpus.size(), t.min_seeds) << "corpus for " << t.name << " not found or short; "
                                              << "run the suite from the repository root";
        for (size_t i = 0; i < corpus.size(); ++i) {
            EXPECT_EQ(t.fn(corpus[i].data(), corpus[i].size()), POLY_FUZZ_OK) << t.name << " seed " << i;
        }
    }
}

TEST(FuzzReplay, MutatedCorpusPassesEveryTarget) {
    for (const Target &t : kTargets) {
        auto corpus = load_corpus(t.name);
        ASSERT_FALSE(corpus.empty()) << t.name;
        Rng rng{0x504B4642u};   // fixed seed
        for (int i = 0; i < kRandomPerTarget; ++i) {
            auto in = mutate(corpus[rng.below((uint32_t)corpus.size())], rng);
            ASSERT_EQ(t.fn(in.data(), in.size()), POLY_FUZZ_OK) << t.name << " mutated input " << i;
        }
    }
}

TEST(FuzzReplay, RandomBytesPassEveryTarget) {
    for (const Target &t : kTargets) {
        Rng rng{0x46555A5Au};   // fixed seed
        for (int i = 0; i < kRandomPerTarget; ++i) {
            std::vector<uint8_t> in(rng.below(512));
            for (auto &b : in) b = (uint8_t)rng.next();
            ASSERT_EQ(t.fn(in.data(), in.size()), POLY_FUZZ_OK) << t.name << " random input " << i;
        }
    }
}

TEST(FuzzReplay, EmptyAndTinyInputs) {
    const uint8_t zero[40] = {0};
    for (const Target &t : kTargets) {
        for (size_t n = 0; n <= sizeof zero; ++n) {
            EXPECT_EQ(t.fn(zero, n), POLY_FUZZ_OK) << t.name << " size " << n;
        }
    }
}

// A PlyF image with `n` one-glyph fonts whose global indices (`reserved`) run
// backwards, so fontpack_assemble() has to reverse them.
std::vector<uint8_t> reversed_pack(uint32_t n) {
    const uint32_t       table = 32 + 20 * n;
    const uint32_t       total = table + 8 * n + 4 * n;
    std::vector<uint8_t> b(total, 0);
    fontpack_header_t    h = {};
    memcpy(h.magic, "PlyF", 4);
    h.abi_version    = FONTPACK_ABI_VERSION;
    h.font_count     = n;
    h.font_table_off = 32;
    h.total_size     = total;
    for (uint32_t i = 0; i < n; ++i) {
        fontpack_font_t f = {};
        f.glyph_off       = table + 8 * i;
        f.bitmap_off      = table + 8 * n + 4 * i;
        f.first = f.last = 0x10000u + i;
        f.yAdvance        = 8;
        f.reserved        = (uint16_t)(n - 1 - i);
        memcpy(&b[32 + 20 * i], &f, sizeof f);
    }
    h.crc32 = crc32_large(&b[32], total - 32, 0);
    memcpy(&b[0], &h, sizeof h);
    return b;
}

// Regression: fontpack_assemble() sorted with an int8_t index. From the 130th
// font on (i = 129) the index went negative, the sort wrote order[-127] (below
// the stack array) and the fonts after it kept their table order. Found while
// writing the fontpack fuzzer's order invariant (UBSan reports the write);
// dev/fuzz/corpus/fontpack/regress_130_fonts_reversed is the same input.
TEST(FuzzReplay, AssembleSortsPacksWithMoreThan128Fonts) {
    for (uint32_t n : {2u, 127u, 128u, 129u, 130u, 199u}) {
        auto pack = reversed_pack(n);
        ASSERT_TRUE(fontpack_load_at(pack.data())) << n;
        fontpack_assemble(nullptr, 0);
        ASSERT_EQ(g_all_font_count, n);
        for (uint32_t k = 0; k < n; ++k) {
            // Global index k is the font with first == 0x10000 + (n - 1 - k).
            ASSERT_EQ(g_all_fonts[k]->first, 0x10000u + (n - 1 - k)) << "n=" << n << " position " << k;
        }
        EXPECT_EQ(poly_fuzz_fontpack(pack.data(), pack.size()), POLY_FUZZ_OK) << n;
    }
    (void)fontpack_load_at(nullptr);
    fontpack_assemble(nullptr, 0);
}

} // namespace
