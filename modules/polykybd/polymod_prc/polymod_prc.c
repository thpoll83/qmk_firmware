// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Decoder for PRC-coded 1-bit images. See polymod_prc.h; the reference encoder
// and decoder are tools/prc_tool.py (and PolyKybdHost polyhost/util/prc_codec.py).

#include "polymod_prc.h"

#include <string.h>

// Neighbours as (dy, dx), most significant context bit first. Only pixels that
// are already decoded: the two rows above and the two to the left.
static const int8_t TEMPLATE_DY[10] = {-1, -1, -1, 0, 0, -2, -2, -2, -1, -1};
static const int8_t TEMPLATE_DX[10] = {-1, 0, 1, -2, -1, -1, 0, 1, -2, 2};

#define RC_TOP (1u << 24)

typedef struct {
    const uint8_t *src;
    uint8_t        len;
    uint8_t        pos;
} prc_reader_t;

static inline __attribute__((always_inline)) uint8_t next_byte(prc_reader_t *r) {
    uint8_t b = r->pos < r->len ? r->src[r->pos] : 0;
    if (r->pos < 255) r->pos++;
    return b;
}

// Pixel of a frame_w x frame_h frame; anything outside the frame reads as 0.
static inline uint8_t pixel(const uint8_t *overlay, uint8_t frame_w, uint8_t frame_h, int16_t y, int16_t x) {
    if (y < 0 || x < 0 || y >= frame_h || x >= frame_w) return 0;
    uint16_t bit = (uint16_t)y * frame_w + (uint16_t)x;
    return (overlay[bit >> 3] >> (7 - (bit & 7))) & 1;
}

// The decoder body. always_inline so prc_decode_roi(), which passes the frame
// size as constants, compiles to the same constant-folded loop it always had;
// prc_decode_roi_in() gets a copy that reads the size at run time.
static inline __attribute__((always_inline)) bool decode_roi(uint8_t *overlay, uint8_t frame_w, uint8_t frame_h,
                                                             uint8_t top, uint8_t left, uint8_t height, uint8_t width,
                                                             const uint8_t *payload, uint8_t len, const uint8_t *table) {
    if (height == 0 || width == 0) return false;
    if ((uint16_t)top + height > frame_h || (uint16_t)left + width > frame_w) return false;

    memset(overlay, 0, ((uint16_t)frame_w * frame_h + 7) / 8);
    prc_reader_t r = {payload, len, 0};
    uint32_t range = 0xFFFFFFFFu;
    uint32_t code  = 0;
    for (uint8_t i = 0; i < 4; i++) {
        code = (code << 8) | next_byte(&r);
    }

    for (uint8_t ry = 0; ry < height; ry++) {
        int16_t y = (int16_t)top + ry;
        for (uint8_t rx = 0; rx < width; rx++) {
            int16_t  x   = (int16_t)left + rx;
            uint16_t ctx = 0;
            for (uint8_t t = 0; t < 10; t++) {
                ctx = (uint16_t)((ctx << 1) | pixel(overlay, frame_w, frame_h, y + TEMPLATE_DY[t], x + TEMPLATE_DX[t]));
            }
            uint32_t bound = (range >> 8) * table[ctx];
            uint8_t  bit;
            if (code < bound) {
                range = bound;
                bit   = 0;
            } else {
                code -= bound;
                range -= bound;
                bit = 1;
            }
            while (range < RC_TOP) {
                range <<= 8;
                code = (code << 8) | next_byte(&r);
            }
            if (bit) {
                uint16_t b = (uint16_t)y * frame_w + (uint16_t)x;
                overlay[b >> 3] |= (uint8_t)(0x80u >> (b & 7));
            }
        }
    }
    return true;
}

bool prc_decode_roi(uint8_t *overlay, uint8_t top, uint8_t left, uint8_t height, uint8_t width,
                    const uint8_t *payload, uint8_t len, const uint8_t *table) {
    return decode_roi(overlay, PRC_FRAME_W, PRC_FRAME_H, top, left, height, width, payload, len, table);
}

bool prc_decode_roi_in(uint8_t *frame, uint8_t frame_w, uint8_t frame_h, uint8_t top, uint8_t left,
                       uint8_t height, uint8_t width, const uint8_t *payload, uint8_t len,
                       const uint8_t *table) {
    return decode_roi(frame, frame_w, frame_h, top, left, height, width, payload, len, table);
}
