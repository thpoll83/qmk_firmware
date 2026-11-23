// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Keycap render helpers that update_displays() and kdisp_idle() in poly_keymap.c call:
// the legend-size planner binding, the overlay blit and its on-screen slot set, the
// language flag key, the FW-2 confirm key, the MRU bar, the keycode resolvers (with the
// F-row alignment on split72), the idle legend relocation and the centred legend draw.
// Moved out of keys_com/poly_keymap.c; the code and the call order are unchanged.

#include QMK_KEYBOARD_H

#include <string.h>

#include "keys_com/poly_keymap.h"           // the exports defined here
#include "keys_com/poly_keymap_internal.h"  // shared helpers, legend_plan.h, state.h
#include "keys_com/keycode_helper.h"        // KC_IME, KC_*_MRU_BASE, poly_ime_family()
#include "keys_com/layers.h"                // _FL, _UL
#include "keys_com/mru.h"                   // MRU_CAP
#include "lib/split_link/side.h"            // is_left_side()
#include "lib/drivers/disp_array.h"         // kdisp_*, BUFFER_X, SCREEN_WIDTH
#include "lib/display/fill_overlay.h"       // adjust_overlay_idx_to_mod()
#include "base/overlay.h"                   // display_has_overlay(), get_overlay()
#include "base/fontpack.h"                  // g_all_fonts, fontpack_bundle_version()
#include "base/ime_key_plan.h"              // ime_key_stand_in()
#include "features/hints/os_hints.h"        // os_hint_for_keycode()
#include "lang/lang_lut.h"                  // translate_keycode(), LANG_*

// The resident label fonts, defined by the font headers that poly_keymap.c includes.
extern const GFXfont NotoSans_Regular_Nano_10px7b;
extern const GFXfont NotoSans_Regular_Mid_19px7b;

// ── Keycap legend SIZE (enum poly_glyph_size, HID cmd 34) ────────────────────
//
// The relocation + placement arithmetic is the PURE planner in base/legend_plan.c
// (unit-tested: make test:polykybd_legend_plan); the wrappers below bind it to the
// assembled font set and this variant's visible window, keeping the signatures the
// render path has always called.
_Static_assert(LEGEND_PLAN_SIZE_COUNT == GLYPH_SIZE_COUNT,
               "legend_plan's size table no longer covers poly_glyph_size");
_Static_assert(LEGEND_PLAN_SIZE_S == GLYPH_SIZE_S && LEGEND_PLAN_SIZE_M == GLYPH_SIZE_M &&
               LEGEND_PLAN_SIZE_L == GLYPH_SIZE_L,
               "legend_plan's size indices drifted from poly_glyph_size");

// How far an idle legend with no free space of its own may hang OFF the visible
// window to win itself some anti-burn-in travel (legend_plan_idle_travel). The
// scratch buffer really extends there and kdisp_send_window() streams the window
// only, so the ink is clipped by the panel edge, not by an array bound — but that
// is a property of the geometry, so assert it rather than trusting the number.
#define IDLE_TRAVEL_OVERHANG_PX 3
_Static_assert(IDLE_TRAVEL_OVERHANG_PX <= BUFFER_SLACK_W, "idle travel would run off the west of the buffer");
_Static_assert(IDLE_TRAVEL_OVERHANG_PX <= BUFFER_SLACK_E, "idle travel would run off the east of the buffer");
_Static_assert(IDLE_TRAVEL_OVERHANG_PX <= BUFFER_SLACK_S, "idle travel would run off the south of the buffer");

static bool legend_has_glyph_cb(uint32_t cp, void* ctx) {
    (void)ctx;
    return kdisp_gfx_glyph(g_all_fonts, g_all_font_count, cp) != NULL;
}

static void legend_bbox_cb(const uint32_t* text, int8_t* xmin, int8_t* xmax, int8_t* ymin, int8_t* ymax, void* ctx) {
    (void)ctx;
    kdisp_gfx_text_bbox(g_all_fonts, g_all_font_count, text, xmin, xmax, ymin, ymax);
}

const legend_plan_env_t legend_plan_env = {
    .has_glyph = legend_has_glyph_cb,
    .bbox      = legend_bbox_cb,
    .ctx       = NULL,
    .win_x0    = BUFFER_X,
    .win_x1    = BUFFER_X + SCREEN_WIDTH - 1,
    .win_y1    = SCREEN_HEIGHT - 1,
};


void plan_main_legend(const uint32_t* text, int8_t small_x, int8_t small_y,
                             uint32_t* scratch, uint8_t scratch_cap, main_legend_t* out) {
    legend_plan_main(&legend_plan_env, get_local_state()->glyph_size, text, small_x, small_y,
                     scratch, scratch_cap, out);
}

// Returns builtin icon/symbol overlay text for keycode based on current modifiers and mod-tap states.
// The OS-aware shortcut-hint table itself lives in features/hints/os_hints.c as a PURE
// function. This wrapper is the only thing that knows where the two inputs come
// from, so the table can be exercised directly in a unit test.
const uint32_t* keycode_to_disp_overlay(uint16_t keycode) {
    const uint32_t* hint = os_hint_for_keycode(keycode, get_local_layer()->mods, get_local_state()->active_os);
    // The hint icons are a font-pack font (dev/tools/hint_icons.py, symbol bundle). With
    // no pack flashed their glyphs are absent, and drawing one would show the
    // missing-glyph '!' on every key the modifier touches: show no hint instead, so
    // the key keeps its own legend. A leading control code is a display-list op
    // (the mod-tap badge), whose glyphs are resident.
    if (hint != NULL && hint[0] >= 0x20 && kdisp_gfx_glyph(g_all_fonts, g_all_font_count, hint[0]) == NULL) {
        return NULL;
    }
    return hint;
}

// Which of the 90 overlay keycode-slots are currently on screen, rebuilt as a side
// effect of every full update_displays() pass (set in copy_overlay_to_buffer below,
// the display's own per-key lookup). A slot is "displayed" iff some physical key on
// this half currently resolves to that keycode under the active layer stack — so it
// captures LAYER visibility (an F-key overlay's slot is absent while the Fn layer is
// inactive). The overlay-completion visibility gate (fill_overlay.c) ANDs this with the
// modifier-variant check to skip re-renders for overlays that aren't on screen. The set
// only changes on a layer/mods change, which forces its own (ungated) refresh, so an
// overlay burst — which never changes the layer — safely reads the last full render's set.
static uint8_t s_displayed_slots[(90 + 7) / 8];

void clear_displayed_slots(void) {
    memset(s_displayed_slots, 0, sizeof(s_displayed_slots));
}

bool overlay_slot_displayed(uint16_t base_slot) {
    if (base_slot >= 90) {
        return false;
    }
    return (s_displayed_slots[base_slot >> 3] & (uint8_t)(1u << (base_slot & 7))) != 0;
}

// The keycode's overlay slot (0..89, before the modifier variant), or -1 for a keycode
// that has none. One mapping for copy_overlay_to_buffer() and overlay_present().
static int16_t overlay_key_slot(uint16_t keycode) {
    if(keycode>KC_RGUI || (keycode>KC_NUM_LOCK && keycode<KC_NUBS) || (keycode>KC_APP && keycode<KC_LEFT_CTRL)) {
        return -1;
    }
    uint16_t idx = (keycode>KC_APP) ? (keycode - KC_LEFT_CTRL + 82) : (keycode>KC_NUM_LOCK ? keycode - KC_NUBS + 80 : keycode - KC_A);
    return idx < 90 ? (int16_t)idx : -1;
}

// Whether copy_overlay_to_buffer() would draw an icon on this key. Draws nothing and
// records nothing, so a legend can be placed for the icon before it is drawn.
bool overlay_present(uint16_t keycode, uint8_t mods) {
    const int16_t slot = overlay_key_slot(keycode);
    return slot >= 0 && display_has_overlay(adjust_overlay_idx_to_mod((uint16_t)slot, mods));
}

bool copy_overlay_to_buffer(uint16_t keycode, uint8_t mods) {
    const int16_t slot = overlay_key_slot(keycode);
    if(slot < 0) {
        return false;
    }
    uint16_t idx = (uint16_t)slot;
    // Record that this keycode-slot is on screen (LAYER visibility for the render gate).
    s_displayed_slots[idx >> 3] |= (uint8_t)(1u << (idx & 7));
    idx = adjust_overlay_idx_to_mod(idx, mods);
    // display_has_overlay_bits[] is from-indexed (see set_packed_overlay_mapping): check it
    // here on the display position, before resolving to the pool slot.
    if(!display_has_overlay(idx)) {
        return false;
    }
    const bool dim = display_is_dim(idx);
    const bool no_legend = display_is_no_legend(idx);
    idx = get_display_pool_slot(idx);

    // Overlay images are ROW-MAJOR MSB-first (host: np.packbits over the 40x72 mask),
    // which is what kdisp_draw_bitmap reads — so the courtyard must use the row-major
    // reader. The column-native variant (for font glyphs) reads the same 360 bytes
    // without complaint and dilates a scrambled mask, which punched a big garbage
    // rectangle through the legend underneath (field, 2026-08-01).
    // The courtyard is cut from the FULL icon even when it is drawn dimmed, so the
    // legend keeps the same clearance either way.
    if (no_legend) {
        // v25 (a mapping `to` offset by OVERLAY_MAP_NO_LEGEND_OFFSET): the icon replaces the keycap, so
        // nothing of the legend drawn before this call may remain.
        kdisp_clear_rect(BUFFER_X, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    } else {
        kdisp_clear_rowmajor_courtyard(28, 0, get_overlay(idx), 72, 40, KDISP_CY_DEFAULT);
    }
    if (dim) {
        // v23: a browser's icon under a website's overlay (cmd 33 OVERLAY_MAP_W_DIM).
        kdisp_draw_bitmap_dimmed(28, 0, get_overlay(idx), 72, 40);
    } else {
        kdisp_draw_bitmap(28, 0, get_overlay(idx), 72, 40); //don't understnad why we start at offset 28... need to think about it
    }
    return true;
}

// ─── Language-layer flag rendering ──────────────────────────────────────────
// Each KCL_* key in the language layer (_LL) shows its country flag at full
// keycap height on the left, with the language code (e.g. U"en-US") running
// vertically up the right side in a tiny font.  The currently-selected language's
// code is drawn as dark text on a filled bar (see kdisp_write_gfx_vtext) rather
// than with a frame.  Flag glyphs live in flag_fonts.h at codepoints
// FLAG_CP_BASE + LANG_FLAG_GLYPH[LANG_*] (see fonts/gen-lang-fonts.sh); the label uses the tiny
// NotoSans font.
#define FLAG_CP_BASE   0xE000u
#define FLAG_LEFT_X    (BUFFER_X - 2)    // flag glyph left (keeps the left border on-screen)
#define LABEL_COL_X    (BUFFER_X + 66)   // baseline column of the vertical label

// Draw one language key: oversized country flag on the left (vertically centred
// and clipped so the flag content fills the keycap height), language code running
// vertically up the right side (inverted bar when it is the active language).
//
// The flag glyphs (FLAG_CP_BASE + glyph) live in the external-flash font pack, so
// they resolve through g_all_fonts only when a pack is present. With no pack the
// flag is simply omitted — the xx-YY code label below it still identifies the
// language (graceful fallback). The tiny label font stays resident.
const GFXfont* const lang_label_fonts[] = { &NotoSans_Regular_Nano_10px7b };

// Mid (19px) utility font for the no-pack fallback code — between Small and Base,
// so a full "ll-CC" fits on one line (~52px) yet stays readable. Reuse this
// `mid_fonts` array for any misc utility-key text that wants a middle size.
const GFXfont* const mid_fonts[]        = { &NotoSans_Regular_Mid_19px7b };

// --- FW-2 unsigned-image confirmation prompt -------------------------------
//
// Which key on EACH half carries the prompt. The same LOCAL matrix position on
// both halves, so the two keycaps sit symmetrically: the home-row index finger,
// which is findable by touch without hunting across 36 keycaps. The keycap grid
// is not rectangular and the right half folds its display column, but this is a
// matrix position — update_displays' loop indexes the matrix directly, so no
// fold applies. Left half = ACCEPT, right half = REJECT (fixed by side, not by
// which half happens to be master, so the prompt never moves between flashes).
// FW_CONFIRM_ROW / FW_CONFIRM_COL are defined in poly_keymap_internal.h.


// The REC key while the picker is open. Everything else on the board goes dark, so
// without this the one key that can back OUT of the picker is invisible -- the OLED
// says "REC = cancel", but the board is the dialog and the dialog should say it too.
void render_macro_rec_cancel_key(void) {
    const uint32_t *word = U"cancel";
    int8_t xmin, xmax, ymin, ymax;
    kdisp_gfx_text_bbox(mid_fonts, 1, word, &xmin, &xmax, &ymin, &ymax);
    kdisp_write_gfx_text(mid_fonts, 1,
                         (int8_t)(BUFFER_X + (SCREEN_WIDTH - (xmax - xmin + 1)) / 2 - xmin),
                         (int8_t)((SCREEN_HEIGHT - (ymax - ymin + 1)) / 2 - ymin), word);
}

void render_fw_confirm_key(bool accept) {
    const uint32_t  letter    = accept ? (uint32_t)'A' : (uint32_t)'R';
    const uint32_t *caption   = accept ? U"ACCEPT" : U"REJECT";

    kdisp_set_buffer(0x00);

    int8_t cxmin, cxmax, cymin, cymax;
    kdisp_gfx_text_bbox(lang_label_fonts, 1, caption, &cxmin, &cxmax, &cymin, &cymax);
    // Pin the caption's lowest lit pixel to the last screen row.
    const int8_t cap_base = (int8_t)(SCREEN_HEIGHT - 1 - cymax);
    kdisp_write_gfx_text(lang_label_fonts, 1,
                         (int8_t)(BUFFER_X + (SCREEN_WIDTH - (cxmax - cxmin + 1)) / 2 - cxmin),
                         cap_base, caption);

    // The big letter, centred in what is left above the caption.
    const GFXfont  *lf = NULL;
    const GFXglyph *lg = kdisp_gfx_glyph_font(mid_fonts, 1, letter, &lf);
    if (lg == NULL) return;
    // _double_at takes the literal top-left of the INK (no baseline align, no
    // xOffset), so the glyph box is centred directly — nothing to compensate for.
    const int16_t lw = (int16_t)pgm_read_byte(&lg->width)  * 2;
    const int16_t lh = (int16_t)pgm_read_byte(&lg->height) * 2;
    const int8_t  free_rows = (int8_t)(cap_base + cymin);   // rows 0 .. caption top - 1
    kdisp_draw_glyph_double_at(mid_fonts, 1,
                               (int8_t)(BUFFER_X + (SCREEN_WIDTH - lw) / 2),
                               (int8_t)((free_rows - lh) / 2),
                               letter);
}




// One flag glyph per COUNTRY (fonts/gen-lang-fonts.sh): the six Indian languages,
// the four US ones and so on share theirs, so a language's glyph is looked up rather
// than being its enum index.
#include "fonts/generated/lang_flag_glyph.h"
#include "fonts/generated/fontpack_layout.h"
_Static_assert(sizeof(LANG_FLAG_GLYPH) == NUM_LANG,
               "lang_flag_glyph.h is stale: re-run fonts/gen-lang-fonts.sh");

// The flags bundle's slot index, from the generated layout table.
enum {
#define X(id, index, offset, size) LANG_FLAG_SLOT_##id = (index),
    FONTPACK_BUNDLE_LIST
#undef X
};
// The flags bundle that first carries one glyph per country. An older one is still
// one glyph per LANGUAGE, and a keyboard keeps it until the host re-flashes the slot
// on its next connect, so until then the enum index is still the right glyph, except
// for ja-US: it was added after the per-language bundles and has no glyph of its own
// there, so it borrows ja-JP's. (In the table it shares Japan's glyph already, because
// FLAG_ALIAS in gen-lang-fonts.sh files it under JP, its home country.)
#define LANG_FLAG_PER_COUNTRY_VERSION 6u

static uint8_t lang_flag_glyph(uint8_t idx) {
    if (idx >= NUM_LANG) {
        return 0;
    }
    return fontpack_bundle_version(LANG_FLAG_SLOT_flags) >= LANG_FLAG_PER_COUNTRY_VERSION
               ? LANG_FLAG_GLYPH[idx]
               : (idx == LANG_JAUS ? (uint8_t)LANG_JAJP : idx);
}

void render_lang_flag_key(uint8_t idx, const uint32_t* label, uint8_t current_lang) {
    const uint32_t flag_cp = FLAG_CP_BASE + lang_flag_glyph(idx);
    const GFXfont* flag_font = NULL;
    const GFXglyph* g = kdisp_gfx_glyph_font(g_all_fonts, g_all_font_count,
                                             flag_cp, &flag_font);
    if (g) {
        // The glyph is taller than the keycap, so centre it vertically — the
        // empty top/bottom margins clip off and the flag content fills the height.
        // Compensate BOTH the glyph's x and y bearing (kdisp_write_gfx_char draws
        // at x+xOffset, y+yOffset) so the flag's content lands flush at
        // FLAG_LEFT_X regardless of the glyph's left bearing (was x-shifted).
        //
        // Render through a SINGLE-font array holding just the flag font — NOT
        // g_all_fonts. kdisp_write_gfx_char baseline-aligns each glyph to fonts[0]
        // (`y += currentFont->yAdvance - fonts[0]->yAdvance`). With g_all_fonts,
        // fonts[0] is IconsFont (yAdvance 40) vs the flag's 54 → a spurious +14 px
        // downward shift (the gap-at-top regression introduced when flags moved
        // into the pack). Passing the owning font alone makes that adjustment 0,
        // as the old compiled-in { &flag_font } path did. (kdisp_gfx_glyph_font
        // returned flag_font from the same scan, so no second lookup.)
        const GFXfont* flag_only[1] = { flag_font };
        const int8_t fh  = (int8_t)pgm_read_byte(&g->height);
        const int8_t fyo = (int8_t)pgm_read_byte(&g->yOffset);
        const int8_t fxo = (int8_t)pgm_read_byte(&g->xOffset);
        kdisp_write_gfx_char(flag_only, 1, (int8_t)(FLAG_LEFT_X - fxo),
                             (int8_t)((SCREEN_HEIGHT - fh) / 2 - fyo),
                             flag_cp, 1);   // flags: tight 1px courtyard
        // Language code: vertical, up the right side; inverted bar when selected.
        kdisp_write_gfx_vtext(&NotoSans_Regular_Nano_10px7b, LABEL_COL_X, label,
                              current_lang == idx);
    } else {
        // No font pack flashed: the flag glyphs are pack-only. Show the "ll-CC"
        // code on one centred line in the mid (10px) utility font — readable, and
        // a full code (~52px) fits the 72px keycap. The tiny vertical label is
        // dropped here; underline the active language.
        int8_t lo = 0, hi = 0;
        kdisp_gfx_text_bounds(mid_fonts, 1, label, &lo, &hi);
        int8_t w    = (int8_t)(hi - lo);
        int8_t left = (int8_t)(BUFFER_X + (SCREEN_WIDTH - w) / 2);
        kdisp_write_gfx_text(mid_fonts, 1, (int8_t)(left - lo), 24, label);
        if (current_lang == idx) {
            kdisp_fill_rect(left, 28, w, 2);   // underline = active language
        }
    }
}



// MRU recents (emoji or language) get a full-width bar along the TOP edge —
// the mirror image of the category tabs' bottom bar — to set the row apart.
void draw_mru_top_bar(uint16_t keycode) {
    bool is_mru = (keycode >= KC_EMJ_MRU_BASE  && keycode < KC_EMJ_MRU_BASE  + MRU_CAP) ||
                  (keycode >= KC_LANG_MRU_BASE && keycode < KC_LANG_MRU_BASE + MRU_CAP);
    if (is_mru) kdisp_fill_rect(BUFFER_X, 0, SCREEN_WIDTH, 3);
}

#if defined(POLY_FL_ALIGN_FROW)
// ── Function-layer F-row alignment ───────────────────────────────────────────
// The F-row is DERIVED from the active base layout's own number row, so F5 always
// sits on the key that types 5 whichever layout is loaded. This replaces the old
// _FL0/_FL1 pair: two hand-maintained copies that drifted (Colemak and Workman were
// both wired to the 1..6 variant despite carrying an ordinary 1..5 row).
//
// The rule, which reproduces BOTH retired layers exactly: walk the base layer's top
// row across the two halves; every slot holding KC_1..KC_0 takes the matching
// F1..F10, the two slots after the last number take F11/F12, and whatever is left
// over takes the layer keys (OSL(_UL) at the far left, TO(_UL) for the other).
//
// ⚠️ It is ALL-OR-NOTHING and switches itself OFF the moment the row is customised.
// _FL sits below the dynamic-keymap write cap, so the host layout editor can remap
// it — and the editor reads keycodes through dynamic_keymap_get_buffer(), which does
// NOT come through here, so anything derived at runtime is invisible to it. Aligning
// a row the user had edited would therefore mean the editor showing one keycode
// while the board types another, with no way to see why. So: align only while all 14
// slots still hold exactly what the firmware compiled, and otherwise hand back the
// stored row untouched. Tweak one F-key and the magic stops for the whole row —
// editor and board agree again, which is the state you are in whenever you care.
#define FL_TOP_SLOTS 14

// The 14 top-row matrix positions in reading order: left half is row 0 cols 0..6,
// right half is row MATRIX_ROWS_PER_SIDE cols 1..7 (col 0 is not a key up there).
static bool fl_top_slot(uint8_t row, uint8_t col, uint8_t* slot) {
    if (row == 0 && col <= 6) {
        *slot = col;
        return true;
    }
    if (row == MATRIX_ROWS_PER_SIDE && col >= 1 && col <= 7) {
        *slot = (uint8_t)(6 + col);
        return true;
    }
    return false;
}

static void fl_slot_pos(uint8_t slot, uint8_t* row, uint8_t* col) {
    if (slot <= 6) {
        *row = 0;
        *col = slot;
    } else {
        *row = MATRIX_ROWS_PER_SIDE;
        *col = (uint8_t)(slot - 6);
    }
}

// What the F-row slot SHOULD hold for this base layout. Reads the base layer from the
// compiled keymap, never the dynamic one: the number row is the layout's identity, and
// a user who has remapped their digits has not thereby asked for a different F-row.
static uint16_t fl_aligned_keycode(uint8_t def_layer, uint8_t want) {
    uint8_t last_num = 0xFF;
    for (uint8_t i = 0; i < FL_TOP_SLOTS; i++) {
        uint8_t r, c;
        fl_slot_pos(i, &r, &c);
        const uint16_t base = keymaps[def_layer][r][c];
        if (base >= KC_1 && base <= KC_0) {
            // KC_1..KC_9 then KC_0 are contiguous, and so are KC_F1..KC_F10.
            if (i == want) return (uint16_t)(KC_F1 + (base - KC_1));
            last_num = i;
        }
    }
    // F11/F12 continue past the last digit (where a layout has "-" and "=").
    if (last_num != 0xFF) {
        if (want == last_num + 1) return KC_F11;
        if (want == last_num + 2) return KC_F12;
    }
    // Everything else is a layer key: the far-left slot sits over Esc and holds the
    // one-shot into _UL, the remaining leftover is the sticky TO(_UL).
    return (want == 0) ? OSL(_UL) : TO(_UL);
}

// Is the stored F-row still exactly what we compiled? Cached, because this runs on
// the render path (72 keycaps) as well as the key-event path, and each probe is 14
// dynamic-keymap reads. Invalidated by every write to the dynamic keymap.
static int8_t s_fl_row_pristine = -1;   // -1 unknown, 0 customised, 1 pristine

static bool fl_row_is_pristine(void) {
    if (s_fl_row_pristine < 0) {
        s_fl_row_pristine = 1;
        for (uint8_t i = 0; i < FL_TOP_SLOTS; i++) {
            uint8_t r, c;
            fl_slot_pos(i, &r, &c);
            if (keycode_at_keymap_location(_FL, r, c) != keymaps[_FL][r][c]) {
                s_fl_row_pristine = 0;
                break;
            }
        }
    }
    return s_fl_row_pristine == 1;
}
#endif // POLY_FL_ALIGN_FROW

// Drop the cached "is the F-row untouched?" answer. Called from every dynamic-keymap
// mutation (the three *_poly wrappers in split_sync.c), so a remap takes effect on the
// very next render rather than at the next reboot. A no-op on a board with no F-row to
// align, so the call sites need no #ifdef of their own.
void poly_fl_row_cache_invalidate(void) {
#if defined(POLY_FL_ALIGN_FROW)
    s_fl_row_pristine = -1;
#endif
}

// Layers below the host-write cap are remappable and live in the dynamic keymap
// (EEPROM); layers at/above it (the language/emoji function layers) are served straight
// from the compiled keymap in flash, so they never read the dynamic keymap and always
// reflect the flashed firmware. Used by both the display and the key-event path.
uint16_t poly_keycode_at(uint8_t layer, uint8_t row, uint8_t col) {
    const uint16_t kc = (layer >= DYNAMIC_KEYMAP_UPDATE_MAX_LAYER_COUNT)
                            ? keymaps[layer][row][col]      // static, compiled-in (flash)
                            : keycode_at_keymap_location(layer, row, col);
#if defined(POLY_FL_ALIGN_FROW)
    // Both the render path (display_keycode_at) and the key-event path
    // (keymap_key_to_keycode) resolve through here, so the legend and the keycode can
    // never disagree about which F-key a slot holds — the render/action pairing rule.
    // The base layout comes from the SYNCED snapshot for the same reason: it is the
    // only source both halves share, and it is what the renderer already uses.
    uint8_t slot;
    if (layer == _FL && fl_top_slot(row, col, &slot) && fl_row_is_pristine()) {
        return fl_aligned_keycode((uint8_t)get_local_layer()->def_layer, slot);
    }
#endif
    return kc;
}

// Override QMK's weak resolver so key events on the static function layers come from the
// compiled keymap too (no encoder/dip maps are enabled on this board).
uint16_t keymap_key_to_keycode(uint8_t layer, keypos_t key) {
    if (key.row >= MATRIX_ROWS || key.col >= MATRIX_COLS) return KC_NO;
    return poly_keycode_at(layer, key.row, key.col);
}

// Resolve the keycode whose legend a physical key is currently showing, honouring the
// active layer stack with a one-level transparent fallback. The single source of truth
// for both the awake render (update_displays) and the idle relocation (kdisp_idle), so
// a jittered legend always matches what was last drawn awake instead of snapping to the
// base layer. The effective state folds in the default layer (`def_layer`, tracked
// separately from the momentary `layer` stack — e.g. a Colemak/Neo base) so a key with
// no momentary layer active still shows its default-layer legend, not _BL.
uint16_t display_keycode_at(const poly_layer_t* lyr, uint8_t row, uint8_t col) {
    layer_state_t eff = lyr->layer | ((layer_state_t)1 << lyr->def_layer);
    uint8_t layer = get_highest_layer(eff);
    uint16_t kc = poly_keycode_at(layer, row, col);
    if (kc == KC_TRNS) {
        kc = poly_keycode_at(get_highest_layer(eff & ~((layer_state_t)1 << layer)), row, col);
    }
    // KC_IME that stands in for a plain key (NUBS, or Right Alt on the ANSI English
    // layouts) draws as that key: its legend, overlay slot and macOS swap and all.
    // DISPLAY ONLY — the key event keeps KC_IME, and ime_key_stroke() sends exactly
    // ime_key_stand_in()'s key, so the legend and the action cannot disagree.
    if (kc == KC_IME) {
        const poly_sync_t* st = get_local_state();
        const uint8_t stand_in = ime_key_stand_in(poly_ime_family(st->lang),
                                                  st->active_os & POLY_OS_VALUE_MASK);
        if (stand_in) {
            kc = stand_in;
        }
    }
    return kc;
}

// Resolve what a physical position holds ON THE UTILITY LAYER, whatever layer is
// active. The macro slot picker is a modal dialog over _UL's macro row, and it cannot
// use the live stack: KC_MACRO_REC is SWALLOWED on the press, and QMK clears the
// one-shot layer whenever process_record_user() returns false on a press
// (quantum/action.c process_record), so the very tap that opens the picker drops an
// OSL(_UL). Resolving live then finds the BASE layer -- no macro key, no REC key -- so
// every keycap went dark and neither picking a slot nor cancelling was reachable
// (field, 2026-09-08). Both the render and the key-event path go through here, which is
// what keeps them from disagreeing about which key is which.
uint16_t macro_picker_keycode_at(uint8_t row, uint8_t col) {
    // ⚠️ Bounds-checked for the same reason keymap_key_to_keycode() is: not every
    // record carries a MATRIX position. An encoder event is row KEYLOC_ENCODER_CW/CCW
    // (253/252) with the encoder index as the column, so while the picker is open a
    // turn of the knob would index the keymap far out of range and answer with
    // whatever that read produced -- which can match a macro key or the REC key.
    if (row >= MATRIX_ROWS || col >= MATRIX_COLS) return KC_NO;
    return poly_keycode_at(_UL, row, col);
}

// Small deterministic pseudo-random offset for a given seed and axis salt, mapped
// into [lo, hi]. Used per key, per dark-episode (see kdisp_idle in poly_keymap.c):
// the seed is a rolling counter so each relocation lands somewhere new, the salt
// separates the X and Y axes. Cheap integer hash — no PRNG state to carry on either half.
static int8_t jitter_axis(uint32_t epoch, uint32_t salt, int8_t lo, int8_t hi) {
    uint32_t h = (epoch + salt) * 2654435761u;
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    uint8_t range = (uint8_t)(hi - lo + 1);
    return (int8_t)(lo + (int8_t)(h % range));
}

// Roll a per-glyph idle jitter offset: a uniform random position within the legend's
// OWN on-screen slack, measured from its bounding box at the draw origin (ox/oy). The
// range is glyph-derived — never a global cap — so a slim "i" roams its full free
// width while a wide "w" stays within its small margin, each using all (and only) the
// space it actually has. A global ±N envelope would be counter-productive here: it
// would throttle the slim glyph (lots of slack, but capped) and edge-bias the wide one
// (most rolls clamp to the same boundary).
//
// A glyph with NO slack in an axis used to simply not move in it — which is silent
// and complete failure for exactly the legends that need an idle style most: a 40 px
// tall icon fills the window, so it held the same pixels for the whole idle session.
// Those now borrow up to IDLE_TRAVEL_OVERHANG_PX px of travel by hanging off the
// bottom/left/right edges into the scratch buffer's own slack, which is never sent;
// legend_plan_idle_travel() holds the rule and the reasons. Everything that already
// had room is unaffected, so a legend only ever clips because it had no alternative.
static void roll_idle_offset(const uint32_t* text, int8_t ox, int8_t oy, uint32_t seed,
                             int8_t* dx, int8_t* dy) {
    // ⚠️ The ABSOLUTE box, not the relative one. The whole display list moves as a
    // unit under the jitter offset, so the slack has to be measured over ALL of it —
    // including art positioned by a MOVE and drawn by a composite op, which the
    // relative box cannot see (font_lookup.h). Measuring the relative box instead
    // reports only the cursor-laid-out part: for the context-menu keycap that is the
    // hamburger alone, granting travel that would carry its pointer off the panel,
    // and for a legend that is ONLY composite art (the media-stop badge) it reports
    // an empty box and would allow the lot.
    int8_t xmin, xmax, ymin, ymax;
    kdisp_gfx_text_bbox_abs(g_all_fonts, g_all_font_count, ox, oy, text, &xmin, &xmax, &ymin, &ymax);
    int8_t xlo, xhi, ylo, yhi;
    legend_plan_idle_travel(&legend_plan_env, xmin, xmax, ymin, ymax, IDLE_TRAVEL_OVERHANG_PX,
                            &xlo, &xhi, &ylo, &yhi);
    *dx = (xhi < xlo) ? 0 : jitter_axis(seed, 0x0000u, xlo, xhi);
    *dy = (yhi < ylo) ? 0 : jitter_axis(seed, 0x1000u, ylo, yhi);
}

// Idle (anti-burn-in) per-key relocation: redraws ONLY the resting normal legend — no
// shift/AltGr preview, no overlay image, no tab/MRU chrome — at a fresh random spot
// within THIS glyph's own slack (roll_idle_offset, seeded by `seed`) — fully visible
// unless the legend had no slack at all, in which case it borrows a few px off the
// window's bottom/left/right edges. Renders into the currently selected display's
// buffer, so it works from inside kdisp_idle()'s shift-register walk (the caller selects the key, with the panel
// switched OFF, so the move is invisible). Returns false WITHOUT touching the buffer
// when the keycode has no plain-text legend (a language flag, emoji, region tab, MRU
// control, …): those can't be jittered (full-bleed images), so we leave their current
// frame in place instead of blanking it — e.g. the language-layer flags stay put and
// keep pulsing rather than disappearing on the first idle cycle.
bool render_idle_key(uint16_t keycode, led_t state, uint32_t seed) {
    const poly_sync_t* local_state = get_local_state();
    uint32_t unimap[2] = {0, 0};
    const uint32_t* text = to_static_text(keycode, state);
    if (text == NULL) {
        // letter / symbol legends come from the language table (unshifted, no AltGr)
        text = translate_keycode(local_state->lang, keycode, false, state.caps_lock);
    }
    if (text == NULL && (keycode & QK_UNICODEMAP_PAIR) == QK_UNICODEMAP_PAIR) {
        uint16_t chr = state.caps_lock ? QK_UNICODEMAP_PAIR_GET_SHIFTED_INDEX(keycode)
                                       : QK_UNICODEMAP_PAIR_GET_UNSHIFTED_INDEX(keycode);
        unimap[0] = unicode_map[chr];
        text = unimap;
    }
    if (text == NULL || text[0] == 0) {
        return false;   // no text legend — keep the key's current frame
    }
    // Plan at the ACTIVE legend size, exactly as the awake render does. Drawing
    // `text` at the small face's fixed (BUFFER_X, 23) instead would shrink a
    // medium/large legend the moment its pulse dipped dark and this relocation
    // fired — and leave it small until wake, since update_displays() early-returns
    // while DISP_IDLE is set. roll_idle_offset() has to measure the SAME text it
    // will move, or the slack it computes is the wrong glyph's.
    uint32_t      scratch[GLYPH_SIZE_MAX_LEN + 1];
    main_legend_t plan;
    plan_main_legend(text, BUFFER_X, 23, scratch, (uint8_t)(GLYPH_SIZE_MAX_LEN + 1), &plan);
    int8_t dx, dy;
    roll_idle_offset(plan.text, plan.x, plan.y, seed, &dx, &dy);
    kdisp_set_buffer(0x00);
    kdisp_set_draw_offset(dx, dy);
    kdisp_write_gfx_text(g_all_fonts, g_all_font_count, plan.x, plan.y, plan.text);
    kdisp_set_draw_offset(0, 0);
    // The draw may land up to IDLE_TRAVEL_OVERHANG_PX px outside the 72x40 window
    // (roll_idle_offset); that ink is inside the scratch buffer and this send ignores it.
    kdisp_send_window();
    return true;
}

// How often the Eden idle legend relocates to a fresh spot (anti-burn-in "ghosting").
#define EDEN_LEGEND_DRIFT_MS 7000u

// Eden idle screensaver: draw a key's resting legend LIT into the comet field the
// animation just built, so the letter is clearly visible as a faint "ghost" the
// comets drift around — and relocate it to a fresh in-glyph-slack spot every
// EDEN_LEGEND_DRIFT_MS so it slowly wanders (anti-burn-in), like the jitter style.
// (An earlier version ERASED the legend as a dark cutout, but at the dim idle
// brightness the sparse comet field had too few lit pixels for a cutout to read.)
// Called per panel from sa_render_idle_frame() (startup_anim.c) AFTER the comet
// field is drawn and BEFORE the send, so it does NOT clear the buffer and does NOT
// send. `disp_idx` is the display index == the anim's geom index == LAYOUT_TO_INDEX(r,c);
// invert it to (r,c) to resolve the keycode. Returns false without touching the
// buffer for keys with no plain-text legend (flags/emoji/tabs/overlays) — those faces
// just show the plain comet field. Mirrors render_idle_key's legend derivation.
bool eden_idle_erase_legend(uint8_t disp_idx) {
    if (disp_idx >= MATRIX_ROWS_PER_SIDE * MATRIX_COLS) return false;
    // disp_idx == the anim geom index == display row*8 + col. Invert to the matrix
    // (row,col), undoing the right-half display fold (mirrors the host sim's disp_mp).
    uint8_t mr, mc;
    if (!display_index_to_matrix(!is_left_side(), disp_idx, &mr, &mc)) return false;   // phantom col — no OLED behind it
    const poly_layer_t* local_layer = get_local_layer();
    uint16_t keycode = display_keycode_at(local_layer, mr, mc);
    if (keycode == KC_NO || keycode == KC_TRNS) return false;

    const poly_sync_t* local_state = get_local_state();
    led_t state = local_layer->led_state;
    uint32_t unimap[2] = {0, 0};
    const uint32_t* text = to_static_text(keycode, state);
    if (text == NULL) {
        text = translate_keycode(local_state->lang, keycode, false, state.caps_lock);
    }
    if (text == NULL && (keycode & QK_UNICODEMAP_PAIR) == QK_UNICODEMAP_PAIR) {
        uint16_t chr = state.caps_lock ? QK_UNICODEMAP_PAIR_GET_SHIFTED_INDEX(keycode)
                                       : QK_UNICODEMAP_PAIR_GET_UNSHIFTED_INDEX(keycode);
        unimap[0] = unicode_map[chr];
        text = unimap;
    }
    if (text == NULL || text[0] == 0) {
        return false;   // no text legend — leave the plain comet field on this key
    }
    // Draw the legend LIT but with a scanline half-brightness effect (every other buffer
    // row lit) so it reads lighter over the comet field, at a slowly-drifting position
    // within its own on-screen slack. roll_idle_offset() picks a uniform random offset
    // inside the glyph's free space (per-glyph — the same helper the jitter idle style
    // uses, including the few px of off-window travel it lends a legend that has no free
    // space of its own); the seed changes once per EDEN_LEGEND_DRIFT_MS so every
    // ~7 s the letter jumps to a fresh spot. Per-key phase (disp_idx) so they don't all
    // move in lockstep.
    // Same glyph-size plan as render_idle_key() — this is the second idle draw path
    // and it had the identical fixed-origin bug, so fixing only the one a reviewer
    // pointed at would have left the Eden screensaver shrinking the legend instead.
    uint32_t      scratch[GLYPH_SIZE_MAX_LEN + 1];
    main_legend_t plan;
    plan_main_legend(text, BUFFER_X, 23, scratch, (uint8_t)(GLYPH_SIZE_MAX_LEN + 1), &plan);
    uint32_t epoch = timer_read32() / EDEN_LEGEND_DRIFT_MS;
    uint32_t seed  = epoch * 2654435761u + disp_idx;
    int8_t dx, dy;
    roll_idle_offset(plan.text, plan.x, plan.y, seed, &dx, &dy);
    // ⚠️ The scanline phase has to roll TOO, and the drift offset above does NOT do
    // it: scanline_skip_row() gates on absolute buffer y, so the drift slides the
    // glyph past a stationary stripe pattern — it changes which rows OF THE GLYPH are
    // dropped while the lit PANEL rows never move. Left at a fixed phase this lights
    // the even panel rows and only ever the even panel rows, for every idle session
    // the board will ever have: half the panel takes the legend's entire share of the
    // wear and the other half takes none, which is the fixed-pixel burn an idle style
    // exists to prevent. A third salt on the same jitter hash (so uncorrelated with
    // dx/dy) flips it, and rolling it on the same `epoch` puts the flip on the frame
    // the letter jumps anyway — invisible, rather than a shimmer.
    uint8_t sl_phase = (uint8_t)jitter_axis(seed, 0x2000u, 0, 1);
    kdisp_set_gfx_scanline(true, sl_phase);
    kdisp_set_draw_offset(dx, dy);
    kdisp_write_gfx_text(g_all_fonts, g_all_font_count, plan.x, plan.y, plan.text);
    kdisp_set_draw_offset(0, 0);
    kdisp_set_gfx_scanline(false, 0);
    return true;
}

// Draw a legend horizontally CENTRED in the visible key window
// [BUFFER_X, BUFFER_X+SCREEN_WIDTH), baseline at y, instead of left-aligned at
// BUFFER_X. Used for the bottom (thumb) row so its mixed chrome/icon legends
// (arrows, Base, Make, toggles, world, …) sit centred.
//
// Leading spaces are skipped first: many legends carry manual left-padding (e.g.
// the arrows are U"  " + icon), and the Base font's space glyph is a degenerate
// 1×1 ink box, so kdisp_gfx_text_bounds would count those spaces as ink at x=0,
// inflate the measured width, and push the real glyph right of centre. Skipping
// them makes the measure+draw start at the first real glyph, truly centred.
// cy_radius is exposed because the courtyard clear is only wanted when something
// is drawn UNDERNEATH the legend for it to punch a margin through (a frame, a row
// bar, an overlay). On a deliberately filled ground — the inverted picker Ctrl —
// there is nothing to clear away from, so the clear just eats a dark halo out of
// the fill around every glyph. Pass 0 there.
void draw_legend_cx_cy(const uint32_t* text, int8_t y, int8_t cy_radius) {
    // ⚠️ A legend that MOVEs (\x0E) places its art at ABSOLUTE buffer positions, laid
    // out against the whole cell already, so it is drawn unshifted. Centring shifts the
    // origin, which moves the relatively placed glyphs and not the MOVE'd ones: the
    // context-menu lines slid left under their own pointer on the bottom row (hardware
    // round 34). The scan skips each op's ARGUMENTS, since a coordinate or a size can
    // be the byte 0x0E too (the same trap the bbox walker documents for MOVE).
    for (const uint32_t* p = text; *p; ++p) {
        uint8_t args = 0;
        switch (*p) {
            case U'\x0E':
                kdisp_write_gfx_text_cy(g_all_fonts, g_all_font_count, BUFFER_X, y, text, cy_radius);
                return;
            case U'\x0F': case U'\x11': args = 1; break;   // HALF / THIN glyph
            case U'\x12': case U'\x15': args = 2; break;   // FRAME (w,h) / ROT (step,glyph)
            case U'\x13': args = 3; break;                  // BADGE (w,h,style)
            default: break;
        }
        for (; args > 0 && p[1]; --args) ++p;
    }
    while (*text == U' ') text++;          // drop manual leading padding (skews bbox)
    int8_t lo = 0, hi = 0;
    kdisp_gfx_text_bounds(g_all_fonts, g_all_font_count, text, &lo, &hi);
    const int8_t w    = (int8_t)(hi - lo + 1);
    const int8_t left = (int8_t)(BUFFER_X + (SCREEN_WIDTH - w) / 2 - lo);
    kdisp_write_gfx_text_cy(g_all_fonts, g_all_font_count, left, y, text, cy_radius);
}

void draw_legend_cx(const uint32_t* text, int8_t y) {
    draw_legend_cx_cy(text, y, KDISP_CY_DEFAULT);
}
