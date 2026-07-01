// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#include "disp_array.h"
#include <string.h>

//#include "polykybd.h"
#include "helpers.h"
#include "spi_helper.h"
#include "shift_reg.h"
#include "spi_master.h"

#include "fonts/base_font.h"
#include "com.h"

#define SSD1306_MEMORYMODE 0x20           ///< See datasheet
#define SSD1306_COLUMNADDR 0x21           ///< See datasheet
#define SSD1306_PAGEADDR 0x22             ///< See datasheet
#define SSD1306_SETCONTRAST 0x81          ///< See datasheet
#define SSD1306_CHARGEPUMP 0x8D           ///< See datasheet
#define SSD1306_SEGREMAP 0xA0             ///< See datasheet
#define SSD1306_DISPLAYALLON_RESUME 0xA4  ///< See datasheet
#define SSD1306_DISPLAYALLON 0xA5         ///< Not currently used
#define SSD1306_NORMALDISPLAY 0xA6        ///< See datasheet
#define SSD1306_INVERTDISPLAY 0xA7        ///< See datasheet
#define SSD1306_SETMULTIPLEX 0xA8         ///< See datasheet
#define SSD1306_DISPLAYOFF 0xAE           ///< See datasheet
#define SSD1306_DISPLAYON 0xAF            ///< See datasheet
#define SSD1306_COMSCANINC 0xC0           ///< Not currently used
#define SSD1306_COMSCANDEC 0xC8           ///< See datasheet
#define SSD1306_SETDISPLAYOFFSET 0xD3     ///< See datasheet
#define SSD1306_SETDISPLAYCLOCKDIV 0xD5   ///< See datasheet
#define SSD1306_SETPRECHARGE 0xD9         ///< See datasheet
#define SSD1306_SETCOMPINS 0xDA           ///< See datasheet
#define SSD1306_SETVCOMDETECT 0xDB        ///< See datasheet

#define SSD1306_SETLOWCOLUMN 0x00   ///< Not currently used
#define SSD1306_SETHIGHCOLUMN 0x10  ///< Not currently used
#define SSD1306_SETSTARTLINE 0x40   ///< See datasheet

#define SSD1306_RIGHT_HORIZONTAL_SCROLL 0x26              ///< Init rt scroll
#define SSD1306_LEFT_HORIZONTAL_SCROLL 0x27               ///< Init left scroll
#define SSD1306_VERTICAL_AND_RIGHT_HORIZONTAL_SCROLL 0x29 ///< Init diag scroll
#define SSD1306_VERTICAL_AND_LEFT_HORIZONTAL_SCROLL 0x2A  ///< Init diag scroll
#define SSD1306_DEACTIVATE_SCROLL 0x2E                    ///< Stop scroll
#define SSD1306_ACTIVATE_SCROLL 0x2F                      ///< Start scroll
#define SSD1306_SET_VERTICAL_SCROLL_AREA 0xA3             ///< Set scroll range

// display specific constants
#define BUFFER_BYTE_VIS_HEIGHT 5
#define BUFFER_BYTE_HEIGHT 8
#define BUFFER_BYTE_VIS_WIDTH 72
#define BUFFER_BYTE_WIDTH 128
#define BUFFER_PIXEL_HEIGHT 40
#define BUFFER_PIXEL_WIDTH 128
//#define VISIBLE_PIXEL_X_FIRST ((BUFFER_BYTE_WIDTH - SCREEN_WIDTH) >> 1)
//#define VISIBLE_PIXEL_X_LAST_PLUS_ONE (VISIBLE_PIXEL_X_FIRST + SCREEN_WIDTH)

#define SPI_MODE 3


#define GET_BUFFER_OFFSET(x, y) (((y) >> 3) * BUFFER_BYTE_WIDTH + (x))
#define WITHIN_BUFFER(x, y) ((x)>=0 && (y)>=0 && GET_BUFFER_OFFSET(x, y) < BUFFER_BYTE_WIDTH * BUFFER_BYTE_HEIGHT)
#define SET_PIXEL(x, y) scratch_buffer[GET_BUFFER_OFFSET(x, y)] |= (1 << ((y)&0x7))
#define CLEAR_PIXEL(x, y) scratch_buffer[GET_BUFFER_OFFSET(x, y)] &= ~(1 << ((y)&0x7))
#define SET_PIXEL_CLIPPED(x, y) if(WITHIN_BUFFER(x, y)) { SET_PIXEL(x, y); }
#define CLEAR_PIXEL_CLIPPED(x, y) if(WITHIN_BUFFER(x, y)) { CLEAR_PIXEL(x, y); }
#define COPY_TO_BUFFER_XY(unint16X, uint16Y, srcBuffer, numBytes) memcpy_P(&scratch_buffer[GET_BUFFER_OFFSET((unint16X), (uint16Y))], (srcBuffer), (numBytes))


uint8_t scratch_buffer[BUFFER_BYTE_WIDTH * BUFFER_BYTE_HEIGHT];

// Global pixel offset added to every gfx-char/text draw. Normally 0; the idle
// "jitter" anti-burn-in path sets it for the duration of a relocation redraw and
// restores it to 0 afterwards (see update_displays in poly_keymap.c).
static int8_t s_draw_ox = 0;
static int8_t s_draw_oy = 0;

void kdisp_set_draw_offset(int8_t ox, int8_t oy) {
    s_draw_ox = ox;
    s_draw_oy = oy;
}

uint8_t* get_scratch_buffer(void) {
    return scratch_buffer;
}

int16_t get_scratch_buffer_size(void) {
    return BUFFER_BYTE_WIDTH * BUFFER_BYTE_HEIGHT;
}

inline GFXglyph *pgm_read_glyph_ptr(const GFXfont *font, uint32_t c) {
#ifdef __AVR__
    return &(((GFXglyph *)pgm_read_pointer(&font->glyph))[c]);
#else
    // expression in __AVR__ section may generate "dereferencing type-punned
    // pointer will break strict-aliasing rules" warning In fact, on other
    // platforms (such as STM32) there is no need to do this pointer magic as
    // program memory may be read in a usual way So expression may be simplified
    return font->glyph + c;
#endif  //__AVR__
}

inline uint8_t *pgm_read_bitmap_ptr(const GFXfont *font) {
#ifdef __AVR__
    return (uint8_t *)pgm_read_pointer(&font->bitmap);
#else
    // expression in __AVR__ section generates "dereferencing type-punned pointer
    // will break strict-aliasing rules" warning In fact, on other platforms (such
    // as STM32) there is no need to do this pointer magic as program memory may
    // be read in a usual way So expression may be simplified
    return font->bitmap;
#endif  //__AVR__
}

// Locate the glyph for codepoint `ch` in a font set, scanning front-to-back and
// skipping empty (0x0) gap glyphs, exactly like the renderer's own lookup.
// Returns NULL when no font actually covers `ch` (no '!' fallback) — callers use
// this both to test coverage and to read glyph metrics. Used by the language
// layer to draw a country flag only when the font pack supplying it is present.
// As kdisp_gfx_glyph(), but also reports the font that owns the glyph (when
// out_font != NULL). The language layer uses this to redraw the flag through a
// single-font array { flag_font }: kdisp_write_gfx_char baseline-aligns every
// glyph to fonts[0]->yAdvance, so drawing a pack font via the full g_all_fonts
// (fonts[0] == IconsFont) shifts it by their yAdvance difference — passing the
// owning font alone makes that adjustment zero.
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

const GFXglyph *kdisp_gfx_glyph(const GFXfont *const *fonts, uint8_t num_fonts, uint32_t ch) {
    return kdisp_gfx_glyph_font(fonts, num_fonts, ch, NULL);
}

// Blit a glyph at half resolution (2x2-OR downsample) with its top-left at buffer
// coords (x,y). Each destination pixel is lit if ANY of the four source pixels in
// its 2x2 block is set — this keeps thin strokes (e.g. the reload arrows) visible
// where plain decimation would drop them. No baseline align: (x,y) is the literal
// top-left, so the caller places it precisely (used for the Win+Ctrl+Shift+B
// reload glyph composited into the monitor's screen).
void kdisp_draw_glyph_half_at(const GFXfont *const *fonts, uint8_t num_fonts, int8_t x, int8_t y, uint32_t ch) {
    const GFXfont *font = NULL;
    const GFXglyph *glyph = kdisp_gfx_glyph_font(fonts, num_fonts, ch, &font);
    if (glyph == NULL || font == NULL) return;
    const uint8_t *bitmap = pgm_read_bitmap_ptr(font);
    uint16_t bo = pgm_read_word(&glyph->bitmapOffset);
    int16_t w = pgm_read_byte(&glyph->width);
    int16_t h = pgm_read_byte(&glyph->height);
    // Round up so an odd source width/height keeps its trailing column/row (the
    // 2x2 block at the edge is just partially populated); the sx/sy bounds check
    // below guards the out-of-range half of that block.
    int16_t hw = (w + 1) / 2, hh = (h + 1) / 2;
    for (int16_t dy = 0; dy < hh; ++dy) {
        for (int16_t dx = 0; dx < hw; ++dx) {
            bool lit = false;
            for (int16_t oy = 0; oy < 2 && !lit; ++oy) {
                for (int16_t ox = 0; ox < 2; ++ox) {
                    int16_t sx = dx * 2 + ox, sy = dy * 2 + oy;
                    if (sx >= w || sy >= h) continue;
                    uint32_t bit = (uint32_t)sy * w + sx;
                    uint8_t byte = pgm_read_byte(&bitmap[bo + (bit >> 3)]);
                    if (byte & (0x80 >> (bit & 7))) { lit = true; break; }
                }
            }
            if (lit) { SET_PIXEL_CLIPPED(x + dx, y + dy); }
        }
    }
}


void kdisp_fill_rect(int8_t x_start, int8_t y_start, int8_t width, int8_t height) {
    for (int x = x_start; x < (x_start + width); ++x) {
        for (int y = y_start; y < (y_start + height); ++y) {
            SET_PIXEL_CLIPPED(x, y);
        }
    }
}

void kdisp_clear_rect(int8_t x_start, int8_t y_start, int8_t width, int8_t height) {
    for (int x = x_start; x < (x_start + width); ++x) {
        for (int y = y_start; y < (y_start + height); ++y) {
            CLEAR_PIXEL_CLIPPED(x, y);
        }
    }
}

// Draw a 1px rounded-rectangle outline: four straight edges + four quarter-circle
// corners (the Adafruit-GFX midpoint-circle helper). Coordinates are buffer
// coordinates; pixels are clipped. Used for the Win+R run-dialog hint frame
// (drawn twice, nested, for a 2px border — see keycode_to_disp_overlay()).
void kdisp_draw_round_rect(int8_t x, int8_t y, int8_t width, int8_t height, int8_t r) {
    if (width < 2 || height < 2) return;
    int x0 = x, y0 = y, x1 = x + width - 1, y1 = y + height - 1;
    if (r < 0) r = 0;
    if (r > (width - 1) / 2)  r = (width - 1) / 2;
    if (r > (height - 1) / 2) r = (height - 1) / 2;
    // straight edges
    for (int i = x0 + r; i <= x1 - r; ++i) { SET_PIXEL_CLIPPED(i, y0); SET_PIXEL_CLIPPED(i, y1); }
    for (int j = y0 + r; j <= y1 - r; ++j) { SET_PIXEL_CLIPPED(x0, j); SET_PIXEL_CLIPPED(x1, j); }
    // corner arcs (centres at the four inset corner points)
    int cxl = x0 + r, cxr = x1 - r, cyt = y0 + r, cyb = y1 - r;
    int f = 1 - r, ddF_x = 1, ddF_y = -2 * r, px = 0, py = r;
    while (px < py) {
        if (f >= 0) { py--; ddF_y += 2; f += ddF_y; }
        px++; ddF_x += 2; f += ddF_x;
        SET_PIXEL_CLIPPED(cxr + px, cyt - py); SET_PIXEL_CLIPPED(cxr + py, cyt - px); // top-right
        SET_PIXEL_CLIPPED(cxl - px, cyt - py); SET_PIXEL_CLIPPED(cxl - py, cyt - px); // top-left
        SET_PIXEL_CLIPPED(cxr + px, cyb + py); SET_PIXEL_CLIPPED(cxr + py, cyb + px); // bottom-right
        SET_PIXEL_CLIPPED(cxl - px, cyb + py); SET_PIXEL_CLIPPED(cxl - py, cyb + px); // bottom-left
    }
}

// Draw a single character at bottom-left (x,y); ch is a 32-bit Unicode codepoint
// (SMP codepoints > 0xFFFF allowed).
int8_t kdisp_write_gfx_char(const GFXfont *const *fonts, uint8_t num_fonts, int8_t x, int8_t y, uint32_t ch, int8_t cy_radius) {
    x += s_draw_ox;   // anti-burn-in jitter offset (0 except during an idle relocation redraw)
    y += s_draw_oy;
    const GFXfont * currentFont = 0;
    uint32_t first = 0;
    uint32_t last = 0;

    // Font selection: pick the first font in `fonts` whose [first,last] contains
    // `ch`. Array order is precedence — ranges may overlap deliberately (a narrow
    // gap-filler font placed ahead of a wide sparse font). A small MRU cache of
    // recently-hit font indices skips the O(N) scan on the common case (runs of
    // same-font glyphs: a 49-key emoji page, a Latin label).
    //
    // Correctness: only *disjoint* fonts (range overlaps no other font) are ever
    // cached. A hit on such a font is unambiguous — it is the unique container of
    // `ch`, hence necessarily the scan winner. Fonts that participate in an overlap
    // are never cached and always fall through to the priority-ordered scan, so
    // overlap precedence is preserved. Disjointness is computed once per font set.
    enum { FONT_MRU_N = 4 };
    static const GFXfont *const *s_set = 0;     // font set the cache was built for
    static uint8_t s_set_n   = 0;
    static uint8_t s_disjoint[16];              // bitmap: 1 = font[i] is cacheable (≤128 fonts)
    static uint8_t s_mru[FONT_MRU_N];           // MRU list of cacheable font indices
    static uint8_t s_mru_len = 0;

    if (fonts != s_set || num_fonts != s_set_n) {
        s_set = fonts; s_set_n = num_fonts; s_mru_len = 0;
        memset(s_disjoint, 0, sizeof(s_disjoint));
        if (num_fonts <= 128) {
            for (uint8_t i = 0; i < num_fonts; ++i) {
                uint32_t fi = pgm_read_dword(&fonts[i]->first);
                uint32_t li = pgm_read_dword(&fonts[i]->last);
                bool overlaps = false;
                for (uint8_t j = 0; j < num_fonts; ++j) {
                    if (j == i) continue;
                    uint32_t fj = pgm_read_dword(&fonts[j]->first);
                    uint32_t lj = pgm_read_dword(&fonts[j]->last);
                    if (fi <= lj && fj <= li) { overlaps = true; break; }
                }
                if (!overlaps) s_disjoint[i >> 3] |= (uint8_t)(1u << (i & 7));
            }
        }
    }

    bool hit = false;
    for (uint8_t k = 0; k < s_mru_len; ++k) {
        uint8_t idx = s_mru[k];
        currentFont = fonts[idx];
        first = pgm_read_dword(&currentFont->first);
        last  = pgm_read_dword(&currentFont->last);
        if (ch >= first && ch <= last) {
            for (uint8_t m = k; m > 0; --m) s_mru[m] = s_mru[m - 1];
            s_mru[0] = idx;                     // move to front
            hit = true;
            break;
        }
    }

    if (!hit) {
        uint8_t found = 0xFF;
        for (uint8_t idx = 0; idx < num_fonts; ++idx) {
            currentFont = fonts[idx];
            first = pgm_read_dword(&currentFont->first);
            last  = pgm_read_dword(&currentFont->last);
            if (ch >= first && ch <= last) {
                // A font with non-contiguous ranges is padded with empty 0x0 gap
                // glyphs from first..last; skip such a gap so a later font that
                // actually has this codepoint wins (e.g. Pashto letters shadowed by
                // _PerArab_'s wider span).  A real space is (w,h)=(1,1), advance>0,
                // so it is never skipped.
                const GFXglyph *gg = pgm_read_glyph_ptr(currentFont, ch - first);
                if (pgm_read_byte(&gg->width) == 0 && pgm_read_byte(&gg->height) == 0
                    && pgm_read_byte(&gg->xAdvance) == 0) continue;
                found = idx; break;
            }
        }
        if (found == 0xFF) {
            currentFont = fonts[0];             // no match — fall back to '!'
            first = pgm_read_dword(&currentFont->first);
            last  = pgm_read_dword(&currentFont->last);
            ch = U'!';
        } else if (found < 128 && (s_disjoint[found >> 3] & (uint8_t)(1u << (found & 7)))) {
            uint8_t m = (s_mru_len < FONT_MRU_N) ? s_mru_len : (FONT_MRU_N - 1);
            for (; m > 0; --m) s_mru[m] = s_mru[m - 1];
            s_mru[0] = found;                   // cache cacheable winner
            if (s_mru_len < FONT_MRU_N) ++s_mru_len;
        }
    }
    ch -= first;
    const GFXglyph *glyph  = pgm_read_glyph_ptr(currentFont, ch);
    const uint8_t  *bitmap = pgm_read_bitmap_ptr(currentFont);

    //adjust to the first font y-offset if the two fonts have different heights
    y += pgm_read_byte(&currentFont->yAdvance) - pgm_read_byte(&fonts[0]->yAdvance);

    uint16_t bo = pgm_read_word(&glyph->bitmapOffset);
    int8_t  w = pgm_read_byte(&glyph->width), h = pgm_read_byte(&glyph->height);
    int8_t   xo = pgm_read_byte(&glyph->xOffset), yo = pgm_read_byte(&glyph->yOffset);
    int8_t  xx, yy, bits = 0, bit = 0;

    if(cy_radius > 0) {
        kdisp_clear_bitmap_courtyard(x+xo, y+yo, &bitmap[bo], w, h, cy_radius);
    }

    for (yy = 0; yy < h; yy++) {
        for (xx = 0; xx < w; xx++) {
            if (!(bit++ & 7)) {
                bits = pgm_read_byte(&bitmap[bo++]);
            }
            if (bits & 0x80) {
                SET_PIXEL_CLIPPED(x + xo + xx, y + yo + yy);
            }
            bits <<= 1;
        }
    }

    return pgm_read_byte(&glyph->xAdvance);
}

void kdisp_write_gfx_text(const GFXfont *const *fonts, uint8_t num_fonts, int8_t x, int8_t y, const uint32_t *text) {
    kdisp_write_gfx_text_cy(fonts, num_fonts, x, y, text, 0);
}

void kdisp_write_gfx_text_cy(const GFXfont *const *fonts, uint8_t num_fonts, int8_t x, int8_t y, const uint32_t *text, int8_t cy_radius) {
    int8_t x_cursor = x;
    int8_t y_cursor = y;
    while (*text != 0) {
        switch(*text) {
            case U'\x05'://enquiry
                y_cursor += 2;
                break;
            case U'\x06'://acknowledge - nudge right 2px (symmetric with \b left, \x05 down)
                x_cursor += 2;
                break;
            case U'\x18'://cancel
                x_cursor = x;
                y_cursor = y;
                break;
            case U'\b':
                x_cursor = x_cursor>1 ? x_cursor - 2 : 0;
                break;
            case U'\f':
                y_cursor = y_cursor>1 ? y_cursor - 2 : 0;
                break;
            case U'\t':
                x_cursor += ((x_cursor-x)/36+1)*36;
                break;
            case U'\n':
                y_cursor += pgm_read_byte(&fonts[0]->yAdvance);
                x_cursor = x;
                break;
            case U'\v':
                y_cursor += ((y_cursor-y)/15+1)*15;
                break;
            case U'\r':
                x_cursor = x;
                break;
            // ---- hint display-list ops: let a hint string composite extra art at a
            //      chosen buffer position, so update_displays() needs no per-keycode
            //      special-case. \x0E/\x12 take the next two codepoints as arguments. ----
            case U'\x0E':   // MOVE cursor to buffer coords (next two codepoints = x, y)
                if (text[1] && text[2]) { x_cursor = (int8_t)text[1]; y_cursor = (int8_t)text[2]; text += 2; }
                break;
            case U'\x0F':   // HALF: draw the next codepoint half-scale (2x2-OR) at the cursor, no advance
                if (text[1]) { kdisp_draw_glyph_half_at(fonts, num_fonts, x_cursor, y_cursor, text[1]); text++; }
                break;
            case U'\x10':   // PLUS: 11px '+' centred on the cursor (e.g. magnifier zoom-in)
                kdisp_fill_rect(x_cursor - 5, y_cursor, 11, 2);
                kdisp_fill_rect(x_cursor, y_cursor - 5, 2, 11);
                break;
            case U'\x11':   // MINUS: 11px '-' centred on the cursor (e.g. magnifier zoom-out)
                kdisp_fill_rect(x_cursor - 5, y_cursor, 11, 2);
                break;
            case U'\x12':   // FRAME: 2px nested rounded rect at the cursor (next two codepoints = w, h)
                if (text[1] && text[2]) {
                    int8_t fw = (int8_t)text[1], fh = (int8_t)text[2];
                    kdisp_draw_round_rect(x_cursor, y_cursor, fw, fh, 4);
                    kdisp_draw_round_rect(x_cursor + 1, y_cursor + 1, (int8_t)(fw - 2), (int8_t)(fh - 2), 3);
                    text += 2;
                }
                break;
            default:
                x_cursor += kdisp_write_gfx_char(fonts, num_fonts, x_cursor, y_cursor, *text, cy_radius);
                break;
        }
        text++;
    }
}

void kdisp_gfx_text_bbox(const GFXfont *const *fonts, uint8_t num_fonts, const uint32_t *text,
                         int8_t *out_xmin, int8_t *out_xmax, int8_t *out_ymin, int8_t *out_ymax) {
    // Cursor relative to the draw origin: x from 0, y from the baseline (0). Mirrors
    // every cursor rule of kdisp_write_gfx_text_cy, including the vertical controls,
    // so the measured box matches what would actually be drawn — both axes.
    int16_t x = 0, y = 0;
    int16_t xmn = 127, xmx = -128, ymn = 127, ymx = -128;
    const int16_t base_yadv = pgm_read_byte(&fonts[0]->yAdvance);
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
            default: {
                // Locate the font whose [first,last] contains the codepoint (linear
                // scan; this is a cold measuring path, no MRU cache needed).
                uint32_t ch = *text, first = 0, last = 0;
                const GFXfont *f = 0;
                for (uint8_t i = 0; i < num_fonts; ++i) {
                    first = pgm_read_dword(&fonts[i]->first);
                    last  = pgm_read_dword(&fonts[i]->last);
                    if (ch >= first && ch <= last) { f = fonts[i]; break; }
                }
                if (!f) { f = fonts[0]; first = pgm_read_dword(&f->first); ch = U'!'; }
                const GFXglyph *g = pgm_read_glyph_ptr(f, ch - first);
                int8_t w  = pgm_read_byte(&g->width);
                int8_t h  = pgm_read_byte(&g->height);
                int8_t xo = pgm_read_byte(&g->xOffset);
                int8_t yo = pgm_read_byte(&g->yOffset);
                if (w > 0 && h > 0) {
                    int16_t l = x + xo, r = x + xo + w - 1;
                    if (l < xmn) xmn = l;
                    if (r > xmx) xmx = r;
                    // kdisp_write_gfx_char shifts each glyph by (font yAdvance - fonts[0]
                    // yAdvance) before applying yOffset; mirror it so the vertical box
                    // matches the rasterised pixels.
                    int16_t yadj = (int16_t)pgm_read_byte(&f->yAdvance) - base_yadv;
                    int16_t t = y + yadj + yo, b = t + h - 1;
                    if (t < ymn) ymn = t;
                    if (b > ymx) ymx = b;
                }
                x += pgm_read_byte(&g->xAdvance);
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

void kdisp_gfx_text_bounds(const GFXfont *const *fonts, uint8_t num_fonts, const uint32_t *text, int8_t *out_min, int8_t *out_max) {
    int8_t ymin, ymax;
    kdisp_gfx_text_bbox(fonts, num_fonts, text, out_min, out_max, &ymin, &ymax);
}

// Draw `text` as a vertical column, each glyph rotated -90° (counter-clockwise),
// reading bottom-to-top, with the glyph baseline along x = col_x and the column
// vertically centred in the visible height.  When `selected`, a solid bar is
// filled behind the text and the glyphs are punched out of it (dark on white);
// otherwise the glyphs are drawn lit (white on dark).  Single font, no fallback.
void kdisp_write_gfx_vtext(const GFXfont *font, int8_t col_x, const uint32_t *text, bool selected) {
    const uint32_t first  = pgm_read_dword(&font->first);
    const uint32_t last   = pgm_read_dword(&font->last);
    const uint8_t *bitmap = pgm_read_bitmap_ptr(font);

    // First pass: total advance (column length) and the left (baseline) extent.
    int16_t total = 0;
    int8_t  min_x = 127;
    for (const uint32_t *p = text; *p; ++p) {
        if (*p < first || *p > last) continue;
        const GFXglyph *g = pgm_read_glyph_ptr(font, *p - first);
        int8_t yo = pgm_read_byte(&g->yOffset);
        if (col_x + yo < min_x) min_x = (int8_t)(col_x + yo);
        total += pgm_read_byte(&g->xAdvance);
    }
    if (total <= 0) return;

    int8_t top_y = (int8_t)((SCREEN_HEIGHT - total) / 2);
    // A column longer than the screen (e.g. the "mn-MN" label) centres to a
    // bottom row below the panel — pull it up so the first glyph stays visible.
    if (top_y + total > SCREEN_HEIGHT - 1) top_y = (int8_t)(SCREEN_HEIGHT - 1 - total);
    if (selected) {
        // Full-height bar: from 3 px left of the text to the last visible column
        // (the vertical label sits against the keycap's right edge).
        const int8_t bx = (int8_t)(min_x - 3);
        kdisp_fill_rect(bx, 0, (int8_t)((BUFFER_X + SCREEN_WIDTH - 1) - bx + 1),
                        SCREEN_HEIGHT);
    }

    // Second pass: render each glyph rotated, advancing upward from the bottom.
    int16_t vcur = top_y + total;
    for (const uint32_t *p = text; *p; ++p) {
        if (*p < first || *p > last) continue;
        const GFXglyph *g = pgm_read_glyph_ptr(font, *p - first);
        uint16_t bo = pgm_read_word(&g->bitmapOffset);
        int8_t   w  = pgm_read_byte(&g->width),  h  = pgm_read_byte(&g->height);
        int8_t   xo = pgm_read_byte(&g->xOffset), yo = pgm_read_byte(&g->yOffset);
        uint8_t  bit = 0, bits = 0;
        for (int8_t gy = 0; gy < h; ++gy) {
            for (int8_t gx = 0; gx < w; ++gx) {
                if (!(bit++ & 7)) bits = pgm_read_byte(&bitmap[bo++]);
                if (bits & 0x80) {
                    int8_t sx = (int8_t)(col_x + yo + gy);
                    int8_t sy = (int8_t)(vcur  - xo - gx);
                    if (selected) { CLEAR_PIXEL_CLIPPED(sx, sy); }
                    else          { SET_PIXEL_CLIPPED(sx, sy); }
                }
                bits <<= 1;
            }
        }
        vcur -= pgm_read_byte(&g->xAdvance);
    }
}

void kdisp_write_base_char(int8_t x, int8_t y, const char ch) {
    int8_t font_index = (uint8_t)ch;  // font based on unsigned type for index
    if (font_index < BASIC_FONT_START || font_index > BASIC_FONT_END) {
        memset(&scratch_buffer[GET_BUFFER_OFFSET(x, y)], 0x00, BASIC_FONT_WIDTH);
    } else {
        const uint8_t *glyph = &font[(font_index - BASIC_FONT_START) * BASIC_FONT_WIDTH];
        COPY_TO_BUFFER_XY(x, y, glyph, BASIC_FONT_WIDTH);
    }
}

void kdisp_draw_bitmap(int8_t x, int8_t y, const uint8_t pgm_bmp[], int8_t bmp_width, int8_t bmp_height) {
    int8_t byte_width           = (bmp_width + 7) / 8;
    uint8_t vertical_pixel_row_8 = 0;

    for (int8_t bmp_y = 0; bmp_y < bmp_height; bmp_y++, y++) {
        for (int8_t bmp_x = 0; bmp_x < bmp_width; bmp_x++) {
            if (bmp_x & 0x07) {
                vertical_pixel_row_8 <<= 1;
            } else {
                vertical_pixel_row_8 = pgm_read_byte(&pgm_bmp[bmp_y * byte_width + (bmp_x >> 3)]);
            }
            if (vertical_pixel_row_8 & 0x80) {
                SET_PIXEL_CLIPPED(x + bmp_x, y);
            }
        }
    }
}

void clear_line(int8_t from_x, int8_t to_x, int8_t y) {
    for (int8_t x = from_x; x < to_x; ++x) {
        CLEAR_PIXEL_CLIPPED(x, y);
    }
}

void kdisp_clear_bitmap_courtyard(int8_t x, int8_t y, const uint8_t pgm_bmp[], int8_t bmp_width, int8_t bmp_height, int8_t radius) {
    // Clear a "courtyard" behind the glyph: every buffer pixel within Chebyshev
    // distance `radius` of any glyph-on pixel — i.e. a square-kernel morphological
    // dilation of the glyph mask. This traces the glyph contour with an even margin:
    // unlike a per-row [first,last] span it never bridges horizontally-separated
    // strokes (e.g. the two marks of " or the legs of M), and unlike per-column
    // extents it does not fill interior vertical gaps (= , ü , " keep their holes).
    // Implemented per horizontal run of on-pixels (so a solid row is a few clears,
    // not one-per-pixel): each run [run_start, run_end] is cleared, widened by
    // `radius` on each side, across the rows [bmp_y-radius, bmp_y+radius].
    // clear_line / CLEAR_PIXEL_CLIPPED clip to the buffer, so the margin spilling
    // past the glyph edges (the intended courtyard) is fine. The glyph itself is
    // drawn right after this call, so clearing its own pixels here is harmless.
    // `radius` is the courtyard width (KDISP_CY_DEFAULT for overlays; the lang-layer
    // flags pass a smaller value so their borders stay tight).
    if (radius <= 0) return;
    uint16_t offset = 0;
    int8_t bits = 0, bit = 0;
    for (int8_t bmp_y = 0; bmp_y < bmp_height; ++bmp_y) {
        int8_t run_start = -1;
        for (int8_t bmp_x = 0; bmp_x < bmp_width; ++bmp_x) {
            if (!(bit++ & 7)) {
                bits = pgm_read_byte(&pgm_bmp[offset++]);
            }
            bool on = (bits & 0x80) != 0;
            bits <<= 1;
            if (on) {
                if (run_start < 0) run_start = bmp_x;     // run begins
            } else if (run_start >= 0) {                   // run [run_start, bmp_x-1] ends
                for (int8_t dy = -radius; dy <= radius; ++dy) {
                    clear_line(x + run_start - radius, x + bmp_x + radius, bmp_y + y + dy);
                }
                run_start = -1;
            }
        }
        if (run_start >= 0) {                              // run runs to the row's end
            for (int8_t dy = -radius; dy <= radius; ++dy) {
                clear_line(x + run_start - radius, x + bmp_width + radius, bmp_y + y + dy);
            }
        }
    }
}

void kdisp_set_buffer(uint8_t vertical_pixel_row_of_8_pixels) {
    memset(scratch_buffer, vertical_pixel_row_of_8_pixels, BUFFER_BYTE_WIDTH * BUFFER_BYTE_HEIGHT);
}

void kdisp_send_buffer(void) {
    //spi_start(SPI_SS_PIN, false, SPI_MODE, SPI_DIVISOR);

    spi_prepare_commands();

    static const uint8_t PROGMEM dlist1[] = {SSD1306_PAGEADDR,
                                             0,                       // Page start address
                                             0xFF,                    // Page end (not really, but works here)
                                             SSD1306_COLUMNADDR, 0};  // Column start address
    spi_transmit(dlist1, sizeof(dlist1));
    spi_write(BUFFER_PIXEL_WIDTH - 1);  // Column end address

    spi_prepare_data();

    spi_transmit(scratch_buffer, BUFFER_BYTE_WIDTH * BUFFER_BYTE_HEIGHT);

    //spi_stop();
}

void kdisp_invert(bool invert) {
    //spi_start(SPI_SS_PIN, false, SPI_MODE, SPI_DIVISOR);
    spi_prepare_commands();
    spi_write(invert ? SSD1306_INVERTDISPLAY : SSD1306_NORMALDISPLAY);
    //spi_stop();
}

void kdisp_scroll_vlines(uint8_t lines0to63) {
    spi_prepare_commands();
    spi_write(SSD1306_SET_VERTICAL_SCROLL_AREA);
    spi_write(0); //fixed lines
    spi_write(lines0to63);
}

void kdisp_scroll(bool activate) {
    //spi_start(SPI_SS_PIN, false, SPI_MODE, SPI_DIVISOR);
    spi_prepare_commands();
    spi_write(activate ? SSD1306_ACTIVATE_SCROLL : SSD1306_DEACTIVATE_SCROLL);
    //spi_stop();
}

//also setup the lines to scroll via kdisp_scroll_vlines
void kdisp_scroll_modeh(bool left, uint8_t hspeed0to7) {
    spi_prepare_commands();
    if(left) {
        spi_write(SSD1306_LEFT_HORIZONTAL_SCROLL);
    } else {
        spi_write(SSD1306_RIGHT_HORIZONTAL_SCROLL);
    }
    spi_write(0); //dummy
    spi_write(0); //start page
    switch(hspeed0to7) {
        case 0: spi_write(7); break; //2
        case 1: spi_write(4); break; //3
        case 2: spi_write(5); break; //4
        case 3: spi_write(0); break; //5
        case 4: spi_write(6); break; //25
        case 5: spi_write(1); break; //64
        case 6: spi_write(2); break; //128
        default: spi_write(3); break; //256
    }

    spi_write(0x05); //end page, maybe as param?
    spi_write(0); //dummy
    spi_write(0xff); //dummy
}

//also setup the lines to scroll via kdisp_scroll_vlines
void kdisp_scroll_modehv(bool left, uint8_t hspeed0to7, uint8_t voffset0to63) {
    spi_prepare_commands();
    if(left) {
        spi_write(SSD1306_VERTICAL_AND_LEFT_HORIZONTAL_SCROLL);
    } else {
        spi_write(SSD1306_VERTICAL_AND_RIGHT_HORIZONTAL_SCROLL);
    }
    spi_write(0); //dummy
    spi_write(0); //start page
    switch(hspeed0to7) {
        case 0: spi_write(7); break; //2
        case 1: spi_write(4); break; //3
        case 2: spi_write(5); break; //4
        case 3: spi_write(0); break; //5
        case 4: spi_write(6); break; //25
        case 5: spi_write(1); break; //64
        case 6: spi_write(2); break; //128
        default: spi_write(3); break; //256
    }

    spi_write(0x05); //end page, maybe as param?
    spi_write(voffset0to63&63);
}

void kdisp_set_contrast(uint8_t contrast) {
    //spi_start(SPI_SS_PIN, false, SPI_MODE, SPI_DIVISOR);
    spi_prepare_commands();
    spi_write(SSD1306_SETCONTRAST);
    spi_write(contrast);
    //spi_stop();
}

void kdisp_enable(bool enable) {
    //spi_start(SPI_SS_PIN, false, SPI_MODE, SPI_DIVISOR);
    spi_prepare_commands();
    spi_write(enable ? SSD1306_DISPLAYON : SSD1306_DISPLAYOFF);
    //spi_stop();
}

void kdisp_hw_setup(void) {
    //make sure the pins are output pins and low
    #if defined(KEY_DISPLAYS_VDD_PIN)
        gpio_set_pin_output(KEY_DISPLAYS_VDD_PIN);
        gpio_write_pin_low(KEY_DISPLAYS_VDD_PIN);
    #endif

    #if defined(KEY_DISPLAYS_VBAT_PIN)
        gpio_set_pin_output(KEY_DISPLAYS_VBAT_PIN);
        gpio_write_pin_low(KEY_DISPLAYS_VBAT_PIN);
    #endif

    sr_hw_setup();
}

void kdisp_init(const int8_t num_shift_regs) {
    // first turn on logic power supply
    #if defined(KEY_DISPLAYS_VDD_PIN)
        gpio_set_pin_output(KEY_DISPLAYS_VDD_PIN);
        gpio_write_pin_high(KEY_DISPLAYS_VDD_PIN);
        wait_ms(5);
    #endif

    // and then the power supply for the displays
    #if defined(KEY_DISPLAYS_VBAT_PIN)
        gpio_set_pin_output(KEY_DISPLAYS_VBAT_PIN);
        gpio_write_pin_high(KEY_DISPLAYS_VBAT_PIN);
    #endif

    sr_init();

    //make sure we are talking to all shift registers
    uint8_t all[num_shift_regs];
    for(int8_t i=0;i<num_shift_regs;++i) {
        all[i] = 0;
    }
    sr_shift_out_buffer_latch(all, num_shift_regs);


    spi_init();
    spi_start(SPI_SS_PIN, false, SPI_MODE, SPI_DIVISOR);

    peripherals_reset();
}

void kdisp_setup(bool turn_on) {

    spi_prepare_commands();

    spi_write(SSD1306_DISPLAYOFF);
    static const uint8_t PROGMEM clockDiv[] = {SSD1306_SETDISPLAYCLOCKDIV, 0x80};
    spi_transmit(clockDiv, sizeof(clockDiv));
    static const uint8_t PROGMEM dispOffset[] = {SSD1306_SETDISPLAYOFFSET, 0x00};
    spi_transmit(dispOffset, sizeof(dispOffset));
    spi_write(SSD1306_SETSTARTLINE | 0x0);
    spi_write(SSD1306_DISPLAYALLON_RESUME);
    spi_write(SSD1306_NORMALDISPLAY);
    static const uint8_t PROGMEM chargePump[] = {SSD1306_CHARGEPUMP, 0x95};  // 0x14?
    spi_transmit(chargePump, sizeof(chargePump));
    static const uint8_t PROGMEM memMode[] = {SSD1306_MEMORYMODE, 0x00};
    spi_transmit(memMode, sizeof(memMode));
    spi_write(SSD1306_SEGREMAP | 0x1); //rotate by 180: remove 0x01
    spi_write(SSD1306_COMSCANDEC); //rotate by 180: SSD1306_COMSCANINC
    static const uint8_t PROGMEM contrast[] = {SSD1306_SETCONTRAST, 0x00};
    spi_transmit(contrast, sizeof(contrast));
    spi_write(SSD1306_SETPRECHARGE);
    spi_write(0x22);  // ext vcc
    static const uint8_t PROGMEM vCom[] = {SSD1306_SETVCOMDETECT, 0x20};
    spi_transmit(vCom, sizeof(vCom));
    spi_write(SSD1306_SETMULTIPLEX);
    spi_write(40 - 1);  // height - 1
    spi_write(SSD1306_SETCOMPINS);
    spi_write(0x12);

    static const uint8_t PROGMEM fin[] = {0xad, 0x30};
    spi_transmit(fin, sizeof(fin));

    if(turn_on) {
        spi_write(SSD1306_DISPLAYON);
    }

    //spi_stop();
}
