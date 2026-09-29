// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Decoder for context-coded overlay images. See ctx_codec.h; the reference
// implementation is PolyKybdHost polyhost/util/ctx_codec.py (decode()).

#include "ctx_codec.h"

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
} ctx_reader_t;

static inline uint8_t next_byte(ctx_reader_t *r) {
    uint8_t b = r->pos < r->len ? r->src[r->pos] : 0;
    if (r->pos < 255) r->pos++;
    return b;
}

// Pixel of the 72x40 frame; anything outside the frame reads as 0.
static inline uint8_t pixel(const uint8_t *overlay, int16_t y, int16_t x) {
    if (y < 0 || x < 0 || y >= CTX_FRAME_H || x >= CTX_FRAME_W) return 0;
    uint16_t bit = (uint16_t)y * CTX_FRAME_W + (uint16_t)x;
    return (overlay[bit >> 3] >> (7 - (bit & 7))) & 1;
}

bool ctx_decode_roi(uint8_t *overlay, uint8_t top, uint8_t left, uint8_t height, uint8_t width,
                    const uint8_t *payload, uint8_t len, const uint8_t *table) {
    if (height == 0 || width == 0) return false;
    if ((uint16_t)top + height > CTX_FRAME_H || (uint16_t)left + width > CTX_FRAME_W) return false;

    memset(overlay, 0, CTX_FRAME_BYTES);
    ctx_reader_t r = {payload, len, 0};
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
                ctx = (uint16_t)((ctx << 1) | pixel(overlay, y + TEMPLATE_DY[t], x + TEMPLATE_DX[t]));
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
                uint16_t b = (uint16_t)y * CTX_FRAME_W + (uint16_t)x;
                overlay[b >> 3] |= (uint8_t)(0x80u >> (b & 7));
            }
        }
    }
    return true;
}

uint8_t ctx_parse_record(const uint8_t *buf, uint8_t avail, ctx_record_t *out) {
    if (avail < CTX_RECORD_HDR || buf[0] == 0) return 0;
    // Bits after the keycode byte: 40 bits in buf[1..5].
    uint64_t f = 0;
    for (uint8_t i = 1; i < CTX_RECORD_HDR; i++) {
        f = (f << 8) | buf[i];
    }
    if (f & 0x0F) return 0;   // reserved
    ctx_record_t r;
    r.keycode  = buf[0];
    r.modifier = (uint8_t)((f >> 36) & 0x0F);
    r.top      = (uint8_t)((f >> 30) & 0x3F);
    r.left     = (uint8_t)((f >> 23) & 0x7F);
    r.height   = (uint8_t)(((f >> 17) & 0x3F) + 1);
    r.width    = (uint8_t)(((f >> 10) & 0x7F) + 1);
    r.len      = (uint8_t)((f >> 4) & 0x3F);
    r.payload  = buf + CTX_RECORD_HDR;
    if ((uint16_t)r.top + r.height > CTX_FRAME_H || (uint16_t)r.left + r.width > CTX_FRAME_W) return 0;
    if (r.len > avail - CTX_RECORD_HDR) return 0;
    *out = r;
    return (uint8_t)(CTX_RECORD_HDR + r.len);
}
