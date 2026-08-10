// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Variation slots per (letter, case).  Rows 0..25 are UPPERCASE A..Z, rows
// 26..51 lowercase a..z; a row is padded with NULL past its real variations.
// Must match the bound the cog emits for the definition in lang_lut.c (from the
// widest row of the latin_sup_ex sheet) — a mismatch is a compile error at the
// definition, which is the point.  KC_LAT0..KC_LAT9 are the picker keys, so the
// selectable range is [0, LATIN_EX_VARIATIONS).
#define LATIN_EX_VARIATIONS 10

extern const uint32_t* latin_ex_map[26*2][LATIN_EX_VARIATIONS];

//extern const int8_t poly_settings [4][NUM_LANG * 4];

