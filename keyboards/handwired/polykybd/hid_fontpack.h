// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "quantum.h"

// HID font-pack command IDs (command byte in the HID report). Parallel to the
// firmware-update commands (0x40–0x44) but target the external-flash font pack.
#define CMD_FONTPACK_BEGIN   0x50  // data[2..5]=pack_size, data[6..9]=content_version
#define CMD_FONTPACK_CHUNK   0x51  // data[2..5]=offset, data[6..61]=FW_UP_CHUNK_SIZE bytes
#define CMD_FONTPACK_COMMIT  0x52  // verify CRC from flash + reload (no reboot)
#define CMD_FONTPACK_STATUS  0x53  // report present / abi / content_version / expected_abi

// Handle HID font-pack commands (0x50–0x53). Called from raw_hid_receive() when
// data[1] is in this range. Returns true if handled (response already sent).
bool hid_fontpack_receive(uint8_t *data, uint8_t length);
