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

#include "quantum.h"

#define LAYOUT( \
    k00, k01, k02, k03 \
) { \
    { k00, k01 }, \
    { k02, k03 } \
}

//shift register for display selection
void sr_init(void);
void sr_shift_out(uint8_t val);
void sr_shift_out_latch(uint8_t val);

//spi utility functions
void spi_prepare_commands(void);
void spi_prepare_data(void);
void spi_reset(void);
