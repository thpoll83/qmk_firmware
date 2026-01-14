#pragma once

#include "fifo.h"

#define SIO_IRQ_PROC0 15
#define SIO_FIFO_IRQ_NUM(core) (SIO_IRQ_PROC0 + (core))

void multicore_launch_core1(void);

void core1_entry(void);

static inline void dmb(void) {
    __asm volatile ("dmb" ::: "memory");
}

