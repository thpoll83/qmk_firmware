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
#define MATRIX_ROWS 5
#define MATRIX_COLS 4

#define DIODE_DIRECTION ROW2COL

#define LAYOUT(\
    k19, k18, k17, k16,\
    k15, k14, k13, k12,\
    k11, k10, k09, k08,\
    k07, k06, k05, k04,\
    k03, k02, k01, k00 \
  ) { \
    { k00, k01 , k02, k03 }, \
    { k04, k05 , k06, k07 }, \
    { k08, k09 , k10, k11 }, \
    { k12, k13 , k14, k15 }, \
    { k16, k17 , k18, k19 }, \
}

#define LAYOUT_TO_INDEX(row, col) ((row)*MATRIX_COLS+(col))

#define ENCODER_RESOLUTION 2

#define DRIVER_LED_TOTAL 20

// #define WS2812_PWM_DRIVER PWMD8  // default: PWMD2
// #define WS2812_PWM_CHANNEL 1  // default: 2
// #define WS2812_PWM_PAL_MODE 2  // Pin "alternate function", see the respective datasheet for the appropriate values for your MCU. default: 2
// //#define WS2812_PWM_COMPLEMENTARY_OUTPUT // Define for a complementary timer output (TIMx_CHyN); omit for a normal timer output (TIMx_CHy).
// #define WS2812_DMA_STREAM STM32_DMA1_STREAM2  // DMA Stream for TIMx_UP, see the respective reference manual for the appropriate values for your MCU.
// #define WS2812_DMA_CHANNEL 2  // DMA Channel for TIMx_UP, see the respective reference manual for the appropriate values for your MCU.
// #define WS2812_DMAMUX_ID STM32_DMAMUX1_TIM8_UP // DMAMUX configuration for TIMx_UP -- only required if your MCU has a DMAMUX peripheral, see the respectiv

#define NUM_SHIFT_REGISTERS 5
