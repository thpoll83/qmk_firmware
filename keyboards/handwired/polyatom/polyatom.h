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

void next_rgb_matrix_effect(void);

uint8_t keycode_to_disp_index(uint16_t keycode);

// key updates

void update_performed(void);

void set_last_key(uint16_t keycode);

//layer helpers

void force_layer_switch(void);

void next_layer(int8_t num_layers);

void prev_layer(int8_t num_layers);
