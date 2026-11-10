// Copyright 2026 Thomas Pollak
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Caps Word's letter test (poly_keymap.c caps_word_press_user()): a key is a letter
// when its plain and shifted legends are the lower- and upper-case forms of one
// letter. Header-only and pure, so it is unit-tested on the host
// (make test:polykybd_caps_word_letter).
//
// ⚠️ Not "the legend changes under Caps Lock". Most language tables leave the Caps
// Lock column empty for punctuation and digits, and translate_keycode() reads an
// empty entry as "like Shift". That made de-DE's comma and period letters, so Caps
// Word typed ";" and ":" (review of qmk#425).

#pragma once

#include <stdbool.h>
#include <stdint.h>

// The first printed codepoint of a legend. Codepoints below 0x20 are layout
// controls (\f, \v, nudges), not text. 0 for NULL or an empty legend.
static inline uint32_t legend_first_char(const uint32_t* legend) {
    if (legend == 0) return 0;
    while (*legend != 0 && *legend < 0x20u) legend++;
    return *legend;
}

// True when `upper` is the upper-case form of `lower`, for the cased scripts the
// language tables use: Latin (ASCII, Latin-1, Extended-A/B, Extended Additional),
// Greek, Cyrillic, Armenian. Other scripts have no case, so they never match.
static inline bool is_case_pair(uint32_t lower, uint32_t upper) {
    if (lower >= 'a' && lower <= 'z') {
        return upper == lower - 32u || (lower == 'i' && upper == 0x130u);   // Turkish İ
    }
    if (lower >= 0xE0u && lower <= 0xFEu && lower != 0xF7u) return upper == lower - 32u;
    if (lower == 0xFFu) return upper == 0x178u;                              // ÿ Ÿ
    if (lower == 0x131u) return upper == 'I';                                // Turkish ı I
    if (lower == 0x259u) return upper == 0x18Fu;                             // Azerbaijani ə Ə
    if (lower >= 0x100u && lower <= 0x24Fu) return upper + 1u == lower;
    if (lower >= 0x1E00u && lower <= 0x1EFFu) return upper + 1u == lower;
    if (lower == 0x3C2u) return upper == 0x3A3u;                             // final sigma
    if (lower >= 0x3B1u && lower <= 0x3C9u) return upper == lower - 32u;
    if (lower >= 0x430u && lower <= 0x44Fu) return upper == lower - 32u;
    if (lower >= 0x450u && lower <= 0x45Fu) return upper == lower - 80u;
    if (lower >= 0x460u && lower <= 0x52Fu) return upper + 1u == lower;     // ґ, Kazakh ә ң ү ...
    if (lower >= 0x561u && lower <= 0x586u) return upper == lower - 48u;
    return false;
}

static inline bool caps_word_is_letter(const uint32_t* plain, const uint32_t* shifted) {
    return is_case_pair(legend_first_char(plain), legend_first_char(shifted));
}

