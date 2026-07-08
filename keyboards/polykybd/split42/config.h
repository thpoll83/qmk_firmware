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

/* Reported in the GET_ID string (cmd 6) so the host shows the right variant. */
#define POLY_KB_NAME "Split42"

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
// The keycap SSD1306s are write-only, so SPI never needs MISO. GP4 is the
// split-serial RX (SERIAL_USART_RX_PIN, shared config.h) — NO_PIN keeps
// spi_master from muxing GP4 away from the PIO serial. (Was GP4, a leftover
// from the pre-full-duplex era when GP4 was unused.)
#define SPI_MISO_PIN NO_PIN
#define SPI_DIVISOR  (CPU_CLOCK / 10000000)

/* Shift register select pins */
#define SR_CLK_PIN   GP27
#define SR_DATA_PIN  GP26
#define SR_LATCH_PIN GP28

/* Rotary encoder */
#define ENCODER_RESOLUTION 2

/* Status OLED — 128×32 SSD1306 (half-height vs split72's 128×64) */
#define OLED_DISPLAY_128X32

/* No RGB matrix — split42 has no underglow or per-key LEDs */

#define RAW_USAGE_PAGE 0xFF61
#define RAW_USAGE_ID   0x62
#define RAW_EPSIZE     64

// Host-remappable layers (VIA-style dynamic keymap in EEPROM) are 0..8 — see the
// DYNAMIC_KEYMAP_UPDATE_MAX_LAYER_COUNT write cap below. Layers 9..12 (_SL, _LL,
// _ADDLANG1, _EMJ) are served straight from the compiled keymap in flash by
// poly_keycode_at()/keymap_key_to_keycode() in keymap.c, so they can't be remapped and
// always reflect the flashed firmware (no keymap reset, no stale layout). QMK requires
// DYNAMIC_KEYMAP_LAYER_COUNT >= the number of compiled layers, so it stays 13 even
// though only 0..8 are actually dynamic.
#define DYNAMIC_KEYMAP_LAYER_COUNT 13

// Pin the dynamic-keymap / POLY custom-config base to a FIXED EEPROM address so that
// growing EECONFIG_USER_DATA_SIZE (e.g. adding MRU/feature data) does NOT relocate the
// stored keymap and force a "reset keymap" on every flash. We reserve a fixed user-data
// budget rather than tracking the live EECONFIG_USER_DATA_SIZE; the actual size must stay
// <= the reservation (static_assert in state.h). Bumping the reservation relocates the
// keymap once (one final reset), then it stays put across firmware updates.
#define POLY_EECONFIG_USER_RESERVED 128
#define DYNAMIC_KEYMAP_EEPROM_ADDR  (EECONFIG_BASE_SIZE + EECONFIG_KB_DATA_SIZE + POLY_EECONFIG_USER_RESERVED)
#define POLY_EEPROM_MAGIC_ADDR      DYNAMIC_KEYMAP_EEPROM_ADDR
