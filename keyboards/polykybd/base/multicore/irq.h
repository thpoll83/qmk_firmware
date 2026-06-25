#pragma once

#include "hardware/structs/nvic.h"

bool irq_is_enabled(uint num) {
    return 0 != ((1u << num) & *((io_rw_32 *) (PPB_BASE + M0PLUS_NVIC_ISER_OFFSET)));
}

static inline void irq_set_mask_enabled(uint32_t mask, bool enabled) {
    if (enabled) {
        nvic_hw->icpr = mask;
        nvic_hw->iser = mask;
    } else {
        nvic_hw->icer = mask;
    }
}

void irq_set_enabled(uint num, bool enabled) {
    // really should update irq_set_mask_enabled?
    irq_set_mask_enabled(1u << (num % 32), enabled);
}

