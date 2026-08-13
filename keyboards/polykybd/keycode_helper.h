// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "polykybd.h"
#include "quantum/quantum.h"
#include "quantum/quantum_keycodes.h"
#include "keymap_german.h"

#include "base/com.h"
#include "lang/named_glyphs.h"

#include "led.h"
#include "layers.h"

// Legends for the two Intl-layer keys that do something other than what their
// base-layer symbol says. Both are plain RESIDENT latin, so they render with no
// font pack flashed and cost no glyph: this layer is *about* latin letters, so
// the letters are the icon. An earlier version drew a rounded-rect tile around
// them as a custom IconsFont glyph — dropped, along with the glyph, once the
// frame turned out to add nothing the letters were not already saying.
#define INTL_LAYER_LEGEND   U"\x130\xF1\x21B\x142"   // Intl, spelled in accented latin
#define INTL_PICKER_LEGEND  U"\xC1\xBB\xC6"           // Á»Æ — Ctrl: pick another variation
// à»ñ — reassign which LETTER this key hosts (vs Á»Æ, which picks another form of
// the SAME letter). Same "»" = "becomes" idiom, lower case and a different letter
// on the right, so the pair reads as one family without looking identical.
// ⚠️ à, NOT a: kdisp_write_gfx_char baseline-aligns every glyph by
// `font->yAdvance - fonts[0]->yAdvance`, so glyphs from DIFFERENT fonts land on
// different baselines. Plain 'a' is in _Base_ (yAdvance 37 => -3px) while » and ñ
// are in _SupAndExtA_ (44 => +4px), which sat the 'a' SEVEN pixels high. Á»Æ above
// is even only because all three of its glyphs are Latin-1, i.e. one font. Keep
// every glyph in a multi-glyph legend inside one font, or position it explicitly.
#define INTL_REMAP_LEGEND   U"\xE0\xBB\xF1"

/*[[[cog
import cog
import os
from textwrap import wrap
from openpyxl import load_workbook
wb = load_workbook(filename = os.path.join(os.path.abspath(os.path.dirname(cog.inFile)), "lang", "lang_lut.xlsx"))
sheet = wb['key_lut']

languages = []
lang_index = 0
lang_key = sheet["B1"].value
while lang_key:
    lang_key = lang_key.replace("-", "").upper()
    languages.append(lang_key)
    if lang_index==0:
        cog.outl(f"#define INIT_LANG LANG_{lang_key}")
    lang_index = lang_index + 1
    lang_key = sheet.cell(row = 1, column = 2 + lang_index*4).value
]]]*/
#define INIT_LANG LANG_ENUS
//[[[end]]]

enum my_keycodes {
    KC_LANG = QK_KB_0,
    KC_DMIN,
    KC_DMAX,
    KC_DDIM,
    KC_DBRI,
    KC_DHLF,
    KC_D1Q,
    KC_D3Q,
    KC_BASE,
    KC_L0,
    KC_L1,
    KC_L2,
    KC_L3,
    KC_L4,
    KC_DEADKEY,
    KC_TOGMODS,
    KC_TOGTEXT,
    KC_RGB_TOG,
    KC_STORE_EE,   // flush all user state to EEPROM now (both halves)
    /*[[[cog
      # Join rather than emitting "KC_LAT{i}, " per item: cog.out() writes verbatim,
      # so a per-item separator also lands after the LAST item and the generated line
      # ends in a trailing space. qmk format-text strips trailing whitespace, so the
      # committed line and the cog output then disagree forever and every `cog -r`
      # dirties this file.
      cog.out(", ".join(f"KC_LAT{idx}" for idx in range(10)) + ",")
    ]]]*/
    KC_LAT0, KC_LAT1, KC_LAT2, KC_LAT3, KC_LAT4, KC_LAT5, KC_LAT6, KC_LAT7, KC_LAT8, KC_LAT9,
    //[[[end]]]
    // Toggle host-driven (daylight/auto) display brightness vs. manual control.
    // Appended at the end of the QK_KB_0 range so existing keycode values (incl.
    // KC_LAT*) and the QK_USER_0-based KCL_* range below are NOT renumbered.
    KC_DAUTO,
    // Doom easter-egg menu item (utilities layer, doom builds only): renders
    // blank and is inert until typing IDDQD arms it (doom_mode.c) — then it
    // shows "IDDQD" on the master half and pressing it starts the game.
    // Never emits HID output; a dead blank key in normal builds. Appended
    // for the same no-renumbering reason as KC_DAUTO.
    KC_IDDQD,
    // Latin-variation picker, keys 11 and 12, plus the two page arrows.
    //
    // ⚠️ These are APPENDED here rather than grown out of the cog block above
    // (`range(10)` -> `range(12)`), for the same no-renumbering reason as KC_DAUTO
    // and KC_IDDQD: widening that range would push KC_DAUTO/KC_IDDQD up by two, and
    // those values are what a host-remapped key stores in the dynamic-keymap EEPROM.
    // The cost is that KC_LAT0..KC_LAT9 and KC_LAT10..KC_LAT11 are NOT contiguous —
    // use latin_picker_slot() (poly_keymap.c) instead of `keycode - KC_LAT0`, and do
    // not write `KC_LAT0 ... KC_LAT11` as a case range.
    KC_LAT10,
    KC_LAT11,
    // Page through a letter's variations when it has more than the picker can show
    // at once. Both live on the OUTER LEFT of the picker row on both variants, so
    // the position is the same muscle memory on split72 (12 slots) and split42 (10).
    KC_LAT_PAGE_PREV,
    KC_LAT_PAGE_NEXT,
    // Reassign which LETTER a key hosts on the Intl layer, so several keys can carry
    // variations of the same letter (French wants e, è, é and ê at once). Held with
    // Intl it opens remap mode: pick the key, then pick the letter; Shift+it clears
    // every assignment. See latin_remap_* in poly_keymap.c.
    KC_LAT_REMAP,
    /*[[[cog
        # Joined for the same reason as the KC_LAT block above — no trailing space.
        cog.out(", ".join("KCL_ENUS = QK_USER_0" if lang == "ENUS" else f"KCL_{lang}"
                          for lang in languages) + ",")
    ]]]*/
    KCL_ENUS = QK_USER_0, KCL_DEDE, KCL_FRFR, KCL_ESES, KCL_PTPT, KCL_ITIT, KCL_TRTR, KCL_KOKR, KCL_JAJP, KCL_ARSA, KCL_ELGR, KCL_UKUA, KCL_RURU, KCL_BEBY, KCL_KKKZ, KCL_BGBG, KCL_PLPL, KCL_RORO, KCL_ZHCN, KCL_NLNL, KCL_HEIL, KCL_SVSE, KCL_FIFI, KCL_NNNO, KCL_DADK, KCL_HUHU, KCL_CSCZ, KCL_HRHR, KCL_SKSK, KCL_LTLT, KCL_LVLV, KCL_ETEE, KCL_PTBR, KCL_SRRS, KCL_MKMK, KCL_FAIR, KCL_HIIN, KCL_MRIN, KCL_NENP, KCL_MNMN, KCL_URPK, KCL_ENGB, KCL_ESMX, KCL_DECH, KCL_FRBE, KCL_FRCA, KCL_THTH, KCL_BNIN, KCL_TEIN, KCL_TAIN, KCL_ZHTW, KCL_KAGE, KCL_HYAM, KCL_IDID, KCL_AZAZ, KCL_ISIS, KCL_VIVN, KCL_ZHHK, KCL_ENAU, KCL_ENNZ, KCL_MINZ, KCL_SMWS, KCL_FJFJ, KCL_TLPH, KCL_HWUS, KCL_ENZA, KCL_AFZA, KCL_AREG, KCL_SWKE, KCL_AMET, KCL_YONG, KCL_ENNG, KCL_ARMA, KCL_ARIQ, KCL_KUIQ, KCL_MSMY, KCL_UZUZ, KCL_ENCA, KCL_ESAR, KCL_ENPG, KCL_TYPF, KCL_ESCO, KCL_ESPE, KCL_ESVE, KCL_ESCL, KCL_ESEC, KCL_ESGT, KCL_ESDO, KCL_ESBO, KCL_ESPY, KCL_ESCR, KCL_ESSV, KCL_ESHN, KCL_ESPA, KCL_ESUY, KCL_ESNI, KCL_DEAT, KCL_NLBE, KCL_CAES, KCL_ENIE, KCL_BSBA, KCL_FRCH, KCL_SLSI, KCL_FOFO, KCL_ARAE, KCL_ARSY, KCL_ARJO, KCL_ARLB, KCL_ARYE, KCL_ARKW, KCL_AROM, KCL_ARPS, KCL_ARQA, KCL_ARBH, KCL_ARDZ, KCL_ARSD, KCL_ARTN, KCL_ARLY, KCL_FRCD, KCL_FRCI, KCL_FRCM, KCL_FRSN, KCL_FRMG, KCL_ENGH, KCL_ENUG, KCL_ENZM, KCL_SWTZ, KCL_PTAO, KCL_PTMZ, KCL_BNBD, KCL_ENIN, KCL_ENPK, KCL_ENPH, KCL_ENSG, KCL_ENLK, KCL_KYKG, KCL_TGTJ, KCL_ENGU, KCL_ENSB, KCL_ENVU, KCL_ENFM, KCL_FRNC, KCL_TOTO, KCL_EUES, KCL_GLES, KCL_RMCH, KCL_CYGB, KCL_GAIE, KCL_MTMT, KCL_LBLU, KCL_SENO, KCL_GNPY, KCL_QUPE, KCL_AYBO, KCL_NVUS, KCL_NHMX, KCL_PSAF, KCL_IUCA, KCL_CRCA, KCL_CKUS,
    //[[[end]]]
        //Lables, no functionality:
    LBL_TEXT,

    // ── Emoji category layer keycodes ────────────────────────────────────────
    // Category tab keys: KC_EMJ_CAT_BASE + n  (n = 0 .. EMJ_NUM_CATEGORIES-1)
    KC_EMJ_CAT_BASE,
    // Page navigation (follow sequentially after CAT_BASE + 12 slots)
    KC_EMJ_PAGE_PREV = KC_EMJ_CAT_BASE + 12,
    KC_EMJ_PAGE_NEXT,
    // Top-row MRU controls: "Preset" loads the built-in recents, "Clear" empties them.
    KC_EMJ_PRESET,
    KC_EMJ_CLEAR,
    // MRU slot keys: KC_EMJ_MRU_BASE + n  (n = 0 .. MRU_CAP-1)
    KC_EMJ_MRU_BASE,
    // Emoji slot keys: KC_EMJ_SLOT_BASE + n  (n = 0 .. EMJ_SLOTS_PER_PAGE-1)
    KC_EMJ_SLOT_BASE = KC_EMJ_MRU_BASE + 12,
    // Sentinel — must stay <= QK_USER_MAX (0x7FFF)
    KC_EMJ_END = KC_EMJ_SLOT_BASE + 50,

    // ── Language selection layer keycodes (region tabs + paged slots) ─────────
    // The _LL layer mirrors the emoji layer: a top row of continent region tabs
    // (KC_LANG_CAT_BASE + n), then generic page-relative slot keycodes (resolved
    // to a LANG_* index at render/press time) so the same physical keys can page
    // through every language in the active region.
    KC_LANG_CAT_BASE,                              // region tabs: + n (n = 0 .. NUM_LANG_REGIONS-1)
    // Page navigation (follow sequentially after CAT_BASE + 12 slots, like emoji)
    KC_LANG_PAGE_PREV = KC_LANG_CAT_BASE + 12,
    KC_LANG_PAGE_NEXT,
    KC_LANG_PRESET,
    KC_LANG_CLEAR,
    KC_LANG_MRU_BASE,                              // + n  (n = 0 .. MRU_CAP-1)
    KC_LANG_SLOT_BASE = KC_LANG_MRU_BASE + 12,     // + n  (n = 0 .. LANG_SLOTS_PER_PAGE-1)
    KC_LANG_END = KC_LANG_SLOT_BASE + 58,          // headroom for all NUM_LANG slots

    // ── OS-semantic action keycodes ──────────────────────────────────────────
    // Each resolves at PRESS time to the correct key chord for the *active* OS
    // (get_local_state()->active_os) via the [action][os] table in os_actions.c —
    // e.g. KC_OS_COPY emits Ctrl+C on Windows/Linux but Cmd+C on macOS, KC_OS_LOCK
    // emits Win+L / Ctrl+Cmd+Q / Super+L. Drop them into keymaps[] like any keycode;
    // they give the "full palette on every OS" without per-OS layers. Append-only —
    // os_action_table[] and the label switch in keycode_helper.c track this order.
    KC_OS_ACTION_BASE,
    KC_OS_COPY = KC_OS_ACTION_BASE,
    KC_OS_CUT,
    KC_OS_PASTE,
    KC_OS_UNDO,
    KC_OS_REDO,
    KC_OS_SELALL,
    KC_OS_FIND,
    KC_OS_LOCK,
    KC_OS_SCRSHOT,        // region screenshot
    KC_OS_SEARCH,         // launcher / spotlight
    KC_OS_APP_SWITCH,     // application switcher
    KC_OS_WIN_SWITCH,     // window switcher
    KC_OS_EMOJI,          // emoji picker
    KC_OS_WORD_LEFT,      // move cursor one word left
    KC_OS_WORD_RIGHT,     // move cursor one word right
    KC_OS_LINE_HOME,      // move to start of line
    KC_OS_LINE_END,       // move to end of line
    KC_OS_ACTION_END,

    // OS status / control key: its legend shows the active OS + auto-or-pin badge
    // (the "see and verify the mode" key); pressing it cycles auto -> pin Windows
    // -> macOS -> Linux -> Android -> auto. (iOS is folded into macOS, and the
    // host-detected GNOME/KDE desktops are not separately cyclable.) Not part of
    // os_action_table.
    KC_OS_ICON,

    // OS selection keys (settings layer): one key per choice instead of the single
    // cycling KC_OS_ICON. KC_OS_SET_AUTO clears the manual pin (back to host/USB
    // detection); the rest pin a specific OS. The offset from KC_OS_SET_BASE is the
    // poly_os value, so KC_OS_SET_AUTO sits on the POLY_OS_UNKNOWN(0) slot and is
    // treated as "auto", while KC_OS_SET_WINDOWS == base + POLY_OS_WINDOWS, etc.
    // Append-only — the process_record range and the legend block track this order.
    KC_OS_SET_BASE,
    KC_OS_SET_AUTO = KC_OS_SET_BASE,        // + POLY_OS_UNKNOWN (0) -> auto mode
    KC_OS_SET_WINDOWS,                       // + POLY_OS_WINDOWS (1)
    KC_OS_SET_MACOS,                         // + POLY_OS_MACOS   (2)
    KC_OS_SET_LINUX,                         // + POLY_OS_LINUX   (3)
    KC_OS_SET_ANDROID,                       // + POLY_OS_ANDROID (4)
    // No KC_OS_SET_IOS: iOS is folded into macOS (identical keyboard operation), so
    // it is not separately user-selectable. POLY_OS_IOS (enum slot 5) is kept only as
    // a reserved tombstone — it is not a detection target (QMK OS_IOS maps to
    // POLY_OS_MACOS) and a persisted legacy 5 is migrated to macOS on load.
    KC_OS_SET_END,

    // Settings-layer key: replay the one-time startup ("Eden") animation on demand.
    // Appended at the very end (QK_USER range) so no existing keycode is renumbered.
    KC_EDEN,
};
static_assert((int)KC_DAUTO <= (int)QK_KB_31, "Too many custom QK key codes");
// ⚠️ Anchor the range guards on the LAST keyboard-range keycode, not on KC_DAUTO —
// it stopped being last the moment KC_IDDQD and the four latin-picker keycodes were
// appended after it, so the two asserts above silently stopped covering the tail
// they exist to bound.  Re-anchor these whenever something is appended to the
// QK_KB_0 block.
static_assert((int)KC_LAT_REMAP <= (int)QK_KB_MAX, "Too many custom QK key codes");
static_assert((int)KC_LAT_REMAP < (int)KCL_ENUS, "Overlap detected");
static_assert((int)KC_LANG_END <= 0x7FFF, "Emoji/Lang keycodes exceed QK_USER_MAX");
static_assert((int)KC_OS_SET_END <= 0x7FFF, "OS action keycodes exceed QK_USER_MAX");

// Convenience macros for the emoji category layer keymap entries.
#define KC_EMJ_CAT(n)  ((uint16_t)((uint16_t)KC_EMJ_CAT_BASE  + (uint16_t)(n)))
#define ESLOT(n)       ((uint16_t)((uint16_t)KC_EMJ_SLOT_BASE  + (uint16_t)(n)))
#define EMRU(n)        ((uint16_t)((uint16_t)KC_EMJ_MRU_BASE   + (uint16_t)(n)))
// Convenience macros for the language selection layer keymap entries.
#define LCAT(n)        ((uint16_t)((uint16_t)KC_LANG_CAT_BASE  + (uint16_t)(n)))
#define LSLOT(n)       ((uint16_t)((uint16_t)KC_LANG_SLOT_BASE + (uint16_t)(n)))
#define LMRU(n)        ((uint16_t)((uint16_t)KC_LANG_MRU_BASE  + (uint16_t)(n)))

const uint32_t* keycode_to_static_text(uint16_t keycode, led_t state, uint8_t state_flags);
