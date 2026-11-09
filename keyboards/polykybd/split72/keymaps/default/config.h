// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#define ENABLE_COMPILE_KEYCODE

#define EECONFIG_USER_DATA_SIZE 161  // +39 latin_ex_wide +1 fmt +20 latin_assign, then +18/+9/+1 for the punctuation targets, +1 glyph_size +1 keymap_layers_fmt +1 idle_style_fmt +1 idle_timeout +1 glyph_place +2 tapping_term/flow_tap_term
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
// Backspace and Space in search mode: the right-half key above the innermost
// thumb (Enter on Stag!) and the key beside it. Neither is a letter on any of
// the five base layouts ([ ] on Qwerty, Lang and App/Hyper/+/IME on the others).
#define EMJ_SEARCH_BKSP_SLOT  31
#define EMJ_SEARCH_SPACE_SLOT 32
// A second Space on the left key above the innermost thumb (slot 18, MO(_NL) on
// every base layout), mirroring the right side's row.
#define EMJ_SEARCH_SPACE2_SLOT 18
// Language picker: 38 slots/page (rows 1-3 + thumbs; top row = region tabs +
// unicode-mode keys, bottom row = MRU). Mirrors the emoji slot layout.
#define LANG_SLOTS_PER_PAGE 38

#define USB_VBUS_PIN GP24

