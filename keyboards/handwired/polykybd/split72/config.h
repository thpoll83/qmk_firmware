// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

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


/* key matrix size */
#define MATRIX_ROWS_PER_SIDE 5
#define MATRIX_ROWS 10
#define MATRIX_COLS 8

#define LAYOUT_TO_INDEX(row, col) ((row)*MATRIX_COLS+(col))

#define RGBLED_NUM       72
#define DRIVER_LED_TOTAL RGBLED_NUM
#define RGB_MATRIX_LED_COUNT RGBLED_NUM
#define RGB_MATRIX_SPLIT { 36, 36 }


#define NUM_SHIFT_REGISTERS 5


#define MATRIX_COL_PINS \
    { GP10, GP11, GP12, GP13, GP14, GP15, GP16, GP3 }
#define MATRIX_ROW_PINS \
    { GP18, GP19, GP20, GP21, GP22 }


#define WS2812_DI_PIN GP2


// SPI interface to write to the selected display
#define SPI_DRIVER SPID0
#define SPI_SS_PIN GP17
#define SPI_DC_PIN GP8
#define HW_RST_PIN GP9
#define SPI_SCK_PIN GP6
#define SPI_MOSI_PIN GP7
#define SPI_MISO_PIN GP4

//This number can be calculated by dividing the MCU’s clock speed
//by the desired SPI clock speed. For example, an MCU running at 8 MHz
//wanting to talk to an SPI device at 4 MHz would set the divisor to 2
#define SPI_DIVISOR (CPU_CLOCK / 10000000) //rp1040 runs at 133Mhz, SPI at 10Mhz

// Shift register to select the display
//#define SR_NMR_PIN //NO_PIN if possible
#define SR_CLK_PIN GP27
#define SR_DATA_PIN GP26
#define SR_LATCH_PIN GP28


#define ENCODER_RESOLUTION 2

//see also https://docs.qmk.fm/#/feature_pointing_device?id=split-keyboard-configuration


// Setup Cirque
#define CIRQUE_PINNACLE_DIAMETER_MM 35
#define CIRQUE_PINNACLE_TAP_ENABLE
#define CIRQUE_PINNACLE_TAPPING_TERM 100
#define CIRQUE_PINNACLE_TOUCH_DEBOUNCE 300
#define CIRQUE_PINNACLE_POSITION_MODE  CIRQUE_PINNACLE_RELATIVE_MODE
#define POINTING_DEVICE_GESTURES_CURSOR_GLIDE_ENABLE
#define CIRQUE_PINNACLE_ATTENUATION EXTREG__TRACK_ADCCONFIG__ADC_ATTENUATE_2X
//#define CIRQUE_PINNACLE_SECONDARY_TAP_ENABLE
//#define POINTING_DEVICE_GESTURES_SCROLL_ENABLE

// Enable use of pointing device on slave split.
#define SPLIT_POINTING_ENABLE

// Pointing device is on the right split.
// Use POINTING_DEVICE_COMBINED instead if a left trackpad is also added.
#define POINTING_DEVICE_RIGHT

// Limits the frequency that the sensor is polled for motion.
#define POINTING_DEVICE_TASK_THROTTLE_MS 1

// POINTING_DEVICE_ROTATION_90_RIGHT only applies in POINTING_DEVICE_COMBINED mode.
#define POINTING_DEVICE_ROTATION_90
//#define POINTING_DEVICE_GESTURES_CURSOR_GLIDE_ENABLE

//#define POINTING_DEVICE_DEBUG
//#define PIMORONI_TRACKBALL_SCALE 5
//#define TRACKBALL_LED_TIMEOUT 5000

#define RAW_USAGE_PAGE 0xFF61
#define RAW_USAGE_ID 0x62
#define RAW_EPSIZE 64

#define DYNAMIC_KEYMAP_LAYER_COUNT 13

/* Status OLED — 128×64 */
#define OLED_DISPLAY_128X64

/* RGB matrix effects enabled for split72 */
#define RGB_MATRIX_FRAMEBUFFER_EFFECTS
#define RGB_MATRIX_KEYPRESSES
#define RGB_MATRIX_MAXIMUM_BRIGHTNESS 100

#define ENABLE_RGB_MATRIX_SOLID_REACTIVE_SIMPLE
#define ENABLE_RGB_MATRIX_SOLID_REACTIVE
#define ENABLE_RGB_MATRIX_SOLID_REACTIVE_WIDE
#define ENABLE_RGB_MATRIX_SOLID_REACTIVE_MULTIWIDE
#define ENABLE_RGB_MATRIX_SOLID_REACTIVE_CROSS
#define ENABLE_RGB_MATRIX_SOLID_REACTIVE_MULTICROSS
#define ENABLE_RGB_MATRIX_SOLID_REACTIVE_NEXUS
#define ENABLE_RGB_MATRIX_SOLID_REACTIVE_MULTINEXUS
#define ENABLE_RGB_MATRIX_SPLASH
#define ENABLE_RGB_MATRIX_MULTISPLASH
#define ENABLE_RGB_MATRIX_SOLID_SPLASH
#define ENABLE_RGB_MATRIX_SOLID_MULTISPLASH
#define ENABLE_RGB_MATRIX_ALPHAS_MODS
#define ENABLE_RGB_MATRIX_GRADIENT_UP_DOWN
#define ENABLE_RGB_MATRIX_GRADIENT_LEFT_RIGHT
#define ENABLE_RGB_MATRIX_BREATHING
#define ENABLE_RGB_MATRIX_BAND_SAT
#define ENABLE_RGB_MATRIX_BAND_VAL
#define ENABLE_RGB_MATRIX_BAND_PINWHEEL_SAT
#define ENABLE_RGB_MATRIX_BAND_PINWHEEL_VAL
#define ENABLE_RGB_MATRIX_BAND_SPIRAL_SAT
#define ENABLE_RGB_MATRIX_BAND_SPIRAL_VAL
#define ENABLE_RGB_MATRIX_CYCLE_ALL
#define ENABLE_RGB_MATRIX_CYCLE_LEFT_RIGHT
#define ENABLE_RGB_MATRIX_CYCLE_UP_DOWN
#define ENABLE_RGB_MATRIX_RAINBOW_MOVING_CHEVRON
#define ENABLE_RGB_MATRIX_CYCLE_OUT_IN
#define ENABLE_RGB_MATRIX_CYCLE_OUT_IN_DUAL
#define ENABLE_RGB_MATRIX_CYCLE_PINWHEEL
#define ENABLE_RGB_MATRIX_CYCLE_SPIRAL
#define ENABLE_RGB_MATRIX_DUAL_BEACON
#define ENABLE_RGB_MATRIX_RAINBOW_BEACON
#define ENABLE_RGB_MATRIX_RAINBOW_PINWHEELS
#define ENABLE_RGB_MATRIX_RAINDROPS
#define ENABLE_RGB_MATRIX_JELLYBEAN_RAINDROPS
#define ENABLE_RGB_MATRIX_HUE_BREATHING
#define ENABLE_RGB_MATRIX_HUE_PENDULUM
#define ENABLE_RGB_MATRIX_HUE_WAVE
#define ENABLE_RGB_MATRIX_PIXEL_FRACTAL
#define ENABLE_RGB_MATRIX_PIXEL_FLOW
#define ENABLE_RGB_MATRIX_PIXEL_RAIN

#define RGB_MATRIX_HUE_STEP 2
#define RGB_MATRIX_SAT_STEP 2
#define RGB_MATRIX_VAL_STEP 1
#define RGB_MATRIX_SPD_STEP 1
