// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include QMK_KEYBOARD_H

const uint32_t* get_led_matrix_text(uint8_t rgb_mode);

/* "xx-YY" for a language index (0..NUM_LANG-1), "" when out of range.
   DEFINED IN poly_keymap.c, which owns the cog-generated code table — declared here
   because this is the display-text header the status OLED already includes. */
const uint32_t* poly_lang_code(uint8_t lang);

/* True when the effect's name is followed by a superscript 2 ("Splash²" for
   MULTISPLASH). The ASCII-only status font has no such glyph, so the caller draws it
   as a bitmap right after the name. */
bool led_matrix_text_superscript2(uint8_t rgb_mode);

/* RGB-matrix hue byte (0..255) -> its position on the colour wheel in degrees.
   Shared so the name sectors, the status readout and the preview tool cannot drift. */
uint16_t hue_to_degrees(uint8_t hue);

/* Colour NAME for an RGB-matrix hue byte (0..255 spanning the 360 deg wheel), so the
   status OLED can show "Cyan" instead of a raw 0x80. `sat` is taken into account
   because a washed-out colour has no meaningful hue — it reads as "White". */
const uint32_t* get_hue_name(uint8_t hue, uint8_t sat);
