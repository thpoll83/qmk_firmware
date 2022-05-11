#include "shift_reg.h"
#include QMK_KEYBOARD_H

void sr_hw_setup(void) {
    if( SR_NMR_PIN != NO_PIN ) {
        setPinOutput(SR_NMR_PIN);
    }
    setPinOutput(SR_CLK_PIN);
    setPinOutput(SR_DATA_PIN);
    setPinOutput(SR_LATCH_PIN);

    if( SR_NMR_PIN != NO_PIN ) {
        writePinHigh(SR_NMR_PIN);
    }
    writePinHigh(SR_CLK_PIN);
    writePinHigh(SR_DATA_PIN);
    writePinLow(SR_LATCH_PIN);
 }

void sr_init(void) {
    if( SR_NMR_PIN != NO_PIN ) {
        setPinOutput(SR_NMR_PIN);
        writePinLow(SR_NMR_PIN);
        WAIT_NS(20);//wait_us(10);
        writePinHigh(SR_NMR_PIN);
        WAIT_NS(2);//wait_us(2);
    }
}

void sr_shift_out(uint8_t val) {
    for (uint8_t i = 0; i < 8; i++) {
        writePin(SR_DATA_PIN, !!(val & (1 << (7 - i))));
        WAIT_NS(1);
        writePinLow(SR_CLK_PIN);
        WAIT_NS(1);
        writePinHigh(SR_CLK_PIN);
        //wait_us(1);
    }
}

void sr_shift_out_latch(uint8_t val) {
    setPinOutput(SR_DATA_PIN);
    setPinOutput(SR_CLK_PIN);
    setPinOutput(SR_LATCH_PIN);
    writePinLow(SR_LATCH_PIN);
    sr_shift_out(val);
    //WAIT_NS(2);
    writePinHigh(SR_LATCH_PIN);
    WAIT_NS(1);
    writePinLow(SR_LATCH_PIN);
    //wait_us(1);
}

void sr_shift_out_buffer_latch(const uint8_t* val, uint8_t len) {
    setPinOutput(SR_DATA_PIN);
    setPinOutput(SR_CLK_PIN);
    setPinOutput(SR_LATCH_PIN);
    writePinLow(SR_LATCH_PIN);
    for(uint8_t i=0;i<len;++i) {
        sr_shift_out(val[i]);
    }
    //WAIT_NS(2);
    writePinHigh(SR_LATCH_PIN);
    WAIT_NS(1);
    writePinLow(SR_LATCH_PIN);
    //wait_us(1);
}
