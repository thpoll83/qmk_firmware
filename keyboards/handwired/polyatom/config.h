/*
Copyright 2015 Jun Wako <wakojun@gmail.com>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include "config_common.h"

/* USB Device descriptor parameter */
#define VENDOR_ID       0x2021
#define PRODUCT_ID      0x2007
#define DEVICE_VER      0x0205
#define MANUFACTURER    [thpoll]
#define PRODUCT         PolyKeyboard-Atom v0.2.6

/* key matrix size */
#define MATRIX_ROWS 1
#define MATRIX_COLS 4

#define DIODE_DIRECTION ROW2COL

#define LAYOUT( \
    k00, k01, k02, k03 \
) { \
    { k00, k01 , k02, k03 } \
}

#define LAYOUT_TO_INDEX(row, col) ((row)*MATRIX_COLS+(col))

/* define if matrix has ghost */
//#define MATRIX_HAS_GHOST

/* Set 0 if debouncing isn't needed */
#define DEBOUNCE    5

/* Oled Size */
#define OLED_DISPLAY_128X64
#define OLED_FONT_END 255
#define OLED_FONT_H "fonts/base_font.h"
#define OLED_BRIGHTNESS 128
//#define OLED_TIMEOUT 60000
//#define OLED_FADE_OUT
//#define OLED_FADE_OUT_INTERVAL 2

#define ENCODERS_PAD_A { E13 }
#define ENCODERS_PAD_B { E15 }
#define ENCODER_RESOLUTION 4

#define DRIVER_LED_TOTAL 4

#define RGB_MATRIX_KEYPRESSES

// #   define RGB_MATRIX_FRAMEBUFFER_EFFECTS
#define RGB_MATRIX_LED_PROCESS_LIMIT (DRIVER_LED_TOTAL + 4) / 5  // limits the number of LEDs to process in an animation per task run (increases keyboard responsiveness)
#define RGB_MATRIX_LED_FLUSH_LIMIT 16                            // limits in milliseconds how frequently an animation will update the LEDs. 16 (16ms) is equivalent to limiting to 60fps (increases keyboard responsiveness)
#define RGB_MATRIX_MAXIMUM_BRIGHTNESS 25
#define RGB_DISABLE_AFTER_TIMEOUT 10000

#define DISABLE_RGB_MATRIX_ALPHAS_MODS
#define DISABLE_RGB_MATRIX_GRADIENT_UP_DOWN
#define DISABLE_RGB_MATRIX_GRADIENT_LEFT_RIGHT
#define DISABLE_RGB_MATRIX_BAND_SAT
#define DISABLE_RGB_MATRIX_BAND_VAL
#define DISABLE_RGB_MATRIX_BAND_PINWHEEL_SAT
#define DISABLE_RGB_MATRIX_BAND_PINWHEEL_VAL
#define DISABLE_RGB_MATRIX_BAND_SPIRAL_SAT
#define DISABLE_RGB_MATRIX_BAND_SPIRAL_VAL
#define DISABLE_RGB_MATRIX_CYCLE_LEFT_RIGHT
#define DISABLE_RGB_MATRIX_CYCLE_UP_DOWN
#define DISABLE_RGB_MATRIX_CYCLE_OUT_IN
#define DISABLE_RGB_MATRIX_CYCLE_OUT_IN_DUAL
#define DISABLE_RGB_MATRIX_CYCLE_PINWHEEL
#define DISABLE_RGB_MATRIX_CYCLE_SPIRAL
#define DISABLE_RGB_MATRIX_DUAL_BEACON
#define DISABLE_RGB_MATRIX_RAINBOW_BEACON
#define DISABLE_RGB_MATRIX_RAINBOW_PINWHEELS
#define DISABLE_RGB_MATRIX_RAINDROPS
#define DISABLE_RGB_MATRIX_JELLYBEAN_RAINDROPS
#define DISABLE_RGB_MATRIX_HUE_BREATHING
#define DISABLE_RGB_MATRIX_HUE_PENDULUM
#define DISABLE_RGB_MATRIX_HUE_WAVE
#define DISABLE_RGB_MATRIX_TYPING_HEATMAP
#define DISABLE_RGB_MATRIX_DIGITAL_RAIN
#define DISABLE_RGB_MATRIX_SOLID_REACTIVE_SIMPLE
#define DISABLE_RGB_MATRIX_SOLID_REACTIVE
#define DISABLE_RGB_MATRIX_SOLID_REACTIVE_MULTIWIDE
#define DISABLE_RGB_MATRIX_SOLID_REACTIVE_MULTICROSS
#define DISABLE_RGB_MATRIX_SOLID_REACTIVE_MULTINEXUS
#define DISABLE_RGB_MATRIX_SPLASH
#define DISABLE_RGB_MATRIX_MULTISPLASH
#define DISABLE_RGB_MATRIX_SOLID_SPLASH
#define DISABLE_RGB_MATRIX_SOLID_MULTISPLASH


#define ENABLE_RGB_MATRIX_SOLID_REACTIVE_NEXUS

