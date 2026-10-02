// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Parser for cmd 41 PRC overlay records. See prc_record.h; the reference is
// PolyKybdHost polyhost/util/prc_codec.py (parse_records()).

#include "prc_record.h"

uint8_t prc_parse_record(const uint8_t *buf, uint8_t avail, prc_record_t *out) {
    if (avail < PRC_RECORD_HDR || buf[0] == 0) return 0;
    // Bits after the keycode byte: 40 bits in buf[1..5].
    uint64_t f = 0;
    for (uint8_t i = 1; i < PRC_RECORD_HDR; i++) {
        f = (f << 8) | buf[i];
    }
    if (f & 0x0F) return 0;   // reserved
    prc_record_t r;
    r.keycode  = buf[0];
    r.modifier = (uint8_t)((f >> 36) & 0x0F);
    r.top      = (uint8_t)((f >> 30) & 0x3F);
    r.left     = (uint8_t)((f >> 23) & 0x7F);
    r.height   = (uint8_t)(((f >> 17) & 0x3F) + 1);
    r.width    = (uint8_t)(((f >> 10) & 0x7F) + 1);
    r.len      = (uint8_t)((f >> 4) & 0x3F);
    r.payload  = buf + PRC_RECORD_HDR;
    if ((uint16_t)r.top + r.height > PRC_FRAME_H || (uint16_t)r.left + r.width > PRC_FRAME_W) return 0;
    if (r.len > avail - PRC_RECORD_HDR) return 0;
    *out = r;
    return (uint8_t)(PRC_RECORD_HDR + r.len);
}
