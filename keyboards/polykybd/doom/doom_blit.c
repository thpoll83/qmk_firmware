// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Keycap blitter: carve the 320x200 8bpp framebuffer into 72x40 keycap tiles,
// ordered-dither to 1bpp and push each tile over the existing shift-register +
// SPI path. Bayer (not Floyd–Steinberg): O(1) per pixel, no error propagation
// across frames, so animated frames don't "crawl" (DOOM_FEASIBILITY.md,
// Challenge 1).
//
// v1 uses kdisp_send_buffer() (full 128x64 controller RAM, ~0.85 ms/key at
// 10 MHz -> ~21 ms per 25-key frame). The window-addressed 360 B path from the
// study (~8 ms/frame) is a later optimisation of this file only.
#include QMK_KEYBOARD_H

#include "doom_blit.h"

#include "base/disp_array.h"
#include "base/shift_reg.h"

#include <string.h>

#ifdef POLYKYBD_DOOM

// 4x4 Bayer matrix scaled to 0..255 thresholds (index*16 + 8).
static const uint8_t BAYER4[4][4] = {
    {  8, 136,  40, 168},
    {200,  72, 232, 104},
    { 56, 184,  24, 152},
    {248, 120, 216,  88},
};

// SSD1306 page layout of the shared scratch buffer: 8 pages x 128 columns,
// byte = (y>>3)*stride + x, bit = y&7; the visible 72 px window starts at
// column BUFFER_X (see disp_array.c).
#define OLED_PAGES  (SCREEN_HEIGHT / 8)

// Select the keycap display at viewport (row, col); false when that slot does
// not exist on this variant (split42 has 24 display slots, so the 5x5 viewport
// only partially maps — the demo targets split72's 40-slot halves).
static inline bool select_display(uint8_t view_row, uint8_t view_col) {
    const uint8_t disp_idx = (uint8_t)LAYOUT_TO_INDEX(view_row, view_col);
    if (disp_idx >= (uint8_t)(NUM_SHIFT_REGISTERS * 8)) {
        return false;
    }
    sr_shift_out_buffer_latch(get_key_disp_bitmask(disp_idx), get_disp_bitmask_size());
    return true;
}

void doom_blit_frame(const uint8_t *fb, const uint8_t *luma256) {
    uint8_t      *buf    = get_scratch_buffer();
    const int16_t stride = get_scratch_buffer_size() / 8; // controller bytes per page (128)

    for (uint8_t vr = 0; vr < DOOM_VIEW_ROWS; ++vr) {
        const uint16_t fy0 = (uint16_t)vr * SCREEN_HEIGHT;
        for (uint8_t vc = 0; vc < DOOM_VIEW_COLS; ++vc) {
            const int16_t fx0 = (int16_t)vc * SCREEN_WIDTH - DOOM_CANVAS_XOFF;

            if (!select_display(vr, vc)) {
                continue;
            }
            memset(buf, 0, (size_t)get_scratch_buffer_size());
            for (uint8_t page = 0; page < OLED_PAGES; ++page) {
                uint8_t       *dst = buf + (size_t)page * stride + BUFFER_X;
                const uint16_t fy  = fy0 + (uint16_t)page * 8;
                for (uint8_t x = 0; x < SCREEN_WIDTH; ++x) {
                    const int16_t fx = fx0 + x;
                    if (fx < 0 || fx >= DOOM_FB_WIDTH) {
                        continue; // 20 px canvas margin left/right of the frame
                    }
                    const uint8_t *src  = fb + (size_t)fy * DOOM_FB_WIDTH + fx;
                    uint8_t        bits = 0;
                    for (uint8_t bit = 0; bit < 8; ++bit) {
                        const uint8_t v = luma256[src[(size_t)bit * DOOM_FB_WIDTH]];
                        if (v > BAYER4[(fy + bit) & 3][fx & 3]) {
                            bits |= (uint8_t)(1u << bit);
                        }
                    }
                    dst[x] = bits;
                }
            }
            kdisp_send_buffer();
        }
    }
}

void doom_blit_blank_all(void) {
    kdisp_set_buffer(0x00);
    for (uint8_t i = 0; i < (uint8_t)(NUM_SHIFT_REGISTERS * 8); ++i) {
        sr_shift_out_buffer_latch(get_key_disp_bitmask(i), get_disp_bitmask_size());
        kdisp_send_buffer();
    }
}

#endif // POLYKYBD_DOOM
