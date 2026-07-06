// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "base/fonts/gfxfont.h"

/* Utility: encode an 8-bit value as digit glyphs into a char32 (UTF-32) buffer */
void num_to_u32_string(char* buffer, uint8_t buffer_len, uint8_t value);
/* Same, for a 16-bit value (0..65535) — used by the LTR-559 sensor readout */
void num16_to_u32_string(char* buffer, uint8_t buffer_len, uint16_t value);
void hex_to_u32_string(char* buffer, uint8_t buffer_len, uint8_t value);
/* Widen an ASCII string into a UTF-32 buffer for the kdisp text pipeline */
void ascii_to_u32_string(char* buffer, uint8_t buffer_len, const char* s);

/* Draw the active default-layout name (Qwerty / Colemak DH / …) at (x,y). Shared
   by both status-OLED variants so the layout list can't drift between them. */
void oled_draw_layout_name(const GFXfont* const* font, int8_t x, int8_t y, uint8_t def_layer);

/* Board-specific callbacks — implemented in split72/status_oled.c or split42/status_oled.c */
void oled_update_buffer(void);
void oled_update_buffer_fw_update(void);   /* "Updating fonts/firmware …" screen */
void oled_draw_kybd(void);
void oled_draw_poly(void);

/* Shared OLED task functions — implemented in oled_helper.c */
void oled_status_screen(void);
void oled_fw_update_screen(void);
/* Flash progress bar with its top row at `top_y`: a 6 px bar over a 1 px track,
   `pct` (0..100) filling the full width of EACH status OLED (both halves move). */
void oled_fw_update_progress_bar(int8_t top_y, int8_t bottom_y, uint8_t pct);
/* Draw `pct` right-aligned so it ends just before `pct_sign_x` (where the caller
   draws "%"), so 2-/3-digit numbers grow left instead of overrunning the sign. */
void oled_fw_update_percent(const GFXfont *const *font, int8_t pct_sign_x, int8_t y, uint8_t pct);
uint8_t fw_update_percent(void);   /* 0..100 progress of the in-flight flash */
void oled_render_logos(void);
bool oled_task_user(void);
