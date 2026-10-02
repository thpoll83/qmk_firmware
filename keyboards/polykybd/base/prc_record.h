// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// PRC overlay records (HID cmd 41, protocol v19): the PolyKybd wire format that
// carries PRC-coded keycap images. The image codec itself is the polymod_prc
// community module (modules/polykybd/polymod_prc); this file is only the record
// header, which addresses an image the way cmds 16-19 do.
//
// ⚠️ ONE FORMAT WITH THE HOST: PolyKybdHost polyhost/util/prc_codec.py
// pack_record() writes what this parses.
//
// Pure: no QMK, no globals, so it builds and runs in the googletest suite.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "polymod_prc.h"

// One image inside a cmd 41 report. A report carries records back to back; a
// record whose keycode byte is 0 (KC_NO), or the end of the report, ends the list.
//
// Header: 6 bytes, a big-endian bit field, then `len` payload bytes.
//
//   keycode 8 | modifier 4 | top 6 | left 7 | height-1 6 | width-1 7 | len 6 | 0 4
//
// The image is addressed exactly like cmds 16-19 (keycode + modifier variant), so
// it resolves through the same translate/pool-slot path. The 4 trailing bits are
// reserved and must be 0. The box is checked against the 72x40 PRC_FRAME.
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
