// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "quantum.h"

// HID font-pack command IDs (command byte in the HID report). Parallel to the
// firmware-update commands (0x40–0x44) but target the external-flash font pack.
#define CMD_FONTPACK_BEGIN   0x50  // data[2..5]=pack_size, data[6..9]=pack_crc32 (whole pack)
#define CMD_FONTPACK_CHUNK   0x51  // data[2..5]=offset, data[6..61]=FW_UP_CHUNK_SIZE bytes
#define CMD_FONTPACK_COMMIT  0x52  // verify CRC from flash + reload (no reboot); reply[3..4]=content_version

// COMMIT reply status byte (reply[2]). Three outcomes, deliberately distinguishable:
// a link failure and a data failure need opposite responses from the host, and a
// single '!' for both made a perfectly-flashed bundle read as corrupt (see the long
// comment at CMD_FONTPACK_COMMIT in hid_fontpack.c).
#define FONTPACK_COMMIT_OK        '.'  // both halves finalized; reply[3..4] = content_version
#define FONTPACK_COMMIT_REJECTED  'R'  // master finalize refused the image (staged CRC / invalid PlyF)
#define FONTPACK_COMMIT_NO_SLAVE  'L'  // master committed (reply[3..4] valid), slave did not ACK — retry COMMIT
// A host that predates these reads any non-'.' as a failure, and firmware that
// predates them answers '!', which a new host treats as "unspecified" — so the pair
// degrades gracefully in both directions and needs no PROTOCOL_VERSION bump (the
// font-pack commands are dispatched independently of it).
#define CMD_FONTPACK_STATUS  0x53  // reply: [3]=present [4]=abi [5..6]=content_version [7]=font_count

// Handle HID font-pack commands (0x50–0x53). Called from raw_hid_receive() when
// data[1] is in this range. Returns true if handled (response already sent).
bool hid_fontpack_receive(uint8_t *data, uint8_t length);
