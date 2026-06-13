// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdint.h>
#include <stdbool.h>

/* Utility: encode an 8-bit value as digit glyphs into a char32 (UTF-32) buffer */
void num_to_u32_string(char* buffer, uint8_t buffer_len, uint8_t value);
void hex_to_u32_string(char* buffer, uint8_t buffer_len, uint8_t value);

/* Board-specific callbacks — implemented in split72/status_oled.c or split42/status_oled.c */
void oled_update_buffer(void);
void oled_draw_kybd(void);
void oled_draw_poly(void);

/* Shared OLED task functions — implemented in oled_helper.c */
void oled_status_screen(void);
void oled_render_logos(void);
bool oled_task_user(void);
