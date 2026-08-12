// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "../state.h"      // LATIN_PICK_MAX — the ceiling the assert below checks

// Variation slots per (letter, case).  Rows 0..25 are UPPERCASE A..Z, rows
// 26..51 lowercase a..z; a row is padded with NULL past its real variations.
// Must match the bound the cog emits for the definition in lang_lut.c (from the
// widest row of the latin_sup_ex sheet).
//
// ⚠️ That IS enforced now, but it was not for a long time while this comment
// claimed otherwise: lang_lut.c did not include this header, so the compiler never
// saw the extern declaration and the definition together.  The array grew 14 -> 21
// -> 36 while this stayed 14, and every consumer indexed rows at stride 14 into a
// stride-36 array -- silently reading a neighbouring letter's variations.  The cog
// now emits a _Static_assert against this value AND lang_lut.c includes this
// header, so drift is a build failure that names the file to edit.
//
// The picker shows LATIN_PICKER_SLOTS of these at a time (12 on split72, 10 on
// split42 — see the variant headers) and pages through the rest, so this bound is
// no longer tied to the number of picker KEYS the way it was when both were 10.
#define LATIN_EX_VARIATIONS 36

// ⚠️ The chosen variation is stored in a LATIN_PICK_BITS-wide field per (letter,
// case) in latin_sync_t.ex, so LATIN_PICK_MAX is the hard ceiling — beyond it a
// pick cannot be represented, and latin_variation()'s `idx < LATIN_EX_VARIATIONS`
// guard would silently start reading a truncated index instead.  The sheet-side
// twin of this limit is MAX_SLOTS in lang/_add_latin_variation.py.
//
// Was a nibble (16).  Six bits covers the whole Latin letter+combining-mark space:
// the widest base letter is O with 34 forms, counting the Vietnamese double-mark
// ones (full NFD), so nothing Latin can reach 64.
_Static_assert(LATIN_EX_VARIATIONS <= LATIN_PICK_MAX,
               "latin_sync_t.ex stores the pick in a LATIN_PICK_BITS field — see state.h");

extern const uint32_t* latin_ex_map[26*2][LATIN_EX_VARIATIONS];

//extern const int8_t poly_settings [4][NUM_LANG * 4];

