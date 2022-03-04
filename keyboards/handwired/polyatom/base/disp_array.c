#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include "polyatom.h"
#include "spi_helper.h"
#include "shift_reg.h"
#include "spi_master.h"
#include "disp_array.h"
#include OLED_FONT_H

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

// display specific constants
#define SCREEN_WIDTH 72
#define SCREEN_HEIGHT 40
#define BUFFER_BYTE_HEIGHT 5
#define BUFFER_BYTE_WIDTH 128
#define BUFFER_PIXEL_HEIGHT 40
#define BUFFER_PIXEL_WIDTH 128
#define VISIBLE_PIXEL_X_FIRST ((BUFFER_BYTE_WIDTH - SCREEN_WIDTH) >> 1)
#define VISIBLE_PIXEL_X_LAST_PLUS_ONE (VISIBLE_PIXEL_X_FIRST + SCREEN_WIDTH)

#define SPI_MODE 3

uint8_t scratch_buffer[BUFFER_BYTE_WIDTH * BUFFER_BYTE_HEIGHT];

#define test_bmp_width 82
#define test_bmp_height 64

/*
const uint8_t PROGMEM test_bmp_data[] = {
    0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000001, 0b10000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000011, 0b10000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000111, 0b11000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000111, 0b11000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00001111, 0b11000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00011111, 0b11100000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00011111, 0b11100000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000,
    0b00000000, 0b00000000, 0b00000000, 0b00111111, 0b11100000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00111111, 0b11110000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b01111111, 0b11110000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00011111, 0b11111000, 0b01111111, 0b11110000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00111111, 0b11111110, 0b01111111, 0b11110000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00111111, 0b11111111, 0b01111111, 0b11110000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00011111, 0b11111111, 0b11111011, 0b11100000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00001111,
    0b11111111, 0b11111001, 0b11111111, 0b11000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00001111, 0b11111111, 0b11111001, 0b11111111, 0b11111000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000111, 0b11111111, 0b11110001, 0b11111111, 0b11111111, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000011, 0b11111100, 0b01110011, 0b11111111, 0b11111111, 0b10000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000001, 0b11111110, 0b00111111, 0b11111111, 0b11111111, 0b10000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b11111111, 0b00011110, 0b00001111, 0b11111111, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b01111111, 0b11111110, 0b00011111, 0b11111100, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00111111, 0b11111111,
    0b11111111, 0b11111000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00001111, 0b11011111, 0b11111111, 0b11100000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00011111, 0b00011001, 0b11111111, 0b11000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00111111, 0b00111100, 0b11111111, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b01111110, 0b01111100, 0b11111000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b01111111, 0b11111110, 0b01111100, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b11111111, 0b11111111, 0b11111100, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b11111111, 0b11111111, 0b11111110, 0b00000000,
    0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b11111111, 0b11111111, 0b11111110, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000001, 0b11111111, 0b11101111, 0b11111110, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000001, 0b11111111, 0b11001111, 0b11111110, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000011, 0b11111111, 0b00000111, 0b11111110, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000011, 0b11111100, 0b00000111, 0b11111110, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000011, 0b11110000, 0b00000011, 0b11111110, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000001, 0b10000000, 0b00000000, 0b11111110, 0b00000000, 0b00000000, 0b00000000,
    0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b01111110, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00111110, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00001100, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000111, 0b10000000, 0b00000000, 0b11111100, 0b00000000, 0b00000000, 0b00000011, 0b11000000, 0b00000000, 0b00000000, 0b00000000, 0b00000111, 0b10000000, 0b00000001, 0b11111100, 0b00000000, 0b00000000, 0b00000011, 0b11000000, 0b00000000, 0b00000000, 0b00000000, 0b00000111, 0b10000000, 0b00000001, 0b11111100, 0b00000000, 0b00000000, 0b00000011, 0b11000000, 0b00000000, 0b00000000, 0b00000000, 0b00000111, 0b10000000, 0b00000001, 0b11100000, 0b00000000, 0b00000000, 0b00000000, 0b00011110, 0b00000000, 0b00000000,
    0b00000000, 0b00000111, 0b10000000, 0b00000001, 0b11100000, 0b00000000, 0b00000000, 0b00000000, 0b00011110, 0b00000000, 0b01111111, 0b11100011, 0b11110111, 0b10011111, 0b11111001, 0b11111101, 0b11100111, 0b01111000, 0b01111011, 0b11011111, 0b11000000, 0b11111111, 0b11110111, 0b11111111, 0b10111111, 0b11111101, 0b11111101, 0b11111111, 0b01111000, 0b01111011, 0b11011111, 0b11000000, 0b11111111, 0b11110111, 0b11111111, 0b10111111, 0b11111101, 0b11111101, 0b11111111, 0b01111000, 0b01111011, 0b11011111, 0b11000000, 0b11110000, 0b11110111, 0b10000111, 0b10111100, 0b00111101, 0b11100001, 0b11111111, 0b01111000, 0b01111011, 0b11011110, 0b00000000, 0b11110000, 0b11110111, 0b10000111, 0b10111100, 0b00111101, 0b11100001, 0b11110000, 0b01111000, 0b01111011, 0b11011110, 0b00000000, 0b00000000, 0b11110111, 0b10000111, 0b10000000, 0b00111101, 0b11100001, 0b11100000, 0b01111000, 0b01111011, 0b11011110, 0b00000000, 0b01111111, 0b11110111, 0b10000111,
    0b10011111, 0b11111101, 0b11100001, 0b11100000, 0b01111000, 0b01111011, 0b11011110, 0b00000000, 0b11111111, 0b11110111, 0b10000111, 0b10111111, 0b11111101, 0b11100001, 0b11100000, 0b01111000, 0b01111011, 0b11011110, 0b00000000, 0b11110000, 0b11110111, 0b10000111, 0b10111100, 0b00111101, 0b11100001, 0b11100000, 0b01111000, 0b01111011, 0b11011110, 0b00000000, 0b11110000, 0b11110111, 0b10000111, 0b10111100, 0b00111101, 0b11100001, 0b11100000, 0b01111000, 0b01111011, 0b11011110, 0b00000000, 0b11110000, 0b11110111, 0b10000111, 0b10111100, 0b00111101, 0b11100001, 0b11100000, 0b01111000, 0b01111011, 0b11011110, 0b00000000, 0b11111111, 0b11110111, 0b11111111, 0b10111111, 0b11111101, 0b11100001, 0b11100000, 0b01111111, 0b11111011, 0b11011111, 0b11000000, 0b11111111, 0b11110111, 0b11111111, 0b10111111, 0b11111101, 0b11100001, 0b11100000, 0b01111111, 0b11111011, 0b11011111, 0b11000000, 0b01111100, 0b11110011, 0b11110011, 0b10011111, 0b00111101,
    0b11100001, 0b11100000, 0b00111110, 0b01111011, 0b11001111, 0b11000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b11111111, 0b11111111, 0b11111111, 0b11111111, 0b11111111, 0b11111111, 0b11111111, 0b11111111, 0b11111111, 0b11111111, 0b11000000, 0b11111111, 0b11111111, 0b11111111, 0b11111111, 0b11111101, 0b01101000, 0b11011011, 0b00010001, 0b00011010, 0b00110001, 0b11000000, 0b11111111, 0b11111111, 0b11111111, 0b11111111, 0b11111101, 0b00101011, 0b01011010, 0b11111011, 0b01101010, 0b11101111, 0b11000000, 0b11111111, 0b11111111, 0b11111111, 0b11111111, 0b11111101, 0b01001011, 0b01011011, 0b00111011, 0b00011010, 0b00110011, 0b11000000, 0b11111111, 0b11111111, 0b11111111, 0b11111111, 0b11111101, 0b01101011, 0b01011011, 0b11011011, 0b01101010, 0b11111101, 0b11000000,
};
*/

#define GET_BUFFER_OFFSET(x, y) ((y) >> 3) * BUFFER_BYTE_WIDTH + (x)
#define SET_PIXEL(x, y) scratch_buffer[GET_BUFFER_OFFSET((x), (y))] |= (1 << ((y)&0x7))
#define COPY_TO_BUFFER_XY(unint16X, uint16Y, srcBuffer, numBytes) memcpy_P(&scratch_buffer[GET_BUFFER_OFFSET((unint16X), (uint16Y))], (srcBuffer), (numBytes))

inline GFXglyph *pgm_read_glyph_ptr(const GFXfont *gfxFont, uint8_t c) {
#ifdef __AVR__
    return &(((GFXglyph *)pgm_read_pointer(&gfxFont->glyph))[c]);
#else
    // expression in __AVR__ section may generate "dereferencing type-punned
    // pointer will break strict-aliasing rules" warning In fact, on other
    // platforms (such as STM32) there is no need to do this pointer magic as
    // program memory may be read in a usual way So expression may be simplified
    return gfxFont->glyph + c;
#endif  //__AVR__
}

inline uint8_t *pgm_read_bitmap_ptr(const GFXfont *gfxFont) {
#ifdef __AVR__
    return (uint8_t *)pgm_read_pointer(&gfxFont->bitmap);
#else
    // expression in __AVR__ section generates "dereferencing type-punned pointer
    // will break strict-aliasing rules" warning In fact, on other platforms (such
    // as STM32) there is no need to do this pointer magic as program memory may
    // be read in a usual way So expression may be simplified
    return gfxFont->bitmap;
#endif  //__AVR__
}

void kdisp_fill_rect(uint16_t x_start, uint16_t y_start, uint8_t width, uint8_t height) {
    for (int x = x_start; x < (x_start + width); ++x) {
        for (int y = y_start; y < (x_start + width); ++y) {
            SET_PIXEL(x, y);
        }
    }
}

// Draw a character
/**************************************************************************/
/*!
   @brief   Draw a single character
    @param    x   Bottom left corner x coordinate
    @param    y   Bottom left corner y coordinate
    @param    c   The 8-bit font-indexed character (likely ascii, only defined ones according to the font!)
*/
/**************************************************************************/
uint8_t kdisp_write_gfx_char(const GFXfont *gfxFont, int16_t x, int16_t y, unsigned char c) {
    uint16_t first = pgm_read_byte(&gfxFont->first);
    uint16_t last  = pgm_read_byte(&gfxFont->last);
    if (c < first || c > last) {
        return 0;
    }
    c -= (uint8_t)first;
    GFXglyph *glyph  = pgm_read_glyph_ptr(gfxFont, c);
    uint8_t * bitmap = pgm_read_bitmap_ptr(gfxFont);

    uint16_t bo = pgm_read_word(&glyph->bitmapOffset);
    uint8_t  w = pgm_read_byte(&glyph->width), h = pgm_read_byte(&glyph->height);
    int8_t   xo = pgm_read_byte(&glyph->xOffset), yo = pgm_read_byte(&glyph->yOffset);
    uint8_t  xx, yy, bits = 0, bit = 0;

    // Todo: Add character clipping here

    for (yy = 0; yy < h; yy++) {
        for (xx = 0; xx < w; xx++) {
            if (!(bit++ & 7)) {
                bits = pgm_read_byte(&bitmap[bo++]);
            }
            if (bits & 0x80) {
                SET_PIXEL(x + xo + xx, y + yo + yy);
            }
            bits <<= 1;
        }
    }

    return pgm_read_byte(&glyph->xAdvance);
}

void kdisp_write_gfx_text(const GFXfont *gfxFont, int16_t x, int16_t y, const char *text) {
    uint16_t x_cursor = x;
    while (*text != 0) {
        if(*text=='\n') {
            y += pgm_read_byte(&gfxFont->yAdvance);
            x_cursor = x;
        } else {
            x_cursor +=kdisp_write_gfx_char(gfxFont, x_cursor, y, *text);
        }
        text++;
    }
}

void kdisp_write_char(uint16_t x, uint16_t y, const char ch) {
    uint8_t cast_data = (uint8_t)ch;  // font based on unsigned type for index
    if (cast_data < OLED_FONT_START || cast_data > OLED_FONT_END) {
        memset(&scratch_buffer[GET_BUFFER_OFFSET(x, y)], 0x00, OLED_FONT_WIDTH);
    } else {
        const uint8_t *glyph = &font[(cast_data - OLED_FONT_START) * OLED_FONT_WIDTH];
        COPY_TO_BUFFER_XY(x, y, glyph, OLED_FONT_WIDTH);
    }
}

void kdisp_draw_bitmap(uint16_t x, uint16_t y, const uint8_t pgm_bmp[], uint8_t bmp_width, uint8_t bmp_height) {
    uint8_t byte_width           = (bmp_width + 7) / 8;
    uint8_t vertical_pixel_row_8 = 0;

    for (uint8_t bmp_y = 0; bmp_y < bmp_height; bmp_y++, y++) {
        for (uint8_t bmp_x = 0; bmp_x < bmp_width; bmp_x++) {
            if (bmp_x & 0x07) {
                vertical_pixel_row_8 <<= 1;
            } else {
                vertical_pixel_row_8 = pgm_read_byte(&pgm_bmp[bmp_y * byte_width + (bmp_x >> 3)]);
            }
            if (vertical_pixel_row_8 & 0x80) {
                SET_PIXEL(x + bmp_x, y);
            }
        }
    }
}

void kdisp_set_buffer(uint8_t vertical_pixel_row_of_8_pixels) { memset(scratch_buffer, vertical_pixel_row_of_8_pixels, BUFFER_BYTE_WIDTH * BUFFER_BYTE_HEIGHT); }

void kdisp_send_buffer(void) {
    spi_start(SPI_SS_PIN, false, SPI_MODE, STM32_SYSCLK / 1000000);

    spi_prepare_commands();

    static const uint8_t PROGMEM dlist1[] = {SSD1306_PAGEADDR,
                                             0,                       // Page start address
                                             0xFF,                    // Page end (not really, but works here)
                                             SSD1306_COLUMNADDR, 0};  // Column start address
    spi_transmit(dlist1, sizeof(dlist1));
    spi_write(BUFFER_PIXEL_WIDTH - 1);  // Column end address

    spi_prepare_data();

    spi_transmit(scratch_buffer, BUFFER_BYTE_WIDTH * BUFFER_BYTE_HEIGHT);

    spi_stop();
}

void kdisp_invert(bool invert) {
    spi_start(SPI_SS_PIN, false, SPI_MODE, STM32_SYSCLK / 1000000);
    spi_prepare_commands();
    spi_write(invert ? SSD1306_INVERTDISPLAY : SSD1306_NORMALDISPLAY);
    spi_stop();
}

void kdisp_hw_setup(void) {
    //make sure the pins are output pins and low
    setPinOutput(KEY_DISPLAYS_VDD_PIN);
    setPinOutput(KEY_DISPLAYS_VBAT_PIN);
    writePinLow(KEY_DISPLAYS_VDD_PIN);
    writePinLow(KEY_DISPLAYS_VBAT_PIN);

    sr_hw_setup();
}

void kdisp_init(const int8_t num_shift_regs) {

    // first turn on logic power supply
    writePinHigh(KEY_DISPLAYS_VDD_PIN);
    wait_ms(3);
    // and then the power supply for the displays
    writePinHigh(KEY_DISPLAYS_VBAT_PIN);

    spi_init();

    sr_init();

    //make sure we are talking to all shift registers
    uint8_t all[num_shift_regs];
    for(int8_t i=0;i<num_shift_regs;++i) {
        all[i] = 0;
    }
    sr_shift_out_buffer_latch(all, num_shift_regs);

    spi_reset();

    spi_start(SPI_SS_PIN, false, SPI_MODE, STM32_SYSCLK / 1000000);

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
    static const uint8_t PROGMEM contrast[] = {SSD1306_SETCONTRAST, 0xff};
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

    spi_write(SSD1306_DISPLAYON);

    spi_stop();

    wait_ms(10);
/*
    sr_shift_out_latch(~0x02);
    kdisp_set_buffer(0x00);

    kdisp_draw_bitmap(0, 0, test_bmp_data, test_bmp_width, 40);
    kdisp_send_buffer();

    sr_shift_out_latch(~0x04);
    kdisp_set_buffer(0x00);
    kdisp_write_gfx_char(&FreeSans12pt7b, 28, 18, '1');

    kdisp_send_buffer();

    sr_shift_out_latch(~0x08);
    kdisp_set_buffer(0x00);
    kdisp_write_gfx_text(&FreeSans12pt7b, 28, 18, "ABCD");
    kdisp_send_buffer();

    sr_shift_out_latch(~0x10);
    kdisp_set_buffer(0x00);
    kdisp_write_gfx_text(&FreeSans12pt7b, 28, 18, "abcd");
    // kdisp_write_char(50, 10, '3');
    kdisp_send_buffer();
    */
}
