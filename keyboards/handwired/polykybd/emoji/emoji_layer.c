// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#include "emoji_layer.h"
#include "emoji_data.h"
#include "keycode_helper.h"
#include "base/update.h"
#include "base/disp_array.h"
#include "lang/named_glyphs.h"

#include "quantum/unicode/unicode.h"

// ── State ────────────────────────────────────────────────────────────────────

static uint8_t s_category = 0;
static uint8_t s_page     = 0;

// Hardwired tab icons from PR description; 0 = fall back to first codepoint of category.
static const uint32_t emj_tab_icons[] = {
    0x1F973,  // cat  0: 🥳 Smileys & Faces
    0x1F64F,  // cat  1: 🙏 Gestures & Body
    0x1F46A,  // cat  2: 👪 People & Jobs
    0x1F495,  // cat  3: 💕 Love & Celebrations
    0x1F404,  // cat  4: 🐄 Animals
    0x1F349,  // cat  5: 🍉 Nature, Plants & Food
    0x26C5,   // cat  6: ⛅ Weather & Sky
    0x26F5,   // cat  7: ⛵ Travel & Places
    0x1F3C0,  // cat  8: 🏀 Sports & Entertainment
    0x1F4CE,  // cat  9: 📎 Tools & Objects
    0x2714,   // cat 10: ✔ Symbols & Marks (checks, brackets, arrows…)
    0x0100,   // cat 11: Ā  Latin Extended-A & B (BMP — font index == codepoint)
};

// ── Helpers ──────────────────────────────────────────────────────────────────

// Returns the number of pages for a given category given the slot count per page.
static uint8_t page_count(uint8_t cat) {
    if (cat >= EMJ_NUM_CATEGORIES) return 1;
    uint16_t count = EMJ_CATEGORIES[cat].count;
    if (count == 0) return 1;
    return (uint8_t)((count + EMJ_SLOTS_PER_PAGE - 1) / EMJ_SLOTS_PER_PAGE);
}

// Returns the Unicode codepoint for slot `slot` in the current state, or 0 if out of range.
static uint32_t codepoint_for_slot(uint8_t slot) {
    if (s_category >= EMJ_NUM_CATEGORIES) return 0;
    const emj_category_t *cat = &EMJ_CATEGORIES[s_category];
    uint16_t idx = (uint16_t)s_page * EMJ_SLOTS_PER_PAGE + slot;
    if (idx >= cat->count) return 0;
    return cat->codepoints[idx];
}

// Maps a Unicode codepoint to the font-file index understood by kdisp_write_gfx_char /
// the UTF-16 display string.
//
// SMP emoji fonts in this codebase are generated with fontconvert -n0x10000, which stores
// glyph at (codepoint - 0x10000). BMP emoji fonts have no offset (index == codepoint).
static inline uint16_t cp_to_font_idx(uint32_t cp) {
    return (cp >= 0x10000) ? (uint16_t)(cp - 0x10000) : (uint16_t)cp;
}

// ── Display buffer ───────────────────────────────────────────────────────────
// Single static buffer — safe because to_static_text() is called single-threadedly
// in QMK's matrix scan loop. The caller uses the pointer before the next call.

static uint16_t s_disp_buf[4];  // space x 2 + glyph + NUL

static const uint16_t *make_emoji_str(uint32_t cp) {
    s_disp_buf[0] = ' ';
    s_disp_buf[1] = ' ';
    s_disp_buf[2] = cp_to_font_idx(cp);
    s_disp_buf[3] = 0;
    return s_disp_buf;
}

// ── Public API ───────────────────────────────────────────────────────────────

void emj_init(void) {
    s_category = 0;
    s_page     = 0;
}

const uint16_t *emj_display_text(uint16_t keycode) {
    // ── Page-prev arrow — only shown when a previous page exists ──
    if (keycode == KC_EMJ_PAGE_PREV) {
        return (s_page > 0) ? (const uint16_t *)(u"  " ICON_LEFT) : (const uint16_t *)u"";
    }

    // ── Page-next arrow — only shown when a next page exists ──
    if (keycode == KC_EMJ_PAGE_NEXT) {
        return (s_page + 1 < page_count(s_category))
               ? (const uint16_t *)(u"  " ICON_RIGHT)
               : (const uint16_t *)u"";
    }

    // ── Category tab — hardwired icon, falling back to first codepoint ──
    if (keycode >= KC_EMJ_CAT_BASE && keycode < KC_EMJ_PAGE_PREV) {
        uint8_t cat = (uint8_t)(keycode - KC_EMJ_CAT_BASE);
        if (cat >= EMJ_NUM_CATEGORIES || EMJ_CATEGORIES[cat].count == 0) return (const uint16_t *)u"";
        uint32_t cp = (cat < ARRAY_SIZE(emj_tab_icons)) ? emj_tab_icons[cat] : 0;
        if (cp == 0) cp = EMJ_CATEGORIES[cat].codepoints[0];
        return make_emoji_str(cp);
    }

    // ── Emoji slot — show current category/page emoji ──
    if (keycode >= KC_EMJ_SLOT_BASE && keycode < KC_EMJ_SLOT_BASE + EMJ_SLOTS_PER_PAGE) {
        uint8_t slot = (uint8_t)(keycode - KC_EMJ_SLOT_BASE);
        uint32_t cp  = codepoint_for_slot(slot);
        if (cp == 0) return (const uint16_t *)u"";
        return make_emoji_str(cp);
    }

    return NULL;
}

bool emj_process_keycode(uint16_t keycode, bool pressed) {
    if (!pressed) {
        // Only act on key-down; still consume key-up for handled keycodes.
        if (keycode == KC_EMJ_PAGE_PREV || keycode == KC_EMJ_PAGE_NEXT) return true;
        if (keycode >= KC_EMJ_CAT_BASE && keycode < KC_EMJ_PAGE_PREV) return true;
        if (keycode >= KC_EMJ_SLOT_BASE && keycode < KC_EMJ_SLOT_BASE + EMJ_SLOTS_PER_PAGE) return true;
        return false;
    }

    // Reset idle timer on any emoji keydown so display stays on while typing emojis.
    update_performed();

    // ── Category tab ──
    if (keycode >= KC_EMJ_CAT_BASE && keycode < KC_EMJ_PAGE_PREV) {
        uint8_t cat = (uint8_t)(keycode - KC_EMJ_CAT_BASE);
        if (cat < EMJ_NUM_CATEGORIES && cat != s_category) {
            s_category = cat;
            s_page     = 0;
            request_disp_refresh();
        }
        return true;
    }

    // ── Paging ──
    if (keycode == KC_EMJ_PAGE_PREV) {
        if (s_page > 0) {
            s_page--;
            request_disp_refresh();
        }
        return true;
    }
    if (keycode == KC_EMJ_PAGE_NEXT) {
        if (s_page + 1 < page_count(s_category)) {
            s_page++;
            request_disp_refresh();
        }
        return true;
    }

    // ── Emoji slot — send Unicode ──
    if (keycode >= KC_EMJ_SLOT_BASE && keycode < KC_EMJ_SLOT_BASE + EMJ_SLOTS_PER_PAGE) {
        uint8_t slot = (uint8_t)(keycode - KC_EMJ_SLOT_BASE);
        uint32_t cp  = codepoint_for_slot(slot);
        if (cp != 0) {
            register_unicode(cp);
        }
        return true;
    }

    return false;
}

void emj_apply_sync(uint8_t category, uint8_t page) {
    bool changed = (category != s_category || page != s_page);
    s_category = (category < EMJ_NUM_CATEGORIES) ? category : 0;
    s_page     = page;
    if (changed) {
        request_disp_refresh();
    }
}

uint8_t emj_active_category(void) { return s_category; }
uint8_t emj_active_page(void)     { return s_page; }

void emj_draw_tab_indicator(uint16_t keycode) {
    if (keycode < KC_EMJ_CAT_BASE || keycode >= KC_EMJ_PAGE_PREV) return;
    if ((uint8_t)(keycode - KC_EMJ_CAT_BASE) != s_category) return;

    //kdisp_fill_rect(BUFFER_X, 2, SCREEN_WIDTH, 1);
    kdisp_fill_rect(BUFFER_X + 1, 1, SCREEN_WIDTH - 2, 1);
    kdisp_fill_rect(BUFFER_X + 2, 0, SCREEN_WIDTH - 4, 1);
    kdisp_fill_rect(BUFFER_X, 2, 3, SCREEN_HEIGHT - 2);
    kdisp_fill_rect(BUFFER_X + SCREEN_WIDTH - 2, 2, 3, SCREEN_HEIGHT - 2);
}

void emj_draw_tab_bottom(uint16_t keycode) {
    if (keycode < KC_EMJ_CAT_BASE || keycode > KC_EMJ_PAGE_NEXT) return;
    if ((uint8_t)(keycode - KC_EMJ_CAT_BASE) == s_category) return;

    kdisp_fill_rect(BUFFER_X, SCREEN_HEIGHT-3, SCREEN_WIDTH, 3);
}
