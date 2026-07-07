#pragma once

#include "fifo.h"

#include <stddef.h>

#define SIO_IRQ_PROC0 15
#define SIO_FIFO_IRQ_NUM(core) (SIO_IRQ_PROC0 + (core))

void multicore_launch_core1(void);

// Like multicore_launch_core1(), but the FIFO handshake is bounded by an
// overall deadline instead of blocking forever. False = core1 never answered
// (not in the bootrom wait loop) — the caller can PSM-reset it and retry.
// The unbounded launcher stays for the boot path (a boot-time failure has no
// meaningful fallback); this one is for RUNTIME relaunches (doom session
// teardown), where a wedged handshake means a dead keyboard.
bool multicore_launch_core1_bounded(uint32_t total_timeout_us);

// Launch core1 with a caller-provided entry + stack (the underlying primitive
// of multicore_launch_core1; used by the Doom easter egg to hand core1 to the
// game with a pool-backed stack).
void multicore_launch_core1_with_stack(void (*entry)(void), uint32_t *stack_bottom, size_t stack_size_bytes);

void core1_entry(void);

#ifdef CORE1_STACK_HWM
uint32_t core1_stack_high_water_mark(void);
#endif

static inline void dmb(void) {
    __asm volatile ("dmb" ::: "memory");
}

