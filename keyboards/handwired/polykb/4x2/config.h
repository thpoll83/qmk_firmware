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
#define MATRIX_ROWS 2
#define MATRIX_COLS 4

#define DIODE_DIRECTION ROW2COL

#define LAYOUT(k00, k01, k02, k03, k04, k05, k06, k07) { { k00, k01 , k02, k03 }, { k04, k05 , k06, k07 } }

#define LAYOUT_TO_INDEX(row, col) ((row)*MATRIX_COLS+(col))

#define ENCODER_RESOLUTION 2

#define DRIVER_LED_TOTAL 8

#define NUM_SHIFT_REGISTERS 2
