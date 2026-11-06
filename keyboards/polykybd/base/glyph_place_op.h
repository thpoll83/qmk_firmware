// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdint.h>
#include <stdbool.h>

// The placement op byte of HID cmd 30 (glyph script), protocol v26:
// data[HID_DATA_IDX+1] = 0 keep the stored placement, 1 centre, 2 top-left.
//
// Zero means KEEP, not centre, because every report is zero-padded: an older host
// that sets a script sends a zero here, and must not reset a placement chosen with a
// newer host. The stored placement is the op minus one (enum poly_glyph_place in
// state.h: GLYPH_PLACE_CENTRE = 0, GLYPH_PLACE_TOP_LEFT = 1). Header-only and
// unit-tested (make test:polykybd_glyph_place_op).

#define GLYPH_PLACE_OP_KEEP      0u
#define GLYPH_PLACE_OP_CENTRE    1u
#define GLYPH_PLACE_OP_TOP_LEFT  2u

typedef enum {
    GLYPH_PLACE_OP_IS_KEEP,     // leave the stored placement as it is
    GLYPH_PLACE_OP_IS_SET,      // *place holds the placement to store
    GLYPH_PLACE_OP_IS_INVALID,  // refuse the whole report: nothing may change
} glyph_place_op_kind_t;

// Decodes `op` against `count` placements (GLYPH_PLACE_COUNT). Writes *place only
// for GLYPH_PLACE_OP_IS_SET.
static inline glyph_place_op_kind_t glyph_place_op_decode(uint8_t op, uint8_t count, uint8_t *place) {
    if (op == GLYPH_PLACE_OP_KEEP) {
        return GLYPH_PLACE_OP_IS_KEEP;
    }
    if (op > count) {
        return GLYPH_PLACE_OP_IS_INVALID;
    }
    *place = (uint8_t)(op - 1u);
    return GLYPH_PLACE_OP_IS_SET;
}
