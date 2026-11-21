// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// This half's hardware ID: the 64-bit unique ID of its QSPI flash chip, read with
// the flash RUID command (0x4B). Each half has its own chip, so a keyboard has two
// IDs. Cmd 50 (protocol v37) returns both to the host, which hashes them for the
// telemetry device count (PolyKybdHost docs/telemetry-internals.md).
//
// The read is a flash command: it takes the chip out of XIP mode, so nothing may
// run from flash while it runs. It is done ONCE, from keyboard_setup(), where
// core1 has not been launched yet, with interrupts disabled, and the result is
// kept in RAM. poly_hw_uid() never touches the flash.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define POLY_HW_UID_LEN 8u

// Call once from keyboard_setup(), before multicore_launch_core1().
void poly_hw_uid_boot_init(void);

// Copies this half's ID into out[POLY_HW_UID_LEN]. Returns false (and zeroes
// out) when the boot read has not run, or the chip answered all 0x00 or all
// 0xFF, which is a chip without the command, not an ID.
bool poly_hw_uid(uint8_t out[POLY_HW_UID_LEN]);
