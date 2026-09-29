// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// PRC overlay images (HID cmd 41, protocol v19).
//
// PRC = Predictive Range Coding. "Predictive": before a pixel is decoded, its 10
// already-decoded neighbours predict how likely it is to be 0. "Range coding":
// an arithmetic coder spends few bits on a pixel that matches the prediction and
// more on one that does not. Keycap icons are mostly empty space and clean edges,
// so the predictions are nearly always right. The idea is JBIG's (the fax
// standard); the format is our own and is not JBIG-compatible.
//
// A 1-bit overlay arrives as its region of interest (ROI), coded pixel by pixel:
// each pixel's probability comes from a FIXED table (base/prc_table.h) indexed by
// its 10 already-decoded neighbours, and a binary range coder turns those
// probabilities back into bits. ~26-30 bytes per icon against ~87 for the best of
// the older encodings, so nearly every icon fits one report.
//
// ⚠️ ONE FORMAT WITH THE HOST. PolyKybdHost polyhost/util/prc_codec.py encodes
// exactly what this decodes: template order, zero padding, probability scale and
// every range-coder step. base/tests/prc_codec_tests.cpp checks golden vectors the
// host generated (tools/gen_prc_vectors.py); a divergence draws garbage keycaps
// with nothing reporting an error.
//
// Pure: no QMK, no globals, so it builds and runs in the googletest suite.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define PRC_FRAME_W 72
#define PRC_FRAME_H 40
#define PRC_FRAME_BYTES (PRC_FRAME_W * PRC_FRAME_H / 8)

// Decodes `len` payload bytes into `overlay` (PRC_FRAME_BYTES, row-major,
// MSB-first: bit = y*72 + x), with the ROI at (top, left) of size height x width.
// The whole overlay is cleared first, so pixels outside the ROI end up 0.
// Returns false, and leaves the overlay untouched, if the box does not fit the
// 72x40 frame or is empty. Reading past the payload yields 0 bytes, which is
// what the host's encoder relies on when it drops trailing zeros.
bool prc_decode_roi(uint8_t *overlay, uint8_t top, uint8_t left, uint8_t height, uint8_t width,
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
#define PRC_RECORD_HDR 6

typedef struct {
    uint8_t        keycode;
    uint8_t        modifier;
    uint8_t        top, left, height, width;
    uint8_t        len;
    const uint8_t *payload;
} prc_record_t;

// Parses the record at `buf` (with `avail` bytes left in the report). Returns the
// bytes it occupies, or 0 when the list ends (keycode 0, or fewer than a header's
// bytes left) or the record is malformed (reserved bits set, box outside the
// frame, payload running past `avail`). A malformed record ends the list too:
// nothing after it can be located.
uint8_t prc_parse_record(const uint8_t *buf, uint8_t avail, prc_record_t *out);
