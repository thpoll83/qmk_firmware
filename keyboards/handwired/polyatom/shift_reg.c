#include "shift_reg.h"
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
    writePinLow(SR_LATCH_PIN);
    sr_shift_out(val);
    wait_us(2);
    writePinHigh(SR_LATCH_PIN);
    wait_us(2);
    writePinLow(SR_LATCH_PIN);
    wait_us(2);
}

void sr_shift_out_buffer_latch(const uint8_t* val, uint8_t len) {
    writePinLow(SR_LATCH_PIN);
    for(uint8_t i=0;i<len;++i) {
        sr_shift_out(val[i]);
    }
    wait_us(2);
    writePinHigh(SR_LATCH_PIN);
    wait_us(2);
    writePinLow(SR_LATCH_PIN);
    wait_us(2);
}
