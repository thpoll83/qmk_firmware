// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#define ENABLE_COMPILE_KEYCODE

#define EECONFIG_USER_DATA_SIZE 126  // +39 latin_ex_wide (6-bit picks) +1 format version
                                     // +20 latin_assign (6-bit base-letter per key).
                                     // POLY_EECONFIG_USER_RESERVED was raised 128->256 for
                                     // this: 126 still fits 128, but with 2 bytes left the
                                     // next field of any kind would relocate the keymap, so
                                     // the one-time reset is paid here rather than twice.

// Emoji picker: 38 slots/page (rows 2-4; the top row is the MRU, row 1 the tabs).
#define EMJ_SLOTS_PER_PAGE 38
// Language picker: 38 slots/page (rows 1-3 + thumbs; top row = region tabs +
// unicode-mode keys, bottom row = MRU). Mirrors the emoji slot layout.
#define LANG_SLOTS_PER_PAGE 38

#define USB_VBUS_PIN GP24

