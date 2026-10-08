// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#include "idle_style.h"

#include <assert.h>
#include <stddef.h>

static const idle_style_desc_t s_styles[] = {
    [IDLE_STYLE_PULSE]  = {.name = "pulse", .enter = IDLE_ENTER_PULSE, .in_key_cycle = true},
    [IDLE_STYLE_JITTER] = {.name = "jitter", .enter = IDLE_ENTER_PULSE, .jitter = true, .in_key_cycle = true},
    // Not in the key cycle: a settings key that cycled into the doom easter egg
    // would hand it to anyone who pressed it twice.
    [IDLE_STYLE_IDDQD] = {.name = "iddqd", .enter = IDLE_ENTER_DOOM, .in_key_cycle = false},
    [IDLE_STYLE_EDEN]  = {.name = "eden", .enter = IDLE_ENTER_STEADY, .steady_contrast = EDEN_IDLE_BRIGHTNESS, .owns_keycaps = true, .in_key_cycle = true},
};

// Unsized on purpose: a style appended to the enum without a row here leaves the
// array one short, and this fails. A gap in the middle is caught by the test
// EveryStyleHasARow (make test:polykybd_idle_style).
static_assert(sizeof(s_styles) / sizeof(s_styles[0]) == IDLE_STYLE_COUNT, "one row per idle style");

const idle_style_desc_t *idle_style_desc(uint8_t style) {
    if (style >= IDLE_STYLE_COUNT || s_styles[style].name == NULL) {
        return &s_styles[IDLE_STYLE_PULSE];
    }
    return &s_styles[style];
}

uint8_t idle_style_next_in_cycle(uint8_t style) {
    uint8_t next = style < IDLE_STYLE_COUNT ? style : (uint8_t)(IDLE_STYLE_COUNT - 1u);
    for (uint8_t i = 0; i < IDLE_STYLE_COUNT; ++i) {
        next = (uint8_t)((next + 1u) % IDLE_STYLE_COUNT);
        if (s_styles[next].in_key_cycle) {
            return next;
        }
    }
    return IDLE_STYLE_PULSE;
}
