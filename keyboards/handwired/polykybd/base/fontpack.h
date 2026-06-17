// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdint.h>
#include "fonts/gfxfont.h"

// ── PolyKybd "PlyF" font pack — on-flash format (position-independent) ───────
//
// The font pack is the bulk glyph-bitmap data (emoji, symbols, CJK, Indic,
// Arabic, …) split OUT of the firmware image and stored in the resource region
// of the external QSPI flash, so the firmware partition shrinks and the fonts
// can be updated independently over HID (see fw_staging.h for the flash map).
//
// A small "functional minimum" set of fonts stays compiled into the firmware
// (IconsFont + the Latin/Cyrillic/Greek `latin` category + the status-OLED
// fonts) so the keyboard is fully usable even with NO pack present.
//
// POSITION INDEPENDENCE
//   Every reference inside the pack is a *byte offset from the pack base*
//   (offset 0 == the first byte of fontpack_header_t), NOT an absolute pointer.
//   The same pack image therefore works unchanged from either A/B flash slot —
//   the firmware adds the slot's XIP base at load time. Nothing in the pack is
//   relocated/patched when flashed.
//
// ENDIANNESS / ALIGNMENT
//   All integers are little-endian (RP2040). Every block is 4-byte aligned.
//
// LAYOUT
//   [ fontpack_header_t (32 B) ]
//   [ fontpack_font_t  × font_count ]   @ font_table_off
//   [ per-font GFXglyph[] arrays ]      (referenced by glyph_off)
//   [ per-font bitmap blobs ]           (referenced by bitmap_off)
//
//   For font i the firmware builds a runtime GFXfont with
//       .bitmap   = pack_base + font[i].bitmap_off
//       .glyph    = pack_base + font[i].glyph_off   (cast to GFXglyph*)
//       .first/.last/.yAdvance copied straight across
//   The per-glyph GFXglyph.bitmapOffset stays relative to that font's own
//   bitmap blob (exactly as Adafruit-GFX already uses it), so the renderer in
//   base/disp_array.c works unchanged once the GFXfont is assembled.

#define FONTPACK_MAGIC0 'P'
#define FONTPACK_MAGIC1 'l'
#define FONTPACK_MAGIC2 'y'
#define FONTPACK_MAGIC3 'F'

// Structural ABI of the pack format below. The firmware refuses a pack whose
// abi_version != FONTPACK_ABI_VERSION (the loader falls back to resident-only
// fonts and flags the mismatch so the host can push a matching pack). Bump ONLY
// when the byte layout of the header / font record changes — NOT when the font
// list content changes (that is content_version).
#define FONTPACK_ABI_VERSION 1u

// 32-byte pack header. crc32 covers every byte AFTER the header
// (i.e. [sizeof(fontpack_header_t) .. total_size)).
typedef struct __attribute__((packed)) {
    char     magic[4];         // "PlyF"
    uint16_t abi_version;      // == FONTPACK_ABI_VERSION
    uint16_t flags;            // reserved, 0
    uint32_t content_version;  // monotonic content revision (host "newer?" check)
    uint32_t font_count;       // number of fontpack_font_t records
    uint32_t font_table_off;   // offset of the font table (== sizeof(header))
    uint32_t total_size;       // total pack size in bytes, incl. header
    uint32_t crc32;            // CRC32 over [sizeof(header) .. total_size)
    uint32_t reserved;         // reserved, 0
} fontpack_header_t;

// 20-byte per-font record. Offsets are pack-base-relative.
typedef struct __attribute__((packed)) {
    uint32_t bitmap_off;       // -> concatenated 1-bit glyph bitmaps for this font
    uint32_t glyph_off;        // -> GFXglyph[ last-first+1 ] for this font
    uint32_t first;            // first codepoint (32-bit: SMP-safe)
    uint32_t last;             // last codepoint
    int16_t  yAdvance;         // newline distance
    uint16_t reserved;         // reserved, 0
} fontpack_font_t;

_Static_assert(sizeof(fontpack_header_t) == 32, "fontpack header must be 32 bytes");
_Static_assert(sizeof(fontpack_font_t) == 20, "fontpack font record must be 20 bytes");
// The serialized per-glyph record is the compiler's GFXglyph layout (8 bytes:
// uint16 + 5×int8 + 1 pad). The firmware casts glyph_off straight to GFXglyph*,
// so this MUST hold on the target. The Python serializer emits the same 8 bytes.
_Static_assert(sizeof(GFXglyph) == 8, "GFXglyph must be 8 bytes for direct pack cast");
