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
#include "doom_mode.h"   // doom_shim_compose_begin/line (engine path)
#include "doom_arena.h"  // compose scratch carve

#include "side.h"            // is_left_side() (bottom-row viewport mapping)
#include "base/disp_array.h"
#include "base/fontpack.h"   // g_all_fonts (HUD text keys)
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

// Viewport column -> display column, from the physical positions in
// keyboard.json / g_led_config (field rounds 6+8). The viewport sits one
// column IN from the outer edge on both halves so the outermost column is
// free for the vitals HUD:
//  * left half:  upper viewport = display cols 1-5 (x=1.5..5.5; col 0 at
//    x=0 is the outer/HUD column, separated by an extra 0.5u anyway). The
//    bottom row's keys sit at x=0.5,1.5,2.5,3.5,5.25 — under viewport cols
//    0,1,2 and (nearly) 4, with the thumb-cluster gap at viewport col 3.
//  * right half: upper viewport = display cols 0-4 (x=13.5..17.5 after the
//    matrix col-1 shift of rows 5-8 — see split72.c invert_display; col 6 at
//    x=19.75 is the outer/HUD column). The bottom row keeps raw matrix cols:
//    keys at x=13.5 (col 2), 14.5 (col 3), 16.5 (col 4), 17.5 (col 5) — gap
//    under viewport col 2 (x=15.5).
// A 0xFF entry = physical gap: that canvas tile has no display.
static inline uint8_t view_to_disp_col(uint8_t view_row, uint8_t view_col) {
#if defined(KEYBOARD_polykybd_split72)
    if (view_row == DOOM_VIEW_ROWS - 1) {
        static const uint8_t bottom_left[DOOM_VIEW_COLS]  = {1, 2, 3, 0xFF, 4};
        static const uint8_t bottom_right[DOOM_VIEW_COLS] = {2, 3, 0xFF, 4, 5};
        return is_left_side() ? bottom_left[view_col] : bottom_right[view_col];
    }
    if (is_left_side()) {
        return (uint8_t)(view_col + 1);
    }
#endif
    return view_col;
}

// Select the keycap display at viewport (row, col); false when that slot does
// not exist on this variant (split42 has 24 display slots, so the 5x5 viewport
// only partially maps — the demo targets split72's 40-slot halves) or the
// viewport cell sits in the bottom row's thumb-cluster gap.
static inline bool select_display(uint8_t view_row, uint8_t view_col) {
    const uint8_t disp_col = view_to_disp_col(view_row, view_col);
    if (disp_col == 0xFF) {
        return false;
    }
    const uint8_t disp_idx = (uint8_t)LAYOUT_TO_INDEX(view_row, disp_col);
    if (disp_idx >= (uint8_t)(NUM_SHIFT_REGISTERS * 8)) {
        return false;
    }
    sr_shift_out_buffer_latch(get_key_disp_bitmask(disp_idx), get_disp_bitmask_size());
    return true;
}

void doom_blit_frame(const uint8_t *fb, uint16_t fb_rows, const uint8_t *luma256) {
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
                        if (fy + bit >= fb_rows) {
                            break; // below the source frame: stays black
                        }
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

// Scanline-major engine path: canvas rows advance strictly 0..199 so the
// shim's vpatch overlay bookkeeping (sequential per-patch data offsets, like
// upstream's beam-ordered scanout) holds. Each composed 320 px line is
// dithered into per-column band buffers (5 x 360 B OLED tiles, carved from
// the arena next to the 320 B line — no spare .bss in a doom build), and a
// finished 40-row band is pushed to its 5 keycaps.
void doom_blit_frame_engine(const uint8_t *luma256, bool skip_bottom_row) {
    uint8_t *scratch = doom_arena_at(DOOM_ARENA_COMPOSE_OFF);
    if (!scratch) {
        return;
    }
    uint8_t *line  = scratch;                     // 320 B, 8bpp PLAYPAL indices
    uint8_t *bands = scratch + DOOM_FB_WIDTH;     // 5 x (5 pages x 72 B) 1bpp tiles
    const uint16_t band_bytes = (uint16_t)(SCREEN_HEIGHT / 8) * SCREEN_WIDTH; // 360
    const uint8_t  view_rows  = skip_bottom_row ? DOOM_VIEW_ROWS - 1 : DOOM_VIEW_ROWS;

    doom_shim_compose_begin();
    for (uint8_t vr = 0; vr < view_rows; ++vr) {
        memset(bands, 0, (size_t)DOOM_VIEW_COLS * band_bytes);
        for (uint8_t ln = 0; ln < SCREEN_HEIGHT; ++ln) {
            const uint16_t y = (uint16_t)vr * SCREEN_HEIGHT + ln;
            doom_shim_compose_line(line, y);
            const uint16_t page_off = (uint16_t)(ln >> 3) * SCREEN_WIDTH;
            const uint8_t  bit      = (uint8_t)(1u << (ln & 7));
            for (uint8_t vc = 0; vc < DOOM_VIEW_COLS; ++vc) {
                uint8_t      *row = bands + (size_t)vc * band_bytes + page_off;
                const int16_t fx0 = (int16_t)vc * SCREEN_WIDTH - DOOM_CANVAS_XOFF;
                for (uint8_t x = 0; x < SCREEN_WIDTH; ++x) {
                    const int16_t fx = fx0 + x;
                    if (fx < 0 || fx >= DOOM_FB_WIDTH) {
                        continue; // canvas margin stays black
                    }
                    if (luma256[line[fx]] > BAYER4[y & 3][fx & 3]) {
                        row[x] |= bit;
                    }
                }
            }
        }
        uint8_t      *buf    = get_scratch_buffer();
        const int16_t stride = get_scratch_buffer_size() / 8;
        for (uint8_t vc = 0; vc < DOOM_VIEW_COLS; ++vc) {
            if (!select_display(vr, vc)) {
                continue;
            }
            memset(buf, 0, (size_t)get_scratch_buffer_size());
            for (uint8_t page = 0; page < OLED_PAGES; ++page) {
                memcpy(buf + (size_t)page * stride + BUFFER_X,
                       bands + (size_t)vc * band_bytes + (size_t)page * SCREEN_WIDTH,
                       SCREEN_WIDTH);
            }
            kdisp_send_buffer();
        }
    }
}

// Select a display by raw DISPLAY coordinates (no viewport column mapping) —
// the HUD lives on the outer columns, addressed the same way the normal
// legend renderer does.
static bool select_display_raw(uint8_t row, uint8_t disp_col) {
    const uint8_t disp_idx = (uint8_t)LAYOUT_TO_INDEX(row, disp_col);
    if (disp_idx >= (uint8_t)(NUM_SHIFT_REGISTERS * 8)) {
        return false;
    }
    sr_shift_out_buffer_latch(get_key_disp_bitmask(disp_idx), get_disp_bitmask_size());
    return true;
}

// 10 px label font for the stat keys — defined by poly_keymap.c's inclusion
// of base/fonts/util_font.h (external linkage; including the data header a
// second time would duplicate the arrays).
extern const GFXfont NotoSans_Regular_Mid_10pt7b;
static const GFXfont *const hud_label_fonts[] = {&NotoSans_Regular_Mid_10pt7b};

static int8_t center_x(const GFXfont *const *fonts, uint8_t n, const uint32_t *text) {
    int8_t gmin = 0, gmax = 0;
    kdisp_gfx_text_bounds(fonts, n, text, &gmin, &gmax);
    return (int8_t)(BUFFER_X + (SCREEN_WIDTH - (gmax - gmin)) / 2 - gmin);
}

// Vitals value in the game's own tall red status-bar digits (STTNUM/STTMINUS
// decoded from the WHX, field round 14 "extract the font"): word label on top
// (10 px mid font, same as the string variant), the DOOM digits centred in the
// band below at native size, thresholded on the saturation-floored luma so the
// red gradient renders solid with its dark outline. False when the glyphs are
// unavailable (engine down / patch missing) — the caller falls back to
// doom_blit_stat_key with font digits; nothing is drawn then.
bool doom_blit_stat_num_key(uint8_t row, uint8_t disp_col, const uint32_t *label,
                            int value, const uint8_t *luma256) {
    // Digits first — bail before touching the display on any decode problem.
    uint8_t glyphs[3];
    uint8_t n = 0;
    if (value < 0) {
        glyphs[n++] = DOOM_TALLNUM_MINUS;
    } else {
        if (value > 999) {
            value = 999;
        }
        if (value >= 100) glyphs[n++] = (uint8_t)(value / 100);
        if (value >= 10)  glyphs[n++] = (uint8_t)((value / 10) % 10);
        glyphs[n++] = (uint8_t)(value % 10);
    }
    uint8_t  gw[3], gh[3];
    uint16_t total = 0;
    uint8_t  hmax  = 0;
    for (uint8_t i = 0; i < n; ++i) {
        if (!doom_shim_tallnum_glyph(glyphs[i], NULL, &gw[i], &gh[i])) {
            return false;
        }
        total += gw[i];
        if (gh[i] > hmax) hmax = gh[i];
    }
    if (total > SCREEN_WIDTH || hmax > SCREEN_HEIGHT - 14) {
        return false;
    }
    if (!select_display_raw(row, disp_col)) {
        return true; // no such display — nothing to fall back to either
    }
    kdisp_set_buffer(0x00);
    kdisp_write_gfx_text(hud_label_fonts, 1, center_x(hud_label_fonts, 1, label), 13, label);

    uint8_t      *buf    = get_scratch_buffer();
    const int16_t stride = get_scratch_buffer_size() / 8;
    // Centre the digit run in the band below the label (rows 14..39).
    uint16_t      x  = (uint16_t)(BUFFER_X + (SCREEN_WIDTH - total) / 2);
    const uint8_t y0 = (uint8_t)(14 + (SCREEN_HEIGHT - 14 - hmax) / 2);
    uint8_t dec[DOOM_TALLNUM_MAX_W * DOOM_TALLNUM_MAX_H];
    for (uint8_t i = 0; i < n; ++i) {
        uint8_t w = 0, h = 0;
        if (!doom_shim_tallnum_glyph(glyphs[i], dec, &w, &h)) {
            break; // half-drawn worst case; the next HUD pass repaints
        }
        // Baseline-align digits of differing heights (the minus is short).
        const uint8_t gy = (uint8_t)(y0 + hmax - h);
        for (uint8_t sy = 0; sy < h; ++sy) {
            for (uint8_t sx = 0; sx < w; ++sx) {
                // Threshold BELOW the darkest red of the digit gradient
                // (saturation-floored luma 40..153) but above the near-black
                // outline (<20) — 96 kept only the brightest half and made
                // the digits patchy (field round 15, "unreadable").
                if (luma256[dec[(size_t)sy * w + sx]] >= 36) {
                    const uint16_t X = x + sx;
                    const uint8_t  Y = (uint8_t)(gy + sy);
                    buf[(size_t)(Y >> 3) * stride + X] |= (uint8_t)(1u << (Y & 7));
                }
            }
        }
        x += w;
    }
    kdisp_send_buffer();
    return true;
}

void doom_blit_stat_key(uint8_t row, uint8_t disp_col, const uint32_t *label, const uint32_t *value) {
    if (!select_display_raw(row, disp_col)) {
        return;
    }
    kdisp_set_buffer(0x00);
    // Word label in the 10 px mid font on the top line (y=13 — a 10 px glyph
    // occupies roughly [y-10, y], so this clears the top edge; y=10 clipped
    // ascenders, field round 9); the value full-size below it (y=36 — 33
    // still overlapped the label on hardware, field round 10).
    kdisp_write_gfx_text(hud_label_fonts, 1, center_x(hud_label_fonts, 1, label), 13, label);
    kdisp_write_gfx_text(g_all_fonts, g_all_font_count,
                         center_x(g_all_fonts, g_all_font_count, value), 36, value);
    kdisp_send_buffer();
}

void doom_blit_blank_key(uint8_t row, uint8_t disp_col) {
    if (!select_display_raw(row, disp_col)) {
        return;
    }
    kdisp_set_buffer(0x00);
    kdisp_send_buffer();
}

void doom_blit_blank_all(void) {
    kdisp_set_buffer(0x00);
    for (uint8_t i = 0; i < (uint8_t)(NUM_SHIFT_REGISTERS * 8); ++i) {
        sr_shift_out_buffer_latch(get_key_disp_bitmask(i), get_disp_bitmask_size());
        kdisp_send_buffer();
    }
}

#endif // POLYKYBD_DOOM
