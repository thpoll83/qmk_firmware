// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#define ENABLE_COMPILE_KEYCODE

#define EECONFIG_USER_DATA_SIZE 106  // +39 latin_ex_wide (6-bit picks) +1 latin_pick_migrated (within POLY_EECONFIG_USER_RESERVED=128, no keymap relocation)

#define USB_VBUS_PIN GP24

// split42 emoji picker: 18 slots/page (right half = 3 rows x 6). split72 uses 38.
#define EMJ_SLOTS_PER_PAGE 18
// split42 language picker: 18 slots/page (right half = 3 rows x 6), paged; left
// half = region tabs + unicode-mode keys + MRU. Mirrors the emoji slot layout.
#define LANG_SLOTS_PER_PAGE 18
