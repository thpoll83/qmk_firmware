// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#define ENABLE_COMPILE_KEYCODE

#define EECONFIG_USER_DATA_SIZE 157  // +39 latin_ex_wide +1 fmt +20 latin_assign, then +18/+9/+1 for the punctuation targets, +1 glyph_size +1 keymap_layers_fmt +1 idle_style_fmt
                                     // +20 latin_assign (6-bit base-letter per key).
                                     // POLY_EECONFIG_USER_RESERVED was raised 128->256 for
                                     // this: 126 still fits 128, but with 2 bytes left the
                                     // next field of any kind would relocate the keymap, so
                                     // the one-time reset is paid here rather than twice.

#define USB_VBUS_PIN GP24

// split42 emoji picker: 18 slots/page (right half = 3 rows x 6). split72 uses 38.
#define EMJ_SLOTS_PER_PAGE 18
// split42 language picker: 18 slots/page (right half = 3 rows x 6), paged; left
// half = region tabs + unicode-mode keys + MRU. Mirrors the emoji slot layout.
#define LANG_SLOTS_PER_PAGE 18
