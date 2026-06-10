// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#include "lang_layer.h"
#include "keycode_helper.h"
#include "mru.h"
#include "lang/lang_lut.h"
#include "lang/named_glyphs.h"
#include "base/update.h"
#include "base/disp_array.h"

// ── Region grouping ───────────────────────────────────────────────────────────
//
// Languages are grouped into continent regions, mirroring the PolyHost "Select
// language" menu (services/lang_regions.py: LANG_REGION + LANG_REGION_ORDER).
// REGION_LANGS lists the LANG_* enum indices region by region in display order;
// REGION_OFFSET[r]..REGION_OFFSET[r+1] is region r's slice. Keep both in sync
// with the host whenever the language list or the region mapping changes.
//
// Region order: 0 Americas · 1 Europe · 2 Middle East & Caucasus · 3 Africa ·
//               4 Asia · 5 Oceania   (Africa & Oceania are currently empty).

static const uint8_t REGION_OFFSET[NUM_LANG_REGIONS + 1] = {
    0, 4, 34, 41, 41, 58, 58,
};

static const uint8_t REGION_LANGS[NUM_LANG] = {
    // Americas (4)
    32, 45, 42,  0,
    // Europe (30)
    44, 15, 13, 43, 26,  1, 24, 31,  3, 22,  2, 41, 10, 27, 25, 55,
     5, 29, 30, 34, 19, 23, 16,  4, 17, 33, 12, 21, 28, 11,
    // Middle East & Caucasus (7)
    52, 54, 51, 20, 35,  9,  6,
    // Africa (0) — empty
    // Asia (17)
    18, 57, 53, 36, 37, 47, 48, 49,  8,  7, 14, 39, 38, 40, 46, 50, 56,
    // Oceania (0) — empty
};

static const uint32_t *const REGION_LABELS[NUM_LANG_REGIONS] = {
    U"America",
    U"Europe",
    U"Mid East",
    U"Africa",
    U"Asia",
    U"Oceania",
};

static uint8_t s_region = 0;
static uint8_t s_page   = 0;

void lang_init(void) {
    s_region = 0;
    s_page   = 0;
}

uint8_t lang_active_region(void) { return s_region; }
uint8_t lang_active_page(void)   { return s_page; }

uint8_t lang_region_count(uint8_t region) {
    if (region >= NUM_LANG_REGIONS) return 0;
    return (uint8_t)(REGION_OFFSET[region + 1] - REGION_OFFSET[region]);
}

uint8_t lang_region_page_count(uint8_t region) {
    uint8_t count = lang_region_count(region);
    if (count == 0) return 1;
    return (uint8_t)((count + LANG_SLOTS_PER_PAGE - 1) / LANG_SLOTS_PER_PAGE);
}

void lang_select_region(uint8_t region) {
    if (region < NUM_LANG_REGIONS && region != s_region) {
        s_region = region;
        s_page   = 0;
        request_disp_refresh();
    }
}

void lang_page_next(void) {
    uint8_t pages = lang_region_page_count(s_region);
    s_page = (uint8_t)((s_page + 1) % pages);          // wrap to page 0 past the last
}

void lang_page_prev(void) {
    uint8_t pages = lang_region_page_count(s_region);
    s_page = (uint8_t)((s_page + pages - 1) % pages);  // wrap to the last before 0
}

uint8_t lang_pack_state(void) {
    return (uint8_t)(((s_region & 0x0F) << 4) | (s_page & 0x0F));
}

void lang_apply_sync(uint8_t packed) {
    uint8_t region = (uint8_t)(packed >> 4);
    uint8_t page   = (uint8_t)(packed & 0x0F);
    if (region >= NUM_LANG_REGIONS) region = 0;
    if (region != s_region || page != s_page) {
        s_region = region;
        s_page   = page;
        request_disp_refresh();
    }
}

const uint32_t* lang_region_label(uint8_t region) {
    if (region >= NUM_LANG_REGIONS) return (const uint32_t *)U"";
    return REGION_LABELS[region];
}

int16_t lang_index_for_keycode(uint16_t keycode) {
    if (keycode >= KC_LANG_MRU_BASE && keycode < KC_LANG_MRU_BASE + MRU_CAP) {
        uint8_t lang = mru_lang_get((uint8_t)(keycode - KC_LANG_MRU_BASE));
        return (lang == MRU_LANG_EMPTY) ? -1 : (int16_t)lang;
    }
    if (keycode >= KC_LANG_SLOT_BASE && keycode < KC_LANG_SLOT_BASE + LANG_SLOTS_PER_PAGE) {
        uint8_t  slot  = (uint8_t)(keycode - KC_LANG_SLOT_BASE);
        uint16_t local = (uint16_t)s_page * LANG_SLOTS_PER_PAGE + slot;     // index within region
        uint16_t vi    = REGION_OFFSET[s_region] + local;
        return (local < lang_region_count(s_region)) ? (int16_t)REGION_LANGS[vi] : -1;
    }
    return -1;
}

const uint32_t* lang_display_text(uint16_t keycode) {
    // Page arrows — blank when the active region fits on a single page.
    if (keycode == KC_LANG_PAGE_PREV) {
        return (lang_region_page_count(s_region) > 1) ? (const uint32_t *)(U"  " ICON_LEFT) : (const uint32_t *)U"";
    }
    if (keycode == KC_LANG_PAGE_NEXT) {
        return (lang_region_page_count(s_region) > 1) ? (const uint32_t *)(U"  " ICON_RIGHT) : (const uint32_t *)U"";
    }
    return NULL;
}

void lang_draw_tab_indicator(uint16_t keycode) {
    if (keycode < KC_LANG_CAT_BASE || keycode >= KC_LANG_PAGE_PREV) return;
    if ((uint8_t)(keycode - KC_LANG_CAT_BASE) != s_region) return;

    kdisp_fill_rect(BUFFER_X + 1, 1, SCREEN_WIDTH - 2, 1);
    kdisp_fill_rect(BUFFER_X + 2, 0, SCREEN_WIDTH - 4, 1);
    kdisp_fill_rect(BUFFER_X, 2, 3, SCREEN_HEIGHT - 2);
    kdisp_fill_rect(BUFFER_X + SCREEN_WIDTH - 2, 2, 3, SCREEN_HEIGHT - 2);
}

void lang_draw_tab_bottom(uint16_t keycode) {
    if (keycode < KC_LANG_CAT_BASE || keycode >= KC_LANG_PAGE_PREV) return;
    if ((uint8_t)(keycode - KC_LANG_CAT_BASE) == s_region) return;

    kdisp_fill_rect(BUFFER_X, SCREEN_HEIGHT - 3, SCREEN_WIDTH, 3);
}
