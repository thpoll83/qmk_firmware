// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#include "hw_uid.h"

#include "hardware/flash.h"
#include "hardware/sync.h"

#include <string.h>

_Static_assert(FLASH_UNIQUE_ID_SIZE_BYTES == POLY_HW_UID_LEN, "the flash RUID is 8 bytes");

static uint8_t s_uid[POLY_HW_UID_LEN];
static bool    s_valid = false;

static bool all_bytes(const uint8_t *p, uint8_t v) {
    for (uint8_t i = 0; i < POLY_HW_UID_LEN; ++i) {
        if (p[i] != v) return false;
    }
    return true;
}

void poly_hw_uid_boot_init(void) {
    // No core1_hold(): core1 has not been launched at keyboard_setup(), and a
    // hold's release would relaunch it (see crash_record.c flash_guarded()).
    uint32_t irq = save_and_disable_interrupts();
    flash_get_unique_id(s_uid);
    restore_interrupts(irq);
    s_valid = !all_bytes(s_uid, 0x00u) && !all_bytes(s_uid, 0xFFu);
}

bool poly_hw_uid(uint8_t out[POLY_HW_UID_LEN]) {
    if (!s_valid) {
        memset(out, 0, POLY_HW_UID_LEN);
        return false;
    }
    memcpy(out, s_uid, POLY_HW_UID_LEN);
    return true;
}
