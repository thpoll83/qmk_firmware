// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// PRC: a decoder for 1-bit images, coded with a fixed probability table.
//
// PRC = Predictive Range Coding. "Predictive": before a pixel is decoded, its 10
// already-decoded neighbours predict how likely it is to be 0. "Range coding":
// an arithmetic coder spends few bits on a pixel that matches the prediction and
// more on one that does not. Keycap icons are mostly empty space and clean edges,
// so the predictions are nearly always right. The idea is JBIG's (the fax
// standard); the format is our own and is not JBIG-compatible.
//
// An image is sent as its region of interest (ROI), coded pixel by pixel: each
// pixel's probability comes from a FIXED table (prc_table_v1.h, or one you train
// with tools/prc_tool.py) indexed by its 10 already-decoded neighbours, and a
// binary range coder turns those probabilities back into bits. On PolyKybd's
// 72x40 keycap icons that is ~26-30 bytes per icon, against ~87 for the best RLE
// variant.
//
// ⚠️ ENCODER AND DECODER ARE ONE FORMAT. tools/prc_tool.py and PolyKybdHost's
// polyhost/util/prc_codec.py encode exactly what this decodes: template order,
// zero padding, probability scale and every step of the range coder. Both ends
// must also use the SAME table. tests/prc_tests.cpp checks golden vectors the
// host generated; a divergence draws garbage with nothing reporting an error.
//
// Pixel layout: row-major, most significant bit first, rows packed back to back
// with no padding (bit = y * frame_w + x). A 72-pixel row is exactly 9 bytes.
//
// Pure: no QMK, no globals, so it builds and runs in the googletest suite.

#pragma once

#include <stdbool.h>
#include <stdint.h>

// The default frame, PolyKybd's 72x40 keycap. prc_decode_roi() uses it with the
// sizes known at compile time; prc_decode_roi_in() takes any frame up to 255x255.
#ifndef PRC_FRAME_W
#    define PRC_FRAME_W 72
#endif
#ifndef PRC_FRAME_H
#    define PRC_FRAME_H 40
#endif
#define PRC_FRAME_BYTES (PRC_FRAME_W * PRC_FRAME_H / 8)

// Number of neighbour contexts, i.e. the size of a probability table in bytes.
#define PRC_CONTEXTS 1024

// Decodes `len` payload bytes into `overlay` (PRC_FRAME_BYTES, see the layout
// above), with the ROI at (top, left) of size height x width. `table` is the
// PRC_CONTEXTS-byte probability table the image was encoded with.
// The whole overlay is cleared first, so pixels outside the ROI end up 0.
// Returns false, and leaves the overlay untouched, if the box does not fit the
// frame or is empty. Reading past the payload yields 0 bytes, which is what the
// encoder relies on when it drops trailing zeros.
bool prc_decode_roi(uint8_t *overlay, uint8_t top, uint8_t left, uint8_t height, uint8_t width, const uint8_t *payload, uint8_t len, const uint8_t *table);

// The same for a frame of frame_w x frame_h pixels; `frame` must hold
// (frame_w * frame_h + 7) / 8 bytes, all of which are cleared first.
bool prc_decode_roi_in(uint8_t *frame, uint8_t frame_w, uint8_t frame_h, uint8_t top, uint8_t left, uint8_t height, uint8_t width, const uint8_t *payload, uint8_t len, const uint8_t *table);
