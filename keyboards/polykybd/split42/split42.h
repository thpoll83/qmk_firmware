/*
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

#include QMK_KEYBOARD_H
#include "quantum/keymap_introspection.h"

#include "../base/fonts/gfxfont.h"

#include <stdint.h>
#include <stdbool.h>


struct display_info {
    uint8_t bitmask[NUM_SHIFT_REGISTERS];  /* split42: 3 shift registers (vs split72's 5) */
};

/*
 * BITMASK macros select ONE keycap display by pulling one shift-register output
 * low (all others high). split42 has 3 registers, so 3 mask bytes. bitmask[0] is
 * the byte shifted in LAST (farthest from the MCU in the chain) — same convention
 * as split72's 5-register macros.
 */
#define BITMASK1(x) .bitmask = {~0, ~0, (uint8_t)(~(1<<(x)))}
#define BITMASK2(x) .bitmask = {~0, (uint8_t)(~(1<<(x))), ~0}
#define BITMASK3(x) .bitmask = {(uint8_t)(~(1<<(x))), ~0, ~0}

// Variant parameters consumed by the shared poly_keymap.c:
//   POLY_DISP_ROW_0 / _3 — the shift-register seed update_displays() latches to
//                          select the FIRST keycap display of the row-0 scan and of
//                          the second-half (row-3) scan, then shifts across.
//   POLY_SPLASH_*        — the right-half boot splash (128x32 status OLED, 2 rows).
//
// split42's 6-column matrix occupies only the low 6 bits of each 8-bit shift
// register (the 2 high bits carry the thumb OLEDs), so the split72-style scan
// (one 0-bit walked one shift per column) under-shifts by 2 per row and mis-aligns
// every row below row 0. POLY_DISP_SELECT_BY_TABLE switches the shared render/idle
// scans (poly_keymap.c) to select each key straight from key_display[] instead — the
// SAME table keypress-invert uses — so render and invert can never drift and the
// only place physical-wiring knowledge lives is that one table. Consequently
// POLY_DISP_ROW_0/_3 are unused for split42 (the seed is a no-op in table mode);
// they are kept only to satisfy the shared declarations.
//
// ⚠️ HARDWARE-VERIFY (bench): the THUMB entries in key_display[] (split42.c) are still
// a best guess — press each thumb, note which OLED inverts, and correct that one
// table. The letter rows are confirmed correct.
#define POLY_DISP_SELECT_BY_TABLE 1
#define POLY_DISP_ROW_0    BITMASK1(0)
#define POLY_DISP_ROW_3    BITMASK3(2)
#define POLY_SPLASH_R1     U"SPLIT"
#define POLY_SPLASH_R2     U" 4 2"
#define POLY_SPLASH_R2_ROW 2

// The picker row is only 6 positions per half (12 total), and the right block is
// full, so the two page arrows have to come out of the left block's spare pair —
// which is why they sit on the outer LEFT on both variants rather than one at each
// end. That leaves 10 variation slots here vs 12 on split72; the stored pick is an
// ABSOLUTE variation index either way, so the two page sizes cost nothing.
#define LATIN_PICKER_SLOTS 10

// Board name reported in the GET_ID (cmd 6) string, so the host shows the right
// variant. hid_com.c falls back to "Split72" when a variant doesn't define this,
// so split72's GET_ID stays byte-identical (it relies on that fallback).
#define POLY_KB_NAME "Split42"

void invert_display(uint8_t r, uint8_t c, bool state);

const uint8_t* get_key_disp_bitmask(uint8_t index);

uint8_t get_disp_bitmask_size(void);
