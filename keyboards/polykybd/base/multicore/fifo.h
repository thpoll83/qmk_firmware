#pragma once

#include "hardware/regs/sio.h"
#include "hardware/regs/m0plus.h"
#include "hardware/structs/sio.h"

#define PICO_WAIT_FOR_EVENT() __asm volatile ("wfe")

#define SEV() __asm volatile ("sev")

static inline bool multicore_fifo_wready(void) {
    return sio_hw->fifo_st & SIO_FIFO_ST_RDY_BITS;
}

static inline bool multicore_fifo_rvalid(void) {
    return sio_hw->fifo_st & SIO_FIFO_ST_VLD_BITS;
}


static inline void multicore_fifo_drain(void) {
    while (multicore_fifo_rvalid())
        (void) sio_hw->fifo_rd;
}

static inline void multicore_fifo_push_blocking(uint32_t data) {
    // We wait for the fifo to have some space
    while (!multicore_fifo_wready())
        tight_loop_contents();

    sio_hw->fifo_wr = data;

    // Fire off an event to the other core
    SEV();
}

static inline uint32_t multicore_fifo_pop_blocking(void) {
    // If nothing there yet, we wait for an event first,
    // to try and avoid too much busy waiting
    while (!multicore_fifo_rvalid())
        PICO_WAIT_FOR_EVENT();

    return sio_hw->fifo_rd;
}

static inline void multicore_fifo_wait_for(uint32_t cmd) {
    uint32_t value;
    do {
        value = multicore_fifo_pop_blocking();
    } while (value != cmd);
}
