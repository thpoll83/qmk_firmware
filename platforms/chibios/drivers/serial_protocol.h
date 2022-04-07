// Copyright 2022 Stefan Kerkmann
// SPDX-License-Identifier: GPL-2.0-or-later

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#pragma once

void driver_clear(void);
void driver_master_init(void);
void driver_slave_init(void);
bool __attribute__((nonnull)) receive(uint8_t* destination, const size_t size);
bool __attribute__((nonnull)) receive_blocking(uint8_t* destination, const size_t size);
bool __attribute__((nonnull)) send(const uint8_t* source, const size_t size);

#if defined(SOFT_SERIAL_PIN)
#    define SERIAL_USART_TX_PIN SOFT_SERIAL_PIN
#endif

#if !defined(SERIAL_USART_TX_PIN)
#    if defined(MCU_RP)
#        define SERIAL_USART_TX_PIN GP0
#    else
#        define SERIAL_USART_TX_PIN A9
#    endif
#endif

#if !defined(SERIAL_USART_RX_PIN)
#    if defined(MCU_RP)
#        define SERIAL_USART_RX_PIN GP1
#    else
#        define SERIAL_USART_RX_PIN A10
#    endif
#endif

#if !defined(SELECT_SOFT_SERIAL_SPEED)
#    define SELECT_SOFT_SERIAL_SPEED 1
#endif

#if defined(SERIAL_USART_SPEED)
// Allow advanced users to directly set SERIAL_USART_SPEED
#elif SELECT_SOFT_SERIAL_SPEED == 0
#    define SERIAL_USART_SPEED 460800
#elif SELECT_SOFT_SERIAL_SPEED == 1
#    define SERIAL_USART_SPEED 230400
#elif SELECT_SOFT_SERIAL_SPEED == 2
#    define SERIAL_USART_SPEED 115200
#elif SELECT_SOFT_SERIAL_SPEED == 3
#    define SERIAL_USART_SPEED 57600
#elif SELECT_SOFT_SERIAL_SPEED == 4
#    define SERIAL_USART_SPEED 38400
#elif SELECT_SOFT_SERIAL_SPEED == 5
#    define SERIAL_USART_SPEED 19200
#else
#    error invalid SELECT_SOFT_SERIAL_SPEED value
#endif

#if !defined(SERIAL_USART_TIMEOUT)
#    define SERIAL_USART_TIMEOUT 20
#endif
