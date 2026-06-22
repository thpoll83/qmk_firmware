// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Runtime loader for the "PlyF" external-flash font pack. Validates a pack image
// (magic / ABI / CRC32 / bounds), resolves its position-independent offsets into
// runtime GFXfont structs, and assembles g_all_fonts[] = resident ++ pack.
//
// The format and the firmware contract are in base/fontpack.h. The build-side
// serializer/validator is fonts/fontpack.py.

#include "fontpack.h"
#include "polymod_crc32.h"
#include <string.h>

#ifndef FONTPACK_HOST_TEST
#    include "quantum.h"
#    include "hardware/flash.h"   // XIP_BASE
#    include "base/fw_staging.h"  // FW_RESOURCE_OFFSET (flash map)
// The pack occupies the start of the resource region (see base/fw_staging.h).
#    define FONTPACK_XIP_BASE ((const uint8_t *)(XIP_BASE + FW_RESOURCE_OFFSET))
#endif

// Upper bound on resident + pack fonts. RAM cost: FONTPACK_MAX_FONTS * (20 + 4)
// bytes of static state (~4.8 KB at 200). Current need is ~14 resident + ~126
// pack = 140.
#define FONTPACK_MAX_FONTS 200

static GFXfont        s_pack[FONTPACK_MAX_FONTS];   // GFXfonts built from the pack
static const GFXfont *s_all[FONTPACK_MAX_FONTS];    // resident ++ pack (pointers)
static uint8_t        s_pack_count;
static uint16_t       s_content_version;
static bool           s_loaded;

// Resident set remembered from the last assemble, so a pack (re)flash can
// reload + reassemble without the caller re-passing it.
static const GFXfont *const *s_resident;
static uint8_t               s_resident_n;

const GFXfont *const *g_all_fonts     = NULL;
uint8_t               g_all_font_count = 0;

// CRC32 over a region, chunked because crc32_1byte() takes a uint16_t length.
// Chaining the running value reproduces a one-shot CRC (zlib.crc32), which is
// what fonts/fontpack.py stamps into the header.
static uint32_t pack_crc32(const uint8_t *p, uint32_t len) {
    uint32_t crc = 0;
    while (len) {
        uint16_t chunk = (len > 0x8000u) ? 0x8000u : (uint16_t)len;
        crc = crc32_1byte(p, chunk, crc);
        p   += chunk;
        len -= chunk;
    }
    return crc;
}

bool fontpack_load_at(const uint8_t *base) {
    s_loaded          = false;
    s_pack_count      = 0;
    s_content_version = 0;
    if (!base) return false;

    const fontpack_header_t *h = (const fontpack_header_t *)base;
    if (h->magic[0] != FONTPACK_MAGIC0 || h->magic[1] != FONTPACK_MAGIC1 ||
        h->magic[2] != FONTPACK_MAGIC2 || h->magic[3] != FONTPACK_MAGIC3) {
        return false;  // erased flash / not a pack
    }
    if (h->abi_version != FONTPACK_ABI_VERSION) return false;
    // A deliberately-empty pack (font_count == 0, header only) is the WIPE
    // sentinel: a valid PlyF header with no fonts. It loads as "present but
    // empty" so g_all_fonts == resident only (fontpack_assemble appends nothing),
    // letting a `fontpack wipe` clear the pack via the normal BEGIN/CHUNK/COMMIT
    // flow (COMMIT's present-check passes) without a bespoke erase command. An
    // ERASED region (0xFF, no magic) is already handled above and also yields
    // resident-only — this just makes the wipe report success cleanly.
    if (h->font_count == 0) {
        if (h->total_size != sizeof(fontpack_header_t)) return false;
        s_content_version = h->content_version;
        s_pack_count      = 0;
        s_loaded          = true;
        return true;
    }
    if (h->font_table_off != sizeof(fontpack_header_t)) return false;
    if (h->total_size <= sizeof(fontpack_header_t)) return false;
    if (h->font_count > FONTPACK_MAX_FONTS) return false;
    if ((uint32_t)h->font_table_off + (uint32_t)h->font_count * sizeof(fontpack_font_t) > h->total_size) {
        return false;
    }

    // Integrity: CRC32 over everything after the 32-byte header.
    if (pack_crc32(base + sizeof(fontpack_header_t),
                   h->total_size - sizeof(fontpack_header_t)) != h->crc32) {
        return false;
    }

    const fontpack_font_t *tbl = (const fontpack_font_t *)(base + h->font_table_off);
    for (uint32_t i = 0; i < h->font_count; ++i) {
        if (tbl[i].glyph_off >= h->total_size || tbl[i].bitmap_off >= h->total_size) {
            return false;  // dangling offset
        }
        // bitmap/glyph are non-const in GFXfont; we only ever read them.
        s_pack[i].bitmap   = (uint8_t *)(base + tbl[i].bitmap_off);
        s_pack[i].glyph    = (GFXglyph *)(base + tbl[i].glyph_off);
        s_pack[i].first    = tbl[i].first;
        s_pack[i].last     = tbl[i].last;
        s_pack[i].yAdvance = tbl[i].yAdvance;
    }
    s_pack_count      = (uint8_t)h->font_count;
    s_content_version = h->content_version;
    s_loaded          = true;
    return true;
}

void fontpack_assemble(const GFXfont *const *resident, uint8_t n_resident) {
    if (n_resident > FONTPACK_MAX_FONTS) n_resident = FONTPACK_MAX_FONTS;
    s_resident   = resident;
    s_resident_n = n_resident;
    uint8_t k = 0;
    for (uint8_t i = 0; i < n_resident; ++i) s_all[k++] = resident[i];
    if (s_loaded) {
        for (uint8_t i = 0; i < s_pack_count && k < FONTPACK_MAX_FONTS; ++i) {
            s_all[k++] = &s_pack[i];
        }
    }
    g_all_fonts      = s_all;
    g_all_font_count = k;
}

bool     fontpack_present(void)          { return s_loaded; }
uint16_t fontpack_content_version(void)  { return s_content_version; }
uint8_t  fontpack_font_count(void)       { return s_pack_count; }

#ifndef FONTPACK_HOST_TEST
bool fontpack_load(void) {
    return fontpack_load_at(FONTPACK_XIP_BASE);
}

void fontpack_init(const GFXfont *const *resident, uint8_t n_resident) {
    fontpack_load();
    fontpack_assemble(resident, n_resident);
}

void fontpack_reload(void) {
    fontpack_load();
    fontpack_assemble(s_resident, s_resident_n);
}
#endif
