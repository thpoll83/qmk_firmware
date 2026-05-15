#include "spi_helper.h"
#include QMK_KEYBOARD_H

void spi_hw_setup(void) {
   /* gpio_set_pin_output(SPI_SS_PIN);
    gpio_write_pin_low(SPI_SS_PIN);

    gpio_set_pin_output(SPI_DC_PIN);
    gpio_write_pin_low(SPI_DC_PIN);

    gpio_set_pin_output(SPI_SCK_PIN);
    gpio_write_pin_high(SPI_SCK_PIN);

    gpio_set_pin_output(SPI_MOSI_PIN);
    gpio_write_pin_high(SPI_MOSI_PIN);

    gpio_set_pin_output(SPI_MISO_PIN);
    gpio_write_pin_low(SPI_MISO_PIN);
    gpio_set_pin_output(SPI_RST_PIN);
    gpio_write_pin_high(SPI_RST_PIN);*/
}

void spi_prepare_commands(void) {
    gpio_set_pin_output(SPI_DC_PIN);
    gpio_write_pin_low(SPI_DC_PIN);
    //wait_us(1);
}

void spi_prepare_data(void) {
    gpio_set_pin_output(SPI_DC_PIN);
    gpio_write_pin_high(SPI_DC_PIN);
    //wait_us(1);
}

