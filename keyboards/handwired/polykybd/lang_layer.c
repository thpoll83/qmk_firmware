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
//               4 Asia · 5 Oceania
// Within a region: alphabetical by country code; same-country entries keep
// enum (= GET_LANG_LIST) order — matching the host's stable sort.

static const uint8_t REGION_OFFSET[NUM_LANG_REGIONS + 1] = {
    0, 21, 59, 78, 101, 129, 143,
};

static const uint8_t REGION_LANGS[NUM_LANG] = {
    // Americas (21): es-AR es-BO pt-BR fr-CA en-CA es-CL es-CO es-CR es-DO es-EC
    //                es-GT es-HN es-MX es-NI es-PA es-PE es-PY es-SV en-US es-UY es-VE
    78, 88, 32, 45, 77, 84, 81, 90, 87, 85, 86, 92, 42, 95, 93, 82,
    89, 91,  0, 94, 83,
    // Europe (38): de-AT bs-BA fr-BE nl-BE bg-BG be-BY de-CH fr-CH cs-CZ de-DE
    //              da-DK et-EE es-ES ca-ES fi-FI fo-FO fr-FR en-GB el-GR hr-HR
    //              hu-HU en-IE is-IS it-IT lt-LT lv-LV mk-MK nn-NO nl-NL pl-PL
    //              pt-PT ro-RO sr-RS ru-RU se-SV? sl-SI sk-SK uk-UA
    96, 100, 44, 97, 15, 13, 43, 101, 26,  1, 24, 31,  3, 98, 22, 103,
     2, 41, 10, 27, 25, 99, 55,  5, 29, 30, 34, 19, 23, 16,  4, 17,
    33, 12, 21, 102, 28, 11,
    // Middle East & Caucasus (19): ar-AE hy-AM az-AZ ar-BH ka-GE he-IL ar-IQ ku-IQ
    //                              fa-IR ar-JO ar-KW ar-LB ar-OM ar-PS ar-QA ar-SA
    //                              ar-SY tr-TR ar-YE
    104, 52, 54, 113, 51, 20, 73, 74, 35, 106, 109, 107, 110, 111, 112,  9,
    105,  6, 108,
    // Africa (23): pt-AO fr-CD fr-CI fr-CM ar-DZ ar-EG am-ET en-GH sw-KE ar-LY
    //              ar-MA fr-MG pt-MZ yo-NG en-NG ar-SD fr-SN ar-TN sw-TZ en-UG
    //              en-ZA af-ZA en-ZM
    127, 118, 119, 120, 114, 67, 69, 123, 68, 117, 72, 122, 128, 70, 71, 115,
    121, 116, 126, 124, 65, 66, 125,
    // Asia (28): bn-BD zh-CN zh-HK id-ID hi-IN mr-IN bn-IN te-IN ta-IN en-IN
    //            ja-JP ky-KG kk-KZ mn-MN en-LK ms-MY ne-NP tl-PH en-PH ur-PK
    //            en-PK ko-KR en-SG tg-TJ th-TH zh-TW uz-UZ vi-VN
    129, 18, 57, 53, 36, 37, 47, 48, 49, 130,  8, 135,  7, 14, 134, 39,
    75, 38, 63, 132, 40, 131, 133, 46, 136, 50, 76, 56,
    // Oceania (14): en-AU fj-FJ en-FM en-GU fr-NC en-NZ mi-NZ ty-PF en-PG en-SB
    //               to-TO hw-US en-VU sm-WS
    // (hw-US: geographic override — Hawaiian is Polynesian, despite the US
    //  country code; mirrored by LANG_REGION_OVERRIDE on the host.)
    58, 62, 140, 137, 141, 59, 60, 80, 79, 138, 142, 64, 139, 61,
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
