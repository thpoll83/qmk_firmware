// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#include "mru.h"
#include "base/update.h"

#include <string.h>

// ── Backing storage ──────────────────────────────────────────────────────────
static uint16_t s_emoji[MRU_CAP];   // packed category|offset codes
static uint8_t  s_lang[MRU_CAP];

static bool s_dirty        = false;   // differs from EEPROM
static bool s_sync_pending = false;   // master needs to push to slave

// LANG_* enum indices (see lang/lang_lut.h). Common everyday languages.
static const uint8_t s_lang_preset[MRU_CAP] = {
    0,   // LANG_ENUS  en-US
    41,  // LANG_ENGB  en-GB
    1,   // LANG_DEDE  de-DE
    2,   // LANG_FRFR  fr-FR
    3,   // LANG_ESES  es-ES
    5,   // LANG_ITIT  it-IT
    32,  // LANG_PTBR  pt-BR
    12,  // LANG_RURU  ru-RU
    18,  // LANG_ZHCN  zh-CN
    8,   // LANG_JAJP  ja-JP
    7,   // LANG_KOKR  ko-KR
    9,   // LANG_ARSA  ar-SA
};

// ── Generic front-insert with de-dup, parameterised over element size ─────────
// Returns true if the list content changed.
static bool push_u16(uint16_t *list, uint16_t empty, uint16_t value) {
    if (value == empty) return false;
    // Find an existing copy (so a re-pick just bumps it to the front).
    uint8_t found = MRU_CAP;
    for (uint8_t i = 0; i < MRU_CAP; ++i) {
        if (list[i] == value) { found = i; break; }
    }
    if (found == 0) return false;                 // already most-recent: no change
    uint8_t top = (found < MRU_CAP) ? found : (MRU_CAP - 1);
    for (uint8_t i = top; i > 0; --i) {
        list[i] = list[i - 1];                    // shift older entries down
    }
    list[0] = value;
    return true;
}

static bool push_u8(uint8_t *list, uint8_t empty, uint8_t value) {
    if (value == empty) return false;
    uint8_t found = MRU_CAP;
    for (uint8_t i = 0; i < MRU_CAP; ++i) {
        if (list[i] == value) { found = i; break; }
    }
    if (found == 0) return false;
    uint8_t top = (found < MRU_CAP) ? found : (MRU_CAP - 1);
    for (uint8_t i = top; i > 0; --i) {
        list[i] = list[i - 1];
    }
    list[0] = value;
    return true;
}

// ── 14-bit-per-entry bit packing (LSB-first) for the emoji codes ─────────────
static void emoji_pack(const uint16_t codes[MRU_CAP], uint8_t out[MRU_EMOJI_PACKED]) {
    memset(out, 0, MRU_EMOJI_PACKED);
    uint32_t bit = 0;
    for (uint8_t i = 0; i < MRU_CAP; ++i) {
        uint16_t v = codes[i] & 0x3FFF;
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
        codes[i] = v;
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
const uint8_t*  mru_lang_array(void)  { return s_lang; }

void mru_load(const uint8_t emoji_packed[MRU_EMOJI_PACKED], const uint8_t lang[MRU_CAP]) {
    emoji_unpack(emoji_packed, s_emoji);
    memcpy(s_lang, lang, sizeof(s_lang));
    s_dirty        = false;   // freshly loaded == matches EEPROM
    s_sync_pending = true;    // but the slave still needs a copy
}

bool mru_sync_pending(void)       { return s_sync_pending; }
void mru_clear_sync_pending(void) { s_sync_pending = false; }

void mru_apply_sync(const uint8_t emoji_packed[MRU_EMOJI_PACKED], const uint8_t lang[MRU_CAP]) {
    uint16_t emoji[MRU_CAP];
    emoji_unpack(emoji_packed, emoji);
    if (memcmp(s_emoji, emoji, sizeof(s_emoji)) != 0 ||
        memcmp(s_lang,  lang,  sizeof(s_lang))  != 0) {
        memcpy(s_emoji, emoji, sizeof(s_emoji));
        memcpy(s_lang,  lang,  sizeof(s_lang));
        request_disp_refresh();
    }
}
