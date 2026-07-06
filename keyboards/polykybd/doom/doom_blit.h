// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Keycap blitter for game mode: 320x200 8bpp framebuffer -> ordered dither ->
// per-keycap 72x40 1-bit tiles over the existing shift-register/SPI path.
#pragma once

#include <stdint.h>
#include <stdbool.h>

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

// Engine-frame variant: scanline-major over the full 200-row canvas, each
// line sourced + overlay-composed by doom_shim_compose_line() (menus, HUD,
// status bar), so the sequential vpatch bookkeeping holds. Uses the arena
// compose scratch (DOOM_ARENA_COMPOSE_OFF). With skip_bottom_row the
// viewport's bottom key row is left untouched — the slave mirror keeps its
// thumb/cursor-key legends there (the lost canvas band is the map's bottom
// 20 % and the map follows the player anyway; field round 13). Safe to stop
// the compose early: its vpatch bookkeeping fully resets per frame.
// map_frame draws a 2 px border around the blitted block (the slave's
// automap view, field round 17).
// `place_row_off` (0/1) shifts the whole block DOWN by that many keycap rows and
// `place_col_inset` (SIGNED) shifts it along the display columns: 0 = game
// placement (byte-identical to the old call), NEGATIVE reclaims the HUD's outer
// column so the block sits flush to this half's true outer edge, POSITIVE insets
// it. The IDDQD attract screensaver rolls row_off 0/1 and inset -1..+2 (i.e. 0..3
// empty columns counted from the outer edge) so its lit keycaps migrate
// (anti-burn-in); at the max inset + row_off 1 the bottom row reaches the two
// inner thumb keys (matrix cols 6/7). The blitter BOUNDS the result column to
// [0, MATRIX_COLS-1] so an edge cell can never wrap into the next keycap row.
// Vacated cells are NOT cleared here (blank the viewport when the placement changes).
void doom_blit_frame_engine(const uint8_t *luma256, bool skip_bottom_row, bool map_frame,
                            uint8_t place_row_off, int8_t place_col_inset);

// Slave readable menu mirror (field round 17): the master's menu items +
// blinking skull on the viewport's upper 4 key rows. doom_mode.c drives the
// repaints (snapshot change / skull blink; !full repaints only column 0).
void doom_blit_menu(const uint8_t *luma256, bool skull_alt, bool full);

// Fire reticle on the keycap display at raw (row, disp_col) — the master's
// Ctrl key at the bottom of its outer HUD column.
void doom_blit_fire_key(uint8_t row, uint8_t disp_col);

// The shared ESC exit-hint face on the display at raw (row, disp_col) —
// the master's HUD corner (the slave pad renders the same face through
// doom_render_esc_key on its aliased corner).
void doom_blit_esc_key(uint8_t row, uint8_t disp_col);

// Blank every keycap display of this half (entering game mode: the keys
// outside the viewport keep whatever legend they held otherwise).
void doom_blit_blank_all(void);

// Outer-column HUD helpers: draw a stat key — word label (10 px) over a
// full-size value — on the display at (row, DISPLAY col — no viewport
// mapping), or blank that key. Used for the vitals beside the game viewport.
void doom_blit_stat_key(uint8_t row, uint8_t disp_col, const uint32_t *label, const uint32_t *value);
// Same key, but the value drawn in the game's tall red status-bar digits
// (decoded from the WHX). False when the glyphs are unavailable — fall back
// to doom_blit_stat_key with font digits (nothing has been drawn then).
bool doom_blit_stat_num_key(uint8_t row, uint8_t disp_col, const uint32_t *label,
                            int value, const uint8_t *luma256);
void doom_blit_blank_key(uint8_t row, uint8_t disp_col);
