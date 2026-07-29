// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#include "status_oled.h"
#include "../oled_helper.h"

#include "split42.h"
#include "../bridge_helper.h"
#include "../state.h"
#include "../side.h"
#include "../base/com.h"
#include "../base/disp_array.h"
#include "../base/text_helper.h"
#include "../base/fonts/NotoSans_Regular_Base_11pt.h"
#include "../base/fonts/NotoSans_Medium_Base_8pt.h"
#include "../lang/named_glyphs.h"
#include "../config.h"                          // FULL_BRIGHT

// Layout name (Mid 10pt, half-scale) + portrait body text (Tiny 6px). These font
// data live in util_font.h / lang_label_font.h, already compiled into poly_keymap.c
// (mid_fonts / lang label) — declare them extern here to avoid a duplicate
// definition at link time (same pattern oled_helper.c uses for its externs).
extern const GFXfont NotoSans_Regular_Mid_10pt7b;
extern const GFXfont NotoSans_Regular_Tiny_6pt7b;

#include QMK_KEYBOARD_H
#include "quantum.h"

#include <stdint.h>
#include <stdio.h>

// Status-OLED chrome (layer/lock icons, arrows) lives in the resident font set,
// which sits at the front of the runtime g_all_fonts[] table.
#include "base/fontpack.h"
#include "base/fw_staging.h"  // fw_staging_active_target/image_size/next_offset, FW_TARGET_*

// Role icons (16x16, MSB-first) — USB trident (host) and Link ⇄ data-exchange
// (bridge). Mirror of split72/status_oled.c.
static const uint8_t usb_status_bitmap[] PROGMEM = {
    0x00, 0x80, 0x01, 0xc0, 0x01, 0xc0, 0x03, 0xe0, 0x03, 0xe0, 0x00, 0x80, 0x00, 0xb8, 0x04, 0xb8,
    0x0e, 0xb8, 0x0e, 0x90, 0x04, 0xe0, 0x03, 0x80, 0x00, 0x80, 0x01, 0xc0, 0x03, 0x60, 0x01, 0xc0,
};
static const uint8_t link_status_bitmap[] PROGMEM = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x00, 0x1c, 0x7f, 0xfe, 0x7f, 0xfe, 0x00, 0x00,
    0x00, 0x00, 0x7f, 0xfe, 0x7f, 0xfe, 0x38, 0x00, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

/*
 * PORTRAIT status screen for the 128x32 OLED. The panel is physically 128 wide x
 * 32 tall but MOUNTED ROTATED 90°, so the user reads it as 32 wide x 128 tall.
 * The poly render pipeline blits a raw page-format buffer via oled_write_raw()
 * (which bypasses QMK's OLED_ROTATION), so we compose the layout in a *logical*
 * 32x128 portrait space and software-rotate every lit pixel into the physical
 * 128x32 page buffer (pset() below). split42 has no RGB, so both halves share the
 * top chrome; the middle band differs by physical side (see the layout comment in
 * oled_update_buffer). Design/preview: tools/status_oled42_preview.py mirrors this
 * (--diag for the clip check).
 *
 *   logical portrait (both halves)          left side        right side
 *   ─────────────────────────────           ─────────        ──────────
 *   role icon + Usb/Lnk                      brightness bars   Num lock
 *   layer icon + number                      WPM + value       Caps lock
 *   layout name (Mid 10pt, half-scale)       ┐
 *   [ per-side band ]                         ┴ globe + lang index (both)
 *   globe + lang index                        side L/R (both, very bottom)
 */
#define P_W 32                 // logical portrait width  (= physical panel height)
#define P_H 128                // logical portrait height (= physical panel width)
#define STATUS_STRIDE 128      // scratch page-buffer stride (BUFFER_BYTE_WIDTH)

// 90° mapping: logical portrait (lx,ly) -> physical panel (px,py) in page format.
// If the screen comes up upside-down / mirrored on real hardware, flip this switch
// (it is the ONLY orientation knob — everything else composes in logical space).
#define POLY42_STATUS_ROT_CW 1
static inline void pset(uint8_t* buf, int lx, int ly) {
    if ((unsigned)lx >= P_W || (unsigned)ly >= P_H) return;   // clips negative too
#if POLY42_STATUS_ROT_CW
    int px = ly;               int py = (P_W - 1) - lx;
#else
    int px = (P_H - 1) - ly;   int py = lx;
#endif
    buf[(py >> 3) * STATUS_STRIDE + px] |= (uint8_t)(1u << (py & 7));
}

// Blit a full-scale glyph: (x, baseline) in logical portrait coords, no baseline
// align (uses the glyph's own y/xOffset). Returns the glyph's xAdvance (0 if none).
static int pdraw_glyph(const GFXfont* const* fonts, uint8_t n, int x, int baseline, uint32_t cp, uint8_t* buf) {
    const GFXfont* f;
    const GFXglyph* g = kdisp_gfx_glyph_font(fonts, n, cp, &f);
    if (!g) return 0;
    uint16_t bo = pgm_read_word(&g->bitmapOffset);
    int w = pgm_read_byte(&g->width),  h  = pgm_read_byte(&g->height);
    int xo = (int8_t)pgm_read_byte(&g->xOffset), yo = (int8_t)pgm_read_byte(&g->yOffset);
    const uint8_t* bmp = (const uint8_t*)pgm_read_ptr(&f->bitmap);
    int bit = 0; uint8_t bits = 0;
    for (int gy = 0; gy < h; gy++)
        for (int gx = 0; gx < w; gx++) {
            if (!(bit++ & 7)) bits = pgm_read_byte(&bmp[bo++]);
            if (bits & 0x80)  pset(buf, x + xo + gx, baseline + yo + gy);
            bits <<= 1;
        }
    return pgm_read_byte(&g->xAdvance);
}

static int ptext_adv(const GFXfont* const* fonts, uint8_t n, const uint32_t* t) {
    int w = 0;
    for (; *t; t++) { const GFXfont* f; const GFXglyph* g = kdisp_gfx_glyph_font(fonts, n, *t, &f); if (g) w += pgm_read_byte(&g->xAdvance); }
    return w;
}

static void pdraw_text(const GFXfont* const* fonts, uint8_t n, int x, int baseline, const uint32_t* t, uint8_t* buf) {
    for (; *t; t++) x += pdraw_glyph(fonts, n, x, baseline, *t, buf);
}

static void pdraw_text_center(const GFXfont* const* fonts, uint8_t n, int baseline, const uint32_t* t, uint8_t* buf) {
    int x = (P_W - ptext_adv(fonts, n, t)) / 2; if (x < 0) x = 0;
    pdraw_text(fonts, n, x, baseline, t, buf);
}

static void pdraw_glyph_center(const GFXfont* const* fonts, uint8_t n, int baseline, uint32_t cp, uint8_t* buf) {
    const GFXfont* f; const GFXglyph* g = kdisp_gfx_glyph_font(fonts, n, cp, &f); if (!g) return;
    int x = (P_W - pgm_read_byte(&g->xAdvance)) / 2; if (x < 0) x = 0;
    pdraw_glyph(fonts, n, x, baseline, cp, buf);
}

// Draw a glyph downsampled 2x (raw top-left at (x, top_y), offsets ignored — like
// kdisp_draw_glyph_half_at). Returns the halved height. Used for the globe.
static int pdraw_glyph_half(const GFXfont* const* fonts, uint8_t n, int x, int top_y, uint32_t cp, uint8_t* buf) {
    const GFXfont* f; const GFXglyph* g = kdisp_gfx_glyph_font(fonts, n, cp, &f); if (!g) return 0;
    uint16_t bo = pgm_read_word(&g->bitmapOffset);
    int w = pgm_read_byte(&g->width), h = pgm_read_byte(&g->height);
    const uint8_t* bmp = (const uint8_t*)pgm_read_ptr(&f->bitmap);
    int bit = 0; uint8_t bits = 0;
    for (int gy = 0; gy < h; gy++)
        for (int gx = 0; gx < w; gx++) {
            if (!(bit++ & 7)) bits = pgm_read_byte(&bmp[bo++]);
            if (bits & 0x80)  pset(buf, x + gx / 2, top_y + gy / 2);
            bits <<= 1;
        }
    return (h + 1) / 2;
}

// Word-level half-scale text, centered horizontally at logical row top_y. Renders
// the whole word full-scale into a temp bitmap then 2x2-OR downsamples it (keeps
// inter-glyph spacing, unlike per-glyph halving). Used for the compact layout name.
#define TMP_W 96
#define TMP_H 24
#define TMP_WB ((TMP_W + 7) / 8)
#define TMP_BASE 18
static void pdraw_text_center_half(const GFXfont* const* fonts, uint8_t n, int top_y, const uint32_t* t, uint8_t* buf) {
    uint8_t tmp[TMP_H * TMP_WB];
    memset(tmp, 0, sizeof(tmp));
    int cx = 0, minx = TMP_W, maxx = -1, miny = TMP_H, maxy = -1;
    for (; *t; t++) {
        const GFXfont* f; const GFXglyph* g = kdisp_gfx_glyph_font(fonts, n, *t, &f);
        if (!g) { cx += 4; continue; }   // fallback advance (e.g. a space with no glyph)
        uint16_t bo = pgm_read_word(&g->bitmapOffset);
        int w = pgm_read_byte(&g->width),  h  = pgm_read_byte(&g->height);
        int xo = (int8_t)pgm_read_byte(&g->xOffset), yo = (int8_t)pgm_read_byte(&g->yOffset);
        const uint8_t* bmp = (const uint8_t*)pgm_read_ptr(&f->bitmap);
        int bit = 0; uint8_t bits = 0;
        for (int gy = 0; gy < h; gy++)
            for (int gx = 0; gx < w; gx++) {
                if (!(bit++ & 7)) bits = pgm_read_byte(&bmp[bo++]);
                if (bits & 0x80) {
                    int tx = cx + xo + gx, ty = TMP_BASE + yo + gy;
                    if ((unsigned)tx < TMP_W && (unsigned)ty < TMP_H) {
                        tmp[ty * TMP_WB + (tx >> 3)] |= (uint8_t)(0x80 >> (tx & 7));
                        if (tx < minx) minx = tx;
                        if (tx > maxx) maxx = tx;
                        if (ty < miny) miny = ty;
                        if (ty > maxy) maxy = ty;
                    }
                }
                bits <<= 1;
            }
        cx += pgm_read_byte(&g->xAdvance);
    }
    if (maxx < 0) return;                          // empty
    int hw = ((maxx - minx) / 2) + 1;
    int ox = (P_W - hw) / 2; if (ox < 0) ox = 0;
    for (int ty = miny; ty <= maxy; ty++)
        for (int tx = minx; tx <= maxx; tx++)
            if (tmp[ty * TMP_WB + (tx >> 3)] & (0x80 >> (tx & 7)))
                pset(buf, ox + (tx - minx) / 2, top_y + (ty - miny) / 2);
}

static void pdraw_bitmap(const uint8_t* pgm, int ox, int oy, int w, int h, uint8_t* buf) {
    int bw = (w + 7) / 8;
    for (int gy = 0; gy < h; gy++)
        for (int gx = 0; gx < w; gx++)
            if (pgm_read_byte(&pgm[gy * bw + (gx >> 3)]) & (0x80 >> (gx & 7)))
                pset(buf, ox + gx, oy + gy);
}

// 10-segment brightness gauge (contrast 0..FULL_BRIGHT), left-aligned. Filled
// segments step up in height left-to-right (same staircase as split72's gauge, at
// half the pitch to fit 32px), so the level reads from the silhouette and not just
// from where the bars stop. Unlit segments keep a 1px foot so the full scale shows.
#define P42_GAUGE_MIN_H 2
static void pdraw_brightness(uint8_t contrast, int bottom_y, uint8_t* buf) {
    int bars = (contrast * 10 + FULL_BRIGHT / 2) / FULL_BRIGHT; if (bars > 10) bars = 10;
    for (int i = 0; i < 10; i++) {
        int bx = i * 3;
        int h  = (i < bars) ? P42_GAUGE_MIN_H + i : 1;
        for (int yy = 0; yy < h; yy++) { pset(buf, bx, bottom_y - yy); pset(buf, bx + 1, bottom_y - yy); }
    }
}
#define P42_GAUGE_H (P42_GAUGE_MIN_H + 9)   // tallest bar == band height

// split42 short layout names — must stay in sync with oled_helper.c's full-name
// array (indexed by def_layer). Shortened so they fit the 32px-wide portrait column.
static const uint32_t* layout_name_short(uint8_t def_layer) {
    static const uint32_t* const names[] = { U"Qwrty", U"Stag!", U"ColDH", U"Neo", U"Wkmn" };
    return (def_layer < ARRAY_SIZE(names)) ? names[def_layer] : U"Unkn";
}

void oled_update_buffer(void) {
    uint8_t* buf = get_scratch_buffer();
    kdisp_set_buffer(0);                            // clear the whole scratch to black

    const poly_layer_t* gl = get_global_layer();
    const poly_sync_t*  ls = get_local_state();
    const GFXfont* midFont[]  = { &NotoSans_Regular_Mid_10pt7b };
    const GFXfont* tinyFont[] = { &NotoSans_Regular_Tiny_6pt7b };
    uint32_t nbuf[8];

    // Row 1: role icon (USB trident / Link ⇄) + word. Link icon runs half off the
    // left edge (intentional); pset() clips the negative-x columns.
    if (is_usb_host_side()) { pdraw_bitmap(usb_status_bitmap,  -3, 0, 16, 16, buf); pdraw_text(tinyFont, 1, 10, 12, U"Usb", buf); }
    else                    { pdraw_bitmap(link_status_bitmap, -8, 0, 16, 16, buf); pdraw_text(tinyFont, 1, 10, 12, U"Lnk", buf); }

    // The two halves carry DIFFERENT panels rather than the same one twice: at 32px
    // wide there is no room to repeat, and layer/layout/brightness/speed all describe
    // the keyboard as a whole, so one copy is enough. The layout half keeps those; the
    // lock half gets the locks and the language slot. Each is then spread over the
    // full 128px column instead of bunching at the top.
    const bool locks_side = (!side_is_undecided() && !is_left_side());
    if (!locks_side) {
        // Layer icon + number
        pdraw_glyph(g_all_fonts, g_all_font_count, 0, 41, 0x80 /*ICON_LAYER*/, buf);
        hex_to_u32_string((char*)nbuf, sizeof(nbuf), get_highest_layer(gl->layer));
        pdraw_text(tinyFont, 1, 18, 38, nbuf, buf);
        // Layout name (short), Mid 10pt at half scale, centered
        pdraw_text_center_half(midFont, 1, 52, layout_name_short(get_local_layer()->def_layer), buf);
        pdraw_brightness(ls->contrast, 82, buf);
        // Typing speed: dial over the value
        pdraw_bitmap(wpm_gauge_bitmap, (P_W - WPM_ICON_W) / 2, 93, WPM_ICON_W, WPM_ICON_H, buf);
        num_to_u32_string((char*)nbuf, sizeof(nbuf), get_current_wpm());
        pdraw_text_center(tinyFont, 1, 110, nbuf, buf);
    } else {
        pdraw_glyph_center(g_all_fonts, g_all_font_count, 36, gl->led_state.num_lock  ? 0x8D : 0x8C, buf);
        pdraw_glyph_center(g_all_fonts, g_all_font_count, 60, gl->led_state.caps_lock ? 0x8F : 0x8E, buf);
        // Globe (half-scale) + the "xx-YY" code stacked under it, as split72 does in
        // its RGB-off column. One line will not do: "en-US" is 32px at 6pt, the exact
        // panel width, and the widest code ("mn-MN") is 40px.
        const int gh = pdraw_glyph_half(g_all_fonts, g_all_font_count, (P_W - 20) / 2, 68, 0x1F310 /*🌐*/, buf);
        const uint32_t* code = poly_lang_code(ls->lang);
        for(uint8_t half = 0; half < 2 && code[0]; ++half) {
            nbuf[0] = code[half * 3];
            nbuf[1] = code[half * 3 + 1];
            nbuf[2] = 0;
            pdraw_text_center(tinyFont, 1, 68 + gh + 12 + half * 12, nbuf, buf);
        }
    }

    // Physical side marker at the very bottom
    pdraw_text_center(tinyFont, 1, 126, side_is_undecided() ? U"?" : (is_left_side() ? U"L" : U"R"), buf);
}

// "Updating fonts/firmware …" screen (128x32) shown while a flash is in progress.
// kdisp_set_buffer(0) clears the scratch first. For a FIRMWARE flash the master can't
// repaint a moving bar mid-stream, so it shows a static notice and the slave the bar.
void oled_update_buffer_fw_update(void) {
    uint32_t buffer[8];
    kdisp_set_buffer(0);
    const GFXfont* smallFont[] = { &NotoSans_Medium8pt7b };
    uint8_t target = fw_staging_active_target();
    bool    fonts = (target == FW_TARGET_FONTPACK || target == FW_TARGET_DOOMWAD);
    uint8_t pct   = fw_update_percent();

    if (!fonts && is_keyboard_master()) {
        // Firmware, master half: static notice (only 32 px tall → two lines).
        kdisp_write_gfx_text(smallFont, 1, 0, 12, U"PolyKybd");
        kdisp_write_gfx_text(smallFont, 1, 0, 28, U"FW Update...");
        return;
    }

    if (fonts) {
        // Name the bundle being written (fixed slot) so progress shows bundle-by-bundle.
        const char* bname = fontpack_slot_name(fw_staging_fontpack_slot_off());
        kdisp_write_gfx_text(smallFont, 1, 0, 10, U"Fonts:");
        if (bname) {
            ascii_to_u32_string((char*)buffer, sizeof(buffer), bname);
            kdisp_write_gfx_text(smallFont, 1, 44, 10, buffer);
        }
        oled_fw_update_percent(smallFont, 24, 22, pct);
        kdisp_write_gfx_text(smallFont, 1, 24, 22, U"% — do not unplug");
    } else {
        // Firmware, slave half: live progress.
        kdisp_write_gfx_text(smallFont, 1, 0, 10, U"Progress:");
        oled_fw_update_percent(smallFont, 84, 10, pct);
        kdisp_write_gfx_text(smallFont, 1, 84, 10, U"%");
    }
    oled_fw_update_progress_bar(25, 31, pct);
}

/*
 * 128×32 logo bitmaps (512 bytes each = 128*32/8).
 * TODO: Replace these placeholder bitmaps with actual 128×32 artwork.
 * For now, a minimal placeholder pattern is used so the firmware compiles.
 */
void oled_draw_kybd(void) {
    static const char kybd_bitmap[512] = { 0 };
    oled_write_raw_P(kybd_bitmap, sizeof(kybd_bitmap));
}

void oled_draw_poly(void) {
    static const char poly_bitmap[512] = { 0 };
    oled_write_raw_P(poly_bitmap, sizeof(poly_bitmap));
}
