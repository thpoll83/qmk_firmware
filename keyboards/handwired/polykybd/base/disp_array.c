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

// Draw a character
/**************************************************************************/
/*!
   @brief   Draw a single character
    @param    x   Bottom left corner x coordinate
    @param    y   Bottom left corner y coordinate
    @param    ch  The 32-bit Unicode codepoint (SMP codepoints > 0xFFFF allowed)
*/
/**************************************************************************/
int8_t kdisp_write_gfx_char(const GFXfont *const *fonts, uint8_t num_fonts, int8_t x, int8_t y, uint32_t ch, bool clear_cy) {
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
            if (ch >= first && ch <= last) { found = idx; break; }
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

    if(clear_cy) {
        kdisp_clear_bitmap_courtyard(x+xo, y+yo, &bitmap[bo], w, h);
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
    kdisp_write_gfx_text_cy(fonts, num_fonts, x, y, text, false);
}

void kdisp_write_gfx_text_cy(const GFXfont *const *fonts, uint8_t num_fonts, int8_t x, int8_t y, const uint32_t *text, bool clear_cy) {
    int8_t x_cursor = x;
    int8_t y_cursor = y;
    while (*text != 0) {
        switch(*text) {
            case U'\x05'://enquiry
                y_cursor += 2;
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
            default:
                x_cursor += kdisp_write_gfx_char(fonts, num_fonts, x_cursor, y_cursor, *text, clear_cy);
                break;
        }
        text++;
    }
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

    const int8_t top_y = (int8_t)((SCREEN_HEIGHT - total) / 2);
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

void kdisp_clear_bitmap_courtyard(int8_t x, int8_t y, const uint8_t pgm_bmp[], int8_t bmp_width, int8_t bmp_height) {
    uint16_t offset = 0;
    int8_t first = 127;
    int8_t last=0;
    int8_t num_empty = 0;
    int8_t bits = 0, bit = 0;
    for (int8_t bmp_y = 0; bmp_y < bmp_height; ++bmp_y) {
        num_empty++;
        for (int8_t bmp_x = 0; bmp_x < bmp_width; ++bmp_x) {
            if (!(bit++ & 7)) {
                bits = pgm_read_byte(&pgm_bmp[offset++]);
            }
            if (bits & 0x80) {
                first = PK_MIN(bmp_x-3, first);
                last = PK_MAX(bmp_x+3, last);
                num_empty = 0;
            }
            bits <<= 1;

        }
        if(first!=127) {
            if(num_empty==0) {
                clear_line(x+first+4, x+last-4, bmp_y + y-2);
                clear_line(x+first+2, x+last-2, bmp_y + y-1);
            } else {
                num_empty = PK_MIN(num_empty, 6);
            }
            clear_line(x+first, x+last, bmp_y + y);
            uint8_t dist;
            PK_POW(dist, 2, (num_empty+1));
            first+=dist;
            last -=dist;
            if(first>=last) {
                first = 127;
                last=0;
                num_empty = 0;
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
