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
#include "doom_mode.h"        // doom_shim_compose_begin/line (engine path)
#include "doom_arena.h"       // compose scratch carve
#include "doom_playpal_luma.h" // ESC-hint STCFN threshold (256 B, per-TU const)

#include "side.h"            // is_left_side() (bottom-row viewport mapping)
#include "bridge_helper.h"   // is_usb_host_side() (slave-left viewport shift)
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
    // BOTH halves' inward shift depends on their ROLE (field rounds 24+25):
    //  * left half:  as MASTER the outer col 0 is the vitals HUD (viewport
    //    cols 1-5); as SLAVE the outer TWO display columns (0+1) are the
    //    ESC/weapon pad, so the viewport sits one further in (cols 2-6).
    //  * right half: as SLAVE its pad is cols 5+6, viewport 0-4; as MASTER
    //    the HUD is col 6 and the viewport hugs it at cols 1-5 (round 25:
    //    "move one column further out (right)" — 0-4 left a dark column
    //    between viewport and HUD, mirroring nothing).
    const bool master = is_usb_host_side();
    if (view_row == DOOM_VIEW_ROWS - 1) {
        static const uint8_t bottom_left_m[DOOM_VIEW_COLS]  = {1, 2, 3, 0xFF, 4};
        static const uint8_t bottom_left_s[DOOM_VIEW_COLS]  = {2, 3, 0xFF, 4, 0xFF};
        static const uint8_t bottom_right_m[DOOM_VIEW_COLS] = {3, 4, 0xFF, 5, 6};
        static const uint8_t bottom_right_s[DOOM_VIEW_COLS] = {2, 3, 0xFF, 4, 5};
        if (is_left_side()) {
            return master ? bottom_left_m[view_col] : bottom_left_s[view_col];
        }
        return master ? bottom_right_m[view_col] : bottom_right_s[view_col];
    }
    if (is_left_side()) {
        return (uint8_t)(view_col + (master ? 1 : 2));
    }
    return (uint8_t)(view_col + (master ? 1 : 0));
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

// Placement-aware select for the IDDQD attract screensaver. `inset` < 0 means
// "no placement" — behaves EXACTLY like select_display() (the game/mirror path,
// byte-identical). `inset` 0..4 is the anti-burn-in placement: the 5x4 block is
// drawn `row_off` keycap rows lower and slid `inset` GRID columns inward.
//
// GRID columns run OUTER(0) -> INNER on both halves — but the display-column
// numbering does NOT: composing split72.c invert_display + the BITMASK chip-select
// with g_led_config's physical x shows disp_col 0 is the OUTER edge on the LEFT yet
// the INNER edge on the RIGHT (the `c--` fold reverses the right's upper rows), and
// the bottom matrix row is physically STAGGERED vs the rows above — it has a gap
// where an upper key has no key below it, and the two inner thumb keys are stacked
// (same x, two rows) inboard of everything. So a naive contiguous disp_col sweep is
// backwards on the right and misaligns the bottom row (field: "right bottom shifted
// left by one"; "left bottom col-5 is a missing display"). Instead we work in grid
// space and translate per half + per row from the physical alignment (offline model
// in tools, validated against hardware): the upper rows are 7 real columns
// (grid 0..6; grid 7/8 = inner phantoms with no OLED), the bottom row maps grid ->
// display col with an explicit GAP and the two thumbs at grid 7/8. Over the inset
// 0..4 cycle this covers every real display on the half — both thumbs and the
// in-between key included — as a physically coherent block.
static inline bool select_display_placed(uint8_t view_row, uint8_t view_col,
                                         uint8_t row_off, int8_t inset) {
    if (inset < 0) {
        return select_display(view_row, view_col);   // game/mirror: no placement
    }
    const uint8_t disp_row = (uint8_t)(view_row + row_off);
    const uint8_t grid     = (uint8_t)(view_col + inset);   // 0..8, outer -> inner
    const bool    left     = is_left_side();
    int16_t       disp_col;
    if (disp_row < 4) {
        // Upper rows: 7 real display columns (grid 0..6); grid 7/8 are the inner
        // phantom slots with no OLED — drop out. Left counts up, right counts down.
        if (grid > 6) {
            return false;
        }
        disp_col = left ? (int16_t)grid : (int16_t)(6 - grid);
    } else {
        // Bottom row: staggered. grid -> display col, GAP(0xFF) where no key sits
        // under that upper slot, thumbs at grid 7/8. (Derived from physical x.)
        static const uint8_t bottom_left[9]  = {0, 1, 2, 3, 0xFF, 4, 5, 6, 7};
        static const uint8_t bottom_right[9] = {7, 6, 5, 4, 0xFF, 3, 2, 0, 1};
        if (grid > 8) {
            return false;
        }
        const uint8_t c = left ? bottom_left[grid] : bottom_right[grid];
        if (c == 0xFF) {
            return false;   // physical gap — no keycap under this grid slot
        }
        disp_col = (int16_t)c;
    }
    if (disp_col < 0 || disp_col >= MATRIX_COLS) {
        return false;   // never let LAYOUT_TO_INDEX wrap into the next keycap row
    }
    const uint8_t disp_idx = (uint8_t)LAYOUT_TO_INDEX(disp_row, (uint8_t)disp_col);
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
void doom_blit_frame_engine(const uint8_t *luma256, bool skip_bottom_row, bool map_frame,
                            uint8_t place_row_off, int8_t place_col_inset) {
    uint8_t *scratch = doom_arena_at(DOOM_ARENA_COMPOSE_OFF);
    if (!scratch) {
        return;
    }
    uint8_t *line  = scratch;                     // 320 B, 8bpp PLAYPAL indices
    uint8_t *bands = scratch + DOOM_FB_WIDTH;     // 5 x (5 pages x 72 B) 1bpp tiles
    const uint16_t band_bytes = (uint16_t)(SCREEN_HEIGHT / 8) * SCREEN_WIDTH; // 360
    const uint8_t  view_rows  = skip_bottom_row ? DOOM_VIEW_ROWS - 1 : DOOM_VIEW_ROWS;
    const uint16_t total_rows = (uint16_t)view_rows * SCREEN_HEIGHT;

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
            if (map_frame) {
                // 2 px frame around the blitted block ("a frame around the
                // 5x4 keys", field round 17) — display space, so it hugs
                // the visible key edges incl. the canvas margins.
                if (y <= 1 || y + 2 > total_rows) {
                    for (uint8_t vc = 0; vc < DOOM_VIEW_COLS; ++vc) {
                        uint8_t *row = bands + (size_t)vc * band_bytes + page_off;
                        for (uint8_t x = 0; x < SCREEN_WIDTH; ++x) {
                            row[x] |= bit;
                        }
                    }
                } else {
                    bands[page_off + 0] |= bit;
                    bands[page_off + 1] |= bit;
                    uint8_t *last = bands + (size_t)(DOOM_VIEW_COLS - 1) * band_bytes + page_off;
                    last[SCREEN_WIDTH - 2] |= bit;
                    last[SCREEN_WIDTH - 1] |= bit;
                }
            }
        }
        uint8_t      *buf    = get_scratch_buffer();
        const int16_t stride = get_scratch_buffer_size() / 8;
        for (uint8_t vc = 0; vc < DOOM_VIEW_COLS; ++vc) {
            if (!select_display_placed(vr, vc, place_row_off, place_col_inset)) {
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

// Word label in the game's own small HUD font (STCFN, decoded from the WHX —
// round 16 "use the font to write Ammo etc"), centred in the top band (rows
// 0..13, above the digits at 14+). Glyphs are upscaled 3:2 VERTICALLY only
// (7 px -> ~11 px) and thresholded at the digits' 64 — taller text with
// unchanged stroke width (field round 17: "maybe 2px too small and could be
// a bit skinnier"). False when any glyph is unavailable or the label doesn't
// fit — the caller draws the 10 px mid font label instead (nothing has been
// drawn then: sizes are probed up front).
static bool draw_hufont_label(const uint32_t *label, const uint8_t *luma256) {
    uint8_t chs[10];
    uint8_t n = 0;
    for (const uint32_t *c = label; *c; ++c) {
        uint32_t ch = *c;
        if (ch >= 'a' && ch <= 'z') {
            ch -= 32; // STCFN is uppercase-only
        }
        if (ch < 33 || ch > 95 || n >= (uint8_t)sizeof(chs)) {
            return false;
        }
        chs[n++] = (uint8_t)ch;
    }
    if (!n) {
        return false;
    }
    uint8_t  gw[10], gh[10];
    uint16_t total = 0;
    uint8_t  hmax  = 0;
    for (uint8_t i = 0; i < n; ++i) {
        if (!doom_shim_hufont_glyph(chs[i], NULL, &gw[i], &gh[i])) {
            return false;
        }
        total += (uint16_t)(gw[i] + 1); // 1 px letter spacing
        const uint8_t hout = (uint8_t)((gh[i] * 3 + 1) / 2);
        if (hout > hmax) hmax = hout;
    }
    total -= 1;
    if (total > SCREEN_WIDTH || hmax > 13) {
        return false;
    }
    uint8_t      *buf    = get_scratch_buffer();
    const int16_t stride = get_scratch_buffer_size() / 8;
    uint16_t      x      = (uint16_t)(BUFFER_X + (SCREEN_WIDTH - total) / 2);
    const uint8_t y0     = (uint8_t)((14 - hmax) / 2);
    uint8_t dec[DOOM_HUFONT_MAX_W * DOOM_HUFONT_MAX_H];
    for (uint8_t i = 0; i < n; ++i) {
        uint8_t w = 0, h = 0;
        if (!doom_shim_hufont_glyph(chs[i], dec, &w, &h)) {
            return true; // half-drawn worst case; the next HUD pass repaints
        }
        const uint8_t hout = (uint8_t)((h * 3 + 1) / 2);
        for (uint8_t oy = 0; oy < hout; ++oy) {
            const uint8_t sy = (uint8_t)((uint16_t)oy * h / hout); // 3:2 NN
            for (uint8_t sx = 0; sx < w; ++sx) {
                if (luma256[dec[(size_t)sy * w + sx]] >= 64) {
                    const uint16_t X = x + sx;
                    const uint8_t  Y = (uint8_t)(y0 + oy);
                    buf[(size_t)(Y >> 3) * stride + X] |= (uint8_t)(1u << (Y & 7));
                }
            }
        }
        x += w + 1;
    }
    return true;
}

// One HU-font word at 2x, top-aligned-centred in the value band (rows
// 14..39), solid at the digits' 64 threshold — the ESC hint's "ESC". False
// when a glyph is unavailable — the caller draws the legacy font pair.
static bool draw_hufont_word2x(const uint32_t *word, const uint8_t *luma256) {
    uint8_t chs[6];
    uint8_t n = 0;
    for (const uint32_t *c = word; *c; ++c) {
        uint32_t ch = *c;
        if (ch >= 'a' && ch <= 'z') {
            ch -= 32;
        }
        if (ch < 33 || ch > 95 || n >= (uint8_t)sizeof(chs)) {
            return false;
        }
        chs[n++] = (uint8_t)ch;
    }
    if (!n) {
        return false;
    }
    uint8_t  gw[6], gh[6];
    uint16_t total = 0;
    uint8_t  hmax  = 0;
    for (uint8_t i = 0; i < n; ++i) {
        if (!doom_shim_hufont_glyph(chs[i], NULL, &gw[i], &gh[i])) {
            return false;
        }
        total += (uint16_t)(2 * gw[i] + 2); // 2 px letter spacing at 2x
        if (2 * gh[i] > hmax) hmax = (uint8_t)(2 * gh[i]);
    }
    total -= 2;
    if (total > SCREEN_WIDTH || hmax > SCREEN_HEIGHT - 14) {
        return false;
    }
    uint8_t      *buf    = get_scratch_buffer();
    const int16_t stride = get_scratch_buffer_size() / 8;
    uint16_t      x      = (uint16_t)(BUFFER_X + (SCREEN_WIDTH - total) / 2);
    const uint8_t y0     = (uint8_t)(14 + (SCREEN_HEIGHT - 14 - hmax) / 2);
    uint8_t dec[DOOM_HUFONT_MAX_W * DOOM_HUFONT_MAX_H];
    for (uint8_t i = 0; i < n; ++i) {
        uint8_t w = 0, h = 0;
        if (!doom_shim_hufont_glyph(chs[i], dec, &w, &h)) {
            return true; // half-drawn worst case; the caller restarts clean
        }
        for (uint8_t sy = 0; sy < h; ++sy) {
            for (uint8_t sx = 0; sx < w; ++sx) {
                if (luma256[dec[(size_t)sy * w + sx]] < 64) {
                    continue;
                }
                for (uint8_t d = 0; d < 2; ++d) {
                    const uint8_t Y = (uint8_t)(y0 + 2 * sy + d);
                    for (uint8_t e = 0; e < 2; ++e) {
                        const uint16_t X = (uint16_t)(x + 2 * sx + e);
                        buf[(size_t)(Y >> 3) * stride + X] |= (uint8_t)(1u << (Y & 7));
                    }
                }
            }
        }
        x += (uint16_t)(2 * w + 2);
    }
    return true;
}

// Shared ESC exit-hint face — IDENTICAL on the master's HUD corner and the
// slave pad's aliased corner (field round 18: "make sure the layout on both
// ESC keys is the same"): "hold" in the game's small HUD font on the top
// band, "ESC" in the same font at 2x below. Legacy font pair when the STCFN
// glyphs are unavailable (no WHX / engine down).
void doom_render_esc_key(void) {
    kdisp_set_buffer(0x00);
    if (draw_hufont_label(U"hold", DOOM_PLAYPAL_LUMA) &&
        draw_hufont_word2x(U"ESC", DOOM_PLAYPAL_LUMA)) {
        return;
    }
    kdisp_set_buffer(0x00); // half-drawn worst case: restart clean
    kdisp_write_gfx_text(hud_label_fonts, 1, center_x(hud_label_fonts, 1, U"hold"), 13, U"hold");
    kdisp_write_gfx_text(g_all_fonts, g_all_font_count,
                         center_x(g_all_fonts, g_all_font_count, U"Esc"), 36, U"Esc");
}

void doom_blit_esc_key(uint8_t row, uint8_t disp_col) {
    if (!select_display_raw(row, disp_col)) {
        return;
    }
    doom_render_esc_key();
    kdisp_send_buffer();
}

// Vitals value in the game's own tall red status-bar digits (STTNUM/STTMINUS
// decoded from the WHX, field round 14 "extract the font"): word label on top
// (STCFN HUD font, mid-font fallback), the DOOM digits centred in the
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
    if (!draw_hufont_label(label, luma256)) {
        kdisp_write_gfx_text(hud_label_fonts, 1, center_x(hud_label_fonts, 1, label), 13, label);
    }

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
                // Threshold inside the digit gradient (saturation-floored
                // luma 40..153): 96 kept only the brightest half — patchy
                // (round 15 "unreadable"); 36 took the whole body — "a bit
                // too thick" (round 16). 64 drops just the darkest edge
                // shades, thinning the strokes while staying contiguous.
                if (luma256[dec[(size_t)sy * w + sx]] >= 64) {
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

// Fire/attack symbol for the slave pad's Ctrl keys (Ctrl is DOOM's fire
// binding — round 16 "the CTRL would need a symbol for fire or attack"):
// a crosshair reticle — 2 px ring, four axis ticks crossing it, centre dot —
// drawn into the CURRENTLY selected display's buffer. The update_displays
// doom_ctl branch owns selection, buffer clear and send (like the weapon
// icons), so this only sets pixels.
void doom_render_fire_key(void) {
    uint8_t      *buf    = get_scratch_buffer();
    const int16_t stride = get_scratch_buffer_size() / 8;
    const int16_t cx     = BUFFER_X + SCREEN_WIDTH / 2;
    const int16_t cy     = SCREEN_HEIGHT / 2;
    for (int8_t dy = -18; dy <= 18; ++dy) {
        const int16_t Y = (int16_t)(cy + dy);
        if (Y < 0 || Y >= SCREEN_HEIGHT) {
            continue;
        }
        const int16_t ay = (int16_t)(dy < 0 ? -dy : dy);
        for (int8_t dx = -18; dx <= 18; ++dx) {
            const int16_t X  = (int16_t)(cx + dx);
            const int16_t ax = (int16_t)(dx < 0 ? -dx : dx);
            const int16_t d2 = (int16_t)(dx * dx + dy * dy);
            const bool on = (d2 >= 11 * 11 && d2 <= 13 * 13) ||             // ring
                            (d2 <= 2 * 2) ||                                // centre dot
                            (ax <= 1 && ay >= 8 && ay <= 17) ||             // N/S ticks
                            (ay <= 1 && ax >= 8 && ax <= 17);               // E/W ticks
            if (on) {
                buf[(size_t)(Y >> 3) * stride + X] |= (uint8_t)(1u << (Y & 7));
            }
        }
    }
}

// Fire hint on the MASTER's own Ctrl key (round 17 "I cannot see a symbol
// on the CTRL key": the v17 reticle hung off the slave pad's Ctrl keycode,
// but the right half — the usual slave — has no Ctrl at all; the fire key
// the player actually presses is the LEFT half's Ctrl, which sits at the
// bottom of the master's outer HUD column, outside the viewport).
void doom_blit_fire_key(uint8_t row, uint8_t disp_col) {
    if (!select_display_raw(row, disp_col)) {
        return;
    }
    kdisp_set_buffer(0x00);
    doom_render_fire_key();
    kdisp_send_buffer();
}

// Use/open symbol for the pad's Space key (DOOM's use binding — round 17
// "for the space key we could use a door symbol"): a door leaf with a knob
// and a recessed panel, same currently-selected-display contract as the
// fire reticle below.
void doom_render_use_key(void) {
    uint8_t      *buf    = get_scratch_buffer();
    const int16_t stride = get_scratch_buffer_size() / 8;
    const int16_t x0     = BUFFER_X + SCREEN_WIDTH / 2 - 13; // 26 x 34 leaf
    const int16_t y0     = 3;
    for (int16_t y = 0; y < 34; ++y) {
        for (int16_t x = 0; x < 26; ++x) {
            const bool edge  = x < 2 || x >= 24 || y < 2 || y >= 32;
            const bool knob  = x >= 19 && x <= 21 && y >= 16 && y <= 18;
            const bool panel = (x >= 5 && x <= 15 && y >= 5 && y <= 28) &&
                               (x == 5 || x == 15 || y == 5 || y == 28);
            if (edge || knob || panel) {
                const int16_t X = x0 + x;
                const int16_t Y = y0 + y;
                buf[(size_t)(Y >> 3) * stride + X] |= (uint8_t)(1u << (Y & 7));
            }
        }
    }
}

// Readable menu mirror on the slave viewport (field round 17): rows 0-3 of
// the 5x5 block carry the master's menu — items in the game's big menu font
// at 2x, the blinking skull on the selected row's first column, everything
// else dark. `full` repaints all 4x5 keys (snapshot changed); !full only
// column 0 (the skull blink). The per-key tiles come from the shim (it
// decodes this half's own WHX vpatches).
void doom_blit_menu(const uint8_t *luma256, bool skull_alt, bool full) {
    uint8_t       tile[(SCREEN_HEIGHT / 8) * SCREEN_WIDTH];
    uint8_t      *buf    = get_scratch_buffer();
    const int16_t stride = get_scratch_buffer_size() / 8;
    for (uint8_t vr = 0; vr < DOOM_VIEW_ROWS - 1; ++vr) {
        const uint8_t vc_end = full ? DOOM_VIEW_COLS : 1;
        for (uint8_t vc = 0; vc < vc_end; ++vc) {
            if (!doom_shim_menu_key_tile(vr, vc, tile, luma256, skull_alt)) {
                return; // menu just closed — the regular view resumes
            }
            if (!select_display(vr, vc)) {
                continue;
            }
            memset(buf, 0, (size_t)get_scratch_buffer_size());
            for (uint8_t page = 0; page < OLED_PAGES; ++page) {
                memcpy(buf + (size_t)page * stride + BUFFER_X,
                       tile + (size_t)page * SCREEN_WIDTH, SCREEN_WIDTH);
            }
            kdisp_send_buffer();
        }
    }
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
