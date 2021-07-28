#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include "kdisp.h"

#include "polyatom.h"
#include "spi_master.h"


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

#define BUFFER_BYTE_HEIGHT 40
#define BUFFER_BYTE_WIDTH 16
#define BUFFER_PIXEL_HEIGHT 40
#define BUFFER_PIXEL_WIDTH 128

#define SPI_MODE 3

uint8_t scratch_buffer[BUFFER_BYTE_WIDTH*BUFFER_BYTE_HEIGHT];

void kdisp_draw_bitmap(int16_t x, int16_t y, const uint8_t* bitmap, int16_t w, int16_t h) {

//   int16_t byteWidth = (w + 7) / 8; // Bitmap scanline pad = whole byte
//   uint8_t byte = 0;

//   startWrite();
//   for (int16_t j = 0; j < h; j++, y++) {
//     for (int16_t i = 0; i < w; i++) {
//       if (i & 7)
//         byte <<= 1;
//       else
//         byte = pgm_read_byte(&bitmap[j * byteWidth + i / 8]);
//       if (byte & 0x80)
//         writePixel(x + i, y, color);
//     }
//   }
//   endWrite();
}

void kdisp_set_buffer(uint8_t vertical_pixel_row_of_8_pixels) {
    memset(scratch_buffer, vertical_pixel_row_of_8_pixels, BUFFER_BYTE_WIDTH*BUFFER_BYTE_HEIGHT);
}

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

    spi_transmit(scratch_buffer, BUFFER_BYTE_WIDTH*BUFFER_BYTE_HEIGHT);

    spi_stop();
}

void kdisp_invert(bool invert) {
    spi_prepare_commands();
    spi_write(invert ? SSD1306_INVERTDISPLAY : SSD1306_NORMALDISPLAY);
}

void kdisp_init(void) {

    // first turn on logic power supply
    setPinOutput(KEY_DISPLAYS_VDD_PIN);
    writePinHigh(KEY_DISPLAYS_VDD_PIN);
    wait_ms(10);
    // and then the power supply for the displays
    setPinOutput(KEY_DISPLAYS_VBAT_PIN);
    writePinHigh(KEY_DISPLAYS_VBAT_PIN);

    sr_init();
    sr_shift_out_latch(0x00);

    spi_init();
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
    spi_write(SSD1306_SEGREMAP);    // | 0x1 ?
    spi_write(SSD1306_COMSCANINC);  // SSD1306_COMSCANDEC ?
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

    sr_shift_out_latch(~0x02);
    kdisp_set_buffer(0x01);
    kdisp_send_buffer();

    sr_shift_out_latch(~0x04);
    kdisp_set_buffer(0x05);
    kdisp_send_buffer();

    sr_shift_out_latch(~0x08);
    kdisp_set_buffer(0xf0);
    kdisp_send_buffer();

    sr_shift_out_latch(~0x10);
    kdisp_set_buffer(0xfa);
    kdisp_send_buffer();
 }
