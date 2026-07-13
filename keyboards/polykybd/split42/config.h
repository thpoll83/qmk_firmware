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


/* key matrix size — split42 is a CRKBD footprint: 4 rows per side, 6 columns.
   All pin assignments below are taken from the KiCad schematic
   (poly_corne/poly_corne_split42_left.kicad_sch): Col1..Col6 = GP10..GP15,
   Row1..Row4 = GP18..GP21. */
#define MATRIX_ROWS_PER_SIDE 4
#define MATRIX_ROWS 8
#define MATRIX_COLS 6

#define LAYOUT_TO_INDEX(row, col) ((row)*MATRIX_COLS+(col))


/* Shift registers select the active keycap display. split42 has 3 (24 outputs,
   21 keycap displays per side) vs split72's 5. Nets SR_DATA/SR_CLOCK/SR_LATCH. */
#define NUM_SHIFT_REGISTERS 3


#define MATRIX_COL_PINS \
    { GP10, GP11, GP12, GP13, GP14, GP15 }
#define MATRIX_ROW_PINS \
    { GP18, GP19, GP20, GP21 }


/* SPI interface to write to the selected keycap display — same wiring as split72
   (schematic nets: CS=GP17, D-C=GP8, RESET=GP9, SCK=GP6, MOSI=GP7). */
#define SPI_DRIVER SPID0
#define SPI_SS_PIN GP17
#define SPI_DC_PIN GP8
#define HW_RST_PIN GP9
#define SPI_SCK_PIN GP6
#define SPI_MOSI_PIN GP7
// Mirror split72 (GP4). NOTE from the schematic: GP4 carries SERIAL_COM1 (the
// split-serial RX) and there is no dedicated MISO net — the keycap SSD1306s are
// write-only. Left exactly as split72 has it; not changed here (baseline first).
#define SPI_MISO_PIN GP4

//This number can be calculated by dividing the MCU’s clock speed
//by the desired SPI clock speed. For example, an MCU running at 8 MHz
//wanting to talk to an SPI device at 4 MHz would set the divisor to 2
#define SPI_DIVISOR (CPU_CLOCK / 10000000) //rp1040 runs at 133Mhz, SPI at 10Mhz

// Shift register to select the display (schematic nets SR_DATA/SR_CLOCK/SR_LATCH)
#define SR_CLK_PIN GP27
#define SR_DATA_PIN GP26
#define SR_LATCH_PIN GP28


/* Rotary encoder — schematic nets ENC_A=GP2, ENC_B=GP3 (declared in keyboard.json) */
#define ENCODER_RESOLUTION 2

/* split42 has NO Cirque trackpad and NO RGB matrix — the split72 pointing-device
   and RGB blocks are intentionally omitted. */

#define RAW_USAGE_PAGE 0xFF61
#define RAW_USAGE_ID 0x62
#define RAW_EPSIZE 64

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

/* Status OLED — 128×32 (split72 is 128×64). NOTE: on this hardware rev the
   intended I2C0 status-OLED bus (GP0/GP1) is not broken out, so the status OLED
   is currently unconnected; the shared config.h I2C defaults apply. */
#define OLED_DISPLAY_128X32
