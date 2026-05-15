#include "helpers.h"

#include "gpio.h"
#include "wait.h"

void peripherals_reset(void) {
    gpio_set_pin_output(HW_RST_PIN);
    gpio_write_pin_low(HW_RST_PIN);
    wait_us(2);
    gpio_write_pin_high(HW_RST_PIN);
}
