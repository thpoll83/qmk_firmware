#include "polyatom.h"
#include QMK_KEYBOARD_H


void sr_hw_setup(void) {
    setPinOutput(SR_NMR_PIN);
    setPinOutput(SR_CLK_PIN);
    setPinOutput(SR_DATA_PIN);
    setPinOutput(SR_LATCH_PIN);

    writePinHigh(SR_NMR_PIN);
    writePinHigh(SR_CLK_PIN);
    writePinHigh(SR_DATA_PIN);
    writePinLow(SR_LATCH_PIN);
 }

void sr_init(void) {
    // shift register setup
    //writePinHigh(SR_NMR_PIN);
    //wait_us(10); //1?
    writePinLow(SR_NMR_PIN);
    wait_us(5);
    writePinHigh(SR_NMR_PIN);
    wait_us(5);
}

void sr_shift_out(uint8_t val) {
    for (uint8_t i = 0; i < 8; i++) {

        writePin(SR_DATA_PIN, !!(val & (1 << (7 - i))));
        wait_us(1);
        writePinLow(SR_CLK_PIN);
        wait_us(2);
        writePinHigh(SR_CLK_PIN);
        wait_us(2);
    }
}

void sr_shift_out_latch(uint8_t val) {
    //wait_us(10);
    writePinLow(SR_LATCH_PIN);
    sr_shift_out(val);
    wait_us(2);
    writePinHigh(SR_LATCH_PIN);
    wait_us(2);
    writePinLow(SR_LATCH_PIN);
    wait_us(2);
}

void spi_hw_setup(void) {
    setPinOutput(SPI_SS_PIN);
    writePinLow(SPI_SS_PIN);

    setPinOutput(SPI_DC_PIN);
    writePinLow(SPI_DC_PIN);

    setPinOutput(SPI_RST_PIN);
    writePinLow(SPI_RST_PIN);

    setPinOutput(SPI_SCK_PIN);
    writePinLow(SPI_SCK_PIN);

    setPinOutput(SPI_MOSI_PIN);
    writePinLow(SPI_MOSI_PIN);

    setPinOutput(SPI_MISO_PIN);
    writePinLow(SPI_MISO_PIN);
}

void spi_prepare_commands(void) {
    setPinOutput(SPI_DC_PIN);
    writePinLow(SPI_DC_PIN);
    wait_us(50); //?
}

void spi_prepare_data(void) {
    setPinOutput(SPI_DC_PIN);
    writePinHigh(SPI_DC_PIN);
    wait_us(5); //?
}

void spi_reset(void) {
    setPinOutput(SPI_RST_PIN);

    writePinHigh(SPI_RST_PIN);
    wait_ms(1);
    writePinLow(SPI_RST_PIN);
    wait_ms(1);
    writePinHigh(SPI_RST_PIN);
    wait_ms(1);
}
