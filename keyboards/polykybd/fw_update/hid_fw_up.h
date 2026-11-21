// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "quantum.h"

// HID firmware-update command IDs (command byte in the HID report)
#define CMD_FW_UP_BEGIN       0x40
#define CMD_FW_UP_CHUNK       0x41
#define CMD_FW_UP_COMMIT      0x42
#define CMD_FW_UP_GET_VERSION 0x43
#define CMD_FW_UP_APPLY       0x44
#define CMD_FW_UP_SIGNATURE   0x45   // FW-2: 64-byte Ed25519 image signature, sent in 2 parts before COMMIT

// Handle HID firmware-update commands (0x40–0x45).
// Called from raw_hid_receive() when data[1] is in this range.
// Returns true if the command was handled (response already sent), false otherwise.
bool hid_fw_up_receive(uint8_t *data, uint8_t length);
