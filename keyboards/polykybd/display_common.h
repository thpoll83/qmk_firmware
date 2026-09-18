// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "quantum.h"

// Weak function declarations - each variant provides its own implementation
// This follows QMK's convention for keyboard-specific overrides
__attribute__((weak)) const uint8_t* get_key_disp_bitmask(uint8_t index);
__attribute__((weak)) uint8_t get_disp_bitmask_size(void);
__attribute__((weak)) bool key_has_display(uint8_t r, uint8_t c);
__attribute__((weak)) void invert_display(uint8_t r, uint8_t c, bool state);



// Shared matrix scan implementation
void matrix_scan_display_common(void);
