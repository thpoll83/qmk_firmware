// Copyright 2026 Thomas Pollak
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// A packed bit array over uint8_t bytes, bit i at m[i/8] bit (i%8). Size an
// array for N bits as uint8_t m[BITSET_BYTES(N)]. No bounds checks: the caller
// owns the index range, as with any array.

#include <stdbool.h>
#include <stdint.h>

#define BITSET_BYTES(n) (((n) + 7u) / 8u)

static inline bool bitset_get(const uint8_t *m, uint8_t i) {
    return (m[i >> 3] >> (i & 7u)) & 1u;
}

static inline void bitset_put(uint8_t *m, uint8_t i, bool v) {
    if (v) m[i >> 3] |= (uint8_t)(1u << (i & 7u));
    else   m[i >> 3] &= (uint8_t)~(1u << (i & 7u));
}
