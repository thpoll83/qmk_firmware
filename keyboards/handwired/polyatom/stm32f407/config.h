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

#include "config_common.h"

#define MATRIX_COL_PINS { A2, A3 }
#define MATRIX_ROW_PINS { A4, A5 }

#define I2C_DRIVER I2CD1
#define I2C1_SCL_BANK GPIOB
#define I2C1_SCL 8

#define I2C1_SDA_BANK GPIOB
#define I2C1_SDA 9

#define I2C1_OPMODE OPMODE_I2C
#define I2C1_CLOCK_SPEED 100000
#define I2C1_DUTY_CYCLE STD_DUTY_CYCLE

#define RGB_DI_PIN C6
#define DRIVER_LED_TOTAL 10

#   define RGB_MATRIX_KEYPRESSES
// #   define RGB_MATRIX_FRAMEBUFFER_EFFECTS
#   define RGB_MATRIX_LED_PROCESS_LIMIT (DRIVER_LED_TOTAL + 4) / 5 // limits the number of LEDs to process in an animation per task run (increases keyboard responsiveness)
#   define RGB_MATRIX_LED_FLUSH_LIMIT 16 // limits in milliseconds how frequently an animation will update the LEDs. 16 (16ms) is equivalent to limiting to 60fps (increases keyboard responsiveness)
#    define RGB_MATRIX_MAXIMUM_BRIGHTNESS 25

//SPI interface to write to the selected display
#define SPI_DRIVER SPID2
#define  SPI_CS_PIN C7
#define  SPI_CD_PIN C5
#define  SPI_RST_PIN B12
#define  SPI_SCK_PIN B13
#define  SPI_SCK_PAL_MODE 5
#define SPI_MOSI_PIN B15
#define SPI_MOSI_PAL_MODE 5
#define SPI_MISO_PIN B14
#define SPI_MISO_PAL_MODE 5

//Shift register to select the display
#define SR_NMR_PIN D4
#define SR_CLK_PIN D5
#define SR_DATA_PIN D6
#define SR_LATCH_PIN D7
