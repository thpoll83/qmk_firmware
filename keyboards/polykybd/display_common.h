// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "quantum.h"

// Display configuration structure - each variant provides its own
typedef struct {
    // Variant-specific display table accessor
    const uint8_t* (*get_key_disp_bitmask)(uint8_t index);
    
    // Size of each bitmask in bytes
    uint8_t (*get_disp_bitmask_size)(void);
    
    // Check if (r,c) has a physical display
    bool (*key_has_display)(uint8_t r, uint8_t c);
    
    // Invert the display at (r,c)
    void (*invert_display)(uint8_t r, uint8_t c, bool state);
    
    // Variant geometry
    uint8_t matrix_rows_per_side;
    uint8_t matrix_cols;
    
    // Column adjustment for asymmetric layouts (split72: c-- for rows 5-8)
    bool needs_col_adjustment;
    uint8_t col_adjustment_start_row;
} display_config_t;

// Register the variant's display configuration (called from matrix_init_kb)
void display_register_config(const display_config_t* config);

// Shared matrix scan implementation - uses the registered config
void matrix_scan_display_common(void);
