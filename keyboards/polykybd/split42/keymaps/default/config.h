// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#define ENABLE_COMPILE_KEYCODE

#define EECONFIG_USER_DATA_SIZE 65   // +1 for poly_eeconf_t.glyph_script (within POLY_EECONFIG_USER_RESERVED=128, no keymap relocation)

#define USB_VBUS_PIN GP24

// Split42 emoji layer: right half shows the current tab's 18 slots (3 rows × 6).
#define EMJ_SLOTS_PER_PAGE 18
// Split42 language layer: right half shows the active region's 18 slots
// (3 rows × 6), paged; left half = region tabs + unicode-mode keys + MRU.
#define LANG_SLOTS_PER_PAGE 18
