// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Pure font lookup + the display-list-aware bounding-box interpreter, moved
// verbatim out of disp_array.c so they link on the host without the SPI/display
// plumbing. See font_lookup.h for the contract; the tests live in
// base/tests/font_bbox_tests.cpp (make test:polykybd_font_bbox).

#include "font_lookup.h"

const GFXglyph *kdisp_gfx_glyph_font(const GFXfont *const *fonts, uint8_t num_fonts, uint32_t ch,
                                     const GFXfont **out_font) {
    for (uint8_t idx = 0; idx < num_fonts; ++idx) {
        const GFXfont *f = fonts[idx];
        uint32_t first = pgm_read_dword(&f->first);
        uint32_t last  = pgm_read_dword(&f->last);
        if (ch >= first && ch <= last) {
            const GFXglyph *gg = pgm_read_glyph_ptr(f, ch - first);
            if (pgm_read_byte(&gg->width) == 0 && pgm_read_byte(&gg->height) == 0 &&
                pgm_read_byte(&gg->xAdvance) == 0) {
                continue;  // non-contiguous-range padding; a later font may have it
            }
            if (out_font) *out_font = f;
            return gg;
        }
    }
    if (out_font) *out_font = NULL;
    return NULL;
}

void kdisp_gfx_text_bbox_in(const GFXfont *const *fonts, uint8_t num_fonts,
                            const GFXfont *const *mid_font, uint8_t mid_count, const uint32_t *text,
                            int8_t *out_xmin, int8_t *out_xmax, int8_t *out_ymin, int8_t *out_ymax) {
    // Cursor relative to the draw origin: x from 0, y from the baseline (0). Mirrors
    // every cursor rule of kdisp_write_gfx_text_cy, including the vertical controls,
    // so the measured box matches what would actually be drawn — both axes.
    int16_t x = 0, y = 0;
    int16_t xmn = 127, xmx = -128, ymn = 127, ymx = -128;
    const int16_t base_yadv = pgm_read_byte(&fonts[0]->yAdvance);
    bool small = false;
    // HINT_MID (\x16) swaps the font array for the rest of the run, exactly as the
    // draw does — and, exactly as the draw does, PER GLYPH: the mid face is
    // ASCII-only and anything outside it falls back to the caller's pool. The
    // baseline reference has to follow that per-glyph choice, because the draw
    // aligns by `font->yAdvance - fonts[0]->yAdvance` and fonts[0] is the mid face
    // only for the glyphs the mid face supplied. Using one base for the whole run
    // would report every fallback glyph shifted by the difference between the faces.
    bool mid   = false;
    while (*text != 0) {
        switch (*text) {
            case U'\x05':                     y += 2; break; // down 2px
            case U'\f':                       y = y > 1 ? y - 2 : 0; break; // up 2px
            case U'\v':                       y += ((y) / 15 + 1) * 15; break;
            case U'\x18':                     x = 0; y = 0; break; // cancel -> origin
            case U'\r':                       x = 0; break;
            case U'\b':                       x = x > 1 ? x - 2 : 0; break;
            case U'\x06':                     x += 2; break; // nudge right 2px
            case U'\t':                       x += ((x) / 36 + 1) * 36; break;
            case U'\n':                       y += base_yadv; x = 0; break;
            // ---- the hint display-list ops, mirrored so their ARGUMENT codepoints are
            //      not measured as glyphs. Without this each op byte and each argument
            //      falls into `default:`, matches no font and is substituted with '!',
            //      so a legend carrying one MOVE measured three bogus glyphs.
            //      \x0E is an ABSOLUTE buffer position, which this relative-to-origin
            //      measurement cannot resolve; skipping it is strictly better than
            //      measuring '!', but a MOVE'd legend's box still only covers the part
            //      of it that is laid out relatively.
            case U'\x10':                    small = true; break;   // SMALL: rest of the run is half-scale
            case U'\x16':                                            // MID: rest of the run is the 19px UI face
                mid = true;                                          //   (per glyph — see the fallback below)
                break;
            case U'\x0F':                                           // HALF / THIN composite a glyph at the
            case U'\x11': if (text[1]) text += 1; break;            //   cursor and do not advance
            case U'\x15': if (text[1] && text[2]) text += 2; break;            // ROT (angle, glyph)
            case U'\x0E':   // MOVE (x,y): the cursor lands on an ABSOLUTE buffer position, which
                             //   this relative-to-origin measurement cannot resolve — so the move
                             //   itself is ignored and a MOVE'd legend's box covers only the part
                             //   laid out relatively. Its two ARGUMENTS are still skipped.
                             //
                             //   ⚠️ They MUST be, and this used to fall through to \x14 and skip
                             //   nothing. A coordinate is an arbitrary byte, so it lands in this
                             //   very switch on the next iteration: 13 of the 31 HINT_POS_* /
                             //   HINT_SZ_* / MTB_* macros carry a byte that is also an op
                             //   (HINT_SZ_STOPSQ is 15,15 = \x0F \x0F, i.e. HALF HALF). Before,
                             //   that mis-measured a glyph; once \x15/\x16 existed it could
                             //   silently latch a font for the rest of the run or eat two real
                             //   codepoints. Skipping the arguments closes the whole class,
                             //   including for any op added later.
                if (text[1] && text[2]) text += 2;
                break;
            case U'\x14':                    /* ERASE: a mode, no extent */ break;
            case U'\x13': if (text[1] && text[2] && text[3]) text += 3; break;  // BADGE (w,h,style)
            case U'\x12': if (text[1] && text[2]) text += 2; break;             // FRAME (w,h)
            default: {
                // Locate the font whose [first,last] contains the codepoint (linear
                // scan; this is a cold measuring path, no MRU cache needed).
                uint32_t ch = *text, first = 0, last = 0;
                // Mirror the draw's per-glyph fallback: MID applies only to codepoints
                // the mid face actually has.
                const bool            use_mid = mid && kdisp_gfx_glyph(mid_font, mid_count, *text) != NULL;
                const GFXfont *const *pool = use_mid ? mid_font : fonts;
                const uint8_t         cnt  = use_mid ? mid_count : num_fonts;
                const GFXfont *f = 0;
                for (uint8_t i = 0; i < cnt; ++i) {
                    first = pgm_read_dword(&pool[i]->first);
                    last  = pgm_read_dword(&pool[i]->last);
                    if (ch >= first && ch <= last) { f = pool[i]; break; }
                }
                if (!f) { f = pool[0]; first = pgm_read_dword(&f->first); ch = U'!'; }
                const GFXglyph *g = pgm_read_glyph_ptr(f, ch - first);
                int8_t w  = pgm_read_byte(&g->width);
                int8_t h  = pgm_read_byte(&g->height);
                int8_t xo = pgm_read_byte(&g->xOffset);
                int8_t yo = pgm_read_byte(&g->yOffset);
                if (w > 0 && h > 0) {
                    // In a SMALL run mirror kdisp_write_gfx_char_half instead of
                    // kdisp_write_gfx_char — halved extents and offsets with the same
                    // floor rounding — so the measured box still matches the pixels.
                    int16_t gx = small ? glyph_half_floor((int16_t)xo) : (int16_t)xo;
                    int16_t gw = small ? (int16_t)((w + 1) / 2) : (int16_t)w;
                    int16_t gh = small ? (int16_t)((h + 1) / 2) : (int16_t)h;
                    int16_t l = x + gx, r = x + gx + gw - 1;
                    if (l < xmn) xmn = l;
                    if (r > xmx) xmx = r;
                    // kdisp_write_gfx_char shifts each glyph by (font yAdvance - fonts[0]
                    // yAdvance) before applying yOffset; mirror it so the vertical box
                    // matches the rasterised pixels.
                    int16_t yadj = (int16_t)pgm_read_byte(&f->yAdvance) -
                                   (use_mid ? (int16_t)pgm_read_byte(&mid_font[0]->yAdvance) : base_yadv);
                    int16_t gy = small ? glyph_half_floor((int16_t)(yadj + yo)) : (int16_t)(yadj + yo);
                    int16_t t = y + gy, b = t + gh - 1;
                    if (t < ymn) ymn = t;
                    if (b > ymx) ymx = b;
                }
                {
                    int16_t adv = (int16_t)pgm_read_byte(&g->xAdvance);
                    x += small ? (int16_t)((adv + 1) / 2) : adv;
                }
                break;
            }
        }
        text++;
    }
    if (xmx < xmn) { xmn = 0; xmx = 0; ymn = 0; ymx = 0; }   // empty / whitespace-only
    *out_xmin = (int8_t)xmn;
    *out_xmax = (int8_t)xmx;
    *out_ymin = (int8_t)ymn;
    *out_ymax = (int8_t)ymx;
}
