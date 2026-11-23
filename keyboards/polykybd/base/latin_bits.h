// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The Intl latin variation packing, moved out of lib/system/state.h so the host unit
// tests link the real accessors (base/tests/latin_bits_tests.cpp). state.h includes
// this file; nothing here needs QMK.
#pragma once

#include <stdint.h>

// --- Intl latin variations: which letter a key hosts, and which form it picks --
//
// Two 6-bit arrays, both indexed by TARGET SLOT (the remappable key), not by
// letter.  Today the targets are A..Z, so slot == letter index by default and the
// two readings coincide -- which is exactly why an existing EEPROM keeps working.
//
//   assign[slot]        the base LETTER whose variation row this key hosts.
//                       LATIN_ASSIGN_NONE = "this key's own natural behaviour",
//                       i.e. its own letter.  Shared across case: remapping is a
//                       property of the KEY, so Shift follows it (r -> e means
//                       Shift+r gives E's upper-case form, not <).
//   ex[slot][case]      the chosen variation WITHIN that row.  Per case, because
//                       the two rows are not parallel -- lower n has 12 variations
//                       and upper N has 11, so one shared index would be out of
//                       range for one of them.
//
// Six bits covers every letter+combining-mark form that exists in Unicode (the
// widest is O at 34 -- 16 single-mark plus 18 Vietnamese double-mark) and, for
// assign, the 26 letters plus the sentinel.  It also means BOTH arrays use the
// same accessor.
//
// ⚠️ NEVER infer "this map was never written" from its bytes -- gate it on the
// latin_pick_migrated format version instead.  An earlier version of this relied
// on LATIN_ASSIGN_NONE being all-bits-set and an unwritten EEPROM reading 0xFF.
// That is wrong here: QMK's wear-levelling normalises its backing store so cleared
// bytes arrive as ZERO (it memsets its cache to 0 and requires a 0xFF-based store
// to return the complement), so the map read back all-0x00 = "every key hosts
// letter 0" and the whole Intl layer showed variations of 'a' (field, first flash).
// LATIN_ASSIGN_NONE stays all-bits-set only because it must be a non-letter value.
//
// ⚠️ Go through latin_pick_get/set.  The old width was open-coded at the call site
// as `(pick << 4) | (cur & 0xf)`, which is precisely the shape that turns a width
// change into a silent corruption of the other case's pick.
#define LATIN_PICK_BITS    6
#define LATIN_PICK_MAX     (1u << LATIN_PICK_BITS)             /* 64 */
// Slots 0..25 are the LETTERS A..Z; 26..37 are the printable punctuation keys,
// KC_MINUS 0x2D .. KC_SLASH 0x38 — a CONTIGUOUS run of twelve, so the mapping is
// `kc - KC_MINUS + 26` with no table (see latin_target_slot()).  The run is taken
// whole rather than hand-filtered: a key that is masked or absent on a given board
// is simply unreachable there, which excludes `\` / non-US-hash by itself on the
// layouts that do not want them, and costs nothing on the ones that do.
// Only a letter can be a SOURCE — a punctuation key hosts a letter's row, never
// the other way round — so the two bounds are deliberately separate constants.
#define LATIN_LETTER_TARGETS 26
#define LATIN_PUNCT_TARGETS  12
#define LATIN_TARGETS      (LATIN_LETTER_TARGETS + LATIN_PUNCT_TARGETS)   /* 38 */
#define LATIN_PICK_FIELDS  (LATIN_TARGETS * 2)                 /* upper + lower  */
#define LATIN_PICK_BYTES   ((LATIN_PICK_FIELDS * LATIN_PICK_BITS + 7) / 8)  /* 57 */
#define LATIN_ASSIGN_BYTES ((LATIN_TARGETS * LATIN_PICK_BITS + 7) / 8)      /* 29 */

// ⚠️ EEPROM keeps the letters and the punctuation in SEPARATE blocks, and the
// letter block keeps its original offsets and size.  Growing the stored arrays in
// place would shift latin_pick_migrated, and a version byte that MOVES cannot be
// found — the load would read a pick byte as the version and could not tell a real
// format from a coincidence.  Appending instead means an old EEPROM reads the new
// block as zeros (wear-levelling, above), which is unambiguously "not written",
// and every existing pick and assignment survives untouched.
//
// The PICK split lands on a byte boundary, which is what makes it a plain memcpy:
// 52 letter fields x 6 bits = 312 = 39 bytes exactly, and the 24 punctuation
// fields x 6 = 144 = 18 bytes exactly, starting at byte 39.  The ASSIGN split does
// NOT (26 x 6 = 156 bits = 19.5 bytes), so the punctuation assignments live in
// their own array indexed FROM ZERO and are copied field-by-field, not blitted.
#define LATIN_PICK_BASE_BYTES   ((LATIN_LETTER_TARGETS * 2 * LATIN_PICK_BITS + 7) / 8) /* 39 */
#define LATIN_PICK_EXT_BYTES    (LATIN_PICK_BYTES - LATIN_PICK_BASE_BYTES)             /* 18 */
#define LATIN_ASSIGN_BASE_BYTES ((LATIN_LETTER_TARGETS * LATIN_PICK_BITS + 7) / 8)     /* 20 */
#define LATIN_ASSIGN_EXT_BYTES  ((LATIN_PUNCT_TARGETS * LATIN_PICK_BITS + 7) / 8)      /* 9  */
#define LATIN_ASSIGN_NONE  (uint8_t)(LATIN_PICK_MAX - 1u)      /* 0x3F -- see above */
// Byte fill that makes EVERY 6-bit field read LATIN_ASSIGN_NONE. 0x3F does not tile
// a byte (6 does not divide 8), so memset(map, LATIN_ASSIGN_NONE, n) would leave a
// mix of values -- it is 0xFF that fills every field with all-ones.
#define LATIN_ASSIGN_FILL  0xFFu

// Pick fields are CASE-INTERLEAVED (slot*2 + case), not case-blocked.  Growing
// LATIN_TARGETS then APPENDS fields instead of inserting a second block in the
// middle, so the existing letters keep their field indices.  Blocked layout was
// the original and is converted once at load (LATIN_PICK_INTERLEAVED).
#define LATIN_PICK_UPPER   0
#define LATIN_PICK_LOWER   1
#define latin_pick_field(slot, upper) (uint8_t)((slot) * 2u + ((upper) ? LATIN_PICK_UPPER : LATIN_PICK_LOWER))

// A field can straddle a byte boundary (6 does not divide 8), so both helpers
// read/write a 16-bit window -- guarded by the array's own length, because the
// last field ends exactly on the final byte and buf[len] would be one past it.
// ⚠️ `len` is why these take the size rather than assuming LATIN_PICK_BYTES: the
// same accessor serves the 39-byte pick array and the 20-byte assignment map, and
// a hardcoded bound would read past the end of the shorter one.
static inline uint8_t latin_bits_get(const uint8_t* buf, uint8_t len, uint8_t field) {
    const uint16_t bit = (uint16_t)field * LATIN_PICK_BITS;
    const uint16_t by  = bit >> 3;
    const uint8_t  sh  = (uint8_t)(bit & 7u);
    uint16_t       w   = (uint16_t)buf[by];
    if (by + 1u < len) w |= (uint16_t)buf[by + 1u] << 8;
    return (uint8_t)((w >> sh) & (LATIN_PICK_MAX - 1u));
}

static inline void latin_bits_set(uint8_t* buf, uint8_t len, uint8_t field, uint8_t value) {
    const uint16_t bit  = (uint16_t)field * LATIN_PICK_BITS;
    const uint16_t by   = bit >> 3;
    const uint8_t  sh   = (uint8_t)(bit & 7u);
    const uint16_t mask = (uint16_t)(LATIN_PICK_MAX - 1u) << sh;
    const uint16_t val  = (uint16_t)(value & (LATIN_PICK_MAX - 1u)) << sh;
    buf[by]             = (uint8_t)((buf[by] & ~(uint8_t)mask) | (uint8_t)val);
    if (by + 1u < len) {
        buf[by + 1u] = (uint8_t)((buf[by + 1u] & ~(uint8_t)(mask >> 8)) | (uint8_t)(val >> 8));
    }
}

static inline uint8_t latin_pick_get(const uint8_t* ex, uint8_t field) {
    return latin_bits_get(ex, LATIN_PICK_BYTES, field);
}

static inline void latin_pick_set(uint8_t* ex, uint8_t field, uint8_t value) {
    latin_bits_set(ex, LATIN_PICK_BYTES, field, value);
}

// The letter whose variation row this key hosts, or -1 for "none".  Unassigned
// (or a stale value past the alphabet) falls back to the key's OWN letter, so a
// fresh EEPROM and a corrupt one both behave like the un-remapped keyboard — but
// only slots 0..25 have an own letter to fall back to.  An unassigned punctuation
// key has no row at all, which is what keeps `,` a comma until someone maps it.
static inline int8_t latin_assign_get(const uint8_t* assign, uint8_t slot) {
    const uint8_t v = latin_bits_get(assign, LATIN_ASSIGN_BYTES, slot);
    if (v < LATIN_LETTER_TARGETS) return (int8_t)v;
    return (slot < LATIN_LETTER_TARGETS) ? (int8_t)slot : -1;
}

static inline void latin_assign_set(uint8_t* assign, uint8_t slot, uint8_t letter) {
    latin_bits_set(assign, LATIN_ASSIGN_BYTES, slot, letter);
}
