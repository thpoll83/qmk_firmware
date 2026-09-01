// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Runtime loader for the "PlyF" external-flash font pack. Validates a pack image
// (magic / ABI / CRC32 / bounds), resolves its position-independent offsets into
// runtime GFXfont structs, and assembles g_all_fonts[] = resident ++ pack.
//
// SPLIT PACK (bundles): the pack is flashed as N independent PlyF "bundles" — one
// per script family — each living in a fixed, sector-aligned slot inside the
// fontpack window (the slot table is the generated fonts/generated/fontpack_layout.h).
// The firmware reads every slot's header at boot; the set of valid slot headers
// IS the directory (no separate directory sector to keep consistent). Each
// bundle's per-font record carries the font's GLOBAL ALL_FONTS index (the spare
// `reserved` u16), so the loader merges the fonts of all present bundles back into
// global priority order before appending them after the resident set.
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
#    include "fonts/generated/fontpack_layout.h"  // slot table (X-macro)
// The pack region occupies the start of the resource region (see fw_staging.h).
#    define FONTPACK_XIP_BASE ((const uint8_t *)(XIP_BASE + FW_RESOURCE_OFFSET))
#endif

// Upper bound on resident + pack fonts. RAM cost: FONTPACK_MAX_FONTS * (20 + 4)
// bytes of static state (~4.8 KB at 200). Current need is ~14 resident + ~119
// pack = 133.
#define FONTPACK_MAX_FONTS 200

static GFXfont        s_pack[FONTPACK_MAX_FONTS];   // GFXfonts built from all bundles
static uint16_t       s_pack_gidx[FONTPACK_MAX_FONTS]; // each font's global ALL_FONTS index
static const GFXfont *s_all[FONTPACK_MAX_FONTS];    // resident ++ pack (pointers), assembled
static uint8_t        s_pack_count;                 // total fonts loaded across all slots
static bool           s_loaded;                     // at least one valid (non-empty) bundle present

// Resident set remembered from the last assemble, so a pack (re)flash can
// reload + reassemble without the caller re-passing it.
static const GFXfont *const *s_resident;
static uint8_t               s_resident_n;

const GFXfont *const *g_all_fonts     = NULL;
uint8_t               g_all_font_count = 0;

// ── Bundle slot table (from the generated layout header) ─────────────────────
#ifndef FONTPACK_HOST_TEST
typedef struct { uint8_t id; uint32_t offset; uint32_t size; } fontpack_slot_t;
static const fontpack_slot_t s_slots[] = {
#    define X(name, idx, off, sz) { (idx), (off), (sz) },
    FONTPACK_BUNDLE_LIST
#    undef X
};
#    define FONTPACK_N_SLOTS (sizeof(s_slots) / sizeof(s_slots[0]))
static uint16_t s_slot_ver[FONTPACK_N_SLOTS];   // per-bundle content_version (0 if absent)
static bool     s_slot_present[FONTPACK_N_SLOTS];
#endif

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

// Validate the PlyF at `base` and APPEND its fonts to s_pack[] (from s_pack_count),
// recording each font's global ALL_FONTS index. `cap` (0 = no cap) bounds the pack
// to its slot so a corrupt total_size can't read past the slot into the next.
// *out_ver gets the content_version on success. Returns true for a valid pack
// (including the empty WIPE sentinel, which appends no fonts).
static bool validate_and_append(const uint8_t *base, uint32_t cap, uint16_t *out_ver) {
    if (out_ver) *out_ver = 0;
    if (!base) return false;

    const fontpack_header_t *h = (const fontpack_header_t *)base;
    if (h->magic[0] != FONTPACK_MAGIC0 || h->magic[1] != FONTPACK_MAGIC1 ||
        h->magic[2] != FONTPACK_MAGIC2 || h->magic[3] != FONTPACK_MAGIC3) {
        return false;  // erased flash / not a pack
    }
    if (h->abi_version != FONTPACK_ABI_VERSION) return false;
    if (cap && h->total_size > cap) return false;   // overruns its slot

    // A deliberately-empty pack (font_count == 0, header only) is the WIPE
    // sentinel: a valid PlyF header with no fonts. It loads as "present but
    // empty" (appends nothing) so a `fontpack wipe` clears the slot via the
    // normal BEGIN/CHUNK/COMMIT flow without a bespoke erase command.
    if (h->font_count == 0) {
        if (h->total_size != sizeof(fontpack_header_t)) return false;
        if (out_ver) *out_ver = h->content_version;
        return true;
    }
    if (h->font_table_off != sizeof(fontpack_header_t)) return false;
    if (h->total_size <= sizeof(fontpack_header_t)) return false;
    if ((uint32_t)s_pack_count + h->font_count > FONTPACK_MAX_FONTS) return false;
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
        // ⚠️ glyph_off is FILE-CONTROLLED and is cast to PolyColGlyph*, whose
        // first member is a uint16_t — so an ODD offset makes every glyph lookup
        // an unaligned 16-bit read, which is a HardFault on the M0+. A .plyf
        // rides the UNSIGNED resource transport (SECURITY_AUDIT.md FW-9 tail:
        // "the exposure is parser bugs"), and the pack lives in flash and is
        // re-loaded at boot, so a crafted odd offset is a PERSISTENT fault loop
        // needing BOOTSEL to clear. Found by -Wcast-align (see rules.mk), which
        // is the whole argument for that flag.
        if ((tbl[i].glyph_off & 1u) != 0u) {
            return false;  // misaligned glyph array
        }
        // The old check bounded only the START of the glyph array. The array is
        // (last - first + 1) records long, all read on lookup, so a truthful
        // offset with an oversized range still walks off the end of the pack.
        if (tbl[i].last < tbl[i].first) {
            return false;  // inverted range
        }
        {
            // Division, not multiplication: `count * sizeof(GFXglyph)` overflows
            // a uint32 for a hostile `last`, and 64-bit arithmetic would pull a
            // libcall into a Cortex-M0+ for no reason. `total_size - glyph_off`
            // cannot underflow — glyph_off < total_size is checked above.
            uint32_t count = tbl[i].last - tbl[i].first + 1u;
            if (count > (h->total_size - tbl[i].glyph_off) / sizeof(GFXglyph)) {
                return false;  // glyph array runs past the end of the pack
            }
        }
        GFXfont *f = &s_pack[s_pack_count];
        // bitmap/glyph are non-const in GFXfont; we only ever read them.
        f->bitmap   = (uint8_t *)(base + tbl[i].bitmap_off);
        // -Wcast-align (rules.mk) is right that this widens alignment; the
        // even-offset check above is what makes it safe, and it is a RUNTIME
        // check because the offset is runtime data. Suppressed for this one
        // line, with that validation as the proof.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-align"
        f->glyph    = (GFXglyph *)(base + tbl[i].glyph_off);
#pragma GCC diagnostic pop
        f->first    = tbl[i].first;
        f->last     = tbl[i].last;
        f->yAdvance = tbl[i].yAdvance;
        s_pack_gidx[s_pack_count] = tbl[i].reserved;   // global ALL_FONTS index
        s_pack_count++;
    }
    if (out_ver) *out_ver = h->content_version;
    return true;
}

bool fontpack_load_at(const uint8_t *base) {
    s_pack_count = 0;
    s_loaded     = false;
    uint16_t ver = 0;
    bool ok = validate_and_append(base, 0, &ver);
    s_loaded = ok;
    return ok;
}

void fontpack_assemble(const GFXfont *const *resident, uint8_t n_resident) {
    if (n_resident > FONTPACK_MAX_FONTS) n_resident = FONTPACK_MAX_FONTS;
    s_resident   = resident;
    s_resident_n = n_resident;
    uint8_t k = 0;
    for (uint8_t i = 0; i < n_resident; ++i) s_all[k++] = resident[i];

    // Merge all loaded bundle fonts back into global ALL_FONTS order (the bundles
    // were sliced out of one ordered list; their global indices interleave). A
    // small insertion sort on an index array keeps the GFXfont structs put.
    uint8_t order[FONTPACK_MAX_FONTS];
    for (uint8_t i = 0; i < s_pack_count; ++i) order[i] = i;
    for (uint8_t i = 1; i < s_pack_count; ++i) {
        uint8_t  v  = order[i];
        uint16_t vg = s_pack_gidx[v];
        int8_t   j  = (int8_t)i - 1;
        while (j >= 0 && s_pack_gidx[order[j]] > vg) {
            order[j + 1] = order[j];
            --j;
        }
        order[j + 1] = v;
    }
    for (uint8_t i = 0; i < s_pack_count && k < FONTPACK_MAX_FONTS; ++i) {
        s_all[k++] = &s_pack[order[i]];
    }
    g_all_fonts      = s_all;
    g_all_font_count = k;
}

bool     fontpack_present(void)    { return s_loaded; }
uint8_t  fontpack_font_count(void) { return s_pack_count; }

#ifndef FONTPACK_HOST_TEST
// content_version of the most-recently-loaded bundle is not meaningful across
// slots; callers should use fontpack_bundle_version(). Kept for the legacy
// STATUS/COMMIT logs — returns the first present bundle's version (or 0).
uint16_t fontpack_content_version(void) {
    for (uint8_t i = 0; i < FONTPACK_N_SLOTS; ++i)
        if (s_slot_present[i]) return s_slot_ver[i];
    return 0;
}

bool fontpack_slot(uint8_t id, uint32_t *offset, uint32_t *size) {
    for (uint8_t i = 0; i < FONTPACK_N_SLOTS; ++i) {
        if (s_slots[i].id == id) {
            if (offset) *offset = s_slots[i].offset;
            if (size)   *size   = s_slots[i].size;
            return true;
        }
    }
    return false;
}

// Human-readable bundle name for the slot at flash offset `slot_off` (the on-screen
// "Updating fonts: <name>" label). Names come straight from the generated layout
// X-macro, so they can never drift from the slot table. Returns NULL for an unknown
// offset.
const char *fontpack_slot_name(uint32_t slot_off) {
#define X(name, idx, off, sz) if ((slot_off) == (off)) return #name;
    FONTPACK_BUNDLE_LIST
#undef X
    return NULL;
}

// Did the slot at this flash offset load as a valid PlyF on the last fontpack_load()?
// True for a valid EMPTY (wiped) bundle too — so a wipe COMMIT reports success even
// when every bundle is now empty (when fontpack_present() is false). Used by the
// FONTPACK finalize to gate per-bundle, not on the whole pack.
bool fontpack_slot_present(uint32_t slot_off) {
    for (uint8_t i = 0; i < FONTPACK_N_SLOTS; ++i)
        if (s_slots[i].offset == slot_off) return s_slot_present[i];
    return false;
}

uint8_t  fontpack_bundle_count(void)            { return (uint8_t)FONTPACK_N_SLOTS; }
uint16_t fontpack_bundle_version(uint8_t id) {
    for (uint8_t i = 0; i < FONTPACK_N_SLOTS; ++i)
        if (s_slots[i].id == id) return s_slot_present[i] ? s_slot_ver[i] : 0;
    return 0;
}
bool fontpack_bundle_present(uint8_t id) {
    for (uint8_t i = 0; i < FONTPACK_N_SLOTS; ++i)
        if (s_slots[i].id == id) return s_slot_present[i];
    return false;
}

bool fontpack_load(void) {
    s_pack_count = 0;
    s_loaded     = false;
    bool any = false;
    for (uint8_t i = 0; i < FONTPACK_N_SLOTS; ++i) {
        const uint8_t *base = FONTPACK_XIP_BASE + s_slots[i].offset;
        uint16_t ver    = 0;
        uint8_t  before = s_pack_count;
        bool ok = validate_and_append(base, s_slots[i].size, &ver);
        s_slot_present[i] = ok;
        s_slot_ver[i]     = ver;
        // "present" (s_loaded) means at least one bundle carries fonts; an empty
        // WIPE sentinel is a valid-but-fontless slot and does not set it.
        any = any || (ok && s_pack_count > before);
    }
    s_loaded = any;
    return any;
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
