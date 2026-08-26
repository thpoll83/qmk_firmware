// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdint.h>

#include "config.h"

// The ONE place that answers "what is this layer called".
//
// Three consumers want the same answer at three different width budgets, and
// before this file they each carried their own copy of the table:
//   - split72's status OLED  -> the full name  ("Qwerty Stag!")
//   - split42's status OLED  -> <= 5 chars, its panel is 32 px wide ("Stag!")
//   - HID cmd 35             -> <= 8 chars, what the host layout editor labels
//                               its layer tabs with ("Stag!")
// Keeping three lists in sync by hand is the guard shape this repo keeps getting
// caught by, so all three forms live in ONE record per layout: a new layout
// cannot be added without giving it every form.
#define POLY_LAYER_NAME_MAX 8   // wire form, excluding the NUL

// Worst case for HID cmd 35: a total byte, a count byte, then one full-width name
// plus its terminator per layer. The total is reported in ONE byte, so the whole
// payload has to stay addressable by it.
#define LAYER_NAMES_PAYLOAD_MAX \
    (2 + DYNAMIC_KEYMAP_UPDATE_MAX_LAYER_COUNT * (POLY_LAYER_NAME_MAX + 1))
_Static_assert(LAYER_NAMES_PAYLOAD_MAX <= 255,
               "the layer-name payload no longer fits its one-byte total length");

// def_layer (_L0.._L4) -> the status-OLED forms.
const uint32_t* poly_layout_name(uint8_t def_layer);        // split72, full width
const uint32_t* poly_layout_name_short(uint8_t def_layer);  // split42, <= 5 chars

// Layer index (_L0.._UL) -> the <= POLY_LAYER_NAME_MAX ASCII name reported over
// HID cmd 35. Layers 0..4 are the base layouts, so they answer with the wire form
// of the table above; the rest are fixed. NULL for a layer with no name.
const char* poly_layer_name_wire(uint8_t layer);
