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

/* Key matrix — 4 rows per side, 6 columns (standard CRKBD layout) */
#define MATRIX_ROWS_PER_SIDE 4
#define MATRIX_ROWS          8
#define MATRIX_COLS          6

#define LAYOUT_TO_INDEX(row, col) ((row) * MATRIX_COLS + (col))

/* Shift registers — 3×8=24 bits, covers 21 keycap displays per side */
#define NUM_SHIFT_REGISTERS 3

/* Matrix pin assignments — update to match actual PCB schematic */
#define MATRIX_COL_PINS \
    { GP10, GP11, GP12, GP13, GP14, GP15 }
#define MATRIX_ROW_PINS \
    { GP18, GP19, GP20, GP21 }

/* SPI interface for keycap displays — adjust if wired differently from split72 */
#define SPI_DRIVER   SPID0
#define SPI_SS_PIN   GP17
#define SPI_DC_PIN   GP8
#define HW_RST_PIN   GP9
#define SPI_SCK_PIN  GP6
#define SPI_MOSI_PIN GP7
#define SPI_MISO_PIN GP4
#define SPI_DIVISOR  (CPU_CLOCK / 10000000)

/* Shift register select pins */
#define SR_CLK_PIN   GP27
#define SR_DATA_PIN  GP26
#define SR_LATCH_PIN GP28

/* Rotary encoder */
#define ENCODER_RESOLUTION 2

/* Status OLED — 128×32 SSD1306 (half-height vs split72's 128×64) */
#define OLED_DISPLAY_128X32

/* No RGB matrix — corne42 has no underglow or per-key LEDs */

#define RAW_USAGE_PAGE 0xFF61
#define RAW_USAGE_ID   0x62
#define RAW_EPSIZE     64

#define DYNAMIC_KEYMAP_LAYER_COUNT 13
