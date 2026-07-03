// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Keycap blitter for game mode: 320x200 8bpp framebuffer -> ordered dither ->
// per-keycap 72x40 1-bit tiles over the existing shift-register/SPI path.
#pragma once

#include <stdint.h>

#define DOOM_FB_WIDTH  320
#define DOOM_FB_HEIGHT 200
#define DOOM_FB_SIZE   (DOOM_FB_WIDTH * DOOM_FB_HEIGHT)

// The viewport is a 5x5 keycap block (one half): 5 keycap columns x 72 px =
// 360 px canvas; the 320 px frame sits centred with 20 px side margins.
#define DOOM_VIEW_COLS   5
#define DOOM_VIEW_ROWS   5
#define DOOM_CANVAS_XOFF ((DOOM_VIEW_COLS * 72 - DOOM_FB_WIDTH) / 2)

// Dither + push one frame to the 5x5 viewport of THIS half's displays.
// `fb_rows` is the source height (200 for the fire demo's full canvas, 168
// for the engine's view buffer — rows below it render black); `luma256` maps
// a framebuffer palette index to 0..255 brightness.
void doom_blit_frame(const uint8_t *fb, uint16_t fb_rows, const uint8_t *luma256);

// Blank every keycap display of this half (entering game mode: the keys
// outside the viewport keep whatever legend they held otherwise).
void doom_blit_blank_all(void);
