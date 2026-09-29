// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Context-coded overlay images (HID cmd 41, protocol v19).
//
// A 1-bit overlay arrives as its region of interest (ROI), coded pixel by pixel:
// each pixel's probability comes from a FIXED table (base/ctx_table.h) indexed by
// its 10 already-decoded neighbours, and a binary range coder turns those
// probabilities back into bits. ~26-30 bytes per icon against ~87 for the best of
// the older encodings, so nearly every icon fits one report.
//
// ⚠️ ONE FORMAT WITH THE HOST. PolyKybdHost polyhost/util/ctx_codec.py encodes
// exactly what this decodes: template order, zero padding, probability scale and
// every range-coder step. base/tests/ctx_codec_tests.cpp checks golden vectors the
// host generated (tools/gen_ctx_vectors.py); a divergence draws garbage keycaps
// with nothing reporting an error.
//
// Pure: no QMK, no globals, so it builds and runs in the googletest suite.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define CTX_FRAME_W 72
#define CTX_FRAME_H 40
#define CTX_FRAME_BYTES (CTX_FRAME_W * CTX_FRAME_H / 8)

// Decodes `len` payload bytes into `overlay` (CTX_FRAME_BYTES, row-major,
// MSB-first: bit = y*72 + x), with the ROI at (top, left) of size height x width.
// The whole overlay is cleared first, so pixels outside the ROI end up 0.
// Returns false, and leaves the overlay untouched, if the box does not fit the
// 72x40 frame or is empty. Reading past the payload yields 0 bytes, which is
// what the host's encoder relies on when it drops trailing zeros.
bool ctx_decode_roi(uint8_t *overlay, uint8_t top, uint8_t left, uint8_t height, uint8_t width,
                    const uint8_t *payload, uint8_t len, const uint8_t *table);

// One image inside a cmd 41 report. A report carries records back to back; a
// record whose keycode byte is 0 (KC_NO), or the end of the report, ends the list.
//
// Header: 6 bytes, a big-endian bit field, then `len` payload bytes.
//
//   keycode 8 | modifier 4 | top 6 | left 7 | height-1 6 | width-1 7 | len 6 | 0 4
//
// The image is addressed exactly like cmds 16-19 (keycode + modifier variant), so
// it resolves through the same translate/pool-slot path. The 4 trailing bits are
// reserved and must be 0.
#define CTX_RECORD_HDR 6

typedef struct {
    uint8_t        keycode;
    uint8_t        modifier;
    uint8_t        top, left, height, width;
    uint8_t        len;
    const uint8_t *payload;
} ctx_record_t;

// Parses the record at `buf` (with `avail` bytes left in the report). Returns the
// bytes it occupies, or 0 when the list ends (keycode 0, or fewer than a header's
// bytes left) or the record is malformed (reserved bits set, box outside the
// frame, payload running past `avail`). A malformed record ends the list too:
// nothing after it can be located.
uint8_t ctx_parse_record(const uint8_t *buf, uint8_t avail, ctx_record_t *out);
