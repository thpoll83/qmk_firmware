// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#include "layer_names.h"
#include "layers.h"

#include <stddef.h>
#include "config.h"

// One record per base layout, carrying every width budget a consumer needs.
// ⚠️ The `wire` form is what the host layout editor prints on its layer tabs,
// so it is capped at POLY_LAYER_NAME_MAX (8) — "Colemak DH" does not fit and is
// spelled "ColemkDH". `short_` is capped at 5 by split42's 32 px panel.
static const struct {
    const uint32_t* full;    // split72 status OLED
    const uint32_t* short_;  // split42 status OLED, <= 5 chars
    const char*     wire;    // HID cmd 35, <= POLY_LAYER_NAME_MAX chars
} layouts[] = {
    { U"Qwerty",       U"Qwrty", "Qwerty"   },
    { U"Qwerty Stag!", U"Stag!", "Stag!"    },
    { U"Colemak DH",   U"ColDH", "ColemkDH" },
    { U"Neo",          U"Neo",   "Neo"      },
    { U"Workman",      U"Wkmn",  "Workman"  },
};
#define NUM_LAYOUTS (sizeof(layouts) / sizeof(layouts[0]))

// The layers above the base layouts. Indexed by (layer - NUM_LAYOUTS), so this
// list must stay in step with the enum in layers.h from _FL up to the write cap.
static const char* const fixed_wire[] = {
    "Fn",       // _FL
    "Numpad",   // _NL
    "Utility",  // _UL
};

// The host may only remap layers below the write cap, so anything at or above it
// has no editor tab to label. Assert the two lists together cover exactly that
// range: a layer added or reordered in layers.h then fails the build here rather
// than silently reporting the name of its neighbour.
_Static_assert(NUM_LAYOUTS + (sizeof(fixed_wire) / sizeof(fixed_wire[0]))
                   == DYNAMIC_KEYMAP_UPDATE_MAX_LAYER_COUNT,
               "layer_names.c does not name exactly the host-remappable layers");
_Static_assert(_FL == NUM_LAYOUTS, "the base layouts must be the first layers");

const uint32_t* poly_layout_name(uint8_t def_layer) {
    return (def_layer < NUM_LAYOUTS) ? layouts[def_layer].full : U"Unknown";
}

const uint32_t* poly_layout_name_short(uint8_t def_layer) {
    return (def_layer < NUM_LAYOUTS) ? layouts[def_layer].short_ : U"Unkn";
}

const char* poly_layer_name_wire(uint8_t layer) {
    if (layer < NUM_LAYOUTS) {
        return layouts[layer].wire;
    }
    const uint8_t idx = (uint8_t)(layer - NUM_LAYOUTS);
    return (idx < sizeof(fixed_wire) / sizeof(fixed_wire[0])) ? fixed_wire[idx] : NULL;
}
