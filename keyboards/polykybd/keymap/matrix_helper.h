// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later


#pragma once

#include <stdint.h>
#include <stdbool.h>


enum key_split_pos get_split_matrix_pos(uint16_t keycode, uint8_t layer, uint8_t* row, uint8_t* col, bool prefer_rc_left);

enum key_split_pos get_split_matrix_side(uint16_t keycode, uint8_t layer);


