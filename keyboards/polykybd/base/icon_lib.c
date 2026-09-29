// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// PlyI overlay icon library: validation, blit and the cmd 42 pair list. The
// format and contract are in icon_lib.h.
//
// Every header, record and glyph is copied out with memcpy rather than cast in
// place. The bundle is file-controlled data on the UNSIGNED resource transport,
// so an offset it names must never decide the alignment of a load: an unaligned
// wide access is a HardFault on the M0+ (see the glyph_off note in fontpack.c).

#include "icon_lib.h"
#include "fontpack.h"
#include "map_codec.h"
#include "polymod_crc32.h"
#include <string.h>

static const uint8_t *s_base;
static uint32_t       s_total;
static uint8_t        s_n_records;
static uint16_t       s_count;
static bool           s_present;
static fontpack_font_t s_rec[ICONLIB_MAX_RECORDS];

static uint32_t crc_region(const uint8_t *p, uint32_t len) {
    uint32_t crc = 0;
    while (len) {
        uint16_t chunk = (len > 0x8000u) ? 0x8000u : (uint16_t)len;
        crc = crc32_1byte(p, chunk, crc);
        p   += chunk;
        len -= chunk;
    }
    return crc;
}

void iconlib_unload(void) {
    s_base      = NULL;
    s_total     = 0;
    s_n_records = 0;
    s_count     = 0;
    s_present   = false;
}

bool iconlib_present(void) { return s_present; }
uint16_t iconlib_count(void) { return s_count; }

bool iconlib_load_at(const uint8_t *base, uint32_t cap, uint16_t *out_ver) {
    iconlib_unload();
    if (out_ver) *out_ver = 0;
    if (!base) return false;

    fontpack_header_t h;
    memcpy(&h, base, sizeof(h));
    if (memcmp(h.magic, "PlyI", 4) != 0) return false;   // erased / not an icon bundle
    if (h.abi_version != FONTPACK_ABI_VERSION) return false;
    if (cap && h.total_size > cap) return false;

    if (h.font_count == 0) {                // the wipe sentinel: present, no icons
        if (h.total_size != sizeof(fontpack_header_t)) return false;
        s_present = true;
        if (out_ver) *out_ver = (uint16_t)h.content_version;
        return true;
    }
    if (h.font_table_off != sizeof(fontpack_header_t)) return false;
    if (h.font_count > ICONLIB_MAX_RECORDS) return false;
    if (h.total_size <= sizeof(fontpack_header_t)) return false;
    if (h.font_table_off + h.font_count * sizeof(fontpack_font_t) > h.total_size) return false;
    if (crc_region(base + sizeof(fontpack_header_t), h.total_size - sizeof(fontpack_header_t)) != h.crc32) {
        return false;
    }

    uint32_t next = 0;                      // records are contiguous from id 0
    for (uint32_t i = 0; i < h.font_count; ++i) {
        fontpack_font_t r;
        memcpy(&r, base + h.font_table_off + i * sizeof(fontpack_font_t), sizeof(r));
        if (r.first != next || r.last < r.first || r.last > 0xFFFEu) return false;
        if (r.glyph_off >= h.total_size || r.bitmap_off >= h.total_size) return false;
        // Spans, not counts: see the #264 note in fontpack.c. last >= first holds.
        uint32_t span     = r.last - r.first;
        uint32_t capacity = (h.total_size - r.glyph_off) / sizeof(GFXglyph);
        if (capacity == 0u || span > capacity - 1u) return false;
        s_rec[i] = r;
        next     = r.last + 1u;
    }
    s_base      = base;
    s_total     = h.total_size;
    s_n_records = (uint8_t)h.font_count;
    s_count     = (uint16_t)next;
    s_present   = true;
    if (out_ver) *out_ver = (uint16_t)h.content_version;
    return true;
}

bool iconlib_blit(uint16_t id, uint8_t *frame) {
    if (!s_base || !frame) return false;
    const fontpack_font_t *r = NULL;
    for (uint8_t i = 0; i < s_n_records; ++i) {
        if (id >= s_rec[i].first && id <= s_rec[i].last) {
            r = &s_rec[i];
            break;
        }
    }
    if (!r) return false;

    GFXglyph g;
    memcpy(&g, s_base + r->glyph_off + (uint32_t)(id - r->first) * sizeof(GFXglyph), sizeof(g));
    // Signed int8 fields: a negative size or offset is a corrupt glyph, not a
    // large one, and must be refused before any arithmetic uses it.
    if (g.width <= 0 || g.height <= 0 || g.xOffset < 0 || g.yOffset < 0) return false;
    const uint32_t w = (uint32_t)g.width, hgt = (uint32_t)g.height;
    const uint32_t x0 = (uint32_t)g.xOffset, y0 = (uint32_t)g.yOffset;
    if (x0 + w > ICONLIB_FRAME_W || y0 + hgt > ICONLIB_FRAME_H) return false;
    const uint32_t nbytes = (w * hgt + 7u) / 8u;
    const uint32_t start  = r->bitmap_off + (uint32_t)g.bitmapOffset;
    if (start > s_total || nbytes > s_total - start) return false;

    const uint8_t *bits = s_base + start;
    memset(frame, 0, ICONLIB_FRAME_BYTES);
    uint32_t src = 0;
    for (uint32_t y = 0; y < hgt; ++y) {
        uint32_t dst = (y0 + y) * ICONLIB_FRAME_W + x0;
        for (uint32_t x = 0; x < w; ++x, ++src, ++dst) {
            if (bits[src >> 3] & (0x80u >> (src & 7u))) {
                frame[dst >> 3] |= (uint8_t)(0x80u >> (dst & 7u));
            }
        }
    }
    return true;
}

uint8_t iconlib_fill_pairs(const uint8_t *buf, uint8_t bytes, uint8_t width,
                           uint16_t n_slots, uint8_t *(*slot_ptr)(uint16_t slot)) {
    if (!buf || !slot_ptr || width < 8u || width > 16u) return 0;
    const uint16_t pairs = (uint16_t)(((uint16_t)bytes * 8u / width) / 2u);
    for (uint16_t p = 0; p < pairs; ++p) {
        uint16_t slot = map_codec_read(buf, (uint16_t)(2u * p), width);
        uint16_t id   = map_codec_read(buf, (uint16_t)(2u * p + 1u), width);
        uint8_t *frame = (slot < n_slots) ? slot_ptr(slot) : NULL;
        if (!frame || !iconlib_blit(id, frame)) {
            return (uint8_t)p;
        }
    }
    return ICONLIB_FILL_OK;
}
