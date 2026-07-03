// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Keycap-dither brightness of PLAYPAL palette 0 from the v1.9 shareware
// DOOM1.WAD (sha256 1d7d43be…, the same IWAD doom1.whx is converted from):
// L = max(Rec709, 0.6 * max(R,G,B)). Plain Rec.709 maps DOOM's saturated
// reds/blues to ~21%/8% — menu text and the big status-bar digits (red) and
// keycards (blue) dithered to near-black on the 1-bit keycaps (field round
// 7). The max-channel floor lifts saturated colors to readable while leaving
// neutral grays/browns (walls, floors) EXACTLY at their Rec.709 value.
// Generated from the WAD by a one-off script (see doom/tools/README note).
// Palette flashes (damage/pickup, doom_shim_palette) are ignored in v1.
#pragma once
#include <stdint.h>

static const uint8_t DOOM_PLAYPAL_LUMA[256] = {
      0,  24,  16,  75, 255,  27,  19,  11,   7,  52,  39,  28,  20,  62,  54,  46,
    198, 187, 180, 169, 162, 154, 143, 135, 127, 120, 115, 112, 107, 105, 100,  98,
     93,  91,  86,  83,  79,  76,  71,  69,  64,  62,  57,  55,  50,  47,  43,  40,
    238, 232, 225, 219, 215, 209, 202, 199, 192, 184, 176, 168, 160, 152, 144, 140,
    134, 125, 121, 116, 108, 102,  95,  90,  85,  76,  71,  65,  57,  49,  43,  35,
    239, 231, 223, 219, 211, 203, 199, 191, 183, 179, 171, 167, 159, 151, 147, 139,
    131, 127, 119, 111, 107,  99,  91,  87,  79,  71,  67,  59,  55,  47,  39,  35,
    216, 202, 188, 175, 162, 148, 134, 123, 110,  97,  83,  70,  56,  42,  30,  19,
    170, 162, 154, 146, 138, 132, 126, 118, 111, 103,  99,  91,  86,  78,  70,  66,
    135, 122, 110,  98,  85,  73,  62,  53, 124, 112, 104,  95,  84,  75,  67,  60,
    245, 213, 184, 156, 127,  99,  81,  69, 255, 227, 201, 176, 153, 153, 153, 153,
    153, 143, 136, 129, 122, 115, 107, 100,  93,  83,  76,  69,  62,  55,  47,  40,
    233, 203, 177, 153, 153, 153, 153, 153, 153, 136, 122, 107,  93,  79,  64,  50,
    255, 238, 221, 208, 191, 177, 161, 153, 146, 141, 134, 129, 122, 117, 110, 105,
    255, 252, 250, 247, 244, 242, 239, 237, 100,  95,  88,  81,  62,  50,  38,  30,
     50,  43,  35,  28,  21,  14,   7,   0, 173, 225, 161, 153, 124,  95,  67, 120
};
