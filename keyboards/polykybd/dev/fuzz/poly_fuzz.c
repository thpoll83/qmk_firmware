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
#include "base/host_tutorial_plan.h"
#include "base/macro_decode.h"
#include "base/attn_guide.h"
#include "base/ai_events.h"
#include "base/combo_store.h"
#include "base/map_codec.h"
#include "features/anim/key_attention.h"   // KEY_ATTN_PERIOD_MIN/MAX
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

// Abort on allocation failure, so it never reaches a parser as a NULL pointer.
static void *xmalloc(size_t size) {
    void *p = malloc(size ? size : 1);
    if (!p) abort();
    return p;
}

static void *xcalloc(size_t size) {
    void *p = calloc(size ? size : 1, 1);
    if (!p) abort();
    return p;
}

static uint8_t *dup_exact(const uint8_t *data, size_t size) {
    uint8_t *p = (uint8_t *)xmalloc(size);
    if (size) memcpy(p, data, size);
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

    uint8_t *row  = (uint8_t *)xmalloc(OVERLAY_ROW_BYTES);
    uint8_t *orig = (uint8_t *)xmalloc(OVERLAY_ROW_BYTES);
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
        uint8_t *chunk = (uint8_t *)xcalloc(data_len);
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
    uint8_t *frame  = (uint8_t *)xmalloc(PRC_FRAME_BYTES);
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
    const uint8_t n_res = (uint8_t)(sizeof resident / sizeof resident[0]);

    if (fontpack_load_at(b)) {
        fontpack_assemble(resident, n_res);
        const fontpack_header_t *h   = (const fontpack_header_t *)b;
        const fontpack_font_t   *tbl = (const fontpack_font_t *)(b + h->font_table_off);
        const uint8_t            n   = fontpack_font_count();
        // The table holds FONTPACK_MAX_FONTS entries: pack fonts past it are
        // dropped, the first `m` in global order are kept.
        const uint8_t            m   = n > FONTPACK_MAX_FONTS - n_res ? FONTPACK_MAX_FONTS - n_res : n;
        // 1: every pack font that fits is in the table, after the resident fonts.
        if (n != h->font_count || g_all_font_count != n_res + m) {
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
        uint8_t *frame = (uint8_t *)xmalloc(ICONLIB_FRAME_BYTES);
        uint16_t count = iconlib_count();
        // 1: an id past the library is refused.
        if (iconlib_blit(count, frame)) rc = 1;
        for (uint16_t id = 0; id < count && id < 1024u; ++id) {
            (void)iconlib_blit(id, frame);
        }
        free(frame);
        for (int i = 0; i < ICON_FUZZ_SLOTS; ++i) s_icon_slots[i] = (uint8_t *)xmalloc(ICONLIB_FRAME_BYTES);
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

// ── Board geometry for the HID command parsers ─────────────────────────────
//
// The handlers pass MATRIX_ROWS, MATRIX_COLS and DYNAMIC_KEYMAP_LAYER_COUNT of the
// variant (split72/config.h, split42/config.h). Those headers are not included here,
// so the values are copied; every input is parsed with both.
typedef struct {
    uint8_t rows, cols, layers;
} poly_fuzz_geom_t;
static const poly_fuzz_geom_t k_geoms[] = {
    {10, 8, 13},   // split72
    {8, 6, 13},    // split42
};
#define N_GEOMS (sizeof k_geoms / sizeof k_geoms[0])

// The report as the handler gets it: at most one raw HID report, in a buffer of
// exactly that many bytes, with `length` equal to its size.
static uint8_t *report_dup(const uint8_t *data, size_t size, uint8_t *length) {
    if (size > HOST_TUT_REPORT_BYTES) size = HOST_TUT_REPORT_BYTES;
    *length = (uint8_t)size;
    return dup_exact(data, size);
}

// ── cmd 49: host tutorial ──────────────────────────────────────────────────
//
// Input: one report (data[0] 'P', data[1] the command, data[2] the op, data[3..] the
// arguments), at most 64 bytes. Every input goes through all three parsers (TEXT,
// SPOTLIGHT, ARM), whatever data[2] is, with both geometries.
int poly_fuzz_host_tut(const uint8_t *data, size_t size) {
    uint8_t  length;
    uint8_t *r  = report_dup(data, size, &length);
    int      rc = POLY_FUZZ_OK;

    // TEXT.
    host_tut_text_t t, before;
    memset(&t, 0x5A, sizeof t);
    before = t;
    if (host_tut_parse_text(r, length, &t)) {
        const uint8_t n0 = t.len[0], n1 = t.len[1];
        // 1: both lines fit the buffer, and line 1 starts right after line 0.
        if ((unsigned)n0 + n1 > HOST_TUT_TEXT_BYTES || host_tut_text_line(&t, 0) != t.buf ||
            host_tut_text_line(&t, 1) != t.buf + n0) {
            rc = 1;
        }
        // 2: every byte is printable ASCII and is the byte of the report.
        for (unsigned k = 0; rc == POLY_FUZZ_OK && k < (unsigned)n0 + n1; ++k) {
            const unsigned src = k < n0 ? HOST_TUT_IDX_ARG + 1u + k : HOST_TUT_IDX_ARG + 2u + k;
            const uint8_t  ch  = (uint8_t)t.buf[k];
            if (ch < 0x20u || ch > 0x7Eu || src >= length || r[src] != ch) rc = 2;
        }
        // 3: the length bytes are the report's, and the checksum ignores unused bytes.
        if (rc == POLY_FUZZ_OK && (r[HOST_TUT_IDX_ARG] != n0 || r[HOST_TUT_IDX_ARG + 1u + n0] != n1)) rc = 3;
        if (rc == POLY_FUZZ_OK && host_tut_text_empty(&t) != (n0 == 0u && n1 == 0u)) rc = 3;
        if (rc == POLY_FUZZ_OK) {
            host_tut_text_t u = t;
            for (unsigned k = (unsigned)n0 + n1; k < HOST_TUT_TEXT_BYTES; ++k) u.buf[k] = (char)~u.buf[k];
            if (host_tut_state_crc(&t, true, NULL) != host_tut_state_crc(&u, true, NULL)) rc = 3;
        }
    } else if (memcmp(&t, &before, sizeof t) != 0) {
        rc = 4;   // 4: a refused TEXT leaves the output alone
    }

    for (unsigned gi = 0; rc == POLY_FUZZ_OK && gi < N_GEOMS; ++gi) {
        const poly_fuzz_geom_t g = k_geoms[gi];

        // SPOTLIGHT. `on` starts true here; a refused report is parsed again with it
        // false, so a write to *on before the refusal is caught whichever value it writes.
        bool     on = true;
        uint8_t *mask = (uint8_t *)xmalloc(HOST_TUT_MASK_BYTES);
        memset(mask, 0x5A, HOST_TUT_MASK_BYTES);
        if (host_tut_parse_spotlight(r, length, g.rows, g.cols, &on, mask)) {
            const unsigned bits = (unsigned)g.rows * g.cols;
            // 5: the mask is the report's, with no bit at or past rows * cols.
            if (length < HOST_TUT_IDX_ARG + 1u + HOST_TUT_MASK_BYTES ||
                memcmp(mask, r + HOST_TUT_IDX_ARG + 1u, HOST_TUT_MASK_BYTES) != 0 ||
                on != ((r[HOST_TUT_IDX_ARG] & HOST_TUT_SPOT_FLAG_ON) != 0u)) {
                rc = 5;
            }
            for (unsigned b = bits; rc == POLY_FUZZ_OK && b < HOST_TUT_MASK_BYTES * 8u; ++b) {
                if (mask[b / 8u] & (1u << (b % 8u))) rc = 5;
            }
            // 6: host_tut_mask_has() reads the same bits, and nothing outside the board.
            for (unsigned row = 0; rc == POLY_FUZZ_OK && row < 16u; ++row) {
                for (unsigned col = 0; col < 16u; ++col) {
                    const unsigned b    = row * g.cols + col;
                    const bool     want = row < g.rows && col < g.cols && (mask[b / 8u] & (1u << (b % 8u)));
                    if (host_tut_mask_has(mask, g.cols, (uint8_t)row, (uint8_t)col) != want) {
                        rc = 6;
                        break;
                    }
                }
            }
        } else {
            // 7: a refused SPOTLIGHT leaves both outputs alone, the mask and `on`.
            for (unsigned k = 0; k < HOST_TUT_MASK_BYTES; ++k) {
                if (mask[k] != 0x5A) rc = 7;
            }
            bool on2 = false;
            if (!on || host_tut_parse_spotlight(r, length, g.rows, g.cols, &on2, mask) || on2) rc = 7;
        }
        free(mask);
        if (rc != POLY_FUZZ_OK) break;

        // ARM.
        host_tut_conds_t c, cbefore;
        memset(&c, 0x5A, sizeof c);
        cbefore = c;
        if (host_tut_parse_arm(r, length, g.rows, g.cols, g.layers, &c)) {
            // 8: the count and every condition are in range and are the report's bytes.
            if (c.count > HOST_TUT_MAX_CONDS || c.matched != 0u ||
                HOST_TUT_IDX_ARG + 1u + 3u * c.count > length) {
                rc = 8;
            }
            for (uint8_t i = 0; rc == POLY_FUZZ_OK && i < c.count; ++i) {
                const host_tut_cond_t *k = &c.cond[i];
                const uint8_t         *p = r + HOST_TUT_IDX_ARG + 1u + 3u * i;
                bool ok = k->kind == p[0] && k->a == p[1] && k->b == p[2];
                if (k->kind == HOST_TUT_COND_KEY) ok = ok && k->a < g.rows && k->b < g.cols;
                else if (k->kind == HOST_TUT_COND_LAYER) ok = ok && k->a < g.layers && k->b == 0u;
                else if (k->kind != HOST_TUT_COND_KEYCODE) ok = false;
                if (!ok) rc = 8;
            }
            // 9: matching marks exactly the conditions that exist. Every key is pressed,
            // every condition's keycode is sent and every layer is on, so all of them match.
            if (rc == POLY_FUZZ_OK) {
                for (unsigned row = 0; row < g.rows; ++row) {
                    for (unsigned col = 0; col < g.cols; ++col) {
                        host_tut_note_key(&c, (uint8_t)row, (uint8_t)col, (uint16_t)(row << 8 | col), true);
                    }
                }
                for (uint8_t i = 0; i < c.count; ++i) {
                    host_tut_note_key(&c, 0, 0, (uint16_t)(c.cond[i].a | c.cond[i].b << 8), true);
                }
                host_tut_note_layers(&c, 0xFFFFFFFFu);
                if (c.matched != (uint8_t)((1u << c.count) - 1u)) rc = 9;
            }
        } else if (memcmp(&c, &cbefore, sizeof c) != 0) {
            rc = 10;   // 10: a refused ARM leaves the output alone
        }
    }
    free(r);
    return rc;
}

// ── Macro buffer decode ────────────────────────────────────────────────────
//
// Input: the macro body buffer (the EEPROM region poly_macro.c reads through
// body_read), at most POLY_FUZZ_MACRO_MAX bytes. The read callback reads an
// exact-size heap copy and records any offset at or past the end.
#define POLY_FUZZ_MACRO_MAX 1024u

typedef struct {
    const uint8_t *buf;
    uint16_t       end;
    bool           bad;
} macro_ctx_t;

static uint8_t macro_rd(uint16_t offset, void *ctx) {
    macro_ctx_t *m = (macro_ctx_t *)ctx;
    if (offset >= m->end) {
        m->bad = true;
        return 0;
    }
    return m->buf[offset];
}

int poly_fuzz_macro_decode(const uint8_t *data, size_t size) {
    if (size > POLY_FUZZ_MACRO_MAX) size = POLY_FUZZ_MACRO_MAX;
    uint8_t    *b   = dup_exact(data, size);
    macro_ctx_t m   = {b, (uint16_t)size, false};
    const uint16_t end = (uint16_t)size;
    int         rc  = POLY_FUZZ_OK;

    // 1: used is within the buffer, and every byte past it is NUL.
    const uint16_t used = poly_macro_used(macro_rd, &m, end);
    if (used > end) rc = 1;
    for (uint16_t i = used; rc == POLY_FUZZ_OK && i < end; ++i) {
        if (b[i] != 0) rc = 1;
    }
    // 2: intact is "the last byte is NUL".
    if (rc == POLY_FUZZ_OK && poly_macro_buffer_intact(macro_rd, &m, end) != (end > 0 && b[end - 1] == 0)) rc = 2;

    // 3: macro id starts right after its id-th NUL (a model scans for it), or is
    // `end`; starts never go down. Once one id is past the buffer, so is every later
    // one (checked for id 255).
    uint16_t prev = 0;
    for (unsigned id = 0; rc == POLY_FUZZ_OK && id < 256u; ++id) {
        const uint16_t s    = poly_macro_find(macro_rd, &m, (uint8_t)id, end);
        uint16_t       want = 0;
        for (unsigned nul = 0; nul < id && want < end; ++want) {
            if (b[want] == 0) ++nul;
            if (nul == id) { ++want; break; }
        }
        if (want > end) want = end;
        if (s != want) rc = 3;
        if (s > end || s < prev || (id == 0 && s != 0) || (id > 0 && s < end && b[s - 1] != 0)) rc = 3;
        prev = s;
        if (s == end && id < 255u) {
            if (poly_macro_find(macro_rd, &m, 255u, end) != end) rc = 3;
            break;
        }
    }

    // 4: one step from every cursor stays inside the buffer and moves forward,
    // and what it decodes is the buffer's bytes.
    for (uint16_t cur = 0; rc == POLY_FUZZ_OK && cur <= end; ++cur) {
        const poly_macro_step_t st = poly_macro_decode(macro_rd, &m, cur, end);
        // END is right exactly at the buffer end, at a NUL, or at a prefix whose op is
        // missing, or is a keycode op without its keycode byte, or is unknown.
        bool want_end = cur >= end || b[cur] == 0;
        if (!want_end && b[cur] == POLY_MACRO_PREFIX) {
            if (cur + 1u >= end) {
                want_end = true;
            } else {
                const uint8_t op = b[cur + 1u];
                if (op != POLY_MACRO_OP_DELAY) {
                    want_end = cur + 2u >= end ||
                               (op != POLY_MACRO_OP_TAP && op != POLY_MACRO_OP_DOWN && op != POLY_MACRO_OP_UP);
                }
            }
        }
        if ((st.kind == POLY_MACRO_STEP_END) != want_end) {
            rc = 4;
            break;
        }
        if (st.kind == POLY_MACRO_STEP_END) {
            if (st.next != cur) rc = 4;
            continue;
        }
        if (st.next <= cur || st.next > end) {
            rc = 4;
            break;
        }
        switch (st.kind) {
            case POLY_MACRO_STEP_CHAR:
                if (st.code != b[cur] || st.code == 0 || st.code == POLY_MACRO_PREFIX || st.next != cur + 1) rc = 5;
                break;
            case POLY_MACRO_STEP_TAP:
            case POLY_MACRO_STEP_DOWN:
            case POLY_MACRO_STEP_UP:
                if (b[cur] != POLY_MACRO_PREFIX || st.next != cur + 3 || st.code != b[cur + 2] ||
                    b[cur + 1] != (uint8_t)(POLY_MACRO_OP_TAP + (st.kind - POLY_MACRO_STEP_TAP))) {
                    rc = 5;
                }
                break;
            case POLY_MACRO_STEP_DELAY: {
                // The digits are consumed and the first non-digit is not.
                if (b[cur] != POLY_MACRO_PREFIX || b[cur + 1] != POLY_MACRO_OP_DELAY || st.next < cur + 2) {
                    rc = 5;
                    break;
                }
                uint32_t ms = 0;
                for (uint16_t i = (uint16_t)(cur + 2); i < st.next; ++i) {
                    if (b[i] < '0' || b[i] > '9') rc = 5;
                    ms = ms * 10u + (uint32_t)(b[i] - '0');
                    if (ms > 0xFFFFu) ms = 0xFFFFu;
                }
                if (st.next < end && b[st.next] >= '0' && b[st.next] <= '9') rc = 5;
                if (st.ms != ms) rc = 5;
                break;
            }
            default:
                rc = 5;
                break;
        }
    }

    // 6: playing each macro as poly_macro_tick() does ends within the buffer.
    for (unsigned id = 0; rc == POLY_FUZZ_OK && id < 8u; ++id) {
        uint16_t cur = poly_macro_find(macro_rd, &m, (uint8_t)id, end);
        for (unsigned steps = 0;; ++steps) {
            const poly_macro_step_t st = poly_macro_decode(macro_rd, &m, cur, end);
            if (st.kind == POLY_MACRO_STEP_END) break;
            if (st.next <= cur || steps > end) {
                rc = 6;
                break;
            }
            cur = st.next;
        }
    }
    // 7: no read at or past the end.
    if (rc == POLY_FUZZ_OK && m.bad) rc = 7;
    free(b);
    return rc;
}

// ── cmd 47 op 2: key attention guide ───────────────────────────────────────
//
// Input: one report (data[2] the op, data[3..] the guide), at most 64 bytes,
// parsed with both geometries and the handler's period range.
int poly_fuzz_attn_guide(const uint8_t *data, size_t size) {
    uint8_t  length;
    uint8_t *r  = report_dup(data, size, &length);
    int      rc = POLY_FUZZ_OK;
    for (unsigned gi = 0; rc == POLY_FUZZ_OK && gi < N_GEOMS; ++gi) {
        const poly_fuzz_geom_t g = k_geoms[gi];
        attn_guide_t a, before;
        memset(&a, 0x5A, sizeof a);
        before = a;
        if (!attn_guide_parse(r, length, g.rows, g.cols, g.layers, KEY_ATTN_PERIOD_MIN, KEY_ATTN_PERIOD_MAX, &a)) {
            if (memcmp(&a, &before, sizeof a) != 0) rc = 1;   // 1: a refusal leaves *out alone
            continue;
        }
        // 2: the header fields are the report's and in range.
        if (length < ATTN_GUIDE_IDX_STEP0 + 3u * a.nsteps || a.count != r[ATTN_GUIDE_IDX_COUNT] ||
            a.period != r[ATTN_GUIDE_IDX_PERIOD] || a.target != r[ATTN_GUIDE_IDX_TARGET] ||
            a.nsteps != r[ATTN_GUIDE_IDX_NSTEPS] || a.period < KEY_ATTN_PERIOD_MIN ||
            a.period > KEY_ATTN_PERIOD_MAX || a.target >= g.layers || a.nsteps == 0u ||
            a.nsteps > ATTN_GUIDE_MAX_STEPS) {
            rc = 2;
            break;
        }
        // 3: every step is the report's, on the board, on a layer of its own that is
        // not the target.
        for (uint8_t i = 0; rc == POLY_FUZZ_OK && i < a.nsteps; ++i) {
            const attn_guide_step_t *s = &a.step[i];
            const uint8_t           *p = r + ATTN_GUIDE_IDX_STEP0 + 3u * i;
            if (s->row != p[0] || s->col != p[1] || s->layer != p[2] || s->row >= g.rows ||
                s->col >= g.cols || s->layer >= g.layers || s->layer == a.target) {
                rc = 3;
            }
            for (uint8_t j = 0; j < i; ++j) {
                if (a.step[j].layer == s->layer) rc = 3;
            }
        }
        // 4: select picks DONE on the target, the step on its layer, else IDLE.
        for (unsigned layer = 0; rc == POLY_FUZZ_OK && layer < 256u; ++layer) {
            uint8_t want = ATTN_GUIDE_IDLE;
            if (layer == a.target) {
                want = ATTN_GUIDE_DONE;
            } else {
                for (uint8_t i = 0; i < a.nsteps; ++i) {
                    if (a.step[i].layer == layer) want = i;
                }
            }
            if (attn_guide_select(&a, (uint8_t)layer) != want) rc = 4;
        }
    }
    free(r);
    return rc;
}

// ── cmd 45/46: AI mode ─────────────────────────────────────────────────────
//
// Input: [35 op 1 bytes] [3 page bytes] [2 cmd 46 header bytes, little endian]
// [ring script]. A short input is padded with zeros, as a short report is. In the
// ring script a byte < 0x80 pushes that key; a byte >= 0x80 reads with `since` =
// the next byte. The ring is checked against a list of every pushed event.
#define AI_FUZZ_OVERLAY_BYTES (NUM_SEGMENTS_PER_OVERLAY * BYTES_PER_SEGMENT)   // 360

static bool ai_model_valid(const uint8_t *p, uint16_t num_slots) {
    for (unsigned i = 0; i < AI_NUM_KEYS; ++i) {
        if (p[i] > AI_STATE_STALE) return false;
    }
    if (p[AI_NUM_KEYS] > AI_FLAGS_VALID) return false;
    for (unsigned i = 0; i < AI_NUM_CARDS; ++i) {
        const unsigned s = p[AI_NUM_KEYS + 1u + 2u * i] | (unsigned)p[AI_NUM_KEYS + 2u + 2u * i] << 8;
        if (s != AI_SLOT_NONE && s >= num_slots) return false;
    }
    return true;
}

int poly_fuzz_ai_events(const uint8_t *data, size_t size) {
    int    rc  = POLY_FUZZ_OK;
    size_t pos = 0;
    // Op 1: the payload and the page bytes, each in a buffer of its own.
    uint8_t *payload = (uint8_t *)xcalloc(AI_SET_KEYS_LEN);
    uint8_t *tail    = (uint8_t *)xcalloc(AI_SET_KEYS_PAGE_LEN);
    for (size_t k = 0; k < AI_SET_KEYS_LEN && pos < size; ++k) payload[k] = data[pos++];
    for (size_t k = 0; k < AI_SET_KEYS_PAGE_LEN && pos < size; ++k) tail[k] = data[pos++];
    const bool valid = ai_set_keys_valid(payload, NUM_OVERLAY_SLOTS);
    // 1: the check accepts exactly the payloads the format allows.
    if (valid != ai_model_valid(payload, NUM_OVERLAY_SLOTS)) rc = 1;
    if (rc == POLY_FUZZ_OK && valid) {
        uint8_t  state[AI_NUM_KEYS], flags = 0xFF, left[AI_NUM_KEYS] = {0};
        uint16_t slot[AI_NUM_CARDS];
        ai_set_keys_decode(payload, state, &flags, slot);
        // 2: the decoded values are the payload's: each slot is the little-endian
        // pair at its own offset, and in range.
        if (memcmp(state, payload, AI_NUM_KEYS) != 0 || flags != payload[AI_NUM_KEYS]) rc = 2;
        for (unsigned i = 0; i < AI_NUM_CARDS; ++i) {
            const unsigned want = payload[AI_NUM_KEYS + 1u + 2u * i] |
                                  (unsigned)payload[AI_NUM_KEYS + 2u + 2u * i] << 8;
            if (slot[i] != want) rc = 2;
            if (slot[i] != AI_SLOT_NONE && slot[i] >= NUM_OVERLAY_SLOTS) rc = 2;
        }
        // 3: a finished agent rings at most AI_DONE_RIPPLES times.
        uint8_t prev[AI_NUM_KEYS];
        for (unsigned i = 0; i < AI_NUM_KEYS; ++i) prev[i] = (uint8_t)((state[i] + 1u) % AI_STATE_COUNT);
        ai_done_rings_update(prev, state, left);
        // 3: exactly the keys that went from working/stale to done/idle ring, AI_DONE_RIPPLES
        // times. done -> idle and any unfinished key do not (left starts at 0).
        for (unsigned i = 0; i < AI_NUM_KEYS; ++i) {
            const bool finished = state[i] == AI_STATE_DONE || state[i] == AI_STATE_IDLE;
            const bool was_busy = prev[i] == AI_STATE_WORKING || prev[i] == AI_STATE_STALE;
            const bool rings    = finished && was_busy && !(prev[i] == AI_STATE_DONE && state[i] == AI_STATE_IDLE);
            if (left[i] != (rings ? AI_DONE_RIPPLES : 0u)) rc = 3;
        }
    }
    // 4: the page bytes are valid when no page count is sent or the page is below it.
    const bool pv = ai_set_keys_page_valid(tail);
    if (rc == POLY_FUZZ_OK &&
        pv != (tail[AI_PAGES_BYTE] == 0 ? tail[AI_PAGE_BYTE] == 0 : tail[AI_PAGE_BYTE] < tail[AI_PAGES_BYTE])) {
        rc = 4;
    }
    if (rc == POLY_FUZZ_OK) {
        uint8_t prev = 0, left = 0;
        bool    seen = false;
        ai_elsewhere_rings_update(tail[AI_DONE_SEQ_BYTE], tail[AI_PAGES_BYTE], &prev, &seen, &left);
        const uint8_t seq2 = (uint8_t)~tail[AI_DONE_SEQ_BYTE];
        ai_elsewhere_rings_update(seq2, tail[AI_PAGES_BYTE], &prev, &seen, &left);
        // The first update only records the sequence; the second one always differs, so it
        // rings unless the new sequence is 0. With no pages nothing changes.
        if (tail[AI_PAGES_BYTE] == 0) {
            if (prev != 0 || seen || left != 0) rc = 4;
        } else if (prev != seq2 || !seen || left != (seq2 != 0 ? AI_DONE_RIPPLES : 0u)) {
            rc = 4;
        }
    }
    free(payload);
    free(tail);

    // Cmd 46: the card header.
    uint16_t header = 0;
    if (pos < size) header = data[pos++];
    if (pos < size) header |= (uint16_t)(data[pos++] << 8);
    uint16_t slot = 0xBEEF;
    uint8_t  seg  = 0xEE;
    const bool hok = ai_card_header(header, NUM_OVERLAY_SLOTS, &slot, &seg);
    // 5: an accepted header names a pool slot, and its segment ends inside the overlay.
    const bool hwant = (header & 0x3FFu) < NUM_OVERLAY_SLOTS && ((header >> 12) & 7u) <= 5u;
    if (rc == POLY_FUZZ_OK && hok != hwant) rc = 5;
    if (rc == POLY_FUZZ_OK && hok &&
        (slot != (header & 0x3FFu) || slot >= NUM_OVERLAY_SLOTS ||
         (unsigned)seg * BYTES_PER_SEGMENT + BYTES_PER_SEGMENT > AI_FUZZ_OVERLAY_BYTES)) {
        rc = 5;
    }
    if (rc == POLY_FUZZ_OK && !hok && (slot != 0xBEEF || seg != 0xEE)) rc = 5;

    // Cmd 45 op 2: the ring, against a list of every event pushed.
    ai_ring_t  ring;
    ai_ring_init(&ring);
    ai_event_t all[1024];
    unsigned   n_all = 0;
    uint8_t    seq   = 0;
    ai_event_t *out  = (ai_event_t *)xmalloc(AI_RING_SIZE * sizeof(ai_event_t));
    while (rc == POLY_FUZZ_OK && pos < size && n_all < 1024u) {
        const uint8_t op = data[pos++];
        if (op < 0x80u) {
            seq = seq == 0xFFu ? 1u : (uint8_t)(seq + 1u);
            all[n_all].seq = seq;
            all[n_all].key = op;
            ++n_all;
            // 6: push returns the next seq, never 0.
            if (ai_ring_push(&ring, op) != seq || ring.count > AI_RING_SIZE) rc = 6;
            continue;
        }
        const uint8_t since = pos < size ? data[pos++] : 0;
        const uint8_t n     = ai_ring_read(&ring, since, out);
        // 7: the events are the newest ones held, after `since` if it is held.
        const unsigned held  = n_all < AI_RING_SIZE ? n_all : AI_RING_SIZE;
        unsigned       first = n_all - held;
        if (since != 0) {
            for (unsigned i = n_all - held; i < n_all; ++i) {
                if (all[i].seq == since) {
                    first = i + 1;
                    break;
                }
            }
        }
        if (n != n_all - first) {
            rc = 7;
            break;
        }
        for (unsigned i = 0; i < n; ++i) {
            if (out[i].seq != all[first + i].seq || out[i].key != all[first + i].key) rc = 7;
        }
    }
    free(out);
    return rc;
}

// ── cmd 48: combo and tap-dance records ────────────────────────────────────
//
// Input: chunks of 11 bytes, [control] [10 record bytes]. Control bit 0 is the kind
// (combo or tap dance), bits 1..3 the tap count for poly_td_plan(), bit 4 "held".
// The record bytes also drive poly_combo_pick_slot(): bytes 0..1 the clean bits,
// bytes 2..3 the tried bits, byte 4 % 3 the newest sector (2 = none).

static bool td_kc_model(uint16_t kc) {
    return kc <= 0x1FFFu || (kc >= 0x5200u && kc <= 0x523Fu) || (kc >= 0x5260u && kc <= 0x527Fu);
}

static bool combo_model(const uint16_t w[POLY_COMBO_REC_WORDS]) {
    bool zero = true;
    for (unsigned i = 0; i < POLY_COMBO_REC_WORDS; ++i) zero = zero && w[i] == 0u;
    if (zero) return true;
    unsigned n = 0;
    while (n < POLY_COMBO_MAX_KEYS && w[n] != 0u) ++n;
    for (unsigned i = n; i < POLY_COMBO_MAX_KEYS; ++i) {
        if (w[i] != 0u) return false;
    }
    for (unsigned i = 0; i < n; ++i) {
        for (unsigned j = i + 1; j < n; ++j) {
            if (w[i] == w[j]) return false;
        }
    }
    return n >= 2u && w[POLY_COMBO_MAX_KEYS] != 0u;
}

static int check_pick_slot(const uint8_t *rec) {
    bool clean[POLY_COMBO_STORE_SECTORS][POLY_COMBO_STORE_SLOTS];
    bool tried[POLY_COMBO_STORE_SECTORS][POLY_COMBO_STORE_SLOTS];
    for (unsigned s = 0; s < POLY_COMBO_STORE_SECTORS; ++s) {
        for (unsigned i = 0; i < POLY_COMBO_STORE_SLOTS; ++i) {
            clean[s][i] = (rec[s] >> i) & 1u;
            tried[s][i] = (rec[2 + s] >> i) & 1u;
        }
    }
    const uint8_t newest = (uint8_t)(rec[4] % (POLY_COMBO_STORE_SECTORS + 1u));
    const poly_combo_slot_pick_t p = poly_combo_pick_slot(clean, tried, newest);
    if (p.sector >= POLY_COMBO_STORE_SECTORS || p.slot >= POLY_COMBO_STORE_SLOTS) return 1;
    // The first usable slot of the newest sector, else of the others in order.
    const unsigned first = newest < POLY_COMBO_STORE_SECTORS ? newest : 0u;
    for (unsigned k = 0; k < POLY_COMBO_STORE_SECTORS; ++k) {
        const unsigned sec = (first + k) % POLY_COMBO_STORE_SECTORS;
        for (unsigned i = 0; i < POLY_COMBO_STORE_SLOTS; ++i) {
            if (clean[sec][i] && !tried[sec][i]) {
                return (!p.erase && p.sector == sec && p.slot == i) ? 0 : 1;
            }
        }
    }
    // No usable slot: erase, never the sector of the newest image, and take its first
    // untried slot when it has one.
    if (!p.erase || (newest < POLY_COMBO_STORE_SECTORS && p.sector == newest)) return 1;
    for (unsigned i = 0; i < POLY_COMBO_STORE_SLOTS; ++i) {
        if (!tried[p.sector][i]) return p.slot == i ? 0 : 1;
    }
    return 0;
}

int poly_fuzz_combo_store(const uint8_t *data, size_t size) {
    int rc = POLY_FUZZ_OK;
    for (size_t pos = 0; rc == POLY_FUZZ_OK && pos + 1u + POLY_COMBO_REC_BYTES <= size;
         pos += 1u + POLY_COMBO_REC_BYTES) {
        const uint8_t ctl   = data[pos];
        uint8_t      *rec   = dup_exact(data + pos + 1u, POLY_COMBO_REC_BYTES);
        uint16_t      w[POLY_COMBO_REC_WORDS];
        poly_combo_words_get(rec, w);
        // 1: the words are the little-endian bytes, and put() writes them back.
        uint8_t *back = (uint8_t *)xcalloc(POLY_COMBO_REC_BYTES);
        poly_combo_words_put(back, w);
        if (memcmp(back, rec, POLY_COMBO_REC_BYTES) != 0) rc = 1;
        for (unsigned i = 0; i < POLY_COMBO_REC_WORDS; ++i) {
            if (w[i] != (uint16_t)(rec[2 * i] | rec[2 * i + 1] << 8)) rc = 1;
        }
        free(back);

        if (rc == POLY_FUZZ_OK && (ctl & 1u) == POLY_COMBO_KIND_COMBO) {
            poly_combo_rec_t r;
            memcpy(&r, w, sizeof r);
            const bool    valid  = poly_combo_rec_valid(&r);
            const uint8_t n      = poly_combo_key_count(&r);
            const bool    active = poly_combo_rec_active(&r);
            // 2: valid is what the format allows; an active record is valid and has
            // 2..4 keys, an inactive valid one is empty.
            if (valid != combo_model(w) || n > POLY_COMBO_MAX_KEYS || (active && (!valid || n < 2u)) ||
                (valid && !active && !poly_combo_words_zero(w))) {
                rc = 2;
            }
        } else if (rc == POLY_FUZZ_OK) {
            poly_tap_dance_rec_t r;
            memcpy(&r, w, sizeof r);
            const bool valid = poly_td_rec_valid(&r);
            const bool want  = td_kc_model(r.on_tap) && td_kc_model(r.on_hold) && td_kc_model(r.on_double_tap) &&
                              td_kc_model(r.on_tap_hold) &&
                              (r.term_ms == 0u || (r.term_ms >= 100u && r.term_ms <= 500u && r.term_ms % 10u == 0u));
            // 3: valid is what the format allows.
            if (valid != want) rc = 3;
            // 4: the plan follows the table in combo_store.h.
            const uint8_t  count = (uint8_t)((ctl >> 1) & 7u);
            const bool     held  = (ctl & 0x10u) != 0u;
            poly_td_plan_t p     = poly_td_plan(&r, count, held);
            poly_td_plan_t q     = {0, 0, 0};
            if (count == 1u) {
                if (held) q.hold_kc = r.on_hold ? r.on_hold : r.on_tap;
                else {
                    q.tap_kc = r.on_tap;
                    q.taps   = 1;
                }
            } else if (count == 2u && held && r.on_tap_hold) {
                q.hold_kc = r.on_tap_hold;
            } else if (count == 2u && !held && r.on_double_tap) {
                q.tap_kc = r.on_double_tap;
                q.taps   = 1;
            } else if (count >= 2u) {
                q.tap_kc = r.on_tap;
                q.taps   = (uint8_t)(held ? count - 1u : count);
                if (held) q.hold_kc = r.on_tap;
            }
            if (q.tap_kc == 0u) q.taps = 0;
            if (rc == POLY_FUZZ_OK && (p.tap_kc != q.tap_kc || p.taps != q.taps || p.hold_kc != q.hold_kc ||
                                       p.taps > count || (!held && p.hold_kc != 0u))) {
                rc = 4;
            }
        }
        // 5: the slot pick follows the three rules in combo_store.h.
        if (rc == POLY_FUZZ_OK && check_pick_slot(rec)) rc = 5;
        free(rec);
    }
    return rc;
}

// ── cmd 21/33: overlay mapping codec ───────────────────────────────────────
//
// Input: [width selector] [packed bytes, at most HID_DATA_MAX]. The width is
// 8 + selector % 9, the range set_packed_overlay_mapping() accepts. Every value the
// report holds (OVERLAY_MAP_VALUES) is read from an exact-size buffer, compared with
// a bit-by-bit reader, and written back into a zeroed exact-size buffer.
int poly_fuzz_map_codec(const uint8_t *data, size_t size) {
    if (size < 1) return POLY_FUZZ_OK;
    const uint8_t width = (uint8_t)(OVERLAY_MAP_WIDTH_MIN + data[0] % (OVERLAY_MAP_WIDTH_MAX - OVERLAY_MAP_WIDTH_MIN + 1u));
    size_t        bytes = size - 1u;
    if (bytes > HID_DATA_MAX) bytes = HID_DATA_MAX;
    uint8_t       *in     = dup_exact(data + 1, bytes);
    uint8_t       *out    = (uint8_t *)xcalloc(bytes);
    const uint16_t values = OVERLAY_MAP_VALUES(bytes, width);
    int            rc     = POLY_FUZZ_OK;
    for (uint16_t idx = 0; idx < values; ++idx) {
        const uint16_t v = map_codec_read(in, idx, width);
        uint32_t       want = 0;
        for (unsigned k = 0; k < width; ++k) {
            const unsigned bit = (unsigned)idx * width + k;
            want |= (uint32_t)((in[bit / 8u] >> (bit % 8u)) & 1u) << k;
        }
        // 1: the value is the width bits at its position, LSB first.
        if (v != want) {
            rc = 1;
            break;
        }
        map_codec_write(out, idx, v, width);
        // 2: the no-legend split and its inverse agree (OVERLAY_MAP_NO_LEGEND_OFFSET).
        bool           no_legend = false;
        const uint16_t s         = map_codec_split_to(v, OVERLAY_MAP_NO_LEGEND_OFFSET, &no_legend);
        if (no_legend != (v >= OVERLAY_MAP_NO_LEGEND_OFFSET && v < 2u * OVERLAY_MAP_NO_LEGEND_OFFSET) ||
            map_codec_to_value(s, no_legend, OVERLAY_MAP_NO_LEGEND_OFFSET) != v) {
            rc = 2;
            break;
        }
        const uint16_t e = map_codec_entry_with_no_legend(s, no_legend);
        if (s < 0x8000u && (map_codec_entry_slot(e) != s || map_codec_entry_no_legend(e) != no_legend)) {
            rc = 2;
            break;
        }
    }
    // 3: writing every value back gives the same bits, and no bit past the values.
    if (rc == POLY_FUZZ_OK) {
        const unsigned nbits = (unsigned)values * width;
        for (unsigned bit = 0; bit < bytes * 8u; ++bit) {
            const unsigned a = (in[bit / 8u] >> (bit % 8u)) & 1u;
            const unsigned b = (out[bit / 8u] >> (bit % 8u)) & 1u;
            if (bit < nbits ? a != b : b != 0u) {
                rc = 3;
                break;
            }
        }
    }
    free(in);
    free(out);
    return rc;
}
