// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#define ENABLE_COMPILE_KEYCODE

#define EECONFIG_USER_DATA_SIZE 167  // +39 latin_ex_wide +1 fmt +20 latin_assign, then +18/+9/+1 for the punctuation targets, +1 glyph_size +1 keymap_layers_fmt +1 idle_style_fmt +1 idle_timeout +1 glyph_place +2 tapping_term/flow_tap_term +1 emj_search_mode +1 tail_fmt +1 auto_shift +1 tail2_fmt +1 keymap_mig_pad +1 keymap_mig
                                     // +20 latin_assign (6-bit base-letter per key).
                                     // POLY_EECONFIG_USER_RESERVED was raised 128->256 for
                                     // this: 126 still fits 128, but with 2 bytes left the
                                     // next field of any kind would relocate the keymap, so
                                     // the one-time reset is paid here rather than twice.

// ⚠️ The user datablock's VERSION is fixed at 159 and must never change. QMK
// defaults it to EECONFIG_USER_DATA_SIZE and re-initialises (zeroes) the whole block
// when the stored version differs, and a zeroed keymap_layers_fmt then discards the
// stored keymap and macros too. Fixing it lets a tail byte be appended without that
// reset. An appended byte reads 0 on a board that never wrote it, so 0 must be its
// default, EXCEPT on a board older than 2026-08-13, where offsets 128 and up held
// the stored keymap: there it can read an old keymap byte. A field at offset 159
// or later must be covered by tail_fmt (state.h), which resets it once. 159 is the
// size the 1.11.0 release wrote, so boards on 1.11.0 keep their settings.
#define EECONFIG_USER_DATA_VERSION 159

// Emoji picker: 38 slots/page (rows 2-4; the top row is the MRU, row 1 the tabs).
#define EMJ_SLOTS_PER_PAGE 38
// Emoji search mode (emoji/EMOJI_SEARCH.md). Result index of each category tab, by
// category: the number row holds cats 0 1 2 4 5 6 | 7 8 9 10 3 11 left to right.
#define EMJ_SEARCH_RESULT_OF_CAT { 0, 1, 2, 10, 3, 4, 5, 6, 7, 8, 9, 11 }
// Language picker: 38 slots/page (rows 1-3 + thumbs; top row = region tabs +
// unicode-mode keys, bottom row = MRU). Mirrors the emoji slot layout.
#define LANG_SLOTS_PER_PAGE 38

#define USB_VBUS_PIN GP24

