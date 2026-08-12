// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
/* Exhaustive round-trip of the 6-bit latin_pick_get/set packing.
 *
 * Compiled for the HOST, with the accessors copied verbatim from state.h -- the
 * point is the bit arithmetic, and a field that straddles a byte boundary (6
 * does not divide 8) plus a last field that ends exactly on the final byte is
 * precisely where an off-by-one lives.  Checks every field x every value, and
 * that writing one field never disturbs another.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define LATIN_PICK_BITS   6
#define LATIN_PICK_MAX    (1u << LATIN_PICK_BITS)
#define LATIN_PICK_FIELDS (26 * 2)
#define LATIN_PICK_BYTES  ((LATIN_PICK_FIELDS * LATIN_PICK_BITS + 7) / 8)

static inline uint8_t latin_pick_get(const uint8_t* ex, uint8_t field) {
    const uint16_t bit = (uint16_t)field * LATIN_PICK_BITS;
    const uint16_t by  = bit >> 3;
    const uint8_t  sh  = (uint8_t)(bit & 7u);
    uint16_t w = (uint16_t)ex[by];
    if (by + 1u < LATIN_PICK_BYTES) w |= (uint16_t)ex[by + 1u] << 8;
    return (uint8_t)((w >> sh) & (LATIN_PICK_MAX - 1u));
}

static inline void latin_pick_set(uint8_t* ex, uint8_t field, uint8_t value) {
    const uint16_t bit  = (uint16_t)field * LATIN_PICK_BITS;
    const uint16_t by   = bit >> 3;
    const uint8_t  sh   = (uint8_t)(bit & 7u);
    const uint16_t mask = (uint16_t)(LATIN_PICK_MAX - 1u) << sh;
    const uint16_t val  = (uint16_t)(value & (LATIN_PICK_MAX - 1u)) << sh;
    ex[by] = (uint8_t)((ex[by] & ~(uint8_t)mask) | (uint8_t)val);
    if (by + 1u < LATIN_PICK_BYTES) {
        ex[by + 1u] = (uint8_t)((ex[by + 1u] & ~(uint8_t)(mask >> 8)) | (uint8_t)(val >> 8));
    }
}

int main(void) {
    int fail = 0;
    printf("LATIN_PICK_BYTES = %d (fields %d x %d bits = %d bits)\n",
           LATIN_PICK_BYTES, LATIN_PICK_FIELDS, LATIN_PICK_BITS,
           LATIN_PICK_FIELDS * LATIN_PICK_BITS);

    /* 1. every field holds every value */
    for (int f = 0; f < LATIN_PICK_FIELDS; f++) {
        for (unsigned v = 0; v < LATIN_PICK_MAX; v++) {
            uint8_t ex[LATIN_PICK_BYTES];
            memset(ex, 0, sizeof ex);
            latin_pick_set(ex, (uint8_t)f, (uint8_t)v);
            uint8_t got = latin_pick_get(ex, (uint8_t)f);
            if (got != v) { printf("  FAIL field %d value %u -> %u\n", f, v, got); fail = 1; }
        }
    }

    /* 2. all fields distinct simultaneously (each holds f % 64) */
    {
        uint8_t ex[LATIN_PICK_BYTES];
        memset(ex, 0, sizeof ex);
        for (int f = 0; f < LATIN_PICK_FIELDS; f++)
            latin_pick_set(ex, (uint8_t)f, (uint8_t)(f % LATIN_PICK_MAX));
        for (int f = 0; f < LATIN_PICK_FIELDS; f++) {
            uint8_t want = (uint8_t)(f % LATIN_PICK_MAX), got = latin_pick_get(ex, (uint8_t)f);
            if (got != want) { printf("  FAIL coexist field %d: %u != %u\n", f, got, want); fail = 1; }
        }
    }

    /* 3. writing one field disturbs no other, starting from all-ones */
    for (int f = 0; f < LATIN_PICK_FIELDS; f++) {
        uint8_t ex[LATIN_PICK_BYTES], ref[LATIN_PICK_FIELDS];
        memset(ex, 0xFF, sizeof ex);
        for (int i = 0; i < LATIN_PICK_FIELDS; i++) ref[i] = latin_pick_get(ex, (uint8_t)i);
        latin_pick_set(ex, (uint8_t)f, 0);
        for (int i = 0; i < LATIN_PICK_FIELDS; i++) {
            uint8_t want = (i == f) ? 0 : ref[i], got = latin_pick_get(ex, (uint8_t)i);
            if (got != want) {
                printf("  FAIL bleed: set field %d, field %d %u -> %u\n", f, i, ref[i], got);
                fail = 1;
            }
        }
    }

    /* 4. the legacy nibble migration preserves every old pick */
    {
        uint8_t legacy[26], ex[LATIN_PICK_BYTES];
        for (int i = 0; i < 26; i++) legacy[i] = (uint8_t)((i % 16) << 4 | ((i * 7) % 16));
        memset(ex, 0, sizeof ex);
        for (int i = 0; i < 26; i++) {
            latin_pick_set(ex, (uint8_t)i,        (uint8_t)(legacy[i] >> 4));
            latin_pick_set(ex, (uint8_t)(26 + i), (uint8_t)(legacy[i] & 0x0F));
        }
        for (int i = 0; i < 26; i++) {
            if (latin_pick_get(ex, (uint8_t)i) != (legacy[i] >> 4) ||
                latin_pick_get(ex, (uint8_t)(26 + i)) != (legacy[i] & 0x0F)) {
                printf("  FAIL migration letter %d\n", i); fail = 1;
            }
        }
    }

    printf(fail ? "FAILED\n" : "all packing checks passed\n");
    return fail;
}
