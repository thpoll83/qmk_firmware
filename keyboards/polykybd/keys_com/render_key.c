// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The keycap renderers, split out of poly_keymap.c: render_key(), the menu,
// macro-picker, macro, MRU and region-tab keys, and their private helpers.
//
// ⚠️ render_key() and to_static_text() are still a PAIR: render_key() runs only when
// to_static_text() returns NULL. to_static_text() stays in poly_keymap.c because
// PolyKybdHost parses it from that file (tools/lang_demo.py parse_to_static_text_map).
// Helpers and tables still shared with poly_keymap.c are in poly_keymap_internal.h.
//
// ⚠️ The font headers DEFINE their GFXfont data, so they are included only in
// poly_keymap.c; this file reaches the fonts it draws with through extern
// declarations.
#include QMK_KEYBOARD_H

#include "quantum/quantum_keycodes.h"
#include "quantum/keymap_extras/keymap_german.h"
#include "quantum/keymap_introspection.h"
#include "quantum/via.h"

#include "raw_hid.h"
#include "oled_driver.h"
#include "version.h"
#include "print.h"
#include "debug.h"

#include <transactions.h>
#include "split_util.h"   // is_transport_connected() — gate the boot layer-resync one-shot
#include <hardware/flash.h>

#include "polykybd.h"
#include "features/hints/os_hints.h"
#include "poly_keymap.h"   // our own exports — declarations checked against the definitions below
#include "status_oled.h"
#include "lib/split_link/bridge_helper.h"
#include "dev/profiling/loop_profile.h"
#ifdef POLYKYBD_LOOP_PROFILE
#    include "hardware/structs/timer.h"   // timer_hw->timerawl — raw 1 MHz us counter
#endif
#include "lib/fw_update/split_fw_up.h"
#ifdef POLYKYBD_BULK_PIPE
#    include "lib/hid/poly_pipe.h"
#endif
#include "lib/drivers/fw_staging.h"
#include "polymod_core1_owner.h"
#include "lib/split_link/side.h"
#include "lib/display/fill_overlay.h"
#include "lib/system/poly_util.h"

#include "base/com.h"
#include "lib/hid/mac_compose.h"   // mac_compose_set (protocol v38)
#include "polymod_rle.h"
#include "lib/drivers/e2prom.h"
#include "base/overlay.h"
#include "lib/drivers/disp_array.h"
#include "lib/drivers/helpers.h"
#include "base/update.h"
#include "lib/drivers/spi_helper.h"
#include "lib/drivers/shift_reg.h"
#include "base/text_helper.h"
#include "base/fontpack.h"                // g_all_fonts/g_all_font_count + loader
#include "features/modes/midi_legend.h"                 // QK_MIDI_* legends from the synced MIDI settings
#include "base/legend_plan.h"            // the pure keycap legend-SIZE planner
#include "base/ime_key_plan.h"           // what KC_IME sends, per language and OS
#include "base/ime_held_slots.h"         // which KC_IME key holds which usage
// Country flags (NotoColorEmoji_Regular_LangFlags, codepoints FLAG_CP_BASE+glyph)
// now ship in the external-flash font pack, resolved via g_all_fonts — they are
// NOT compiled in. The tiny label font stays resident (no-pack fallback label).
#include "polymod_core1.h"
#include "features/anim/tutorial.h"
#include "features/anim/demo_mode.h"              // the showroom demo loop (KC_DEMO)
#include "features/modes/mines_mode.h"                   // the idle-screen mine sweeper (hold Hyper while idle)
#include "features/modes/ai_mode.h"                      // AI mode: agent cards on the number row (KC_AI)
#include "features/anim/key_attention.h"            // cmd 47: the focus ripple on a key
#include "features/anim/host_tutorial.h"            // cmd 49: the host tutorial (text, spotlight, events)
#include "features/anim/focus_ring.h"                 // the reusable "point at this key" ripple
#include "base/tutorial_plan.h"             // TUT_SLOT / TUT_SKIP_HOLD_MS
#include "features/anim/menu_cascade.h"             // menu_cascade_hidden() / _tick()
#include "features/anim/lang_sparkle.h"             // lang_sparkle_tick()
#include "features/anim/tutorial_rgb.h"             // the key LEDs during the show and the lesson
#include "lib/system/boot_diag.h"                    // emit_boot_banner(), splash_progress(), SPLASH_DONE
#include "lib/drivers/crash_record.h"            // crash_record_init(), the watchdog, the phase breadcrumb
#include "dev/diag/usb_stress.h"              // no-op unless -e POLYKYBD_USB_STRESS=yes
#include "base/hand_stamp.h"              // handedness that survives an EEPROM wipe
#include "lib/drivers/hw_uid.h"                  // flash unique ID, read once here (cmd 50)
#include "base/status_brightness.h"      // one brightness scale for keycaps + status OLED
#include "usb_util.h"                     // usb_vbus_state() — the suspend-time power check
#include "dev/diag/crash_test.h"              // POLYKYBD_CRASH_TEST: deliberate faults (no-op inlines otherwise)
#include "lib/split_link/slave_data.h"                   // slave_data_register(), slave_data_crash_pull_tick()
#include "polymod_crc32.h"

// The LTR-559 driver is the polykybd/polymod_ltr559 community module; the build
// defines COMMUNITY_MODULE_POLYMOD_LTR559_ENABLE when a variant lists it, and the
// module probes + polls the part from its own hooks. Everything below is the
// PolyKybd-side POLICY on top of it (auto-brightness, idle-inhibit, telemetry).
#ifdef COMMUNITY_MODULE_POLYMOD_LTR559_ENABLE
#    include "polymod_ltr559.h"
#endif
#include "lib/peripherals/ltr559_policy.h"        // the POLICY on the module (POLYKYBD_LTR559_DRIVE)

#include "lib/system/state.h"
#include "poly_macro.h"
#include "poly_macro_record.h"
#include "lib/display/multicore_exec.h"
#include "lib/split_link/split_sync.h"

#include "lang/lang_lut.h"
#include "base/caps_word_letter.h"
#include "poly_combos.h"
#include "lang/lang_lut_ext.h"

#include "keys_com/layers.h"
#include "keycode_helper.h"
#include "doom/doom_mode.h"   // Doom easter egg (inline no-ops unless POLYKYBD_DOOM)
#include "features/anim/startup_anim.h"   // one-time procedural boot animation (split72; no-op stubs on split42)
#include "lib/display/status_idle.h"          // the status panel's idle screen (split72 only)
#include "polymod_os_actions.h"
#include "features/emoji/emoji_layer.h"
#include "lang_layer.h"
#include "mru.h"

#include <stdint.h>
#include <string.h>
#include <stdbool.h>


#include "latin_picker.h"
#include "poly_keymap_internal.h"

extern const GFXfont NotoSans_Regular_Nano_10px7b;

// C64 keycap script, LETTER keys only: the letter at 22 px on top and the key's two
// PETSCII graphics (14x14 framed cells) side by side below it, the way a real C64 key
// prints them on its front: Commodore+key on the left, Shift+key on the right. They
// are drawn, never typed. Each glyph comes from its own block in the fantasy bundle
// (fonts.yaml _C64KeyLtr_ / _C64Petscii_): letters a..z at LETTER_BASE, then the
// Commodore graphics a..z and the Shift graphics a..z at PETSCII_BASE.
#define C64KEYS_LETTER_BASE  0xEAC0u
#define C64KEYS_PETSCII_BASE 0xEB00u
#define C64KEYS_LETTER_TOP   1    // 22 px capitals: y 1..22
#define C64KEYS_CELL_TOP     25   // 14 px cells: y 25..38, 2 px under the letter
#define C64KEYS_CELL_LEFT_CX  (SCREEN_WIDTH / 2 - 12)
#define C64KEYS_CELL_RIGHT_CX (SCREEN_WIDTH / 2 + 12)
// GLYPH_PLACE_TOP_LEFT: the same three glyphs packed against the left edge. The two
// 14 px cells sit at x 1..14 and 17..30 (centres 8 and 24, 2 px apart), and the letter
// is centred over them, so the whole key uses x 1..30 and leaves the right half free
// for an overlay icon.
#define C64KEYS_TL_CELL_LEFT_CX   8
#define C64KEYS_TL_CELL_RIGHT_CX  24
#define C64KEYS_TL_LETTER_CX      16

// The AltGr hint is drawn half-scale unless its ink is this tall or shorter. See
// the comment at the use site for why 7 is a measured gap and not a taste call.
#define ALTGR_HALF_MIN_INK_H 7
// Longest AltGr cell the halving scratch can hold. The longest in the LUT today is
// 9 codepoints (ps-AF KC_Z, five cursor nudges then the glyph); a longer one simply
// stays full size rather than being truncated.
#define ALTGR_HALF_MAX_LEN   12

// The MACRO caption band has two faces, largest first. _Nano_ 10px was the only one
// and is far smaller than the band can carry: "Macro 0" measures 40 px in a 72 px
// panel, and the caption is what a reader is meant to read. _Small_ 15px draws the
// same string at 57 px and still leaves 30 rows for the mark, which every stock
// numeral (4..19 px ink) clears with room to spare.
//
// ⚠️ A label too wide for _Small_ drops to _Nano_ with its TEXT INTACT rather than
// being truncated at the bigger face -- losing characters is worse than losing size,
// and the truncation loop below is the floor face's last resort, not the ladder's.
//
// ⚠️ extern, not #include: NotoSans_Medium_Base_8pt.h DEFINES the font (non-static),
// and each variant's status_oled.c already includes it. A second include here is a
// multiple-definition LINK error that compiles cleanly -- the pattern oled_helper.c
// uses for the same font, and the trap util_font.h/nano_font.h documented first.
extern const GFXfont NotoSans_Regular_Small_15px7b;

// Language region tab — the continent name centred in the keycap (continent
// silhouettes will replace the text later). The active-tab frame / inactive
// bottom bar is drawn separately by lang_draw_tab_indicator/bottom.
//
// In the _Small_ 15px face, the macro caption's ladder: the 10px _Nano_ face read as
// "really small" (hardware). Measured from the glyph tables, the widest label ("Mid
// East") is 60 px against the 66 px between the active frame's 3-px rails; _Mid_ 19px
// would be 77 px and overflow on four of the six. A future label too wide for _Small_
// drops to _Nano_ with its text intact rather than being clipped.
#define LANG_TAB_MAX_W  (SCREEN_WIDTH - 8)   // inside the rails, 1 px air each side

// Slide a hint's origin so its measured ink lands on the panel. The main legend
// gets this inside legend_plan_main(); the Shift preview and the AltGr hint call it
// here, so all three elements share ONE definition of the panel edge.
static inline void clamp_legend(int8_t* x, int8_t* y,
                                int8_t xmin, int8_t xmax, int8_t ymin, int8_t ymax) {
    legend_plan_clamp(&legend_plan_env, x, y, xmin, xmax, ymin, ymax);
}

static void render_macro_key(uint8_t id);   // defined below, used from render_key()

// Draws `mark` centred in the `free_rows` a caption left above it, and reports
// whether it landed.
//
// ⚠️ An ICON that overflows at its native size is HALVED rather than dropped, and
// that is the whole point of this function existing. A pack emoji is rendered at
// 40 px while a captioned keycap leaves about 29 rows, so the earlier
// native-size-or-nothing rule drew NOTHING for most icons a user could pick --
// measured over the picker's own set, four in five (field, 2026-08-27: "after
// selecting the icon I cannot see it ... also not on the keyboard"). The failure
// was silent on both ends because the host preview mirrors this placement.
//
// The 2x2-OR downsample keeps thin strokes that plain decimation loses, and half of
// even the tallest pack glyph is ~20 px, which fits any single-line caption.
static bool draw_macro_mark(const GFXfont* const* fonts, uint8_t count,
                            const uint32_t* mark, int8_t free_rows,
                            const GFXglyph* glyph, uint32_t cp) {
    int8_t xmin, xmax, ymin, ymax;
    kdisp_gfx_text_bbox(fonts, count, mark, &xmin, &xmax, &ymin, &ymax);
    const int8_t h = (int8_t)(ymax - ymin + 1);
    if (h < free_rows) {
        kdisp_write_gfx_text(fonts, count,
                             (int8_t)(BUFFER_X + (SCREEN_WIDTH - (xmax - xmin + 1)) / 2 - xmin),
                             (int8_t)((free_rows - h) / 2 - ymin), mark);
        return true;
    }
    if (glyph == NULL) return false;   // text marks (the index) are never rescaled
    // ⚠️ kdisp_draw_glyph_half_at takes the literal TOP-LEFT of the ink -- no baseline
    // align, no xOffset -- so the position comes from the glyph's own halved extents
    // and NOT from the bbox above. Halve rounding UP, or an odd-width glyph loses its
    // last column.
    const int8_t hw = (int8_t)((pgm_read_byte(&glyph->width) + 1) / 2);
    const int8_t hh = (int8_t)((pgm_read_byte(&glyph->height) + 1) / 2);
    if (hh >= free_rows) return false;
    kdisp_draw_glyph_half_at(fonts, count,
                             (int8_t)(BUFFER_X + (SCREEN_WIDTH - hw) / 2),
                             (int8_t)((free_rows - hh) / 2), cp);
    return true;
}


// True if `s` is a single bare combining mark (ignoring positioning control codes) —
// e.g. the Devanagari/Bengali nukta used as an AltGr "+nukta" hint. Drawn on its own
// such a mark is invisible, so the AltGr-held view composes it onto the base glyph.
static bool altgr_is_bare_combining(const uint32_t* s) {
    uint32_t cp = 0;
    for (; *s; ++s) {
        if (*s < 0x20) continue;          // skip preview-positioning control codes
        if (cp) return false;             // more than one visible glyph -> not a bare mark
        cp = *s;
    }
    return cp == 0x093C || cp == 0x09BC;  // Devanagari / Bengali nukta (extend as needed)
}

// Draws one glyph with its ink centred on column `cx` and its top on row `top`.
static void c64keys_glyph_at(uint32_t cp, int8_t cx, int8_t top, int8_t cy_radius) {
    const uint32_t s[2] = { cp, 0 };
    int8_t xmin, xmax, ymin, ymax;
    kdisp_gfx_text_bbox(g_all_fonts, g_all_font_count, s, &xmin, &xmax, &ymin, &ymax);
    const int8_t gx = (int8_t)(BUFFER_X + cx - (xmax - xmin + 1) / 2 - xmin);
    const int8_t gy = (int8_t)(top - ymin);
    kdisp_write_gfx_text_cy(g_all_fonts, g_all_font_count, gx, gy, s, cy_radius);
}

// Returns false, drawing nothing, when any of the three glyphs is missing (a fantasy
// bundle from before these fonts): the caller then draws the plain large letter.
static bool render_c64_keycap(uint16_t keycode, uint8_t place) {
    if (keycode < KC_A || keycode > KC_Z) return false;
    const uint32_t i      = (uint32_t)(keycode - KC_A);
    const uint32_t letter = C64KEYS_LETTER_BASE + i;
    const uint32_t cbm    = C64KEYS_PETSCII_BASE + i;
    const uint32_t shift  = C64KEYS_PETSCII_BASE + 26u + i;
    if (kdisp_gfx_glyph(g_all_fonts, g_all_font_count, letter) == NULL ||
        kdisp_gfx_glyph(g_all_fonts, g_all_font_count, cbm) == NULL ||
        kdisp_gfx_glyph(g_all_fonts, g_all_font_count, shift) == NULL) {
        return false;
    }
    const bool tl = place == GLYPH_PLACE_TOP_LEFT;
    c64keys_glyph_at(letter, tl ? C64KEYS_TL_LETTER_CX : SCREEN_WIDTH / 2, C64KEYS_LETTER_TOP,
                     KDISP_CY_DEFAULT);
    // Courtyard 0 for the cells: they sit 2 px under the letter, and the default 3 px
    // courtyard would erase the letter's bottom row.
    c64keys_glyph_at(cbm,   tl ? C64KEYS_TL_CELL_LEFT_CX  : C64KEYS_CELL_LEFT_CX,  C64KEYS_CELL_TOP, 0);
    c64keys_glyph_at(shift, tl ? C64KEYS_TL_CELL_RIGHT_CX : C64KEYS_CELL_RIGHT_CX, C64KEYS_CELL_TOP, 0);
    return true;
}

static bool glyph_size_remap(uint8_t size, const uint32_t* text, uint32_t* out, uint8_t out_cap) {
    return legend_plan_remap(&legend_plan_env, size, text, out, out_cap);
}

static inline void draw_main_legend(const main_legend_t* p) {
    kdisp_write_gfx_text(g_all_fonts, g_all_font_count, p->x, p->y, p->text);
}

bool render_key(uint16_t keycode, led_t state, uint8_t mods) {
    // ⚠️ A mod-tap's LEGEND is its tap keycode's legend. to_static_text() unwraps
    // this one function away, and update_displays() consults render_key() exactly
    // when to_static_text() returned NULL -- which is every letter, since the
    // language translation lives down here. So without the same unwrap a
    // RSFT_T(KC_A) fell through every branch below (is_letter is false for 0x3204,
    // and translate_keycode() has no row for it) and the keycap drew NO letter at
    // all: only the mod-tap hint badge, floating in an empty cell (field, 2026-08-18).
    // The two legend producers have to agree; keep the unwrap in both.
    keycode = poly_mt_tap(keycode);

    const poly_layer_t* local_layer = get_local_layer();

    const bool shift = ((local_layer->mods & MOD_MASK_SHIFT) != 0);
    const bool add_lang = get_highest_layer(local_layer->layer)==_ADDLANG1;
    const bool picker_mod = ((local_layer->mods & LATIN_PICKER_MOD) != 0);
    const bool is_letter = keycode>=KC_A && keycode<=KC_Z;

    // --- letter-remap mode: the board becomes a "which key?" / "which letter?"
    // prompt.  Everything outside the set being chosen from blanks out, so the only
    // thing to look at is that set.
    if(add_lang && local_layer->remap_mode != LATIN_REMAP_OFF) {
        const int8_t  slot   = latin_target_slot(keycode);
        const bool    picked = (local_layer->remap_mode == LATIN_REMAP_PICKLTR) &&
                               (slot >= 0) && ((uint8_t)slot == local_layer->remap_target);
        // The two steps offer DIFFERENT sets. PICKKEY picks the key to change, so
        // every TARGET is live — the letters and the twelve punctuation keys alike.
        // PICKLTR picks the letter that key should host, and only a letter can be a
        // source, so the punctuation keys go dark again — EXCEPT the one that was
        // just picked, which has to stay visible (inverted) or the board stops
        // showing what is being changed.
        const bool selectable = (local_layer->remap_mode == LATIN_REMAP_PICKKEY) ? (slot >= 0)
                                                                                 : (is_letter || picked);
        if(!selectable) {
            return false;                   // blank — nothing else is pressable now
        }
        // ⚠️ Render the inversion, do NOT call kdisp_invert(): matrix_scan_kb toggles
        // that on every press/release independently of process_record, so a latched
        // indicator driven through it is undone by the next keypress.  Fill the
        // ground and erase the glyph out of it instead — and with cy_radius 0, or
        // the courtyard clear eats a dark halo and the key reads as outlined.
        if(picked) {
            kdisp_set_buffer(0xFF);
            kdisp_set_gfx_erase(true);
        }
        // In PICKKEY every target still shows what it currently hosts (so you can
        // see what you are about to change); in PICKLTR the letters are the SOURCE
        // menu, so each shows its own plain letter.
        bool drawn;
        if(local_layer->remap_mode == LATIN_REMAP_PICKKEY) {
            const uint32_t* variation = latin_variation(keycode, shift || state.caps_lock);
            if(variation == NULL) {
                // A punctuation key that has not been mapped yet hosts nothing —
                // draw its own symbol, so the set you are choosing from reads as the
                // actual keyboard rather than a row of holes.
                variation = translate_keycode(get_local_state()->lang, keycode,
                                              shift, state.caps_lock);
            }
            drawn = (variation != NULL);
            if(drawn) {
                kdisp_write_gfx_text_cy(g_all_fonts, g_all_font_count, BUFFER_X, 23, variation, 0);
            }
        } else if(is_letter) {
            const uint32_t letter[2] = { (uint32_t)('a' + (keycode - KC_A)), 0 };
            kdisp_write_gfx_text_cy(g_all_fonts, g_all_font_count, BUFFER_X, 23, letter, 0);
            drawn = true;
        } else {
            // Only reachable for a PICKED punctuation target (see `selectable`), so
            // it must draw its own symbol -- 'a' + (keycode - KC_A) would be a
            // nonsense codepoint here.
            const uint32_t* sym = translate_keycode(get_local_state()->lang, keycode,
                                                    shift, state.caps_lock);
            drawn = (sym != NULL);
            if(drawn) {
                kdisp_write_gfx_text_cy(g_all_fonts, g_all_font_count, BUFFER_X, 23, sym, 0);
            }
        }
        if(picked) {
            kdisp_set_gfx_erase(false);     // static flag — leaving it on blanks every later key
            drawn = true;                   // the filled ground IS the render
        }
        return drawn;
    }

    // ⚠️ Gated on "hosts a row", not on is_letter: a punctuation key that has been
    // remapped shows its host letter's variation here, and an unmapped one must
    // fall THROUGH to the normal translate_keycode path below so it keeps drawing
    // (and typing) its own symbol.
    if(add_lang && latin_has_row(keycode)) {
        //display the previously selected latin variation of the letter
        const uint32_t* variation = latin_variation(keycode, shift || state.caps_lock);
        if(variation!=NULL) {
            // The variation IS this key's main legend on this layer (there is no
            // preview to share the cell with), so it follows the legend size too.
            uint32_t      scratch[GLYPH_SIZE_MAX_LEN + 1];
            main_legend_t plan;
            plan_main_legend(variation, BUFFER_X, 23, scratch, (uint8_t)(GLYPH_SIZE_MAX_LEN + 1), &plan);
            draw_main_legend(&plan);
            return true;
        }
        return false;
    }

    //variation selection on the picker row
    uint16_t local_last_latin_keycode = get_local_last_latin_keycode();
    const bool picker_open = add_lang && picker_mod && latin_has_row(local_last_latin_keycode);
    const int8_t picker_slot = latin_picker_slot(keycode);
    if(picker_slot >= 0) {
        if(picker_open) {
            //show the alternatives on the current page for the selected latin letter
            const uint8_t row = latin_picker_row(local_last_latin_keycode, shift || state.caps_lock);
            const int8_t  idx = latin_picker_index(row, local_layer->picker_page, picker_slot);
            if(idx >= 0) {
                kdisp_write_gfx_text(g_all_fonts, g_all_font_count, BUFFER_X, 23, latin_ex_map[row][idx]);
                return true;
            }
        }
        return false;
    }
    if(keycode==KC_LAT_PAGE_PREV || keycode==KC_LAT_PAGE_NEXT) {
        // Blank unless this letter actually pages — same rule (and legend: arrow plus
        // "page/pages") as the language and emoji layers' page keys, so a row that
        // fits on one page shows nothing rather than a control that does nothing.
        if(picker_open) {
            const uint8_t pages = latin_page_count(latin_picker_row(local_last_latin_keycode, shift || state.caps_lock));
            if(pages > 1) {
                // Same clamp as latin_picker_index(): the case may have changed since
                // the page was set, and the page shown must be the one the slots show.
                const uint8_t page = (local_layer->picker_page < pages) ? local_layer->picker_page
                                                                        : (uint8_t)(pages - 1);
                kdisp_write_gfx_text(g_all_fonts, g_all_font_count, BUFFER_X, 23,
                                     kdisp_page_key_legend(keycode==KC_LAT_PAGE_PREV ? ICON_LEFT[0] : ICON_RIGHT[0],
                                                           page, pages));
                return true;
            }
        }
        return false;
    }

    const poly_sync_t* local_state = get_local_state();

    // Glyph-script override: on the normal language layer, replace a letter/digit's
    // legend with the selected alternative script (e.g. Tengwar), centered in the
    // keycap. Overlays and OS-hints are drawn elsewhere and are untouched. Falls
    // through to the normal legend when the script has no glyph for this key OR the
    // font-pack bundle providing it is not present (so a keyboard without the
    // "fantasy" bundle still shows Latin rather than blanks). While AltGr is held we
    // fall through to the real AltGr symbol (translate_keycode_only_altgr below) —
    // the override is only for the resting/base letter legend, and the AltGr output
    // is a genuinely different character, not a cased form of the same letter.
    if (local_state->glyph_script != GLYPH_STD && !add_lang && !(mods & MOD_RALT)) {
        const uint8_t place = local_state->glyph_place;
        if (local_state->glyph_script == GLYPH_C64KEYS && render_c64_keycap(keycode, place)) {
            return true;
        }
        uint32_t cp = glyph_script_codepoint(local_state->glyph_script, keycode);
        if (cp != 0 && kdisp_gfx_glyph(g_all_fonts, g_all_font_count, cp) != NULL) {
            const uint32_t s[2] = { cp, 0 };
            // Center the glyph in BOTH axes from its full pixel bbox, rather than
            // drawing at the base font's fixed y=23 baseline. The script fonts are
            // rendered taller (~30 px) than the 14 px Latin base, so at baseline 23
            // a tall glyph's top (23 + yOffset) lands above 0 and clips off the top
            // of the 40 px display (Amiga/C64/APL ran ~10 px off-screen). bbox gives
            // the y extent too, so we place the baseline to fit — same idea the idle
            // jitter uses (roll_idle_offset).
            int8_t xmin, xmax, ymin, ymax;
            kdisp_gfx_text_bbox(g_all_fonts, g_all_font_count, s, &xmin, &xmax, &ymin, &ymax);
            int8_t gx = (int8_t)(BUFFER_X + (SCREEN_WIDTH  - (xmax - xmin + 1)) / 2 - xmin);
            int8_t gy = (int8_t)(         (SCREEN_HEIGHT - (ymax - ymin + 1)) / 2 - ymin);
            if (place == GLYPH_PLACE_TOP_LEFT) {
                // cmd 30 v26: the ink's top-left corner at (1, 1), leaving the right of
                // the keycap free for an overlay icon and its 3 px courtyard.
                gx = (int8_t)(BUFFER_X + 1 - xmin);
                gy = (int8_t)(1 - ymin);
            }
            kdisp_write_gfx_text(g_all_fonts, g_all_font_count, gx, gy, s);
            return true;
        }
    }

    // Macro keys draw their own cell: the index, and the label along the bottom edge.
    // Reached because to_static_text() has no case for QK_MACRO_*, which is exactly the
    // seam update_displays() uses -- a key WITH a legend never gets here.
    if (keycode >= QK_MACRO && keycode <= QK_MACRO_MAX) {
        // Shift reaches the second bank (see poly_macro_banked_id). `shift` here is the
        // SYNCED modifier state, not get_mods(): the slave draws the macro keys that
        // land on its own half and only ever sees poly_layer_t.
        const uint8_t id = poly_macro_banked_id((uint8_t)(keycode - QK_MACRO), shift);
        if (id != POLY_MACRO_NONE) {
            render_macro_key(id);
        }
        // A shifted slot with nothing behind it draws BLANK rather than "M16" -- the
        // keycap is the documentation for how far the second bank goes.
        return true;
    }

    if (mods & MOD_RALT) {
        const uint32_t* letter = translate_keycode_only_altgr(local_state->lang, keycode);
        if (letter != NULL) {
            // Draw it WHERE THE RESTING VIEW DRAWS THE HINT: this key's own category
            // and its VAR_ALTGR offsets, so holding AltGr enlarges the glyph in place
            // instead of teleporting it across the keycap. Measured over the 1719
            // AltGr cells, 1148 (67%) land exactly on the hint's position and the
            // rest are pulled back a median 5 px by the clamp below — which is the
            // only reason this is safe to do at all (worst: the ar-* KC_F letters at
            // 34-38 px, where a wide script glyph cannot fit at x=+55).
            //
            // ⚠️ This replaced a defensive MIXTURE that predates the clamp: the
            // NUM-or-SYM category even on a LETTER key, H from VAR_SMALL, and V as
            // PK_MIN(VAR_SMALL, VAR_ALTGR). That kept the glyph near the base legend
            // where it could not fall off, at the cost of using offsets belonging to
            // a different category and a different variation than the glyph drawn.
            int8_t v_set;
            int8_t h_set;
            int8_t held_v_set;   // {<cat>.heldvoffset} — the delta from the hint position
            int8_t held_h_set;   // {<cat>.heldhoffset}
            if(is_letter) {
                v_set      = SETTING_LETTER_VOFFSET;
                h_set      = SETTING_LETTER_HOFFSET;
                held_v_set = SETTING_LETTER_HELDVOFFSET;
                held_h_set = SETTING_LETTER_HELDHOFFSET;
            } else {
                const bool is_num = keycode>=KC_1 && keycode<=KC_0; // yes the first is 1 and the last is 0
                v_set      = is_num ? SETTING_NUM_VOFFSET      : SETTING_SYM_VOFFSET;
                h_set      = is_num ? SETTING_NUM_HOFFSET      : SETTING_SYM_HOFFSET;
                held_v_set = is_num ? SETTING_NUM_HELDVOFFSET  : SETTING_SYM_HELDVOFFSET;
                held_h_set = is_num ? SETTING_NUM_HELDHOFFSET  : SETTING_SYM_HELDHOFFSET;
            }
            // HIDE on either axis falls through to the resting legend, as before —
            // hiding the hint says "do not show the AltGr" for this layout. Measured:
            // no layout hides its AltGr offsets today, and H/V never disagree, so
            // this is a semantic tidy-up rather than a behaviour change.
            int8_t v_off = get_setting(v_set, local_state->lang, VAR_ALTGR);
            int8_t h_off = get_setting(h_set, local_state->lang, VAR_ALTGR);
            if(v_off!=HIDE_KEY && h_off!=HIDE_KEY) {
                // A bare combining mark (the nukta "+nukta" AltGr hint) is invisible on
                // its own when AltGr is actually held — compose it onto the base
                // consonant (क + ़ = क़) so the held view shows the real output. The
                // unshifted preview still draws the lone dot via the cell's own controls.
                uint32_t composed[10];
                if (altgr_is_bare_combining(letter)) {
                    const uint32_t* base = translate_keycode(local_state->lang, keycode, false, false);
                    if (base != NULL) {
                        uint8_t ci = 0;
                        for (const uint32_t* p = base;   *p && ci < 8; ++p) composed[ci++] = *p;
                        for (const uint32_t* p = letter; *p && ci < 9; ++p) if (*p >= 0x20) composed[ci++] = *p;
                        composed[ci] = 0;
                        letter = composed;
                    }
                }
                // Clamp onto the panel, exactly as the resting view's three elements
                // do. This branch was the ONE element still drawn at a raw origin, and
                // it is the same defect the four-edge clamp closed for the small base
                // legend: the offsets here are the NUM/SYM ones, tuned against those
                // categories' own glyphs, while the glyph drawn is the AltGr cell's —
                // so a layout whose AltGr glyphs are wider or taller silently pushed
                // ink off an edge with nothing to report it. Measured over all 160
                // layouts: 191 keys / 1528 px across 69 layouts, down to the single
                // he-IL KC_BACKSLASH nikud that is 43 px tall in a 40 px panel.
                // {<cat>.heldhoffset} / {<cat>.heldvoffset}: a per-layout, per-category
                // DELTA from the hint position, defaulting to 0 so nothing moves until a
                // layout is tuned. It exists because the hint's own offsets place a glyph
                // that shares the keycap with the base legend and the Shift preview, while
                // the held view has the panel to itself — so the best spot is not
                // necessarily the same one, and that is a per-layout judgement.
                //
                // ⚠️ HIDE_KEY is treated as 0, NOT as -128. It is the tuner's "hide this
                // variation" sentinel and is meaningless for a delta; taken literally it
                // would fling the glyph a hundred pixels off the panel. Hiding the held
                // view is what the AltGr offsets above already do.
                int8_t held_h = get_setting(held_h_set, local_state->lang, VAR_ALTGR);
                int8_t held_v = get_setting(held_v_set, local_state->lang, VAR_ALTGR);
                if (held_h == HIDE_KEY) held_h = 0;
                if (held_v == HIDE_KEY) held_v = 0;

                int8_t axmin, axmax, aymin, aymax;
                // ⚠️ SATURATE, don't narrow. Three int8_t values are summed here and the
                // promoted result can leave int8_t range: ps-AF already carries a letter
                // H offset of 65, so 28+65 leaves only +34 of headroom and a hand-tuned
                // held delta of +35 would WRAP to a negative x — drawing the glyph at the
                // wrong place instead of letting clamp_legend pin it at the edge. That is
                // exactly the failure kdisp_sat8()'s comment describes, so use it rather
                // than bounding the tuner's input and hoping the spreadsheet agrees.
                int8_t hx = kdisp_sat8((int16_t)(28+h_off+held_h));
                int8_t hy = kdisp_sat8((int16_t)(23+v_off+held_v));
                kdisp_gfx_text_bbox(g_all_fonts, g_all_font_count, letter,
                                    &axmin, &axmax, &aymin, &aymax);
                clamp_legend(&hx, &hy, axmin, axmax, aymin, aymax);
                kdisp_write_gfx_text(g_all_fonts, g_all_font_count, hx, hy, letter);
                return true;
            }
        }
    }

    //translate to current language
    const uint32_t* letter = translate_keycode(local_state->lang, keycode, shift, state.caps_lock);
    if (letter != NULL) {
        int8_t v_set;
        int8_t h_set;
        int8_t half_set;   // {<cat>.altgrhalf} — the per-layout half-size AltGr opt-in
        if(is_letter) {
            v_set    = SETTING_LETTER_VOFFSET;
            h_set    = SETTING_LETTER_HOFFSET;
            half_set = SETTING_LETTER_ALTGRHALF;
        } else {
            const bool is_num = keycode>=KC_1 && keycode<=KC_0; // yes the first is 1 and the last is 0
            if(is_num){
                v_set    = SETTING_NUM_VOFFSET;
                h_set    = SETTING_NUM_HOFFSET;
                half_set = SETTING_NUM_ALTGRHALF;
            } else {
                v_set    = SETTING_SYM_VOFFSET;
                h_set    = SETTING_SYM_HOFFSET;
                half_set = SETTING_SYM_ALTGRHALF;
            }
        }
        int8_t v_small = get_setting(v_set, local_state->lang, VAR_SMALL);
        int8_t h_small = get_setting(h_set, local_state->lang, VAR_SMALL);
        int8_t base_x = 28+h_small;
        int8_t base_v = v_small;

        // Plan the base legend first: at a bigger legend size it is a different,
        // relocated glyph at a different origin, and everything below lays out
        // around its ink. At GLYPH_SIZE_S the plan is exactly the old placement.
        uint32_t      base_scratch[GLYPH_SIZE_MAX_LEN + 1];
        main_legend_t base_plan;
        plan_main_legend(letter, base_x, (int8_t)(23+base_v), base_scratch,
                         (uint8_t)(GLYPH_SIZE_MAX_LEN + 1), &base_plan);

        // Resolve the shift preview BEFORE drawing, so a wide base + wide preview
        // (e.g. the Arabic SAD/DAD key — both ~39 px) can be laid out as a pair:
        // the preview is placed clear of the base and clamped on screen; if it then
        // still has to overlap (two such glyphs cannot both fit a 72 px window) the
        // flat base is lifted and the preview dropped so the two read diagonally
        // instead of as one connected glyph.  Only this unshifted preview view is
        // affected — when shift is held there is no preview and the active glyph
        // keeps the normal VAR_SMALL baseline, so tall letters / high marks never
        // clip.  All of this is generic and glyph-width driven (no per-language code).
        //
        // ⚠️ The preview stays at the SMALL size by design: a keycap has room for one
        // big thing, and the whole point of the size setting is the main legend.
        const uint32_t* shift_letter = NULL;
        int8_t preview_x = 0, preview_y = 0;
        int8_t pmin = 0, pmax = 0, pymin = 0, pymax = 0;
        if(!shift && !state.caps_lock) {
            int8_t v_pv = get_setting(v_set, local_state->lang, VAR_SHIFT);
            int8_t h_pv = get_setting(h_set, local_state->lang, VAR_SHIFT);
            if(v_pv!=HIDE_KEY && h_pv!=HIDE_KEY) {
                shift_letter = translate_keycode_only_shift(local_state->lang, keycode);
                if (shift_letter != NULL) {
                    kdisp_gfx_text_bbox(g_all_fonts, g_all_font_count, shift_letter,
                                        &pmin, &pmax, &pymin, &pymax);
                    preview_x = 28+h_pv;
                    if (preview_x + pmin < base_plan.ink_max + 2)         // keep clear of the base
                        preview_x = base_plan.ink_max + 2 - pmin;
                    preview_y = (int8_t)(23 + v_pv);
                    clamp_legend(&preview_x, &preview_y, pmin, pmax, pymin, pymax);
                    if (preview_x + pmin <= base_plan.ink_max) {          // forced to overlap -> stagger
                        // Both moves are re-clamped: the panel edge wins over the
                        // stagger, which is a readability nicety and not worth a
                        // clipped glyph. (Before the clamp covered the small base
                        // too, this lift could push a tall flat legend off the top.)
                        if (!base_plan.big) {
                            base_plan.y -= 6;                             // lift the flat base
                            clamp_legend(&base_plan.x, &base_plan.y, base_plan.box_xmin,
                                         base_plan.box_xmax, base_plan.box_ymin, base_plan.box_ymax);
                            base_plan.ink_min = (int8_t)(base_plan.x + base_plan.box_xmin);
                            base_plan.ink_max = (int8_t)(base_plan.x + base_plan.box_xmax);
                        }
                        preview_y += 4;                                   // drop the preview
                        clamp_legend(&preview_x, &preview_y, pmin, pmax, pymin, pymax);
                    }
                }
            }
        }

        // Resolve the AltGr hint BEFORE anything is drawn, for the same reason the
        // shift preview above is resolved first: the two hints have to be laid out
        // as a pair (see the separation block below).
        const uint32_t* alt_letter = NULL;
        int8_t alt_x = 0, alt_y = 0, aymin = 0, aymax = 0;
        uint32_t alt_scratch[ALTGR_HALF_MAX_LEN + 2];   // HINT_SMALL + the cell + NUL
        letter = translate_keycode_only_altgr(local_state->lang, keycode);
        if (letter != NULL) {
            int8_t v_off = get_setting(v_set, local_state->lang, VAR_ALTGR);
            int8_t h_off = get_setting(h_set, local_state->lang, VAR_ALTGR);
            if(v_off!=HIDE_KEY && h_off!=HIDE_KEY) {
                int8_t amin, amax;
                kdisp_gfx_text_bbox(g_all_fonts, g_all_font_count, letter, &amin, &amax, &aymin, &aymax);

                // The AltGr glyph is a HINT — what this key would type under a
                // modifier nobody is holding — so on a script whose letters fill the
                // keycap it is drawn at HALF size: it reads as subordinate to the
                // base legend, and a full-size script glyph is most of what made the
                // two hints fight for the right-hand side.
                //
                // ⚠️ WHICH layouts is DATA, not a size test, and the measurement is
                // why. The intuition is "Arabic and Indic have very large glyphs",
                // but AltGr ink HEIGHT does not separate them at all — median 20 px
                // on Arabic letters against 21 px on Latin ones. What is actually
                // different is that on those layouts the base and the Shift hint are
                // wide too, so the row reads crowded. That is a per-LAYOUT judgement
                // no glyph measurement can make, so it lives where layout decisions
                // live: `{letter|num|sym.altgrhalf}` in lang_lut.xlsx, one cell per
                // language PER CATEGORY — the same three-way split the H/V offsets
                // already use, so a layout can halve its letters while its digit and
                // symbol rows keep full-size hints. Flip one by editing that cell,
                // not by tuning a threshold here. Only `{letter.…}` is set today.
                //
                // ⚠️ The size test that REMAINS is only the mark guard: halving a
                // glyph that is already tiny destroys it — a Hebrew nikud is 2×3 px
                // and comes out a dot. That threshold IS measured: over the 318
                // distinct AltGr cells the ink-height histogram has an EMPTY BIN at
                // 8 px, marks below it (44 cells — nikud, diaeresis, middle dot,
                // hyphen) and letterforms from 9 px up (274). It matters most on the
                // Indic layouts, whose letter AltGr hints are mostly bare combining
                // marks — median 4 px — so most of them stay full size even here.
                if (get_setting(half_set, local_state->lang, VAR_ALTGR) != 0
                    && aymax - aymin + 1 > ALTGR_HALF_MIN_INK_H && letter[0] != U'\x10') {
                    uint8_t n = 0;
                    while (n < ALTGR_HALF_MAX_LEN && letter[n] != 0) n++;
                    if (letter[n] == 0) {        // fits the scratch; else stay full size
                        alt_scratch[0] = U'\x10';    // HINT_SMALL
                        for (uint8_t i = 0; i < n; ++i) alt_scratch[i + 1] = letter[i];
                        alt_scratch[n + 1] = 0;
                        letter = alt_scratch;
                        kdisp_gfx_text_bbox(g_all_fonts, g_all_font_count, letter,
                                            &amin, &amax, &aymin, &aymax);
                    }
                }
                alt_x = 28+h_off;
                // At the small size this mark is kept off the legend by its VERTICAL
                // offset — it sits below the base glyph. A big legend fills that
                // height, so there the only separation left is horizontal: push it
                // clear of the base's ink, exactly as the shift preview does.
                if (base_plan.big && alt_x + amin < base_plan.ink_max + 2)
                    alt_x = (int8_t)(base_plan.ink_max + 2 - amin);
                alt_y = (int8_t)(23 + v_off);
                clamp_legend(&alt_x, &alt_y, amin, amax, aymin, aymax);
                alt_letter = letter;

                // Keep the two hints off EACH OTHER. Both sit right of the base —
                // shift upper, AltGr lower — and it is their VERTICAL offsets that
                // hold them apart. True for a narrow Latin pair, false for a tall
                // script: on every ar-* KC_F the shift tick lands inside the AltGr's
                // 29 px box, and bn-BD KC_D shares 57 px. A per-language offset
                // cannot fix that — the room left over is decided by the WIDTH of
                // this key's three glyphs, so one number per language would have to
                // satisfy the worst key and would crush the rest into the base.
                //
                // The base is bottom-left and narrow on exactly these keys, so the
                // free space is between it and the (right-clamped) AltGr: pull the
                // shift LEFT into that gap, never past the base's own 2 px margin,
                // and never to the right — which could only walk it into the clamp.
                // Where three wide glyphs genuinely do not fit on 72 px the pull
                // still shrinks the overlap rather than removing it.
                if (shift_letter != NULL) {
                    const int8_t sy0 = (int8_t)(preview_y + pymin);
                    const int8_t sy1 = (int8_t)(preview_y + pymax);
                    const int8_t ay0 = (int8_t)(alt_y + aymin);
                    const int8_t ay1 = (int8_t)(alt_y + aymax);
                    if (preview_x + pmin <= alt_x + amax && alt_x + amin <= preview_x + pmax
                        && sy0 <= ay1 && ay0 <= sy1) {
                        int16_t want  = (int16_t)(alt_x + amin - 2 - pmax);
                        const int16_t floor_x = (int16_t)(base_plan.ink_max + 2 - pmin);
                        if (want < floor_x) want = floor_x;
                        if (want < preview_x) preview_x = (int8_t)want;
                        // The pull only ever moves LEFT, so only the west edge can be
                        // violated — but re-clamp through the shared helper rather
                        // than open-coding that one test here.
                        clamp_legend(&preview_x, &preview_y, pmin, pmax, pymin, pymax);
                    }
                }
            }
        }

        draw_main_legend(&base_plan);
        if (shift_letter != NULL)
            kdisp_write_gfx_text(g_all_fonts, g_all_font_count, preview_x, preview_y, shift_letter);
        if (alt_letter != NULL)
            kdisp_write_gfx_text(g_all_fonts, g_all_font_count, alt_x, alt_y, alt_letter);
        return true;
    }
    return false;
}

// Compose the prompt keycap into the scratch buffer: a big 2x letter above a tiny
// caption. Everything is measured from the font metrics rather than hardcoded —
// "REJECT" descends 2px below the baseline (the J) where "ACCEPT" does not, so a
// fixed bottom baseline would clip one of them.
// The slot picker: while the gesture is open the board becomes a list of macros, so
// choosing one is a keypress rather than a number to remember. Draws the slot exactly
// as it will look when recorded -- render_macro_key() -- with a frame around the one
// that is armed, the same way the language layer frames the selected language.
//
// ⚠️ Reuses render_macro_key rather than drawing "M3": the whole point of a picker on
// a display keyboard is that you pick the CAPTION, and duplicating the composition
// here is how the two would drift.
void render_macro_picker_key(uint8_t id, bool armed) {
    render_macro_key(id);
    if (armed) {
        kdisp_draw_round_rect(BUFFER_X, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 4);
    }
}

// Compose a macro keycap: the macro's index above, its label along the bottom edge.
//
// Same split as render_lang_flag_key -- identity on top, a tiny _Nano_ caption below --
// because it is the shape this board already uses for "what is this, and which one".
// The INDEX rather than a generic macro glyph: a generic glyph would be identical on
// all sixteen keys, so it says "this is a macro" and nothing else, while the index says
// which one and needs no font pack (the pack glyphs are the one thing a fresh keyboard
// might not have). When a label is set it carries the meaning and the index shrinks
// out of the way; when it is not, the index is all there is, so it takes the cell.
//
// The label is truncated by MEASURED WIDTH, never by character count: in this face a
// 'W' is about three times an 'i', so a fixed cut either clips a wide label off the
// panel or wastes half the band on a narrow one. The host runs the same measurement in
// its editor, so what you type is what the keycap shows.
// Draw `text` centred in the whole cell at the largest face that fits it, biggest
// first. Returns false when even the smallest face overflows, which the caller treats
// as "nothing to draw" rather than clipping.
//
// The ladder is the point: a caption alone has the entire 72x40 to spend, and the
// _Nano_ 10 px face the bottom band uses would waste it. The two big tiers live in the
// `latinbig` PACK bundle, so they are simply absent on a keyboard with no font pack --
// glyph_size_remap() is all-or-nothing and returns false there, and the ladder walks
// past them to the resident faces. That is why the resident 27 px face is on the list
// rather than being the assumed floor.
static bool draw_macro_caption_big(const uint32_t* text) {
    uint32_t scratch[POLY_MACRO_LABEL_LEN + 1];

    for (uint8_t tier = 0; tier < 4; tier++) {
        const GFXfont* const* fonts = NULL;
        uint8_t               count = 0;
        const uint32_t*       run   = text;

        switch (tier) {
            case 0:
            case 1:
                // GLYPH_SIZE_L then _M, relocated into the supplementary PUA planes the
                // latinbig entries are emitted at. Same helper the legend-size feature
                // uses, so a caption cannot end up at a size the legends cannot reach.
                if (!glyph_size_remap((uint8_t)(tier == 0 ? GLYPH_SIZE_L : GLYPH_SIZE_M),
                                      text, scratch,
                                      (uint8_t)(POLY_MACRO_LABEL_LEN + 1))) {
                    continue;
                }
                run   = scratch;
                fonts = g_all_fonts;
                count = g_all_font_count;
                break;
            case 2:
                // The resident latin face (~20 px caps). Always present.
                fonts = g_all_fonts;
                count = g_all_font_count;
                break;
            default:
                fonts = mid_fonts;
                count = 1;
                break;
        }

        int8_t xmin, xmax, ymin, ymax;
        kdisp_gfx_text_bbox(fonts, count, run, &xmin, &xmax, &ymin, &ymax);
        const int16_t w = (int16_t)(xmax - xmin + 1);
        const int16_t h = (int16_t)(ymax - ymin + 1);
        if (w > SCREEN_WIDTH || h > SCREEN_HEIGHT) continue;

        kdisp_write_gfx_text(fonts, count,
                             (int8_t)(BUFFER_X + (SCREEN_WIDTH - w) / 2 - xmin),
                             (int8_t)((SCREEN_HEIGHT - h) / 2 - ymin),
                             run);
        return true;
    }
    return false;
}

static void render_macro_key(uint8_t id) {
    poly_macro_look_t look;
    poly_macro_look_get(id, &look);
    const char *label = look.text;

    // Widen the caption to codepoints once -- both the big-text style and the captioned
    // styles below need it, and the display list wants uint32_t anyway.
    uint32_t text[POLY_MACRO_LABEL_LEN + 1];
    uint8_t  tlen = 0;
    for (; tlen < POLY_MACRO_LABEL_LEN && label[tlen] != '\0'; tlen++) {
        text[tlen] = (uint32_t)(uint8_t)label[tlen];
    }
    text[tlen] = 0;

    // Caption alone, filling the cell. Falls through to the captioned styles when the
    // caption is empty (there would be nothing to draw) or when even the smallest face
    // overflows -- an empty keycap is worse than the small one it replaced.
    if (look.style == POLY_MACRO_STYLE_TEXT && tlen > 0) {
        if (draw_macro_caption_big(text)) return;
    }

    // "M12" -- built by hand rather than snprintf, which is not worth linking for two
    // digits, and the display list wants uint32_t codepoints anyway.
    uint32_t index_text[4];
    uint8_t  n = 0;
    index_text[n++] = (uint32_t)'M';
    if (id >= 10) index_text[n++] = (uint32_t)('0' + id / 10);
    index_text[n++] = (uint32_t)('0' + id % 10);
    index_text[n]   = 0;

    // The mark above the caption: a chosen glyph, or the index. The icon is looked up
    // through its OWN single-font array once found, because kdisp_write_gfx_char
    // baseline-aligns every glyph to fonts[0] -- with g_all_fonts that is IconsFont
    // (yAdvance 40) and a taller pack glyph is shifted down by the difference, which is
    // exactly the language-flag gap-at-top regression. An icon this keyboard has no
    // glyph for falls back to the index rather than drawing nothing, so a caption
    // picked on a host with a richer font pack still names its macro here.
    const GFXfont*       icon_font  = NULL;
    const GFXglyph*      icon_glyph = NULL;
    const GFXfont*       icon_arr[1];
    const GFXfont* const* mark_fonts = mid_fonts;
    uint8_t              mark_count  = 1;
    uint32_t             mark_text[2];
    const uint32_t*      mark = index_text;

    if ((look.style == POLY_MACRO_STYLE_ICON || look.style == POLY_MACRO_STYLE_ICON_ONLY)
        && look.icon != 0) {
        icon_glyph = kdisp_gfx_glyph_font(g_all_fonts, g_all_font_count, look.icon,
                                          &icon_font);
        if (icon_glyph != NULL) {
            icon_arr[0]  = icon_font;
            mark_fonts   = (const GFXfont* const*)icon_arr;
            mark_count   = 1;
            mark_text[0] = look.icon;
            mark_text[1] = 0;
            mark         = mark_text;
        }
    }

    // ICON_ONLY draws the icon alone, centred in the whole cell, exactly as an
    // uncaptioned key does -- the caption is KEPT in storage so switching back does
    // not lose it, it is simply not drawn. An icon this keyboard has no glyph for
    // leaves icon_glyph NULL and falls through to the captioned index, which is the
    // same fallback every other icon path takes: a keycap is never left blank.
    const bool icon_only = (look.style == POLY_MACRO_STYLE_ICON_ONLY && icon_glyph != NULL);

    if (label[0] == '\0' || icon_only) {
        // No caption to place around: centre the mark in the whole cell. No fit check
        // and no halving here -- the tallest pack glyph is exactly SCREEN_HEIGHT, and
        // filling the cell is the point of this branch.
        int8_t ixmin, ixmax, iymin, iymax;
        kdisp_gfx_text_bbox(mark_fonts, mark_count, mark, &ixmin, &ixmax, &iymin, &iymax);
        kdisp_write_gfx_text(mark_fonts, mark_count,
                             (int8_t)(BUFFER_X + (SCREEN_WIDTH - (ixmax - ixmin + 1)) / 2 - ixmin),
                             (int8_t)((SCREEN_HEIGHT - (iymax - iymin + 1)) / 2 - iymin),
                             mark);
        return;
    }

    // Pick the caption face: the larger one when the WHOLE label fits it, else the
    // floor face. A single-font array either way, so kdisp_write_gfx_char's baseline
    // align is a no-op and the two cannot land on different baselines.
    int8_t lxmin = 0, lxmax = 0, lymin = 0, lymax = 0;
    const GFXfont* cap_arr[1] = { &NotoSans_Regular_Small_15px7b };
    kdisp_gfx_text_bbox(cap_arr, 1, text, &lxmin, &lxmax, &lymin, &lymax);
    if ((int16_t)(lxmax - lxmin + 1) > SCREEN_WIDTH) {
        cap_arr[0] = lang_label_fonts[0];
    }

    // Drop trailing characters until the caption fits the panel. Measuring after each
    // drop rather than estimating from a per-character width is the point -- the face
    // is proportional, so an estimate is wrong in both directions.
    uint8_t len = tlen;
    while (len > 0) {
        kdisp_gfx_text_bbox(cap_arr, 1, text, &lxmin, &lxmax, &lymin, &lymax);
        if ((int16_t)(lxmax - lxmin + 1) <= SCREEN_WIDTH) break;
        text[--len] = 0;
    }
    if (len == 0) return;

    // Pin the caption's lowest lit pixel to the last screen row, exactly as the FW-2
    // prompt does -- the descender budget differs per string, so a fixed baseline
    // clips whichever label happens to carry a 'y'.
    const int8_t cap_base  = (int8_t)(SCREEN_HEIGHT - 1 - lymax);
    const int8_t free_rows = (int8_t)(cap_base + lymin);   // rows above the caption

    kdisp_write_gfx_text(cap_arr, 1,
                         (int8_t)(BUFFER_X + (SCREEN_WIDTH - (lxmax - lxmin + 1)) / 2 - lxmin),
                         cap_base, text);

    // The mark, centred in whatever the caption left: native size, else halved.
    // An icon that fits at NO size falls back to the index, which always does --
    // the same fallback a missing glyph already takes, so an icon can never leave
    // the keycap without a mark.
    if (!draw_macro_mark(mark_fonts, mark_count, mark, free_rows, icon_glyph, look.icon)
        && icon_glyph != NULL) {
        draw_macro_mark(mid_fonts, 1, index_text, free_rows, NULL, 0);
    }
}

// The "Preset" / "Clear" MRU control keys that bracket the top recents row.
// The label sits next to the recents (Preset right-aligned on the left corner,
// Clear left-aligned on the right corner) with an arrow pointing into the row.
// (lang_label_fonts is defined above render_lang_flag_key, in key_render_helpers.c.)
static void render_mru_ctrl_key(bool preset) {
    if (preset) {
        kdisp_write_gfx_text(lang_label_fonts, 1, BUFFER_X + 14, 18, U"Preset");
        kdisp_write_gfx_text(g_all_fonts, g_all_font_count, BUFFER_X + 44, 23, ICON_RIGHT);
    } else {
        // Mirror of "Preset": back-arrow points left into the recents row, label in
        // the small keycap-label font (not the full-size font, which overflowed).
        kdisp_write_gfx_text(g_all_fonts, g_all_font_count, BUFFER_X + 10, 23, ICON_LEFT);
        kdisp_write_gfx_text(lang_label_fonts, 1, BUFFER_X + 26, 18, U"Clear");
    }
}

static void render_lang_region_tab(uint16_t keycode) {
    static const GFXfont* const small[] = { &NotoSans_Regular_Small_15px7b };
    const uint32_t* label = lang_region_label((uint8_t)(keycode - KC_LANG_CAT_BASE));
    const GFXfont* const* f = small;
    int8_t lo = 0, hi = 0;
    kdisp_gfx_text_bounds(f, 1, label, &lo, &hi);
    int8_t baseline = 25;   // cap height 11 rows centred in rows 3..36 (frame/underline clear)
    if (hi - lo + 1 > LANG_TAB_MAX_W) {
        f        = lang_label_fonts;
        baseline = 22;
        kdisp_gfx_text_bounds(f, 1, label, &lo, &hi);
    }
    const int8_t w = (int8_t)(hi - lo + 1);
    const int8_t x = (int8_t)(BUFFER_X + (SCREEN_WIDTH - w) / 2 - lo);
    kdisp_write_gfx_text(f, 1, x, baseline, label);
}

// The language menu's keys that are drawn by a bespoke renderer rather than from a
// legend: a language slot or MRU recent (country flag + tiny code), an MRU Preset/Clear
// control, a region tab. Draws into the CLEARED buffer and returns true, or returns
// false for any other key. ⚠️ ONE copy for update_displays() and the tutorial's focus
// ring: the ring redraws the legend under itself, and while it knew only the static-text
// / render_key() pair, every flag and tab it crossed on the language layer went BLANK
// ("the screen is totally blank as soon as the ring touches them", hardware).
bool render_menu_key(uint16_t keycode, led_t state, uint8_t current_lang) {
    const int16_t lang_idx = lang_index_for_keycode(keycode);
    if (lang_idx >= 0) {
        draw_mru_top_bar(keycode);
        render_lang_flag_key((uint8_t)lang_idx, to_static_text(kcl_for_lang((uint8_t)lang_idx), state),
                             current_lang);
        return true;
    }
    if (keycode == KC_EMJ_PRESET || keycode == KC_LANG_PRESET || keycode == KC_EMJ_CLEAR ||
        keycode == KC_LANG_CLEAR) {
        render_mru_ctrl_key(keycode == KC_EMJ_PRESET || keycode == KC_LANG_PRESET);
        return true;
    }
    if (keycode >= KC_LANG_CAT_BASE && keycode < KC_LANG_PAGE_PREV) {
        lang_draw_tab_indicator(keycode);
        lang_draw_tab_bottom(keycode);
        render_lang_region_tab(keycode);
        return true;
    }
    return false;
}
