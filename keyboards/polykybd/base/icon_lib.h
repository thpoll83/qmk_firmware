// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdbool.h>
#include <stdint.h>

// ── The overlay icon library ("PlyI", HID cmd 42, protocol v20) ──────────────
//
// Icons the host's overlay templates share live in the font-pack region (bundle
// id 8, the tail slot of layout v2). Instead of uploading an icon's bitmap, the
// host sends a (pool slot, icon id) pair and each half draws the icon into that
// pool slot from its own flash. Design: OVERLAY_ICON_LIBRARY_DESIGN.md.
//
// FILE FORMAT
//   The byte layout is a PlyF pack (base/fontpack.h): the 32-byte header, then
//   fontpack_font_t records, the per-record glyph arrays and bitmap blobs, CRC32
//   over [32 .. total_size). Only three things differ:
//     - magic "PlyI". The font loader accepts only "PlyF" and this loader only
//       "PlyI", so an icon bundle can never enter the legend lookup and a font
//       bundle flashed into the icon slot reads as absent (version 0).
//     - A record's first..last is a range of ICON IDS, not codepoints. Records
//       are contiguous from id 0 (record k starts at record k-1's last + 1).
//       There are several only because GFXglyph.bitmapOffset is 16 bits, so one
//       record addresses at most 64 KB of bitmaps.
//     - A glyph bitmap is ROW-major, MSB first, (width*height + 7) / 8 bytes: the
//       pool's own bit order, so a blit is a bit copy. (Font glyphs are
//       column-major.) xOffset/yOffset are the glyph's top-left corner in the
//       72x40 keycap frame: placement is baked in, and nothing travels on the
//       wire but the id.
//
// Dependency-free apart from crc32 so it is unit-tested off-target
// (make test:polykybd_icon_lib).

#define ICONLIB_FRAME_W      72u
#define ICONLIB_FRAME_H      40u
#define ICONLIB_FRAME_BYTES  (ICONLIB_FRAME_W * ICONLIB_FRAME_H / 8u)   // 360
#define ICONLIB_MAX_RECORDS  16u

// Validate the PlyI bundle at `base` and make it the loaded library. `cap`
// bounds it to its slot (0 = no cap). *out_ver gets content_version on success.
// A valid EMPTY bundle (header only, no records) is the wipe sentinel: it loads
// as present with zero icons. On failure the library is unloaded.
bool     iconlib_load_at(const uint8_t *base, uint32_t cap, uint16_t *out_ver);
void     iconlib_unload(void);
bool     iconlib_present(void);   // a valid bundle (possibly empty) is loaded
uint16_t iconlib_count(void);     // number of icon ids (0 when absent)

// Clear the 360-byte `frame` and draw icon `id` into it. False, with the frame
// untouched, for an unknown id or a glyph whose box or bitmap does not fit.
bool     iconlib_blit(uint16_t id, uint8_t *frame);

// ── cmd 42 pair list ─────────────────────────────────────────────────────────
// `bytes` bytes of `width`-bit values (base/map_codec.h), read as
// (pool slot, icon id) pairs. There is no count: the sender pads by repeating
// the last pair, which re-applies the same fill and so is harmless.
//
// `slot_ptr(slot)` returns the 360-byte pool buffer for a slot, or NULL.
// Returns ICONLIB_FILL_OK when every pair applied, otherwise the index of the
// first pair that did not (slot out of range, unknown id, bad glyph); pairs
// before it were applied, it and everything after were not.
#define ICONLIB_FILL_OK 0xFFu
uint8_t  iconlib_fill_pairs(const uint8_t *buf, uint8_t bytes, uint8_t width,
                            uint16_t n_slots, uint8_t *(*slot_ptr)(uint16_t slot));
