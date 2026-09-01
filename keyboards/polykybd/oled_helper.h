// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "base/fonts/gfxfont.h"

/* Utility: encode a value as digit glyphs into a char32 (UTF-32) buffer.
   ⚠️ `buffer` is written as uint32_t codepoints, so every caller must pass a
   uint32_t[] cast to char* (the char* signature is historical) — a char[] would
   be misaligned. `buffer_len` is the BYTE size, i.e. sizeof(that array). */
void num_to_u32_string(uint32_t* buffer, uint8_t buffer_len, uint8_t value);
void num16_to_u32_string(uint32_t* buffer, uint8_t buffer_len, uint16_t value);
void hex_to_u32_string(uint32_t* buffer, uint8_t buffer_len, uint8_t value);
/* Widen an ASCII string into a UTF-32 buffer for the kdisp text pipeline */
void ascii_to_u32_string(uint32_t* buffer, uint8_t buffer_len, const char* s);

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
/* FW-2: "Unsigned firmware! / A = ACCEPT" (left) resp. "R = REJECT" (right) — the
   words behind the big A/R keycaps, so the board says WHY it went modal and not
   just which key does what. Driven by the synced poly_sync_t.fw_confirm. */
void oled_fw_confirm_screen(void);
/* "⭯Applying / Firmware⭯" notice (resident circular refresh arrow U+2B6F) drawn +
   fully flushed on both halves the moment a staged firmware image is applied, right
   before the blocking self-flash + reboot. */
void oled_fw_apply_screen(void);
/* Flash progress bar with its top row at `top_y`: a 6 px bar over a 1 px track,
   `pct` (0..100) filling the full width of EACH status OLED (both halves move). */
void oled_fw_update_progress_bar(int8_t top_y, int8_t bottom_y, uint8_t pct);
/* Draw text / a number so its rightmost lit pixel lands on `right_x` — a variable-width
   value then grows leftward instead of overrunning whatever sits to its right. */
void oled_draw_text_right(const GFXfont *const *font, int8_t right_x, int8_t y, const uint32_t *text);
void oled_draw_num_right(const GFXfont *const *font, int8_t right_x, int8_t y, uint8_t value);
void oled_draw_num16_right(const GFXfont *const *font, int8_t right_x, int8_t y, uint16_t value);
/* Draw `pct` right-aligned so it ends just before `pct_sign_x` (where the caller
   draws "%"), so 2-/3-digit numbers grow left instead of overrunning the sign. */
void oled_fw_update_percent(const GFXfont *const *font, int8_t pct_sign_x, int8_t y, uint8_t pct);
uint8_t fw_update_percent(void);   /* 0..100 progress of the in-flight flash */
/* Typing-speed dial (11x6), defined in oled_helper.c and drawn by both variants'
   status OLEDs via kdisp_draw_bitmap. */
extern const uint8_t wpm_gauge_bitmap[];
#define WPM_ICON_W 11
#define WPM_ICON_H 6
/* Settings -> "More": firmware / protocol / hardware version, this half's uptime and
   the split-link health, in place of the ordinary status screen while the advanced
   settings row is revealed. Driven by the synced poly_sync_t.settings_more. */
void oled_telemetry_screen(void);
void oled_render_logos(void);
bool oled_task_user(void);
