// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// RP2040 + ChibiOS implementation of the core1_owner hardware seam.
#include "core1_owner.h"

#include <ch.h>

#include "hardware/structs/psm.h"
#include "hardware/address_mapped.h"
#include "polymod_core1.h"
#include "print.h"

// Launch budget for one handshake. The same 100 ms the bounded relaunches used
// before this module existed.
#define CORE1_LAUNCH_BUDGET_US (100u * 1000u)

void core1_hw_lock(void) {
    chSysLock();
}

void core1_hw_unlock(void) {
    chSysUnlock();
}

void core1_hw_force_off(void) {
    hw_set_bits(&psm_hw->frce_off, PSM_FRCE_OFF_PROC1_BITS);
    __asm volatile("dsb" ::: "memory");
    // The DSB only orders the store. Wait for PSM DONE to drop: until then core1
    // can still be fetching from XIP, and an erase that starts now stalls the bus
    // for good. Bounded, so a PSM that never reports degrades instead of hanging.
    uint32_t spins = 0;
    while ((psm_hw->done & PSM_DONE_PROC1_BITS) && spins < 1000000u) {
        spins++;
    }
}

void core1_hw_release(void) {
    hw_clear_bits(&psm_hw->frce_off, PSM_FRCE_OFF_PROC1_BITS);
}

bool core1_hw_launch_service(void) {
    return multicore_launch_core1_bounded(CORE1_LAUNCH_BUDGET_US);
}

bool core1_hw_launch(void (*entry)(void), uint32_t *stack_bottom, size_t stack_bytes) {
    return multicore_launch_core1_with_stack_bounded(entry, stack_bottom, stack_bytes, CORE1_LAUNCH_BUDGET_US);
}

void core1_hw_report(const char *what) {
    uprintf("core1: %s\n", what);
}
