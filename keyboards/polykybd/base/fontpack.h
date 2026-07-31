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
// v2: glyph bitmaps are COLUMN-NATIVE (OLED page format) — per-glyph length is
// w*((h+7)>>3), not (w*h+7)/8 (see fonts/gfxfont.h). Old row-major v1 packs are
// rejected (resident-only) until the host reflashes the matching column bundles.
#define FONTPACK_ABI_VERSION 2u

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

// ── Runtime loader (firmware) ────────────────────────────────────────────────
//
// The rendered font table is assembled at boot as:
//     g_all_fonts = RESIDENT_FONTS[]  ++  <pack fonts resolved from flash>
// and the renderer (base/disp_array.c) uses g_all_fonts / g_all_font_count in
// place of the old static ALL_FONTS[]. Resident-first is safe because no
// resident font and pack font ever render the same codepoint (build-time
// invariant; the resident set is the functional minimum — see fonts/README.md).

#include <stdbool.h>

// Assembled lookup table (resident fonts first, then the pack's). Always valid
// after fontpack_init(); resident-only when no pack is present.
extern const GFXfont* const* g_all_fonts;
extern uint8_t               g_all_font_count;

// Validate + load the pack from `base` (a raw XIP pointer to a "PlyF" image).
// Returns true on a valid pack (magic + ABI + CRC + bounds); false (erased /
// corrupt / ABI mismatch) leaves the loader in the resident-only state.
// fontpack_load() uses the firmware's flash slot; fontpack_load_at() is the
// position-independent core (testable from any base pointer).
bool fontpack_load_at(const uint8_t *base);
bool fontpack_load(void);

// Build g_all_fonts = resident[0..n) followed by the loaded pack's fonts.
// Safe with no pack loaded (g_all_fonts becomes resident-only).
void fontpack_assemble(const GFXfont *const *resident, uint8_t n_resident);

// Convenience: fontpack_load() then fontpack_assemble(resident, n_resident).
void fontpack_init(const GFXfont *const *resident, uint8_t n_resident);

bool     fontpack_present(void);          // a valid pack is currently loaded
uint16_t fontpack_content_version(void);  // loaded pack's content_version (0 if none)
uint8_t  fontpack_font_count(void);       // number of pack fonts (0 if none)

// ── Split-pack bundles ───────────────────────────────────────────────────────
// The pack is flashed as N independently-versioned bundles, each in a fixed slot
// (the generated fonts/generated/fontpack_layout.h). `id` is the bundle's index
// in that table (its on-wire id, also the order of the GET_ID version block).
uint8_t  fontpack_bundle_count(void);            // N (== FONTPACK_BUNDLE_COUNT)
bool     fontpack_bundle_present(uint8_t id);    // a valid bundle is in slot `id`
uint16_t fontpack_bundle_version(uint8_t id);    // slot `id` content_version (0 if absent)
// Resolve a bundle id to its flash slot offset (relative to FW_RESOURCE_OFFSET)
// and reserved size. Returns false for an unknown id.
bool     fontpack_slot(uint8_t id, uint32_t *offset, uint32_t *size);
// Human-readable bundle name for the slot at flash offset `slot_off` (for the
// on-screen "Updating fonts: <name>" label). NULL for an unknown offset.
const char *fontpack_slot_name(uint32_t slot_off);
// True if the bundle at this slot offset loaded as a valid PlyF (incl. an empty
// wipe sentinel) on the last fontpack_load(). For the per-bundle flash success gate.
bool     fontpack_slot_present(uint32_t slot_off);

// Re-load the pack from flash and re-assemble g_all_fonts using the resident set
// remembered by the last fontpack_init()/fontpack_assemble(). Called by
// fw_staging_finalize() after a FONTPACK flash so new glyphs render without a
// reboot.
void fontpack_reload(void);

// Max pack size accepted by the HID flasher. The pack is written in place at the
// start of the resource region (fw_staging FW_TARGET_FONTPACK); an interrupted
// flash just leaves "no pack" (resident-only), a safe degraded state.
#define FONTPACK_FLASH_MAX_SIZE 0x100000UL   // 1 MB cap within the 4 MB resource region
