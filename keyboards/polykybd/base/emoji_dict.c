// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Emoji search dictionary: validation and keyword search. Format and contract
// in emoji_dict.h and emoji/emoji_dict.py.
//
// The bundle is file-controlled data on the unsigned resource transport, so
// every field is read with memcpy (an unaligned wide load is a HardFault on the
// M0+) and every offset and count is bounds-checked at load. After a successful
// load the search can trust the tables' extents, not their contents: a word or
// posting value is still range-checked where it is used.

#include "emoji_dict.h"
#include "fontpack.h"
#include "polymod_crc32.h"
#include <string.h>

#define DICT_HEADER_SIZE 44u
#define POST_NAME_BIT    0x8000u
#define POST_INDEX_MASK  0x0FFFu

typedef struct {
    const uint8_t *base;
    uint32_t       total;
    uint16_t       lang, ncp, nword, nfold, nstop;
    uint32_t       alpha_off, fold_off, stop_off, cps_off, words_off, post_off, strings_off;
    uint32_t       npost, strings_len;
} dict_t;

static dict_t s_d;
static bool   s_present;

// Search scratch: the candidate set and a 2-bit rank per codepoint. While the
// AND of the query words is computed, the rank array doubles as the second set.
static uint8_t s_cand[(EMJDICT_MAX_CPS + 7u) / 8u];
static uint8_t s_rank[(EMJDICT_MAX_CPS + 3u) / 4u];

static uint16_t rd16(uint32_t off) {
    uint16_t v;
    memcpy(&v, s_d.base + off, 2);
    return v;
}
static uint32_t rd32(uint32_t off) {
    uint32_t v;
    memcpy(&v, s_d.base + off, 4);
    return v;
}

void emjdict_unload(void) {
    memset(&s_d, 0, sizeof(s_d));
    s_present = false;
}

bool     emjdict_present(void) { return s_present; }
uint16_t emjdict_lang(void) { return s_d.lang; }
uint16_t emjdict_cp_count(void) { return s_present ? s_d.ncp : 0; }

static bool span_ok(uint32_t off, uint32_t len, uint32_t total) {
    return off <= total && len <= total - off;
}

bool emjdict_load_at(const uint8_t *base, uint32_t cap, uint16_t *out_ver) {
    emjdict_unload();
    if (out_ver) *out_ver = 0;
    if (!base) return false;

    fontpack_header_t h;
    memcpy(&h, base, sizeof(h));
    if (memcmp(h.magic, "PlyF", 4) != 0) return false;   // erased / not a bundle
    if (h.abi_version != FONTPACK_ABI_VERSION) return false;
    if (cap && h.total_size > cap) return false;
    if (h.font_count != 0) return false;

    if (h.total_size == sizeof(fontpack_header_t)) {      // the wipe sentinel
        if (out_ver) *out_ver = (uint16_t)h.content_version;
        return true;
    }
    if (!(h.flags & EMJDICT_FLAG_DATA)) return false;
    if (h.total_size < sizeof(fontpack_header_t) + DICT_HEADER_SIZE) return false;
    if (crc32_large(base + sizeof(fontpack_header_t), h.total_size - sizeof(fontpack_header_t), 0) != h.crc32) {
        return false;
    }

    s_d.base  = base;
    s_d.total = h.total_size;
    const uint32_t d = sizeof(fontpack_header_t);
    if (memcmp(base + d, "EmjD", 4) != 0 || rd16(d + 4) != EMJDICT_FORMAT) return emjdict_unload(), false;
    s_d.lang        = rd16(d + 6);
    s_d.ncp         = rd16(d + 8);
    s_d.nword       = rd16(d + 10);
    s_d.cps_off     = rd32(d + 12);
    s_d.words_off   = rd32(d + 16);
    s_d.strings_off = rd32(d + 20);
    uint16_t nalpha = rd16(d + 24);
    s_d.nfold       = rd16(d + 26);
    s_d.alpha_off   = rd32(d + 28);
    s_d.fold_off    = rd32(d + 32);
    s_d.nstop       = rd16(d + 36);
    s_d.stop_off    = rd32(d + 40);
    (void)nalpha;

    const uint32_t t = s_d.total;
    if (s_d.ncp == 0 || s_d.ncp > EMJDICT_MAX_CPS || s_d.nword == 0) return emjdict_unload(), false;
    if (!span_ok(s_d.cps_off, 4u * s_d.ncp, t)) return emjdict_unload(), false;
    if (!span_ok(s_d.fold_off, 4u * s_d.nfold, t)) return emjdict_unload(), false;
    if (!span_ok(s_d.words_off, 4u * (s_d.nword + 1u), t)) return emjdict_unload(), false;
    if (s_d.strings_off > t || s_d.stop_off > t) return emjdict_unload(), false;
    s_d.post_off    = s_d.words_off + 4u * (s_d.nword + 1u);
    s_d.npost       = rd16(s_d.words_off + 4u * s_d.nword + 2u);   // sentinel's first posting
    s_d.strings_len = t - s_d.strings_off;
    if (!span_ok(s_d.post_off, 2u * s_d.npost, t)) return emjdict_unload(), false;

    // Records: string offsets and posting starts never decrease, and both stay
    // inside their tables, so word i is [str(i), str(i+1)) and its postings
    // [post(i), post(i+1)) without further checks.
    uint16_t prev_s = 0, prev_p = 0;
    for (uint32_t i = 0; i <= s_d.nword; ++i) {
        uint16_t so = rd16(s_d.words_off + 4u * i);
        uint16_t ps = rd16(s_d.words_off + 4u * i + 2u);
        if (so < prev_s || ps < prev_p || so > s_d.strings_len || ps > s_d.npost) return emjdict_unload(), false;
        if (i > 0 && so == prev_s) return emjdict_unload(), false;   // a word needs its NUL
        prev_s = so;
        prev_p = ps;
    }
    // Stop words: nstop NUL-terminated strings inside the bundle.
    uint32_t o = s_d.stop_off;
    for (uint16_t i = 0; i < s_d.nstop; ++i) {
        while (o < t && s_d.base[o] != 0) ++o;
        if (o >= t) return emjdict_unload(), false;
        ++o;
    }
    s_present = true;
    if (out_ver) *out_ver = (uint16_t)h.content_version;
    return true;
}

// ── words ──────────────────────────────────────────────────────────────────

typedef struct {
    uint8_t c[EMJDICT_QUERY_MAX];
    uint8_t len;
    bool    typo;     // matched through the edit-distance pass, not by prefix
    uint16_t lo, hi;  // prefix: matching word range; typo: 0..nword, filtered
} qword_t;

// Word i's letter codes, unpacked into s_word: 6 bits per letter, least
// significant bits first, ended by code 0 or by the next word's first byte
// (format 2, emoji_dict.py pack_word()). The pointer is valid until the next call.
static uint8_t s_word[EMJDICT_WORD_MAX];

static const uint8_t *word_ptr(uint16_t i, uint8_t *len) {
    uint16_t a   = rd16(s_d.words_off + 4u * i);
    uint16_t b   = rd16(s_d.words_off + 4u * (i + 1u));
    uint32_t acc = 0;
    uint8_t  nbits = 0, n = 0;
    for (uint16_t o = a; o < b && n < EMJDICT_WORD_MAX; ++o) {
        acc |= (uint32_t)s_d.base[s_d.strings_off + o] << nbits;
        nbits += 8;
        while (nbits >= EMJDICT_LETTER_BITS) {
            uint8_t c = (uint8_t)(acc & ((1u << EMJDICT_LETTER_BITS) - 1u));
            acc >>= EMJDICT_LETTER_BITS;
            nbits -= EMJDICT_LETTER_BITS;
            if (c == 0 || n >= EMJDICT_WORD_MAX) {
                *len = n;
                return s_word;
            }
            s_word[n++] = c;
        }
    }
    *len = n;
    return s_word;
}

// <0, 0, >0 like memcmp on the byte strings, a shorter prefix sorting first.
static int word_cmp(uint16_t i, const uint8_t *k, uint8_t kl) {
    uint8_t        wl;
    const uint8_t *w = word_ptr(i, &wl);
    uint8_t        n = wl < kl ? wl : kl;
    for (uint8_t j = 0; j < n; ++j) {
        if (w[j] != k[j]) return (int)w[j] - (int)k[j];
    }
    return (int)wl - (int)kl;
}

static bool word_has_prefix(uint16_t i, const uint8_t *k, uint8_t kl) {
    uint8_t        wl;
    const uint8_t *w = word_ptr(i, &wl);
    return wl >= kl && memcmp(w, k, kl) == 0;
}

static bool word_equals(uint16_t i, const uint8_t *k, uint8_t kl) {
    uint8_t        wl;
    const uint8_t *w = word_ptr(i, &wl);
    return wl == kl && memcmp(w, k, kl) == 0;
}

// One insert, delete, substitution or swap of neighbours turns a into b.
static bool within_one(const uint8_t *a, uint8_t la, const uint8_t *b, uint8_t lb) {
    if (la == lb && memcmp(a, b, la) == 0) return true;
    if (la > lb + 1 || lb > la + 1) return false;
    uint8_t i = 0, m = la < lb ? la : lb;
    while (i < m && a[i] == b[i]) ++i;
    if (la == lb) {
        if (memcmp(a + i + 1, b + i + 1, la - i - 1) == 0) return true;   // substitution
        return i + 1 < la && a[i] == b[i + 1] && a[i + 1] == b[i] &&
               memcmp(a + i + 2, b + i + 2, la - i - 2) == 0;              // swap
    }
    if (la > lb) return memcmp(a + i + 1, b + i, la - i - 1) == 0;        // delete from a
    return memcmp(a + i, b + i + 1, lb - i - 1) == 0;                      // insert into a
}

// The query word against the start of dictionary word i, so a typo inside the
// part typed so far still matches ("grni" -> "grinning").
static bool word_typo_match(uint16_t i, const uint8_t *k, uint8_t kl) {
    uint8_t        wl;
    const uint8_t *w = word_ptr(i, &wl);
    uint8_t        a = wl < kl ? wl : kl;
    uint8_t        b = wl < kl + 1u ? wl : (uint8_t)(kl + 1u);
    return within_one(k, kl, w, a) || within_one(k, kl, w, b);
}

static bool word_matches(const qword_t *q, uint16_t i) {
    return !q->typo || word_typo_match(i, q->c, q->len);
}

static uint16_t lower_bound(const uint8_t *k, uint8_t kl) {
    uint16_t lo = 0, hi = s_d.nword;
    while (lo < hi) {
        uint16_t mid = (uint16_t)((lo + hi) / 2u);
        if (word_cmp(mid, k, kl) < 0) {
            lo = (uint16_t)(mid + 1u);
        } else {
            hi = mid;
        }
    }
    return lo;
}

// Resolve a query word: a prefix range, else the typo pass. False if neither
// finds a word.
static bool resolve(qword_t *q, bool *typo) {
    q->lo = lower_bound(q->c, q->len);
    q->hi = q->lo;
    while (q->hi < s_d.nword && word_has_prefix(q->hi, q->c, q->len)) ++q->hi;
    if (q->hi > q->lo) return true;
    q->typo = true;
    q->lo   = 0;
    q->hi   = s_d.nword;
    if (typo) *typo = true;
    for (uint16_t i = 0; i < s_d.nword; ++i) {
        if (word_typo_match(i, q->c, q->len)) return true;
    }
    return false;
}

static bool is_stop_word(const qword_t *q) {
    uint32_t o = s_d.stop_off;
    for (uint16_t i = 0; i < s_d.nstop; ++i) {
        const uint8_t *s = s_d.base + o;
        uint32_t       l = 0;
        while (s[l] != 0) ++l;
        if (l == q->len && memcmp(s, q->c, l) == 0) return true;
        o += l + 1u;
    }
    return false;
}

static uint8_t fold_code(uint32_t cp) {
    uint16_t lo = 0, hi = s_d.nfold;
    while (lo < hi) {
        uint16_t mid = (uint16_t)((lo + hi) / 2u);
        uint32_t e   = rd32(s_d.fold_off + 4u * mid);
        uint32_t c   = e & 0xFFFFFFu;
        if (c == cp) return (uint8_t)(e >> 24);
        if (c < cp) {
            lo = (uint16_t)(mid + 1u);
        } else {
            hi = mid;
        }
    }
    return EMJDICT_CODE_UNKNOWN;
}

uint8_t emjdict_letter_code(uint32_t cp) {
    return (s_present && cp != U' ') ? fold_code(cp) : EMJDICT_CODE_UNKNOWN;
}

bool emjdict_maps_letter(uint32_t cp) {
    return s_present && cp != U' ' && fold_code(cp) != EMJDICT_CODE_UNKNOWN;
}

// ── sets ───────────────────────────────────────────────────────────────────

static inline bool bit_get(const uint8_t *s, uint16_t i) { return (s[i >> 3] >> (i & 7u)) & 1u; }
static inline void bit_set(uint8_t *s, uint16_t i) { s[i >> 3] |= (uint8_t)(1u << (i & 7u)); }

static inline uint8_t rank_get(uint16_t i) { return (uint8_t)((s_rank[i >> 2] >> ((i & 3u) * 2u)) & 3u); }
static inline void    rank_set(uint16_t i, uint8_t r) {
    uint8_t sh     = (uint8_t)((i & 3u) * 2u);
    s_rank[i >> 2] = (uint8_t)((s_rank[i >> 2] & ~(3u << sh)) | ((r & 3u) << sh));
}

// Every codepoint index a query word names, through `visit`.
#define FOR_EACH_POSTING(q, w, p, body)                                         \
    for (uint16_t w = (q)->lo; w < (q)->hi; ++w) {                               \
        if (!word_matches((q), w)) continue;                                     \
        uint16_t ps_ = rd16(s_d.words_off + 4u * w + 2u);                        \
        uint16_t pe_ = rd16(s_d.words_off + 4u * (w + 1u) + 2u);                 \
        for (uint16_t k_ = ps_; k_ < pe_; ++k_) {                                \
            uint16_t p = rd16(s_d.post_off + 2u * k_);                           \
            if ((p & POST_INDEX_MASK) >= s_d.ncp) continue;                      \
            body                                                                 \
        }                                                                        \
    }

uint16_t emjdict_search(const uint32_t *query, uint8_t n, uint16_t skip,
                        uint32_t *out, uint8_t out_max, bool *typo) {
    if (typo) *typo = false;
    if (!s_present || !query) return 0;
    if (n > EMJDICT_QUERY_MAX) n = EMJDICT_QUERY_MAX;

    // Split at spaces and map each letter through the fold table.
    qword_t q[EMJDICT_MAX_WORDS];
    uint8_t nq = 0;
    for (uint8_t i = 0; i < n && nq < EMJDICT_MAX_WORDS;) {
        while (i < n && query[i] == U' ') ++i;
        if (i >= n) break;
        qword_t *w = &q[nq];
        w->len     = 0;
        w->typo    = false;
        while (i < n && query[i] != U' ') w->c[w->len++] = fold_code(query[i++]);
        ++nq;
    }
    if (nq == 0) return 0;

    // Drop stop words except the last word, which the user is still typing ("a"
    // is how "apple" starts), then words that match nothing.
    uint8_t m = 0;
    for (uint8_t i = 0; i < nq; ++i) {
        if (i + 1u < nq && is_stop_word(&q[i])) continue;
        if (!resolve(&q[i], typo)) continue;
        if (m != i) q[m] = q[i];
        ++m;
    }
    if (m == 0) return 0;

    // Candidates: what the first word names, narrowed by each further word unless
    // that would leave nothing (a wrong extra word must not empty the results).
    const uint16_t ncp   = s_d.ncp;
    const uint16_t bytes = (uint16_t)((ncp + 7u) / 8u);
    memset(s_cand, 0, bytes);
    FOR_EACH_POSTING(&q[0], w, p, { bit_set(s_cand, p & POST_INDEX_MASK); })
    uint8_t *tmp = s_rank;   // the rank array is free until the ranking below
    for (uint8_t j = 1; j < m; ++j) {
        memset(tmp, 0, bytes);
        bool any = false;
        FOR_EACH_POSTING(&q[j], w, p, {
            uint16_t x = p & POST_INDEX_MASK;
            if (bit_get(s_cand, x)) {
                bit_set(tmp, x);
                any = true;
            }
        })
        if (any) memcpy(s_cand, tmp, bytes);
    }

    // Rank by the first word: 0 exact word in the name, 1 exact keyword, 2 prefix
    // in the name, 3 prefix keyword; the best rank a codepoint gets counts.
    memset(s_rank, 0xFF, (ncp + 3u) / 4u);
    FOR_EACH_POSTING(&q[0], w, p, {
        uint16_t x = p & POST_INDEX_MASK;
        if (!bit_get(s_cand, x)) continue;
        uint8_t r = (uint8_t)((word_equals(w, q[0].c, q[0].len) ? 0u : 2u) + ((p & POST_NAME_BIT) ? 0u : 1u));
        if (r < rank_get(x)) rank_set(x, r);
    })

    // Emit in rank order, picker order inside a rank.
    uint16_t total = 0;
    uint8_t  got   = 0;
    for (uint8_t r = 0; r < 4u; ++r) {
        for (uint16_t x = 0; x < ncp; ++x) {
            if (!bit_get(s_cand, x) || rank_get(x) != r) continue;
            if (total >= skip && got < out_max && out) out[got++] = rd32(s_d.cps_off + 4u * x);
            ++total;
        }
    }
    return total;
}
