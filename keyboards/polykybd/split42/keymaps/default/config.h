// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#define ENABLE_COMPILE_KEYCODE

#define EECONFIG_USER_DATA_SIZE 163  // +39 latin_ex_wide +1 fmt +20 latin_assign, then +18/+9/+1 for the punctuation targets, +1 glyph_size +1 keymap_layers_fmt +1 idle_style_fmt +1 idle_timeout +1 glyph_place +2 tapping_term/flow_tap_term +1 emj_search_mode +1 tail_fmt
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

#define USB_VBUS_PIN GP24

// split42 emoji picker: 18 slots/page (right half = 3 rows x 6). split72 uses 38.
#define EMJ_SLOTS_PER_PAGE 18
// split42 language picker: 18 slots/page (right half = 3 rows x 6), paged; left
// half = region tabs + unicode-mode keys + MRU. Mirrors the emoji slot layout.
#define LANG_SLOTS_PER_PAGE 18
