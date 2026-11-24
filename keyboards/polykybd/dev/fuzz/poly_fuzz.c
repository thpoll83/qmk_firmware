// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Fuzz entry points (poly_fuzz.h). Every buffer a parser reads or writes is a
// separate heap allocation of the exact size the firmware gives it, so
// AddressSanitizer reports a read or write one byte past it. In the firmware the
// same buffers sit next to other data, where an overrun is not detected.

#include "poly_fuzz.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

// config.h derives the report sizes from QMK's RAW_EPSIZE (64 on the RP2040).
#ifndef RAW_EPSIZE
#    define RAW_EPSIZE 64
#endif
#include "config.h"

#include "base/overlay.h"
#include "base/prc_record.h"
#include "base/fontpack.h"
#include "base/icon_lib.h"
#include "base/emoji_dict.h"
#include "lib/drivers/disp_array.h"   // SCREEN_WIDTH, SCREEN_HEIGHT
#include "polymod_prc.h"
#include "prc_table_v1.h"
#include "polymod_crc32.h"

#define OVERLAY_ROW_BYTES (SCREEN_WIDTH * SCREEN_HEIGHT / 8)   // 360
#define OVERLAY_ROW_BITS  (SCREEN_WIDTH * SCREEN_HEIGHT)       // 2880
#define ROI_HEADER_BYTES  5
// One upload has at most this many reports. A full uncompressed 72x40 frame is
// 47 reports; a compressed one can need more.
#define ROI_MAX_REPORTS   128

static uint8_t *dup_exact(const uint8_t *data, size_t size) {
    uint8_t *p = (uint8_t *)malloc(size ? size : 1);
    if (p && size) memcpy(p, data, size);
    return p;
}

static bool bit_at(const uint8_t *buf, uint32_t i) {
    return (buf[i / 8] & (0x80u >> (i % 8))) != 0;
}

// ── cmd 18/19: ROI overlay ─────────────────────────────────────────────────
//
// Input: the 5-byte ROI header (what hid_cmd_18 passes from &data[HID_DATA_IDX]),
// then the fragment data. The first report carries ROI_START data bytes after
// the header, every later report ROI_MAX bytes, zero padded like a HID report.
// The loop is the one in multicore_exec.c (CORE1_CMD_ROI_UPDATE).
int poly_fuzz_overlay_roi(const uint8_t *data, size_t size) {
    if (size < ROI_HEADER_BYTES) return POLY_FUZZ_OK;
    uint8_t *hdr = dup_exact(data, ROI_HEADER_BYTES);
    reset_fragment_context();
    (void)set_fragment_context_from_buffer(hdr);
    free(hdr);
    const roi_update_data_t roi = get_fragment_context()->roi;

    // 1: the clamp leaves a box inside the keycap, with x <= xx and y <= yy.
    if (roi.x >= SCREEN_WIDTH || roi.y >= SCREEN_HEIGHT || roi.xx > SCREEN_WIDTH || roi.yy > SCREEN_HEIGHT ||
        roi.x > roi.xx || roi.y > roi.yy) {
        return 1;
    }

    uint8_t *row  = (uint8_t *)malloc(OVERLAY_ROW_BYTES);
    uint8_t *orig = (uint8_t *)malloc(OVERLAY_ROW_BYTES);
    if (!row || !orig) abort();
    for (int i = 0; i < OVERLAY_ROW_BYTES; ++i) orig[i] = (uint8_t)(0xA5u ^ (uint8_t)i);
    memcpy(row, orig, OVERLAY_ROW_BYTES);

    int      rc        = POLY_FUZZ_OK;
    size_t   pos       = ROI_HEADER_BYTES;
    uint16_t bit_index = 0;
    for (int report = 0; report < ROI_MAX_REPORTS && pos < size; ++report) {
        uint8_t data_len = ROI_MAX;
        if (bit_index == 0) {
            bit_index = (uint16_t)(roi.y * SCREEN_WIDTH + roi.x);
            data_len  = ROI_START;
        }
        uint8_t *chunk = (uint8_t *)calloc(data_len, 1);
        if (!chunk) abort();
        size_t   n     = size - pos < data_len ? size - pos : data_len;
        memcpy(chunk, data + pos, n);
        pos += n;
        bit_index = copy_rectangle_to_overlay(bit_index, row, chunk, &roi, data_len);
        free(chunk);
        // 2: the returned position is inside the row or the "done" value.
        if (bit_index > OVERLAY_ROW_BITS) {
            rc = 2;
            break;
        }
        if (bit_index >= OVERLAY_ROW_BITS) break;   // the image is complete
    }

    // 3: no pixel outside the ROI box changed.
    if (rc == POLY_FUZZ_OK) {
        for (uint32_t i = 0; i < OVERLAY_ROW_BITS; ++i) {
            uint32_t x = i % SCREEN_WIDTH, y = i / SCREEN_WIDTH;
            bool inside = x >= roi.x && x < roi.xx && y >= roi.y && y < roi.yy;
            if (!inside && bit_at(row, i) != bit_at(orig, i)) {
                rc = 3;
                break;
            }
        }
    }
    free(row);
    free(orig);
    return rc;
}

// ── cmd 41: PRC records ────────────────────────────────────────────────────
//
// Input: the report payload, at most ROI_MAX bytes (what
// receive_prc_overlay_report gets). The loop is the one in fill_overlay.c.
int poly_fuzz_prc_report(const uint8_t *data, size_t size) {
    if (size > ROI_MAX) size = ROI_MAX;
    uint8_t *report = dup_exact(data, size);
    uint8_t *frame  = (uint8_t *)malloc(PRC_FRAME_BYTES);
    int      rc     = POLY_FUZZ_OK;
    uint8_t  avail  = (uint8_t)size;
    uint8_t  pos    = 0;
    prc_record_t r;
    uint8_t  n;
    while (pos <= avail && (n = prc_parse_record(report + pos, (uint8_t)(avail - pos), &r)) != 0) {
        // 1: the record lies inside the report, and its box inside the frame.
        if (n > avail - pos || n != PRC_RECORD_HDR + r.len || r.payload != report + pos + PRC_RECORD_HDR ||
            r.height == 0 || r.width == 0 || r.top + r.height > PRC_FRAME_H || r.left + r.width > PRC_FRAME_W) {
            rc = 1;
            break;
        }
        pos += n;
        // 2: a box the parser accepted is one the decoder accepts.
        if (!prc_decode_roi(frame, r.top, r.left, r.height, r.width, r.payload, r.len, prc_table_v1)) {
            rc = 2;
            break;
        }
    }
    free(frame);
    free(report);
    return rc;
}

// ── Bundles: PlyF fonts, PlyI icons, emoji dictionary ─────────────────────

// Writes the CRC32 of [32 .. total_size) into the header when total_size fits
// the buffer, so the loaders' later checks are reached. Returns total_size.
static uint32_t fix_bundle_crc(uint8_t *b, size_t size) {
    fontpack_header_t h;
    memcpy(&h, b, sizeof(h));
    if (h.total_size >= sizeof(h) && h.total_size <= size) {
        h.crc32 = crc32_large(b + sizeof(h), h.total_size - sizeof(h), 0);
        memcpy(b, &h, sizeof(h));
    }
    return h.total_size;
}

static bool font_eq(const GFXfont *a, const GFXfont *b) {
    return a->bitmap == b->bitmap && a->glyph == b->glyph && a->first == b->first && a->last == b->last &&
           a->yAdvance == b->yAdvance;
}

// Input: a PlyF image. fontpack_load_at() has no slot cap (the boot loader passes
// the slot size), so an image whose total_size is larger than the input is
// skipped: in flash, those bytes belong to the slot and are readable.
int poly_fuzz_fontpack(const uint8_t *data, size_t size) {
    if (size < sizeof(fontpack_header_t)) return POLY_FUZZ_OK;
    uint8_t *b     = dup_exact(data, size);
    uint32_t total = fix_bundle_crc(b, size);
    int      rc    = POLY_FUZZ_OK;
    if (total > size) {
        free(b);
        return POLY_FUZZ_OK;
    }
    static const GFXfont resident_font = {0};
    static const GFXfont *const resident[2] = {&resident_font, &resident_font};

    if (fontpack_load_at(b)) {
        fontpack_assemble(resident, 2);
        const fontpack_header_t *h   = (const fontpack_header_t *)b;
        const fontpack_font_t   *tbl = (const fontpack_font_t *)(b + h->font_table_off);
        const uint8_t            n   = fontpack_font_count();
        // The table holds FONTPACK_MAX_FONTS (200, private to fontpack.c) entries:
        // pack fonts past it are dropped, the first `m` in global order are kept.
        const uint8_t            m   = n > 200u - 2u ? 200u - 2u : n;
        // 1: every pack font that fits is in the table, after the resident fonts.
        if (n != h->font_count || g_all_font_count != 2u + m) {
            rc = 1;
        }
        // 2: every glyph array lies inside the image.
        for (uint8_t i = 0; rc == POLY_FUZZ_OK && i < m; ++i) {
            const GFXfont *f   = g_all_fonts[2 + i];
            const uint8_t *g0  = (const uint8_t *)f->glyph;
            uint64_t       len = ((uint64_t)f->last - f->first + 1u) * sizeof(GFXglyph);
            // (An array may overlap the header or the table: that is garbage
            // glyphs, but no read outside the image.)
            if (g0 < b || (uint64_t)(g0 - b) + len > total ||
                f->bitmap < b || f->bitmap >= b + total) {
                rc = 2;
            }
        }
        // 3: the pack fonts are in the order of their global index (the record's
        // `reserved` field), records with the same index in table order. Fonts are
        // compared by content, so two equal records may be in either order.
        if (rc == POLY_FUZZ_OK) {
            uint8_t order[256];
            for (uint8_t i = 0; i < n; ++i) order[i] = i;
            for (int i = 1; i < n; ++i) {   // stable insertion sort, int indices
                uint8_t v = order[i];
                int     j = i - 1;
                while (j >= 0 && tbl[order[j]].reserved > tbl[v].reserved) {
                    order[j + 1] = order[j];
                    --j;
                }
                order[j + 1] = v;
            }
            for (uint8_t i = 0; i < m; ++i) {
                const fontpack_font_t *t = &tbl[order[i]];
                GFXfont want = {
                    .bitmap   = b + t->bitmap_off,
                    .glyph    = (GFXglyph *)(void *)(b + t->glyph_off),
                    .first    = t->first,
                    .last     = t->last,
                    .yAdvance = (int8_t)t->yAdvance,
                };
                if (!font_eq(g_all_fonts[2 + i], &want)) {
                    rc = 3;
                    break;
                }
            }
        }
    }
    // Leave no pointer into the freed image behind.
    (void)fontpack_load_at(NULL);
    fontpack_assemble(resident, 0);
    free(b);
    return rc;
}

#define ICON_FUZZ_SLOTS 4
static uint8_t *s_icon_slots[ICON_FUZZ_SLOTS];
static uint8_t *icon_slot_ptr(uint16_t slot) {
    return slot < ICON_FUZZ_SLOTS ? s_icon_slots[slot] : NULL;
}

// Input: [width] [n] [n pair bytes, n <= OVERLAY_MAP_W_BYTES] [PlyI image].
int poly_fuzz_iconlib(const uint8_t *data, size_t size) {
    if (size < 2) return POLY_FUZZ_OK;
    const uint8_t width = data[0];
    uint8_t       n     = data[1];
    if (n > OVERLAY_MAP_W_BYTES) n = OVERLAY_MAP_W_BYTES;
    if (size < 2u + n + sizeof(fontpack_header_t)) return POLY_FUZZ_OK;
    uint8_t *pairs = dup_exact(data + 2, n);
    size_t   bsize = size - 2u - n;
    uint8_t *b     = dup_exact(data + 2 + n, bsize);
    (void)fix_bundle_crc(b, bsize);
    int      rc    = POLY_FUZZ_OK;
    uint16_t ver   = 0;

    if (iconlib_load_at(b, (uint32_t)bsize, &ver)) {
        uint8_t *frame = (uint8_t *)malloc(ICONLIB_FRAME_BYTES);
        uint16_t count = iconlib_count();
        // 1: an id past the library is refused.
        if (iconlib_blit(count, frame)) rc = 1;
        for (uint16_t id = 0; id < count && id < 1024u; ++id) {
            (void)iconlib_blit(id, frame);
        }
        free(frame);
        for (int i = 0; i < ICON_FUZZ_SLOTS; ++i) s_icon_slots[i] = (uint8_t *)malloc(ICONLIB_FRAME_BYTES);
        uint8_t bad = iconlib_fill_pairs(pairs, n, width, ICON_FUZZ_SLOTS, icon_slot_ptr);
        // 2: the result is "all applied" or the index of a pair in the list.
        if (rc == POLY_FUZZ_OK && bad != ICONLIB_FILL_OK && width >= 8 && width <= 16 &&
            bad >= ((uint16_t)n * 8u / width) / 2u) {
            rc = 2;
        }
        for (int i = 0; i < ICON_FUZZ_SLOTS; ++i) {
            free(s_icon_slots[i]);
            s_icon_slots[i] = NULL;
        }
    }
    iconlib_unload();
    free(b);
    free(pairs);
    return rc;
}

// Input: [n <= EMJDICT_QUERY_MAX] [skip] [n query bytes] [dictionary image].
// A query byte below 0x80 is that codepoint; above, a Latin-1 letter
// (U+00C0 + low 6 bits), so the fold table's accented letters are reached.
int poly_fuzz_emjdict(const uint8_t *data, size_t size) {
    if (size < 2) return POLY_FUZZ_OK;
    const uint8_t n    = (uint8_t)(data[0] % (EMJDICT_QUERY_MAX + 1u));
    const uint8_t skip = data[1];
    if (size < 2u + n + sizeof(fontpack_header_t)) return POLY_FUZZ_OK;
    uint32_t query[EMJDICT_QUERY_MAX];
    for (uint8_t i = 0; i < n; ++i) {
        uint8_t c = data[2 + i];
        query[i]  = c < 0x80u ? c : 0xC0u + (c & 0x3Fu);
    }
    size_t   bsize = size - 2u - n;
    uint8_t *b     = dup_exact(data + 2 + n, bsize);
    (void)fix_bundle_crc(b, bsize);
    int      rc    = POLY_FUZZ_OK;
    uint16_t ver   = 0;

    if (emjdict_load_at(b, (uint32_t)bsize, &ver)) {
        for (uint8_t i = 0; i < n; ++i) {
            bool maps = emjdict_maps_letter(query[i]);
            // 1: a letter the dictionary maps has a code.
            if (maps && emjdict_letter_code(query[i]) == EMJDICT_CODE_UNKNOWN) rc = 1;
        }
        uint32_t out[8];
        bool     typo = false;
        uint16_t total = emjdict_search(query, n, skip, out, 8, &typo);
        // 2: no more results than the dictionary has codepoints.
        if (rc == POLY_FUZZ_OK && total > emjdict_cp_count()) rc = 2;
    }
    emjdict_unload();
    free(b);
    return rc;
}
