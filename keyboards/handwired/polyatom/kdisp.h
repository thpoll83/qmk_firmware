#pragma once

void kdisp_draw_bitmap(int16_t x, int16_t y, const uint8_t* bitmap, int16_t w, int16_t h);

void kdisp_set_buffer(uint8_t vertical_pixel_row_of_8_pixels);

void kdisp_send_buffer(void);

void kdisp_invert(bool invert);

void kdisp_init(void);
