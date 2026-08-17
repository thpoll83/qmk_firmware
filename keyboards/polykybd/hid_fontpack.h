// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// stdint/stdbool only — deliberately NOT quantum.h. Nothing here needs it (the one
// declaration below uses uint8_t and bool), and keeping the header standalone is what
// lets the COMMIT status contract be unit-tested without the keyboard config. The
// host's classify_commit_reply() tests the same contract from its side; a firmware
// test is what catches this end EMITTING the wrong byte, which a host fixture cannot.
#include <stdint.h>
#include <stdbool.h>

// HID font-pack command IDs (command byte in the HID report). Parallel to the
// firmware-update commands (0x40–0x44) but target the external-flash font pack.
#define CMD_FONTPACK_BEGIN   0x50  // data[2..5]=pack_size, data[6..9]=pack_crc32 (whole pack)
#define CMD_FONTPACK_CHUNK   0x51  // data[2..5]=offset, data[6..61]=FW_UP_CHUNK_SIZE bytes
#define CMD_FONTPACK_COMMIT  0x52  // verify CRC from flash + reload (no reboot); reply[3..4]=content_version

// COMMIT reply status byte (reply[2]). Three outcomes, deliberately distinguishable:
// a link failure and a data failure need opposite responses from the host, and a
// single '!' for both made a perfectly-flashed bundle read as corrupt (see the long
// comment at CMD_FONTPACK_COMMIT in hid_fontpack.c).
#define FONTPACK_COMMIT_OK        '.'  // both halves FINALIZED (see the reload note below)
#define FONTPACK_COMMIT_REJECTED  'R'  // master finalize refused the image (staged CRC / invalid PlyF)
#define FONTPACK_COMMIT_NO_SLAVE  'L'  // master committed (reply[3..4] valid), slave did not ACK — retry COMMIT
// reply[3..4] = the slot's content_version, written whenever the MASTER's copy is
// live ('.' and 'L'). ⚠️ FONT bundles only: the DOOMWAD/DOOMPACK pseudo-bundles have
// no font-pack version, so those bytes stay 0 and the host does not read them (its
// flash_doomwad/flash_doompack discard the reply).
// ⚠️ '.' means both halves FINALIZED, not "the slave has rebuilt its font table".
// The slave defers the ~50 ms reload to housekeeping deliberately —
// fw_staging_finalize_defer_reload() must not do heavy work inside the split
// transaction (that overran the window and mis-reported COMMIT as a CRC failure
// once already). It ACKs on the O(1) transport CRC, which proves byte-identity
// with the master's already-verified pack, so the deferred reload cannot fail;
// it lands within a housekeeping tick.
// ⚠️ 'L' cannot tell a LOST ack from an explicit slave REFUSAL: both
// bridge_helper's retry give-up and the slave's own finalize failure surface as
// SYNC_CRC32_ERR. Distinguishing them needs a new slave→master ack value in
// poly_sync_reply_t, which every bridge consumer shares — not done here. The host
// therefore treats 'L' as "the other half is unverified" and re-flashes the bundle
// on the next pass rather than trusting it.
// A host that predates these reads any non-'.' as a failure, and firmware that
// predates them answers '!', which a new host treats as "unspecified" — so the pair
// degrades gracefully in both directions and needs no PROTOCOL_VERSION bump (the
// font-pack commands are dispatched independently of it).
// Pick the COMMIT status byte. Pure, so the host↔firmware contract is testable from
// this end too (base/tests/fw_up_verdict_tests.cpp).
//   master_ok     — the master's own finalize accepted the image
//   slave_ok      — the slave answered SYNC_ACK
//   slave_refused — the slave answered/recorded a REFUSAL. Only meaningful when
//                   !slave_ok && master_ok; the caller passes false otherwise,
//                   because it deliberately does NOT spend a STATUS RPC when the
//                   master already rejected the image (the answer is 'R' either way)
//                   or when the ack is self-describing.
// ⚠️ A master rejection outranks everything: there is no point telling the host to
// retry a half whose own copy is bad.
static inline uint8_t fontpack_commit_status(bool master_ok, bool slave_ok, bool slave_refused) {
    if (master_ok && slave_ok)      return FONTPACK_COMMIT_OK;
    if (!master_ok || slave_refused) return FONTPACK_COMMIT_REJECTED;
    return FONTPACK_COMMIT_NO_SLAVE;
}

#define CMD_FONTPACK_STATUS  0x53  // reply: [3]=present [4]=abi [5..6]=content_version [7]=font_count

// Handle HID font-pack commands (0x50–0x53). Called from raw_hid_receive() when
// data[1] is in this range. Returns true if handled (response already sent).
bool hid_fontpack_receive(uint8_t *data, uint8_t length);
