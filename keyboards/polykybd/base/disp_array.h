// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "fonts/gfxfont.h"

#include <stdint.h>
#include <stdbool.h>

#define SCREEN_WIDTH 72
#define SCREEN_HEIGHT 40
#define BUFFER_X 28

// Courtyard radius (Chebyshev) for the glyph-mask dilation behind a glyph.
// `cy_radius` 0 disables the courtyard clear; KDISP_CY_DEFAULT is the standard
// margin used behind overlay glyphs. The lang-layer flags use a smaller radius.
#define KDISP_CY_DEFAULT 3

int8_t kdisp_write_gfx_char(const GFXfont *const *fonts, uint8_t num_fonts, int8_t x, int8_t y, uint32_t c, int8_t cy_radius);

// Sets a global pixel offset added to every subsequent gfx-char/text draw, used by
// the idle "jitter" anti-burn-in relocation redraw. Pass 0,0 to disable.
void kdisp_set_draw_offset(int8_t ox, int8_t oy);

// Glyph for codepoint `c` in a font set, or NULL if no font covers it (skips
// empty gap glyphs, no '!' fallback). For coverage tests + metric reads.
const GFXglyph *kdisp_gfx_glyph(const GFXfont *const *fonts, uint8_t num_fonts, uint32_t c);

// As above, but also writes the owning font to *out_font (or NULL when not
// found / out_font is NULL) — lets a caller redraw a glyph through a single-font
// array to avoid kdisp_write_gfx_char's baseline-align-to-fonts[0] shift.
const GFXglyph *kdisp_gfx_glyph_font(const GFXfont *const *fonts, uint8_t num_fonts, uint32_t c,
                                     const GFXfont **out_font);

// Composite a glyph downsampled 2x (2x2-OR) with its top-left at buffer coords
// (x,y) — no baseline align, (x,y) is the literal top-left. OR-combining each 2x2
// source block keeps thin strokes visible at half size (plain decimation drops
// them). Used to overlay a small secondary icon inside another glyph (the
// Win+Ctrl+Shift+B reload 🗘 drawn into the monitor's screen cavity).
void kdisp_draw_glyph_half_at(const GFXfont *const *fonts, uint8_t num_fonts, int8_t x, int8_t y, uint32_t c);

void kdisp_write_gfx_text(const GFXfont *const *fonts, uint8_t num_fonts, int8_t x, int8_t y, const uint32_t *text);

void kdisp_write_gfx_text_cy(const GFXfont *const *fonts, uint8_t num_fonts, int8_t x, int8_t y, const uint32_t *text, int8_t cy_radius);

// Measure the horizontal pixel bounding box of `text` (relative to the draw
// origin x) without drawing — mirrors the cursor advance of kdisp_write_gfx_text.
// *out_min / *out_max receive the leftmost / rightmost lit pixel offset, so a
// caller can place adjacent text clear of it (e.g. a shift-preview next to a wide
// base glyph).  Both are 0 for empty/blank text.
void kdisp_gfx_text_bounds(const GFXfont *const *fonts, uint8_t num_fonts, const uint32_t *text, int8_t *out_min, int8_t *out_max);

// Full pixel bounding box of `text` relative to the draw origin (x from the origin
// x, y from the baseline), mirroring every cursor rule — and the per-glyph vertical
// shift — of kdisp_write_gfx_text_cy. Lets a caller clamp a draw offset so the
// glyph stays fully on-screen (idle anti-burn-in jitter). All four are 0 for blank text.
void kdisp_gfx_text_bbox(const GFXfont *const *fonts, uint8_t num_fonts, const uint32_t *text,
                         int8_t *out_xmin, int8_t *out_xmax, int8_t *out_ymin, int8_t *out_ymax);

// Vertical (rotated -90°, bottom-to-top) single-font text, centred in the visible
// height with its baseline along x = col_x.  `selected` draws it as dark text on
// a filled bar (else lit text on the dark background).  Used by the lang layer.
void kdisp_write_gfx_vtext(const GFXfont *font, int8_t col_x, const uint32_t *text, bool selected);

void kdisp_write_base_char(int8_t x, int8_t y, char c);

void kdisp_draw_bitmap(int8_t x, int8_t y, const uint8_t pgm_bmp[], int8_t bmp_width, int8_t bmp_height);

void kdisp_clear_bitmap_courtyard(int8_t x, int8_t y, const uint8_t pgm_bmp[], int8_t bmp_width, int8_t bmp_height, int8_t radius);

void kdisp_set_buffer(uint8_t vertical_pixel_row_of_8_pixels);

void kdisp_send_buffer(void);

void kdisp_invert(bool invert);

void kdisp_scroll(bool activate);

void kdisp_scroll_modeh(bool left, uint8_t horizontal_speed_0to7);
void kdisp_scroll_modehv(bool left, uint8_t horizontal_speed_0to7, uint8_t vertical_offset_0to63);
void kdisp_scroll_vlines(uint8_t lines_0to63);

void kdisp_set_contrast(uint8_t contrast);

void kdisp_enable(bool enable);

void kdisp_hw_setup(void);

void kdisp_init(const int8_t num_shift_regs);

void kdisp_setup(bool turn_on);

uint8_t* get_scratch_buffer(void);

int16_t get_scratch_buffer_size(void);

void kdisp_fill_rect(int8_t x_start, int8_t y_start, int8_t width, int8_t height);

void kdisp_draw_round_rect(int8_t x, int8_t y, int8_t width, int8_t height, int8_t r);

// Shared tab chrome for the emoji and language selection layers: the selected-tab
// frame (3px north/east/west border, open at the bottom, with chamfered top
// corners) and the non-selected-tab bottom underline. Drawn page-wise straight
// into the keycap buffer (see disp_array.c). The keycode-range gating stays in
// each layer (the ranges differ); only the drawing is shared here.
void kdisp_draw_tab_frame(void);
void kdisp_draw_tab_underline(void);

