// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Column-native (OLED page format) glyph decoding, in one place.
//
// Five separate loops used to re-derive this: kdisp_write_gfx_char,
// kdisp_draw_glyph_half_at and kdisp_draw_glyph_double_at in base/disp_array.c,
// and pdraw_glyph / pdraw_glyph_half in split42/status_oled.c. They exist
// separately because the plotter writes to a file-scope scratch buffer through
// macros and takes no target, so the portrait status OLED could not reuse it and
// re-implemented the iteration around its own pset().
//
// Duplicating the *decode* is what actually cost a bug: pgm_read_byte() returns
// uint8_t and zero-extends, so reading a glyph's signed xOffset/yOffset through it
// turns a text glyph's yOffset of -8 into 248 and plots the glyph off-screen. That
// shipped in the split42 portrait copy (PR #149) while disp_array.c's copy had the
// (int8_t) cast all along — the exact failure mode you get from re-deriving logic
// instead of sharing it. These accessors make the signedness impossible to get
// wrong at a call site.
//
// Everything here is `static inline` over pgm_read_*, so the generated code is the
// same as the open-coded version — verified with tools/compare_codegen.py.

#include "fonts/gfxfont.h"

#include <stdint.h>
#include <stdbool.h>

#ifndef pgm_read_byte
#    include "progmem.h"
#endif

// --- metrics -------------------------------------------------------------

static inline uint8_t glyph_width(const GFXglyph *g) {
    return pgm_read_byte(&g->width);
}

static inline uint8_t glyph_height(const GFXglyph *g) {
    return pgm_read_byte(&g->height);
}

static inline uint8_t glyph_x_advance(const GFXglyph *g) {
    return pgm_read_byte(&g->xAdvance);
}

// ⚠️ SIGNED. xOffset/yOffset are int8_t in the GFX format and are routinely
// negative (every text glyph sits above the baseline, so yOffset < 0; the icons
// are -15/-16). Reading them through the raw pgm_read_byte() zero-extends and
// silently plots the glyph off-screen.
static inline int8_t glyph_x_offset(const GFXglyph *g) {
    return (int8_t)pgm_read_byte(&g->xOffset);
}

static inline int8_t glyph_y_offset(const GFXglyph *g) {
    return (int8_t)pgm_read_byte(&g->yOffset);
}

static inline uint16_t glyph_bitmap_offset(const GFXglyph *g) {
    return pgm_read_word(&g->bitmapOffset);
}

// --- column-native bitmap ------------------------------------------------

// Page-bytes per COLUMN for a glyph of this height. The bitmap is column-native:
// one byte is 8 VERTICAL pixels, LSB = top of the page, so a glyph occupies
// width * col_bytes whole bytes. This is NOT the classic Adafruit row-major
// layout — a row-major reader produces garbage that looks like dither noise.
static inline uint8_t glyph_col_bytes(uint8_t height) {
    return (uint8_t)((height + 7u) >> 3);
}

// True when the pixel at (gx, gy) within the glyph is lit. `bitmap` is the font's
// bitmap base, `bo` the glyph's bitmapOffset, `col_bytes` the value above.
//
// Use this only where the iteration order is not row-major (the 2x2 downsample
// probes four scattered source pixels). For a row loop use the row_base/row_mask
// pair below instead: both are loop-invariant across a row, and folding them into
// a single per-pixel call measurably grew three blit functions (+18, +8 and +7
// instructions) because the compiler stopped hoisting them.
static inline bool glyph_pixel_lit(const uint8_t *bitmap, uint16_t bo, uint8_t col_bytes, uint16_t gx, uint16_t gy) {
    const uint8_t byte = pgm_read_byte(&bitmap[bo + (uint16_t)(gx * col_bytes) + (gy >> 3)]);
    return (byte & (uint8_t)(1u << (gy & 7u))) != 0u;
}

// Row-invariant halves of the lookup above, so a `for gy { for gx { } }` blit keeps
// the shape the compiler can hoist.
static inline uint16_t glyph_row_base(uint16_t bo, uint16_t gy) {
    return (uint16_t)(bo + (gy >> 3));
}

static inline uint8_t glyph_row_mask(uint16_t gy) {
    return (uint8_t)(1u << (gy & 7u));
}

static inline bool glyph_row_pixel_lit(const uint8_t *bitmap, uint16_t row_base, uint8_t row_mask, uint8_t col_bytes, uint16_t gx) {
    return (pgm_read_byte(&bitmap[row_base + (uint16_t)(gx * col_bytes)]) & row_mask) != 0u;
}

static inline const uint8_t *font_bitmap(const GFXfont *f) {
    return (const uint8_t *)pgm_read_ptr(&f->bitmap);
}

static inline uint8_t font_y_advance(const GFXfont *f) {
    return pgm_read_byte(&f->yAdvance);
}
