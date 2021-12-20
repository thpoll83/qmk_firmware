#pragma once

#include "fonts/gfxfont.h"

uint8_t kdisp_write_gfx_char(const GFXfont *gfxFont, int16_t x, int16_t y, unsigned char c);

void kdisp_write_gfx_text(const GFXfont *gfxFont, int16_t x, int16_t y, const char *text);

void kdisp_write_char(uint16_t x, uint16_t y, const char ch);

void kdisp_draw_bitmap(uint16_t x, uint16_t y, const uint8_t pgm_bmp[], uint8_t bmp_width, uint8_t bmp_height);

void kdisp_set_buffer(uint8_t vertical_pixel_row_of_8_pixels);

void kdisp_send_buffer(void);

void kdisp_invert(bool invert);

void kdisp_init(void);
