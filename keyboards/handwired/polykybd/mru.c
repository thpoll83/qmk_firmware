// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#include "mru.h"
#include "base/update.h"

#include <string.h>

// ── Backing storage ──────────────────────────────────────────────────────────
static uint32_t s_emoji[MRU_CAP];
static uint8_t  s_lang[MRU_CAP];

static bool s_dirty        = false;   // differs from EEPROM
static bool s_sync_pending = false;   // master needs to push to slave

// Built-in default recents loaded by the "Preset" key. Picked from common,
// widely-supported glyphs that the generated emoji fonts cover.
static const uint32_t s_emoji_preset[MRU_CAP] = {
    0x1F600, // 😀 grinning face
    0x1F602, // 😂 face with tears of joy
    0x1F60D, // 😍 smiling face with heart-eyes
    0x1F44D, // 👍 thumbs up
    0x2764,  // ❤ red heart
    0x1F389, // 🎉 party popper
    0x1F64F, // 🙏 folded hands
    0x1F525, // 🔥 fire
    0x2705,  // ✅ check mark button
    0x1F622, // 😢 crying face
    0x1F914, // 🤔 thinking face
    0x1F44B, // 👋 waving hand
};

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
static bool push_u32(uint32_t *list, uint32_t empty, uint32_t value) {
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

void mru_emoji_push(uint32_t cp) {
    if (push_u32(s_emoji, MRU_EMOJI_EMPTY, cp)) mark_changed();
}

uint32_t mru_emoji_get(uint8_t pos) {
    return (pos < MRU_CAP) ? s_emoji[pos] : MRU_EMOJI_EMPTY;
}

void mru_emoji_clear(void) {
    for (uint8_t i = 0; i < MRU_CAP; ++i) s_emoji[i] = MRU_EMOJI_EMPTY;
    mark_changed();
}

void mru_emoji_preset(void) {
    memcpy(s_emoji, s_emoji_preset, sizeof(s_emoji));
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
const uint32_t* mru_emoji_array(void) { return s_emoji; }
const uint8_t*  mru_lang_array(void)  { return s_lang; }

void mru_load(const uint32_t emoji[MRU_CAP], const uint8_t lang[MRU_CAP]) {
    memcpy(s_emoji, emoji, sizeof(s_emoji));
    memcpy(s_lang,  lang,  sizeof(s_lang));
    s_dirty        = false;   // freshly loaded == matches EEPROM
    s_sync_pending = true;    // but the slave still needs a copy
}

bool mru_sync_pending(void)       { return s_sync_pending; }
void mru_clear_sync_pending(void) { s_sync_pending = false; }

void mru_apply_sync(const uint32_t emoji[MRU_CAP], const uint8_t lang[MRU_CAP]) {
    if (memcmp(s_emoji, emoji, sizeof(s_emoji)) != 0 ||
        memcmp(s_lang,  lang,  sizeof(s_lang))  != 0) {
        memcpy(s_emoji, emoji, sizeof(s_emoji));
        memcpy(s_lang,  lang,  sizeof(s_lang));
        request_disp_refresh();
    }
}
