// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#include "emoji_layer.h"
#include "emoji_data.h"
#include "keycode_helper.h"
#include "mru.h"
#include "base/update.h"
#include "base/disp_array.h"
#include "base/fontpack.h"
#include "lang/named_glyphs.h"
#include "lang/lang_lut.h"
#include "base/emoji_dict.h"
#include "layers.h"
#include "state.h"

#include "quantum/unicode/unicode.h"
#include <string.h>

// ── State ────────────────────────────────────────────────────────────────────

static uint8_t s_category = 0;
static uint8_t s_page     = 0;

// Hardwired tab icons from PR description; 0 = fall back to first codepoint of category.
static const uint32_t emj_tab_icons[] = {
    0x1F973,  // cat  0: 🥳 Smileys & Faces
    0x1F64F,  // cat  1: 🙏 Gestures, Body & Love
    0x1F46A,  // cat  2: 👪 People & Jobs
    0x00B1,   // cat  3: ±  Symbols & Math (resident _SupAndExtA_, so it shows without a pack)
    0x1F404,  // cat  4: 🐄 Animals
    0x1F349,  // cat  5: 🍉 Nature, Plants & Food
    0x26C5,   // cat  6: ⛅ Weather & Sky
    0x26F5,   // cat  7: ⛵ Travel & Places
    0x1F3C0,  // cat  8: 🏀 Sports & Entertainment
    0x1F4CE,  // cat  9: 📎 Tools & Objects
    0x2714,   // cat 10: ✔ Marks & Icons (checks, crosses, ornaments, dingbat arrows)
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

static uint32_t s_disp_buf[24];  // space x 2 + vertical nudges + up to 6 glyph codepoints + NUL

static const uint32_t *make_emoji_str(uint32_t cp) {
    s_disp_buf[0] = ' ';
    s_disp_buf[1] = ' ';
    s_disp_buf[2] = cp;
    s_disp_buf[3] = 0;
    return s_disp_buf;
}

// Rows to move a picker glyph down so it shares the Latin-1 line (see
// make_slot_str). These are the Symbols tab glyphs whose fonts have a smaller line
// height than _SupAndExtA_: ASCII $ (_Base_), Greek, U+2000-218F (_CurrencySigns_)
// and the script currency signs ֏ ৳ ௹ ฿ from the Armenian, Bengali, Tamil and
// Thai fonts. Every other glyph, emoji included, stays where the draw puts it.
static int16_t slot_lift(uint32_t cp) {
    if (cp == 0x0024 || cp == 0x058F || cp == 0x09F3 || cp == 0x0BF9 || cp == 0x0E3F) return 8;
    if (cp >= 0x0370 && cp <= 0x03FF) return 8;
    if (cp >= 0x2000 && cp <= 0x218F) return 8;
    return 0;
}

// Drawn in place of a glyph no loaded font covers: a pack glyph (every emoji, the
// Symbols tab's math and arrows) on a board with no font pack flashed. The draw's
// own fallback is a lone '!', which reads as a fault, and a blank key looks like an
// empty slot. Half-scale base font, which is always resident. Placed from the
// rendered ink: rows 5..34, "Glyph" x 17..54, "missing!" x 9..63 of the 72x40 panel.
// The nudges are separate literals because \x06 followed by a hex digit would be
// read as one escape.
static const uint32_t s_missing_glyph[] =
    HINT_SMALL UP_2PX UP_4PX U"\x06\x06\x06\x06\x06\x06\x06\x06" U"Glyph"
    U"\r\v" U"\x06\x06\x06\x06" U"missing!";

// Superscripts and subscripts (¹ ² ³ and U+2070-209F). Alone on a key they differ
// only in height, so ⁶ and ₆ read the same; make_slot_str() draws a full-size x in
// front of them (x⁶, x₆) to show where they sit.
static bool slot_is_sup_or_sub(uint32_t cp) {
    return cp == 0x00B9 || cp == 0x00B2 || cp == 0x00B3 || (cp >= 0x2070 && cp <= 0x209F);
}

// Rows of a glyph box that fall outside the 40-row panel.
static int16_t box_overshoot(int16_t top, int16_t bottom) {
    return (int16_t)((top < 0 ? -top : 0) + (bottom > SCREEN_HEIGHT - 1 ? bottom - (SCREEN_HEIGHT - 1) : 0));
}

// The legend for an emoji slot or a recent: the glyph moved up or down so it stays
// on the 40-row panel. The draw puts the baseline at row 23, which suits a
// full-height emoji but puts a superscript (⁴) above the top edge and pushes a tall
// operator (∑) past the bottom one. A clamp and not a centre, so ⁴ still sits high
// and ₄ low, and „ low and “ high.
//
// ⚠️ The clamp measures the glyph's BOX (kdisp_gfx_text_bbox), not its ink, and a
// dithered emoji often has empty rows at its edges: 👍's box ends on row 40 while
// its ink ends on 39. It moves in whole 2 px steps (\x05 down, \f up), so pushing
// that box up 2 rows cut 👍's top ink row. Hence two rules, checked over all 1,768
// picker glyphs against the unmoved draw (none loses ink, 23 lose less):
// - a box taller than the panel (√ ∛ ∫, many emoji) keeps the default baseline;
// - of the two 2 px positions around the exact clamp, take the one with fewer box
//   rows off the panel, and do not move at all unless that is fewer than unmoved.
static const uint32_t *make_slot_str(uint32_t cp) {
    if (kdisp_gfx_glyph(g_all_fonts, g_all_font_count, cp) == NULL) return s_missing_glyph;
    // What is drawn: the glyph alone, or for a superscript or subscript, a base-font x
    // and then the glyph. ¹ ² ³ come from _SupAndExtA_ (line height 44), so they are
    // lifted 8 rows (4 \f) to sit as high as ⁴ beside the same x. The pair is then
    // clamped as one box, with no slot_lift(): the x already fixes the height.
    uint32_t glyph[7];
    uint8_t  g    = 0;
    int16_t  lift = 0;
    if (slot_is_sup_or_sub(cp)) {
        glyph[g++] = U'x';
        if (cp < 0x2070) {
            for (uint8_t k = 0; k < 4; ++k) glyph[g++] = U'\f';
        }
    } else {
        lift = slot_lift(cp);
    }
    glyph[g++] = cp;
    glyph[g]   = 0;
    int8_t xmin, xmax, ymin, ymax;
    kdisp_gfx_text_bbox(g_all_fonts, g_all_font_count, glyph, &xmin, &xmax, &ymin, &ymax);
    // Latin-1 (¹ ½ £ ®) is drawn from _SupAndExtA_, whose line height is 44. Greek
    // and U+2000-218F (punctuation, super/subscripts, currency, letterlike,
    // fractions: ⁴ ⅓ € ™) come from _Greek_ and _CurrencySigns_ at 37. The draw
    // shifts each glyph by its font's line height, so the second set came out 7
    // rows higher, and ¹ sat below ⁴ and £ below €. Moving the 37 set down 8 rows
    // (whole 2 px steps) puts the tab on one line, within 1 row. Done here and not
    // in the fonts: a line height of 44 there would also move the € ₽ — … and Greek
    // legends that the language layouts have tuned. slot_lift() lists the set.
    const int16_t top    = 23 + ymin;           // box rows at the default baseline
    const int16_t bottom = 23 + ymax;
    int16_t       move   = lift;                // rows to move down, always even
    if (bottom - top + 1 <= SCREEN_HEIGHT) {
        const int16_t t    = top + lift;
        const int16_t b    = bottom + lift;
        const int16_t need = (t < 0) ? (int16_t)-t : (b > SCREEN_HEIGHT - 1) ? (int16_t)((SCREEN_HEIGHT - 1) - b) : 0;
        if (need != 0) {
            const int16_t d  = lift + need;
            const int16_t lo = (d >= 0) ? (int16_t)((d / 2) * 2) : (int16_t)-(((1 - d) / 2) * 2);
            const int16_t hi = (int16_t)(lo + ((d & 1) ? 2 : 0));
            const int16_t ol = box_overshoot(top + lo, bottom + lo);
            const int16_t oh = box_overshoot(top + hi, bottom + hi);
            const int16_t dl = (int16_t)(lo > lift ? lo - lift : lift - lo);
            const int16_t dh = (int16_t)(hi > lift ? hi - lift : lift - hi);
            const int16_t best = (ol < oh || (ol == oh && dl <= dh)) ? lo : hi;
            if (box_overshoot(top + best, bottom + best) < box_overshoot(t, b)) move = best;
        }
    }
    // Two spaces put every symbol at the same column. A symbol too wide for that
    // (the long arrows ⟷ ⟺, 62 px of the 72) is centred with 2 px \x06 steps instead.
    const GFXglyph *space = kdisp_gfx_glyph(g_all_fonts, g_all_font_count, U' ');
    const int16_t   lead  = space ? (int16_t)(2 * pgm_read_byte(&space->xAdvance)) : 14;
    const bool      wide  = lead + xmax > SCREEN_WIDTH - 1;
    uint8_t         nudge = 0;
    if (wide) {
        const int16_t off = (int16_t)((SCREEN_WIDTH - (xmax - xmin + 1)) / 2 - xmin);
        nudge             = (uint8_t)(off > 0 ? off / 2 : 0);
    }
    int8_t  steps = (int8_t)(move / 2);
    uint8_t n     = 0;
    if (wide) {
        for (; nudge > 0 && n < 8; --nudge) s_disp_buf[n++] = U'\x06';
    } else {
        s_disp_buf[n++] = ' ';
        s_disp_buf[n++] = ' ';
    }
    const uint8_t room = (uint8_t)(ARRAY_SIZE(s_disp_buf) - 1 - g);
    for (; steps > 0 && n < room; --steps) s_disp_buf[n++] = U'\x05';
    for (; steps < 0 && n < room; ++steps) s_disp_buf[n++] = U'\f';
    for (uint8_t k = 0; k < g; ++k) s_disp_buf[n++] = glyph[k];
    s_disp_buf[n] = 0;
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

// ── Search mode ──────────────────────────────────────────────────────────────
// State is master-authoritative and synced (emj_search_export/apply); the slave
// runs the same search on its own dictionary copy. Results are computed lazily,
// on the first draw or press after the query or page changed.

static bool     s_search;                        // search mode chosen
static uint16_t s_query[EMJDICT_QUERY_MAX];      // typed codepoints (BMP), spaces included
static uint8_t  s_qlen;
static uint8_t  s_rpage;                         // result page
static uint32_t s_res[EMJ_SEARCH_PAGE];          // results on that page
static uint16_t s_rtotal;
static bool     s_res_dirty = true;

static const uint8_t s_result_of_cat[] = EMJ_SEARCH_RESULT_OF_CAT;

// Search-mode roles. The page keys (top left, top right) page the results.
// Backspace and Space are two emoji slots with no letter on any base layout
// (EMJ_SEARCH_BKSP_SLOT, EMJ_SEARCH_SPACE_SLOT): on split72 the key above the
// right innermost thumb and the one beside it. The recents keep all 12 keys.
enum { ROLE_NONE = 0, ROLE_SPACE, ROLE_BKSP, ROLE_PREV, ROLE_NEXT };

// The letter each emoji slot types in search mode: the base layer's key at the
// slot's matrix position, in the active language. 0 = no letter (blank, inert).
static uint32_t s_slot_letter[EMJ_SLOTS_PER_PAGE];
// Rebuilt lazily from the main loop: the slave's sync handler runs on the split
// protocol thread and only marks it stale.
static bool     s_letters_dirty = true;

_Static_assert(sizeof(((poly_sync_t *)0)->emj_query) == EMJDICT_QUERY_MAX * sizeof(uint16_t),
               "poly_sync_t.emj_query must hold EMJDICT_QUERY_MAX codepoints");

bool emj_search_active(void) { return s_search && emjdict_present(); }

static void search_changed(void) {
    s_res_dirty = true;
    request_disp_refresh();
}

static void build_slot_letters(void) {
    if (!s_letters_dirty) return;
    s_letters_dirty = false;
    memset(s_slot_letter, 0, sizeof(s_slot_letter));
    const uint8_t base = (uint8_t)get_local_layer()->def_layer;
    const uint8_t lang = get_local_state()->lang;
    for (uint8_t r = 0; r < MATRIX_ROWS; ++r) {
        for (uint8_t c = 0; c < MATRIX_COLS; ++c) {
            keypos_t pos = {.row = r, .col = c};
            uint16_t kc  = keymap_key_to_keycode(_EMJ, pos);
            if (kc < KC_EMJ_SLOT_BASE || kc >= KC_EMJ_SLOT_BASE + EMJ_SLOTS_PER_PAGE) continue;
            uint16_t        bkc = poly_mt_tap(keymap_key_to_keycode(base, pos));
            const uint32_t *t   = translate_keycode(lang, bkc, false, false);
            if (!t) continue;
            while (*t && *t < 0x20u) ++t;   // skip the legend's position nudges
            if (*t && *t <= 0xFFFFu && emjdict_maps_letter(*t)) s_slot_letter[kc - KC_EMJ_SLOT_BASE] = *t;
        }
    }
}

// Role of an emoji-layer key in search mode (ROLE_NONE for letters, results and
// recents).
static uint8_t search_role(uint16_t kc) {
    if (kc == KC_EMJ_PAGE_PREV) return ROLE_PREV;
    if (kc == KC_EMJ_PAGE_NEXT) return ROLE_NEXT;
    if (kc == KC_EMJ_SLOT_BASE + EMJ_SEARCH_BKSP_SLOT) return ROLE_BKSP;
    if (kc == KC_EMJ_SLOT_BASE + EMJ_SEARCH_SPACE_SLOT) return ROLE_SPACE;
    return ROLE_NONE;
}

static void query_clear(void) {
    if (s_qlen == 0 && s_rpage == 0) return;
    s_qlen  = 0;
    s_rpage = 0;
    search_changed();
}

static void refresh_results(void) {
    if (!s_res_dirty) return;
    s_res_dirty = false;
    memset(s_res, 0, sizeof(s_res));
    s_rtotal = 0;
    if (s_qlen == 0) return;
    uint32_t q[EMJDICT_QUERY_MAX];
    for (uint8_t i = 0; i < s_qlen; ++i) q[i] = s_query[i];
    s_rtotal = emjdict_search(q, s_qlen, (uint16_t)s_rpage * EMJ_SEARCH_PAGE, s_res, EMJ_SEARCH_PAGE, NULL);
    // A page past the end (the result count shrank): back to the first page.
    if (s_rtotal > 0 && (uint16_t)s_rpage * EMJ_SEARCH_PAGE >= s_rtotal) {
        s_rpage = 0;
        s_rtotal = emjdict_search(q, s_qlen, 0, s_res, EMJ_SEARCH_PAGE, NULL);
    }
}

static uint8_t result_pages(void) {
    refresh_results();
    uint16_t p = (uint16_t)((s_rtotal + EMJ_SEARCH_PAGE - 1) / EMJ_SEARCH_PAGE);
    return (uint8_t)(p == 0 ? 1 : (p > 255 ? 255 : p));
}

uint16_t emj_search_total(void) {
    refresh_results();
    return s_rtotal;
}

uint8_t emj_search_query(uint32_t *out, uint8_t max) {
    uint8_t n = s_qlen < max ? s_qlen : max;
    for (uint8_t i = 0; i < n; ++i) out[i] = s_query[i];
    return n;
}

static void query_append(uint32_t cp) {
    if (s_qlen >= EMJDICT_QUERY_MAX) return;
    if (cp == U' ' && (s_qlen == 0 || s_query[s_qlen - 1] == U' ')) return;
    s_query[s_qlen++] = (uint16_t)cp;
    s_rpage           = 0;
    search_changed();
}

static uint16_t emj_find_code(uint32_t cp);   // below, with the MRU preset

static void send_emoji(uint32_t cp) {
    if (cp == 0) return;
    register_unicode(cp);
    uint16_t code = emj_find_code(cp);
    if (code != MRU_EMOJI_EMPTY) mru_emoji_push(code);
    request_disp_refresh();
}

static bool is_cat_key(uint16_t kc) { return kc >= KC_EMJ_CAT_BASE && kc < KC_EMJ_PAGE_PREV; }
static bool is_mru_key(uint16_t kc) { return kc >= KC_EMJ_MRU_BASE && kc < KC_EMJ_MRU_BASE + MRU_CAP; }
static bool is_slot_key(uint16_t kc) { return kc >= KC_EMJ_SLOT_BASE && kc < KC_EMJ_SLOT_BASE + EMJ_SLOTS_PER_PAGE; }

static uint32_t result_for_cat(uint8_t cat) {
    if (cat >= sizeof(s_result_of_cat)) return 0;
    refresh_results();
    return s_res[s_result_of_cat[cat] % EMJ_SEARCH_PAGE];
}

static const uint32_t *search_display_text(uint16_t keycode) {
    if (keycode == KC_EMJ_S_CLEAR) return U"Clear";
    switch (search_role(keycode)) {
        case ROLE_SPACE: return ICON_SPACE;
        case ROLE_BKSP:  return TECHNICAL_ERASELEFT;
        case ROLE_PREV:  return kdisp_page_key_legend(ICON_LEFT[0], s_rpage, result_pages());
        case ROLE_NEXT:  return kdisp_page_key_legend(ICON_RIGHT[0], s_rpage, result_pages());
        default:         break;
    }
    if (is_cat_key(keycode)) {
        uint32_t cp = result_for_cat((uint8_t)(keycode - KC_EMJ_CAT_BASE));
        return cp ? make_slot_str(cp) : (const uint32_t *)U"";
    }
    if (is_mru_key(keycode)) {
        uint32_t cp = emj_decode(mru_emoji_get((uint8_t)(keycode - KC_EMJ_MRU_BASE)));
        return cp ? make_slot_str(cp) : (const uint32_t *)U"";
    }
    if (is_slot_key(keycode)) {
        build_slot_letters();
        uint32_t cp = s_slot_letter[keycode - KC_EMJ_SLOT_BASE];
        if (cp == 0) return (const uint32_t *)U"";
        s_disp_buf[0] = ' ';
        s_disp_buf[1] = ' ';
        s_disp_buf[2] = cp;
        s_disp_buf[3] = 0;
        return s_disp_buf;
    }
    return NULL;
}

static bool search_process(uint16_t keycode) {
    if (keycode == KC_EMJ_S_CLEAR) {
        query_clear();
        return true;
    }
    switch (search_role(keycode)) {
        case ROLE_SPACE:
            query_append(U' ');
            return true;
        case ROLE_BKSP:
            if (s_qlen > 0) {
                --s_qlen;
                s_rpage = 0;
                search_changed();
            }
            return true;
        case ROLE_PREV: {
            uint8_t pages = result_pages();
            s_rpage       = (uint8_t)((s_rpage + pages - 1) % pages);
            search_changed();
            return true;
        }
        case ROLE_NEXT: {
            uint8_t pages = result_pages();
            s_rpage       = (uint8_t)((s_rpage + 1) % pages);
            search_changed();
            return true;
        }
        default:
            break;
    }
    if (is_cat_key(keycode)) {
        send_emoji(result_for_cat((uint8_t)(keycode - KC_EMJ_CAT_BASE)));
        return true;
    }
    if (is_mru_key(keycode)) {
        uint16_t code = mru_emoji_get((uint8_t)(keycode - KC_EMJ_MRU_BASE));
        uint32_t cp   = emj_decode(code);
        if (cp != 0) {
            register_unicode(cp);
            mru_emoji_push(code);
            request_disp_refresh();
        }
        return true;
    }
    if (is_slot_key(keycode)) {
        build_slot_letters();
        uint32_t cp = s_slot_letter[keycode - KC_EMJ_SLOT_BASE];
        if (cp != 0) query_append(cp);
        return true;
    }
    return false;
}

void emj_search_on_layer_exit(void) {
    s_letters_dirty = true;   // the base layout or language may change before the next visit
    query_clear();
}

void emj_search_export(uint8_t *flags, uint8_t *page, uint8_t *qlen, uint16_t *query) {
    *flags = s_search ? 1u : 0u;
    *page  = s_rpage;
    *qlen  = s_qlen;
    memset(query, 0, EMJDICT_QUERY_MAX * sizeof(uint16_t));
    memcpy(query, s_query, (size_t)s_qlen * sizeof(uint16_t));
}

void emj_search_apply(uint8_t flags, uint8_t page, uint8_t qlen, const uint16_t *query) {
    if (qlen > EMJDICT_QUERY_MAX) qlen = EMJDICT_QUERY_MAX;
    bool mode = (flags & 1u) != 0;
    bool same = mode == s_search && page == s_rpage && qlen == s_qlen &&
                memcmp(query, s_query, (size_t)qlen * sizeof(uint16_t)) == 0;
    if (same) return;
    if (mode && !s_search) s_letters_dirty = true;
    s_search = mode;
    s_rpage  = page;
    s_qlen   = qlen;
    memcpy(s_query, query, (size_t)qlen * sizeof(uint16_t));
    search_changed();
}

// ── Public API ───────────────────────────────────────────────────────────────

uint8_t emj_page_count(uint8_t cat) {
    if (cat >= EMJ_NUM_CATEGORIES || EMJ_CATEGORIES[cat].count == 0) return 0;
    return page_count(cat);
}

void emj_init(void) {
    s_category = 0;
    s_page     = 0;
}

const uint32_t *emj_display_text(uint16_t keycode) {
    // ── Mode key: "Find" over an on/off switch, like the other toggle keys ──
    if (keycode == KC_EMJ_MODE) {
        if (!emjdict_present()) return (const uint32_t *)U"";
        return s_search ? U"Find\r\v" ICON_SWITCH_ON : U"Find\r\v" ICON_SWITCH_OFF;
    }
    if (emj_search_active()) {
        const uint32_t *t = search_display_text(keycode);
        if (t) return t;
    } else if (keycode == KC_EMJ_S_CLEAR) {
        return (const uint32_t *)U"";   // search-only key: blank in tab mode
    }
    // ── Page arrows with "page/pages" — always live because paging wraps ──
    if (keycode == KC_EMJ_PAGE_PREV) {
        return kdisp_page_key_legend(ICON_LEFT[0], s_page, page_count(s_category));
    }
    if (keycode == KC_EMJ_PAGE_NEXT) {
        return kdisp_page_key_legend(ICON_RIGHT[0], s_page, page_count(s_category));
    }

    // ── MRU recents row — most-recently-used emoji, or blank when empty ──
    if (keycode >= KC_EMJ_MRU_BASE && keycode < KC_EMJ_MRU_BASE + MRU_CAP) {
        uint32_t cp = emj_decode(mru_emoji_get((uint8_t)(keycode - KC_EMJ_MRU_BASE)));
        return (cp == 0) ? (const uint32_t *)U"" : make_slot_str(cp);
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
        return make_slot_str(cp);
    }

    return NULL;
}

bool emj_process_keycode(uint16_t keycode, bool pressed) {
    if (keycode == KC_EMJ_MODE || keycode == KC_EMJ_S_CLEAR) {
        if (!pressed) return true;
        update_performed();
        if (keycode == KC_EMJ_MODE) {
            if (emjdict_present()) {
                s_search = !s_search;
                s_letters_dirty = true;   // the base layer or language may have changed
                search_changed();
            }
            return true;
        }
        if (emj_search_active()) search_process(keycode);
        return true;
    }
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

    if (emj_search_active() && search_process(keycode)) return true;

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
    if (emj_search_active()) return;   // the tabs show results, none is "active"
    if (keycode < KC_EMJ_CAT_BASE || keycode >= KC_EMJ_PAGE_PREV) return;
    if ((uint8_t)(keycode - KC_EMJ_CAT_BASE) != s_category) return;
    kdisp_draw_tab_frame();
}

void emj_draw_tab_bottom(uint16_t keycode) {
    if (emj_search_active()) return;
    if (keycode < KC_EMJ_CAT_BASE || keycode > KC_EMJ_PAGE_NEXT) return;
    if ((uint8_t)(keycode - KC_EMJ_CAT_BASE) == s_category) return;
    kdisp_draw_tab_underline();
}
