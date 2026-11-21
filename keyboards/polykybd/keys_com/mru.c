// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#include "mru.h"
#include "base/update.h"
#include "lang/lang_lut.h"   // LANG_* enum indices used by s_lang_preset

#include <string.h>

// keycode_helper.h can't include mru.h, so KC_EMJ_SLOT_BASE / KC_LANG_SLOT_BASE
// hardcode the MRU row width as "+ 12". Guard that magic number against MRU_CAP
// here so a change to MRU_CAP fails the build instead of silently overlapping
// the MRU and slot keycode ranges.
_Static_assert(MRU_CAP == 12, "MRU_CAP must match the +12 offset in keycode_helper.h (KC_EMJ_SLOT_BASE / KC_LANG_SLOT_BASE)");

// ── Backing storage ──────────────────────────────────────────────────────────
static uint16_t s_emoji[MRU_CAP];   // packed category|offset codes
static uint8_t  s_lang[MRU_CAP];

static bool s_dirty        = false;   // differs from EEPROM
static bool s_sync_pending = false;   // master needs to push to slave

// Common everyday languages, by LANG_* enum symbol (lang/lang_lut.h) so the
// preset stays correct if the enum is ever reordered.
static const uint8_t s_lang_preset[MRU_CAP] = {
    LANG_ENUS,  // en-US
    LANG_ENGB,  // en-GB
    LANG_DEDE,  // de-DE
    LANG_FRFR,  // fr-FR
    LANG_ESES,  // es-ES
    LANG_ITIT,  // it-IT
    LANG_PTBR,  // pt-BR
    LANG_RURU,  // ru-RU
    LANG_ZHCN,  // zh-CN
    LANG_JAJP,  // ja-JP
    LANG_KOKR,  // ko-KR
    LANG_ARSA,  // ar-SA
};

// ── Generic front-insert with de-dup ─────────────────────────────────────────
// Both lists (emoji u16, lang u8) need the same "move-to-front, de-dup" push, so
// the logic lives once in a byte-wise helper and the two typed wrappers below
// adapt it. Move `value` to the front of `count` `elem_size`-byte elements: a
// re-pick bumps the existing entry to the front, a new value evicts the oldest,
// and the `empty` sentinel is never inserted. Returns true if the list changed.
// Byte comparison is exact for the unsigned-integer element types used here.
static bool mru_push(void *list, uint8_t count, size_t elem_size,
                     const void *empty, const void *value) {
    uint8_t *base = (uint8_t *)list;
    if (memcmp(value, empty, elem_size) == 0) return false;
    uint8_t found = count;
    for (uint8_t i = 0; i < count; ++i) {
        if (memcmp(base + (size_t)i * elem_size, value, elem_size) == 0) { found = i; break; }
    }
    if (found == 0) return false;                              // already most-recent: no change
    uint8_t top = (found < count) ? found : (uint8_t)(count - 1);
    for (uint8_t i = top; i > 0; --i) {                        // shift older entries down
        memcpy(base + (size_t)i * elem_size, base + (size_t)(i - 1) * elem_size, elem_size);
    }
    memcpy(base, value, elem_size);
    return true;
}

static bool push_u16(uint16_t *list, uint16_t empty, uint16_t value) {
    return mru_push(list, MRU_CAP, sizeof value, &empty, &value);
}

static bool push_u8(uint8_t *list, uint8_t empty, uint8_t value) {
    return mru_push(list, MRU_CAP, sizeof value, &empty, &value);
}

// ── 14-bit-per-entry bit packing (LSB-first) for the emoji codes ─────────────
// Serialised form uses 0 == empty (so a zeroed/cleared EEPROM block decodes to
// "no recent" rather than category 0 / glyph 0): a real code is stored as
// code+1, empty as 0. The +1 always fits in 14 bits (max real code ≈ 12287).
static void emoji_pack(const uint16_t codes[MRU_CAP], uint8_t out[MRU_EMOJI_PACKED]) {
    memset(out, 0, MRU_EMOJI_PACKED);
    uint32_t bit = 0;
    for (uint8_t i = 0; i < MRU_CAP; ++i) {
        uint16_t v = (codes[i] == MRU_EMOJI_EMPTY) ? 0 : (uint16_t)((codes[i] + 1u) & 0x3FFF);
        for (uint8_t b = 0; b < MRU_EMOJI_BITS; ++b, ++bit) {
            if (v & (1u << b)) out[bit >> 3] |= (uint8_t)(1u << (bit & 7u));
        }
    }
}

static void emoji_unpack(const uint8_t in[MRU_EMOJI_PACKED], uint16_t codes[MRU_CAP]) {
    uint32_t bit = 0;
    for (uint8_t i = 0; i < MRU_CAP; ++i) {
        uint16_t v = 0;
        for (uint8_t b = 0; b < MRU_EMOJI_BITS; ++b, ++bit) {
            if (in[bit >> 3] & (uint8_t)(1u << (bit & 7u))) v |= (uint16_t)(1u << b);
        }
        codes[i] = (v == 0) ? MRU_EMOJI_EMPTY : (uint16_t)(v - 1u);
    }
}

// Serialise the language list with the same 0 == empty convention (lang+1, or 0).
static void lang_pack_into(uint8_t out[MRU_CAP]) {
    for (uint8_t i = 0; i < MRU_CAP; ++i) {
        out[i] = (s_lang[i] == MRU_LANG_EMPTY) ? 0u : (uint8_t)(s_lang[i] + 1u);
    }
}

static void lang_unpack_into(const uint8_t in[MRU_CAP], uint8_t dst[MRU_CAP]) {
    for (uint8_t i = 0; i < MRU_CAP; ++i) {
        dst[i] = (in[i] == 0u) ? MRU_LANG_EMPTY : (uint8_t)(in[i] - 1u);
    }
}

static void mark_changed(void) {
    s_dirty        = true;
    s_sync_pending = true;
    request_disp_refresh();
}

// ── Public API ────────────────────────────────────────────────────────────────

void mru_init(void) {
    for (uint8_t i = 0; i < MRU_CAP; ++i) {
        s_emoji[i] = MRU_EMOJI_EMPTY;
        s_lang[i]  = MRU_LANG_EMPTY;
    }
    s_dirty        = false;
    s_sync_pending = false;
}

void mru_emoji_push(uint16_t code) {
    if (push_u16(s_emoji, MRU_EMOJI_EMPTY, code)) mark_changed();
}

uint16_t mru_emoji_get(uint8_t pos) {
    return (pos < MRU_CAP) ? s_emoji[pos] : MRU_EMOJI_EMPTY;
}

void mru_emoji_clear(void) {
    for (uint8_t i = 0; i < MRU_CAP; ++i) s_emoji[i] = MRU_EMOJI_EMPTY;
    mark_changed();
}

void mru_emoji_set_all(const uint16_t codes[MRU_CAP]) {
    memcpy(s_emoji, codes, sizeof(s_emoji));
    mark_changed();
}

void mru_lang_push(uint8_t lang) {
    if (push_u8(s_lang, MRU_LANG_EMPTY, lang)) mark_changed();
}

uint8_t mru_lang_get(uint8_t pos) {
    return (pos < MRU_CAP) ? s_lang[pos] : MRU_LANG_EMPTY;
}

void mru_lang_clear(void) {
    for (uint8_t i = 0; i < MRU_CAP; ++i) s_lang[i] = MRU_LANG_EMPTY;
    mark_changed();
}

void mru_lang_preset(void) {
    memcpy(s_lang, s_lang_preset, sizeof(s_lang));
    mark_changed();
}

bool mru_dirty(void)           { return s_dirty; }
void mru_clear_dirty(void)     { s_dirty = false; }
void mru_emoji_pack(uint8_t out[MRU_EMOJI_PACKED]) { emoji_pack(s_emoji, out); }
void mru_lang_pack(uint8_t out[MRU_CAP])           { lang_pack_into(out); }

void mru_load(const uint8_t emoji_packed[MRU_EMOJI_PACKED], const uint8_t lang[MRU_CAP]) {
    emoji_unpack(emoji_packed, s_emoji);
    lang_unpack_into(lang, s_lang);
    s_dirty        = false;   // freshly loaded == matches EEPROM
    s_sync_pending = true;    // but the slave still needs a copy
}

bool mru_sync_pending(void)       { return s_sync_pending; }
void mru_clear_sync_pending(void) { s_sync_pending = false; }

void mru_apply_sync(const uint8_t emoji_packed[MRU_EMOJI_PACKED], const uint8_t lang[MRU_CAP]) {
    uint16_t emoji[MRU_CAP];
    uint8_t  lng[MRU_CAP];
    emoji_unpack(emoji_packed, emoji);
    lang_unpack_into(lang, lng);
    if (memcmp(s_emoji, emoji, sizeof(s_emoji)) != 0 ||
        memcmp(s_lang,  lng,   sizeof(s_lang))  != 0) {
        memcpy(s_emoji, emoji, sizeof(s_emoji));
        memcpy(s_lang,  lng,   sizeof(s_lang));
        // The slave persists its own copy too (saved on suspend), so mark the
        // received snapshot dirty — it now differs from this half's EEPROM.
        // Deliberately not s_sync_pending: only the master pushes, so the slave
        // must never echo the snapshot back.
        s_dirty = true;
        request_disp_refresh();
    }
}
