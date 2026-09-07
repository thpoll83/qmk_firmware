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

// sin(k * 15 deg) in 8.8 fixed point, k = 0..23. cos(k) is sin(k + 6), so one
// table covers both. 24 steps is the whole resolution the ROT op offers: at these
// glyph sizes a finer angle changes no pixels, and the table stays 48 bytes.
static const int16_t s_sin15[24] = {
       0,   66,  128,  181,  222,  247,  256,  247,
     222,  181,  128,   66,    0,  -66, -128, -181,
    -222, -247, -256, -247, -222, -181, -128,  -66,
};

void kdisp_gfx_rot_half_extent(int16_t w, int16_t h, uint8_t step, kdisp_rot_half_t *out) {
    // Screen y runs DOWN, so a visually counter-clockwise turn is a negative angle
    // in this arithmetic — hence the (24 - step) index. The op's argument is stated
    // in the direction a reader means by "counter-clockwise", not the sign the
    // maths uses.
    const uint8_t idx = (uint8_t)((24u - (step % 24u)) % 24u);
    out->ct = s_sin15[(idx + 6u) % 24u];
    out->st = s_sin15[idx];

    // Centre of the source box, and the output extent, both in 8.8. The extent
    // comes from forward-rotating the four corners: a rotated box is wider than
    // the original.
    out->cx = ((int32_t)(w - 1) << 8) / 2;
    out->cy = ((int32_t)(h - 1) << 8) / 2;
    int32_t x0 = INT32_MAX, x1 = INT32_MIN, y0 = INT32_MAX, y1 = INT32_MIN;
    for (uint8_t c = 0; c < 4; ++c) {
        const int32_t sx = (c & 1u) ? out->cx : -out->cx;
        const int32_t sy = (c & 2u) ? out->cy : -out->cy;
        const int32_t dx = (sx * out->ct - sy * out->st) >> 8;
        const int32_t dy = (sx * out->st + sy * out->ct) >> 8;
        if (dx < x0) x0 = dx;
        if (dx > x1) x1 = dx;
        if (dy < y0) y0 = dy;
        if (dy > y1) y1 = dy;
    }
    out->x0 = x0;
    out->y0 = y0;
    // The rotation runs at full resolution and is halved afterwards (halving first
    // throws away the pixels the rotation needs to rebuild an edge), so the plotted
    // size is the rotated extent halved — rounded UP, exactly as the drawer loops.
    out->w = (int16_t)((((int16_t)(((x1 - x0) >> 8) + 1)) + 1) / 2);
    out->h = (int16_t)((((int16_t)(((y1 - y0) >> 8) + 1)) + 1) / 2);
}

// One walk of the display list, shared by both bounding-box entry points below.
//
// (ox, oy) is the draw origin: x from there, y being the baseline. In the RELATIVE
// form it is (0, 0), which reduces every cursor rule here to the expression it had
// when this measured only in origin-relative space — so that form is unchanged, to
// the byte, and its 34 tests still pin it.
//
// `resolve` says whether the cursor can be trusted to name a real buffer position.
// It can only when the caller supplied the origin, and it is what gates the two
// things that need one: resolving a MOVE, and measuring the composite ops (which
// plot AT the cursor through primitives of their own, rather than advancing it).
static void bbox_walk(const GFXfont *const *fonts, uint8_t num_fonts,
                      const GFXfont *const *mid_font, uint8_t mid_count, const uint32_t *text,
                      int16_t ox, int16_t oy, bool resolve, bool saturate,
                      int8_t *out_xmin, int8_t *out_xmax, int8_t *out_ymin, int8_t *out_ymax) {
    // Cursor. Mirrors every cursor rule of kdisp_write_gfx_text_cy, including the
    // vertical controls, so the measured box matches what would actually be drawn —
    // both axes.
    int16_t x = ox, y = oy;
    int16_t xmn = 32767, xmx = -32768, ymn = 32767, ymx = -32768;
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
            // ⚠️ `saturate` is what makes the RELATIVE box describe the glyphs the
            // draw actually produces. The draw clamps the cursor at buffer 0 — right
            // there, wrong in a relative walk that starts at 0, where the clamp
            // swallows the lift entirely and `\f` measures as a no-op. That
            // disagreement was real: 73 legends across the 160 layouts open with one
            // to six of these nudges (`é è ç à` on AZERTY are `\f\f <letter>`), so
            // the measured box sat up to 12 px below the ink and the panel clamp in
            // render_key() could not see the overrun. Measured, the last 92 px of
            // off-panel ink across every layout was exactly this. The saturating form
            // stays for the ABSOLUTE walk, where the origin is real and the clamp is
            // the draw's own rule.
            case U'\f':                       y = (saturate && y <= 1) ? 0 : y - 2; break; // up 2px
            case U'\v':                       y += ((y - oy) / 15 + 1) * 15; break;
            case U'\x18':                     x = ox; y = oy; break; // cancel -> origin
            case U'\r':                       x = ox; break;
            case U'\b':                       x = (saturate && x <= 1) ? 0 : x - 2; break;
            case U'\x06':                     x += 2; break; // nudge right 2px
            case U'\t':                       x += ((x - ox) / 36 + 1) * 36; break;
            case U'\n':                       y += base_yadv; x = ox; break;
            // ---- the hint display-list ops, mirrored so their ARGUMENT codepoints are
            //      not measured as glyphs. Without this each op byte and each argument
            //      falls into `default:`, matches no font and is substituted with '!',
            //      so a legend carrying one MOVE measured three bogus glyphs.
            case U'\x10':                    small = true; break;   // SMALL: rest of the run is half-scale
            case U'\x16':                                            // MID: rest of the run is the 19px UI face
                mid = true;                                          //   (per glyph — see the fallback below)
                break;
            case U'\x0F':                                            // HALF / THIN composite a glyph at the
            case U'\x11':                                            //   cursor and do not advance
                if (text[1]) {
                    if (resolve) {
                        // Both plot the literal TOP-LEFT at the cursor, at ceil(w/2)
                        // x ceil(h/2) — no baseline align, no xOffset, no advance.
                        const GFXglyph *g = kdisp_gfx_glyph(fonts, num_fonts, text[1]);
                        if (g != NULL) {
                            const int16_t gw = (int16_t)((pgm_read_byte(&g->width)  + 1) / 2);
                            const int16_t gh = (int16_t)((pgm_read_byte(&g->height) + 1) / 2);
                            if (gw > 0 && gh > 0) {
                                if (x < xmn) xmn = x;
                                if (x + gw - 1 > xmx) xmx = (int16_t)(x + gw - 1);
                                if (y < ymn) ymn = y;
                                if (y + gh - 1 > ymx) ymx = (int16_t)(y + gh - 1);
                            }
                        }
                    }
                    text += 1;
                }
                break;
            case U'\x15':                                            // ROT (angle, glyph)
                if (text[1] && text[2]) {
                    if (resolve) {
                        const GFXglyph *g = kdisp_gfx_glyph(fonts, num_fonts, text[2]);
                        if (g != NULL) {
                            kdisp_rot_half_t rot;
                            kdisp_gfx_rot_half_extent((int16_t)pgm_read_byte(&g->width),
                                                      (int16_t)pgm_read_byte(&g->height),
                                                      (uint8_t)text[1], &rot);
                            const int16_t gw = rot.w, gh = rot.h;
                            if (gw > 0 && gh > 0) {
                                if (x < xmn) xmn = x;
                                if (x + gw - 1 > xmx) xmx = (int16_t)(x + gw - 1);
                                if (y < ymn) ymn = y;
                                if (y + gh - 1 > ymx) ymx = (int16_t)(y + gh - 1);
                            }
                        }
                    }
                    text += 2;
                }
                break;
            case U'\x0E':   // MOVE (x,y): the cursor lands on an ABSOLUTE buffer position.
                             //   Resolvable only when the caller named the origin; otherwise
                             //   the move itself is ignored and a MOVE'd legend's box covers
                             //   only the part laid out relatively. Its two ARGUMENTS are
                             //   skipped either way.
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
                if (text[1] && text[2]) {
                    if (resolve) { x = (int16_t)(int8_t)text[1]; y = (int16_t)(int8_t)text[2]; }
                    text += 2;
                }
                break;
            case U'\x14':                    /* ERASE: a mode, no extent */ break;
            case U'\x13':                                                       // BADGE (w,h,style)
                if (text[1] && text[2] && text[3]) {
                    if (resolve) {
                        const int16_t bw = (int16_t)(int8_t)text[1], bh = (int16_t)(int8_t)text[2];
                        if (bw > 0 && bh > 0) {
                            if (x < xmn) xmn = x;
                            if (x + bw - 1 > xmx) xmx = (int16_t)(x + bw - 1);
                            if (y < ymn) ymn = y;
                            if (y + bh - 1 > ymx) ymx = (int16_t)(y + bh - 1);
                        }
                    }
                    text += 3;
                }
                break;
            case U'\x12':                                                       // FRAME (w,h)
                if (text[1] && text[2]) {
                    if (resolve) {
                        // The rounded rect inks inside its own w x h box (and draws
                        // nothing at all below 2x2 — measuring the box regardless is
                        // the generous direction, which for a clamp is the safe one).
                        const int16_t fw = (int16_t)(int8_t)text[1], fh = (int16_t)(int8_t)text[2];
                        if (fw > 0 && fh > 0) {
                            if (x < xmn) xmn = x;
                            if (x + fw - 1 > xmx) xmx = (int16_t)(x + fw - 1);
                            if (y < ymn) ymn = y;
                            if (y + fh - 1 > ymx) ymx = (int16_t)(y + fh - 1);
                        }
                    }
                    text += 2;
                }
                break;
            default: {
                // Mirror the draw's per-glyph fallback: MID applies only to codepoints
                // the mid face actually has.
                const bool            use_mid = mid && kdisp_gfx_glyph(mid_font, mid_count, *text) != NULL;
                const GFXfont *const *pool = use_mid ? mid_font : fonts;
                const uint8_t         cnt  = use_mid ? mid_count : num_fonts;
                // ⚠️ Resolve through kdisp_gfx_glyph_font, the SAME lookup every draw
                // path uses, rather than a hand-rolled range scan. The scan that used to
                // be here did not skip a 0x0 GAP record (the generated headers' padding
                // for a non-contiguous range), so a codepoint inside a padded span —
                // Pashto letters under _PerArab_'s wider range is the documented case —
                // measured the empty gap while the draw resolved a real glyph from the
                // next font. Measuring a glyph the draw will not produce mis-places the
                // legend, because plan_main_legend() positions from this box.
                const GFXfont  *f = NULL;
                const GFXglyph *g = kdisp_gfx_glyph_font(pool, cnt, *text, &f);
                if (g == NULL) {
                    // Nothing covers it. The full-size writer substitutes '!' from
                    // fonts[0] and advances, so the box must too — but
                    // kdisp_write_gfx_char_half returns 0: it draws NOTHING and does not
                    // advance. Substituting here in a SMALL run therefore invented both
                    // ink and an advance the draw never spends, shifting every following
                    // glyph. Skip the codepoint instead, exactly as the half writer does.
                    if (small) break;
                    f = pool[0];
                    g = pgm_read_glyph_ptr(f, U'!' - pgm_read_dword(&f->first));
                }
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
    // Empty / whitespace-only input degenerates to a point AT THE ORIGIN — which in
    // the relative form (ox = oy = 0) is the all-zero box it has always reported.
    if (xmx < xmn) { xmn = xmx = ox; ymn = ymx = oy; }
    *out_xmin = (int8_t)xmn;
    *out_xmax = (int8_t)xmx;
    *out_ymin = (int8_t)ymn;
    *out_ymax = (int8_t)ymx;
}

void kdisp_gfx_text_bbox_in(const GFXfont *const *fonts, uint8_t num_fonts,
                            const GFXfont *const *mid_font, uint8_t mid_count, const uint32_t *text,
                            int8_t *out_xmin, int8_t *out_xmax, int8_t *out_ymin, int8_t *out_ymax) {
    bbox_walk(fonts, num_fonts, mid_font, mid_count, text, 0, 0, false, false,
              out_xmin, out_xmax, out_ymin, out_ymax);
}

void kdisp_gfx_text_bbox_abs_in(const GFXfont *const *fonts, uint8_t num_fonts,
                                const GFXfont *const *mid_font, uint8_t mid_count, const uint32_t *text,
                                int8_t origin_x, int8_t origin_y,
                                int8_t *out_xmin, int8_t *out_xmax, int8_t *out_ymin, int8_t *out_ymax) {
    bbox_walk(fonts, num_fonts, mid_font, mid_count, text, origin_x, origin_y, true, true,
              out_xmin, out_xmax, out_ymin, out_ymax);
}
