/* Copyright 2019
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#define PK_MIN(x, y) (((x) < (y)) ? (x) : (y))
#define PK_MAX(x, y) (((x) > (y)) ? (x) : (y))
#define PK_ABS(x) ((x) < 0 ? (-(x)) : (x))
#define PK_POW(val, x, n) \
    val = 1; \
    for (uint8_t i = 0; i < n; ++i) { val *= x;}

#include "quantum.h"

