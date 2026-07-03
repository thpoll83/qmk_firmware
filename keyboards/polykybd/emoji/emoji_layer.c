// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#include "emoji_layer.h"
#include "emoji_data.h"
#include "keycode_helper.h"
#include "mru.h"
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

// ── Display buffer ───────────────────────────────────────────────────────────
// Single static buffer — safe because to_static_text() is called single-threadedly
// in QMK's matrix scan loop. The caller uses the pointer before the next call.
//
// The display string now carries real 32-bit Unicode codepoints: every glyph font
// (SMP included) is keyed on its real codepoint, so the emoji codepoint goes into
// the string verbatim — no Private-Use-Area shift.

static uint32_t s_disp_buf[4];  // space x 2 + glyph + NUL

static const uint32_t *make_emoji_str(uint32_t cp) {
    s_disp_buf[0] = ' ';
    s_disp_buf[1] = ' ';
    s_disp_buf[2] = cp;
    s_disp_buf[3] = 0;
    return s_disp_buf;
}

// ── MRU packing ───────────────────────────────────────────────────────────────
// The MRU stores a 16-bit code: 4 bits category | 10 bits glyph offset
// (MRU_EMOJI_BITS = 14), instead of the full 32-bit codepoint. Decode resolves
// it back through the static
// category table (always valid within a firmware build; EEPROM is reset on
// update). MRU_EMOJI_EMPTY (0xFFFF) decodes to 0 (blank slot).

static uint16_t emj_pack(uint8_t cat, uint16_t off) {
    return (uint16_t)(((uint16_t)cat << 10) | (off & 0x03FF));
}

// emj_pack encodes the category in bits [13:10] (4 bits) and the offset in
// [9:0]. MRU_EMOJI_EMPTY (0x3FFF) is reserved as the "no recent" sentinel — it
// equals (category 15 | offset 0x3FF). With at most 15 categories (indices
// 0..14) the largest real code is 0x3BFF, so no real code can collide with the
// sentinel. Adding a 16th category would break that, so fail the build instead.
_Static_assert(EMJ_NUM_CATEGORIES <= 15,
               "emoji category 15 would encode as MRU_EMOJI_EMPTY (0x3FFF) — the reserved empty sentinel");

static uint32_t emj_decode(uint16_t code) {
    if (code == MRU_EMOJI_EMPTY) return 0;
    uint8_t  cat = (uint8_t)(code >> 10);
    uint16_t off = (uint16_t)(code & 0x03FF);
    if (cat >= EMJ_NUM_CATEGORIES) return 0;
    if (off >= EMJ_CATEGORIES[cat].count) return 0;
    return EMJ_CATEGORIES[cat].codepoints[off];
}

// Built-in default recents loaded by the "Preset" key — common, widely-supported
// emoji. Resolved to packed codes by scanning the category table (so the preset
// stays a plain codepoint list, independent of table offsets).
static const uint32_t emj_preset_cps[MRU_CAP] = {
    0x1F600, 0x1F602, 0x1F60D, 0x1F44D, 0x2764,  0x1F389,
    0x1F64F, 0x1F525, 0x2705,  0x1F622, 0x1F914, 0x1F44B,
};

static uint16_t emj_find_code(uint32_t cp) {
    for (uint8_t cat = 0; cat < EMJ_NUM_CATEGORIES; ++cat) {
        const emj_category_t *c = &EMJ_CATEGORIES[cat];
        for (uint16_t off = 0; off < c->count; ++off) {
            if (c->codepoints[off] == cp) return emj_pack(cat, off);
        }
    }
    return MRU_EMOJI_EMPTY;   // not in any category — leave the slot blank
}

static void emj_mru_preset(void) {
    uint16_t codes[MRU_CAP];
    for (uint8_t i = 0; i < MRU_CAP; ++i) {
        codes[i] = emj_find_code(emj_preset_cps[i]);
    }
    mru_emoji_set_all(codes);
}

// ── Public API ───────────────────────────────────────────────────────────────

void emj_init(void) {
    s_category = 0;
    s_page     = 0;
}

const uint32_t *emj_display_text(uint16_t keycode) {
    // ── Page arrows — always live because paging wraps in both directions ──
    if (keycode == KC_EMJ_PAGE_PREV) {
        return (page_count(s_category) > 1) ? (const uint32_t *)(U"  " ICON_LEFT) : (const uint32_t *)U"";
    }
    if (keycode == KC_EMJ_PAGE_NEXT) {
        return (page_count(s_category) > 1) ? (const uint32_t *)(U"  " ICON_RIGHT) : (const uint32_t *)U"";
    }

    // ── MRU recents row — most-recently-used emoji, or blank when empty ──
    if (keycode >= KC_EMJ_MRU_BASE && keycode < KC_EMJ_MRU_BASE + MRU_CAP) {
        uint32_t cp = emj_decode(mru_emoji_get((uint8_t)(keycode - KC_EMJ_MRU_BASE)));
        return (cp == 0) ? (const uint32_t *)U"" : make_emoji_str(cp);
    }

    // ── Category tab — hardwired icon, falling back to first codepoint ──
    if (keycode >= KC_EMJ_CAT_BASE && keycode < KC_EMJ_PAGE_PREV) {
        uint8_t cat = (uint8_t)(keycode - KC_EMJ_CAT_BASE);
        if (cat >= EMJ_NUM_CATEGORIES || EMJ_CATEGORIES[cat].count == 0) return (const uint32_t *)U"";
        uint32_t cp = (cat < ARRAY_SIZE(emj_tab_icons)) ? emj_tab_icons[cat] : 0;
        if (cp == 0) cp = EMJ_CATEGORIES[cat].codepoints[0];
        return make_emoji_str(cp);
    }

    // ── Emoji slot — show current category/page emoji ──
    if (keycode >= KC_EMJ_SLOT_BASE && keycode < KC_EMJ_SLOT_BASE + EMJ_SLOTS_PER_PAGE) {
        uint8_t slot = (uint8_t)(keycode - KC_EMJ_SLOT_BASE);
        uint32_t cp  = codepoint_for_slot(slot);
        if (cp == 0) return (const uint32_t *)U"";
        return make_emoji_str(cp);
    }

    return NULL;
}

bool emj_process_keycode(uint16_t keycode, bool pressed) {
    if (!pressed) {
        // Only act on key-down; still consume key-up for handled keycodes.
        if (keycode == KC_EMJ_PAGE_PREV || keycode == KC_EMJ_PAGE_NEXT) return true;
        if (keycode == KC_EMJ_PRESET || keycode == KC_EMJ_CLEAR) return true;
        if (keycode >= KC_EMJ_CAT_BASE && keycode < KC_EMJ_PAGE_PREV) return true;
        if (keycode >= KC_EMJ_MRU_BASE && keycode < KC_EMJ_MRU_BASE + MRU_CAP) return true;
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

    // ── Paging (wraps in both directions) ──
    if (keycode == KC_EMJ_PAGE_PREV) {
        uint8_t pages = page_count(s_category);
        s_page = (uint8_t)((s_page + pages - 1) % pages);
        request_disp_refresh();
        return true;
    }
    if (keycode == KC_EMJ_PAGE_NEXT) {
        uint8_t pages = page_count(s_category);
        s_page = (uint8_t)((s_page + 1) % pages);
        request_disp_refresh();
        return true;
    }

    // ── MRU controls ── (refresh so the recents row redraws immediately)
    if (keycode == KC_EMJ_PRESET) { emj_mru_preset();  request_disp_refresh(); return true; }
    if (keycode == KC_EMJ_CLEAR)  { mru_emoji_clear(); request_disp_refresh(); return true; }

    // ── MRU recents slot — re-send and bump to front ──
    if (keycode >= KC_EMJ_MRU_BASE && keycode < KC_EMJ_MRU_BASE + MRU_CAP) {
        uint16_t code = mru_emoji_get((uint8_t)(keycode - KC_EMJ_MRU_BASE));
        uint32_t cp   = emj_decode(code);
        if (cp != 0) {
            register_unicode(cp);
            mru_emoji_push(code);
            request_disp_refresh();   // recents reordered — redraw the row
        }
        return true;
    }

    // ── Emoji slot — send Unicode and remember it ──
    if (keycode >= KC_EMJ_SLOT_BASE && keycode < KC_EMJ_SLOT_BASE + EMJ_SLOTS_PER_PAGE) {
        uint8_t  slot = (uint8_t)(keycode - KC_EMJ_SLOT_BASE);
        uint16_t off  = (uint16_t)((uint16_t)s_page * EMJ_SLOTS_PER_PAGE + slot);
        uint32_t cp   = codepoint_for_slot(slot);
        if (cp != 0) {
            register_unicode(cp);
            mru_emoji_push(emj_pack(s_category, off));   // off valid when cp != 0
            request_disp_refresh();   // new entry pushed to recents — redraw the row
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
    kdisp_draw_tab_frame();
}

void emj_draw_tab_bottom(uint16_t keycode) {
    if (keycode < KC_EMJ_CAT_BASE || keycode > KC_EMJ_PAGE_NEXT) return;
    if ((uint8_t)(keycode - KC_EMJ_CAT_BASE) == s_category) return;
    kdisp_draw_tab_underline();
}
