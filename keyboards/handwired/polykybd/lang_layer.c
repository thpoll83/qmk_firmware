// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#include "lang_layer.h"
#include "keycode_helper.h"
#include "mru.h"
#include "lang/lang_lut.h"
#include "lang/named_glyphs.h"
#include "base/update.h"

// Country-first display order (sorted by country code then language code, the
// same ordering the _LL keymap used before paging). Values are LANG_* enum
// indices. Keep this in sync with lang/lang_lut.xlsx if the language list grows.
static const uint8_t LANG_DISPLAY_ORDER[NUM_LANG] = {
    52, 54, 44, 15, 32, 13, 45, 43, 18, 26,  1, 24, 31,  3, 22,  2, 41, 51, 10, 57,
    27, 25, 53, 20, 47, 36, 37, 49, 48, 35, 55,  5,  8,  7, 14, 29, 30, 34, 39, 42,
    19, 23, 38, 40, 16,  4, 17, 33, 12,  9, 21, 28, 46,  6, 50, 11,  0, 56,
};

static uint8_t s_page = 0;

void lang_init(void) {
    s_page = 0;
}

uint8_t lang_page_count(void) {
    return (uint8_t)((NUM_LANG + LANG_SLOTS_PER_PAGE - 1) / LANG_SLOTS_PER_PAGE);
}

uint8_t lang_active_page(void) {
    return s_page;
}

void lang_page_next(void) {
    uint8_t pages = lang_page_count();
    s_page = (uint8_t)((s_page + 1) % pages);   // wrap to 0 past the last page
}

void lang_page_prev(void) {
    uint8_t pages = lang_page_count();
    s_page = (uint8_t)((s_page + pages - 1) % pages);   // wrap to last before 0
}

void lang_apply_page_sync(uint8_t page) {
    if (page != s_page && page < lang_page_count()) {
        s_page = page;
        request_disp_refresh();
    }
}

int16_t lang_index_for_keycode(uint16_t keycode) {
    if (keycode >= KC_LANG_MRU_BASE && keycode < KC_LANG_MRU_BASE + MRU_CAP) {
        uint8_t lang = mru_lang_get((uint8_t)(keycode - KC_LANG_MRU_BASE));
        return (lang == MRU_LANG_EMPTY) ? -1 : (int16_t)lang;
    }
    if (keycode >= KC_LANG_SLOT_BASE && keycode < KC_LANG_SLOT_BASE + LANG_SLOTS_PER_PAGE) {
        uint16_t vi = (uint16_t)s_page * LANG_SLOTS_PER_PAGE + (keycode - KC_LANG_SLOT_BASE);
        return (vi < NUM_LANG) ? (int16_t)LANG_DISPLAY_ORDER[vi] : -1;
    }
    return -1;
}

const uint32_t* lang_display_text(uint16_t keycode) {
    // Arrows always show (paging wraps, so both directions are always live).
    if (keycode == KC_LANG_PAGE_PREV) {
        return (lang_page_count() > 1) ? (const uint32_t *)(U"  " ICON_LEFT) : (const uint32_t *)U"";
    }
    if (keycode == KC_LANG_PAGE_NEXT) {
        return (lang_page_count() > 1) ? (const uint32_t *)(U"  " ICON_RIGHT) : (const uint32_t *)U"";
    }
    return NULL;
}
