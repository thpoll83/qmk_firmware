// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#define ENABLE_COMPILE_KEYCODE

#define EECONFIG_USER_DATA_SIZE 159  // +39 latin_ex_wide +1 fmt +20 latin_assign, then +18/+9/+1 for the punctuation targets, +1 glyph_size +1 keymap_layers_fmt +1 idle_style_fmt +1 idle_timeout +1 glyph_place
                                     // +20 latin_assign (6-bit base-letter per key).
                                     // POLY_EECONFIG_USER_RESERVED was raised 128->256 for
                                     // this: 126 still fits 128, but with 2 bytes left the
                                     // next field of any kind would relocate the keymap, so
                                     // the one-time reset is paid here rather than twice.

// Emoji picker: 38 slots/page (rows 2-4; the top row is the MRU, row 1 the tabs).
#define EMJ_SLOTS_PER_PAGE 38
// Emoji search mode (emoji/EMOJI_SEARCH.md). Result index of each category tab, by
// category: the number row holds cats 0 1 2 4 5 6 | 7 8 9 10 3 11 left to right.
#define EMJ_SEARCH_RESULT_OF_CAT { 0, 1, 2, 10, 3, 4, 5, 6, 7, 8, 9, 11 }
// Recents row in search mode: both Space keys (recents 3 and 8) type a space and
// Del (recents 4) clears the query; the other nine show the nine latest recents.
#define EMJ_SEARCH_MRU_ROLES { 0, 1, 2, EMJ_SR_SPACE, EMJ_SR_CLEAR, 3, 4, 5, EMJ_SR_SPACE, 6, 7, 8 }
// Language picker: 38 slots/page (rows 1-3 + thumbs; top row = region tabs +
// unicode-mode keys, bottom row = MRU). Mirrors the emoji slot layout.
#define LANG_SLOTS_PER_PAGE 38

#define USB_VBUS_PIN GP24

