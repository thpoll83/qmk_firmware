/* Copyright 2020 QMK
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

/*
 * This file was auto-generated by:
 *    `qmk chibios-confmigrate -i keyboards/handwired/polyatom/stm32f407/halconf.h -r platforms/chibios/common/configs/halconf.h`
 */

#pragma once

#define PAL_USE_WAIT TRUE
#define PAL_USE_CALLBACKS TRUE
#include_next <halconf.h>

#undef HAL_USE_I2C
#define HAL_USE_I2C TRUE

#undef HAL_USE_SPI
#define HAL_USE_SPI TRUE

#define SERIAL_PIO_USE_PIO1

//0	460800 baud
//1	230400 baud (default)
//2	115200 baud
//3	57600 baud
//4	38400 baud
//5	19200 baud
#define SELECT_SOFT_SERIAL_SPEED 0

#define PICO_FLASH_SIZE_BYTES (8 * 1024 * 1024)
//for split keyboard setup
//#define PAL_USE_CALLBACKS TRUE
