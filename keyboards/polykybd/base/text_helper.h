// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include QMK_KEYBOARD_H

const uint32_t* get_led_matrix_text(uint8_t rgb_mode);

/* Colour NAME for an RGB-matrix hue byte (0..255 spanning the 360 deg wheel), so the
   status OLED can show "Cyan" instead of a raw 0x80. `sat` is taken into account
   because a washed-out colour has no meaningful hue — it reads as "White". */
const uint32_t* get_hue_name(uint8_t hue, uint8_t sat);
