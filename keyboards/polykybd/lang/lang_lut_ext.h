// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Variation slots per (letter, case).  Rows 0..25 are UPPERCASE A..Z, rows
// 26..51 lowercase a..z; a row is padded with NULL past its real variations.
// Must match the bound the cog emits for the definition in lang_lut.c (from the
// widest row of the latin_sup_ex sheet) — a mismatch is a compile error at the
// definition, which is the point.
//
// The picker shows LATIN_PICKER_SLOTS of these at a time (12 on split72, 10 on
// split42 — see the variant headers) and pages through the rest, so this bound is
// no longer tied to the number of picker KEYS the way it was when both were 10.
#define LATIN_EX_VARIATIONS 14

// ⚠️ The chosen variation is stored as a NIBBLE per (letter, case) in
// latin_sync_t.ex, so 16 is the hard ceiling — beyond it a pick cannot be
// represented, and latin_variation()'s `idx < LATIN_EX_VARIATIONS` guard would
// silently start reading a truncated index instead.  The sheet-side twin of this
// limit is MAX_SLOTS in lang/_add_latin_variation.py.
_Static_assert(LATIN_EX_VARIATIONS <= 16,
               "latin_sync_t.ex stores the pick in a nibble — max 16 variations");

extern const uint32_t* latin_ex_map[26*2][LATIN_EX_VARIATIONS];

//extern const int8_t poly_settings [4][NUM_LANG * 4];

