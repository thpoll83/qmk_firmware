// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Pure DECISION layer for the flash-staging COMMIT outcome.
//
// The classification lived inline in split_fw_up.c's fw_up_slave_refused_commit(),
// mixed in with the RPC call and its uprintf() lines. That made the one part with a
// bug history — deciding whether a failed COMMIT means "the data is bad" or "the
// answer got lost" — the one part no test could reach, because reaching it needed
// the split transport, the keyboard config and a slave.
//
// So the decision moved here: no RPC, no logging, no globals. split_fw_up.c keeps
// the I/O and the diagnostics and calls in for the verdict. This mirrors what the
// host repo already does deliberately (polyhost/core/decisions.py,
// decide_stale_bundles, classify_commit_reply) — pure seams so the decisions can be
// tested; the firmware simply never had them.
//
// Depends on sync_ack.h + fw_staging.h (both dependency-free) and crc32_1byte.

#include <stdint.h>
#include <stdbool.h>

#include "sync_ack.h"
#include "fw_staging.h"

// Why a COMMIT that did not come back SYNC_ACK failed. The two need OPPOSITE
// responses, which is the whole reason this distinction exists: re-flashing a
// dropped ack wastes tens of KB over a busy link, and retrying genuinely bad data
// just fails again.
enum fw_up_commit_verdict {
    FW_UP_COMMIT_REFUSED,       // the slave processed it and said no  -> re-flash
    FW_UP_COMMIT_LINK_FAILURE,  // the answer never arrived            -> retry COMMIT (free)
};

// True when the ack byte alone settles it, so the caller must NOT spend a STATUS
// RPC. Only a refusal is self-describing. SYNC_GIVEUP and SYNC_CRC32_ERR both still
// need the probe — neither says whether the slave's finalize ran, which is the whole
// question. Keeping this separate from the classification below is what lets the
// caller short-circuit before doing I/O inside raw_hid_receive(), which runs on the
// main loop.
bool fw_up_commit_ack_is_self_describing(uint8_t slave_ack);

// Classify a non-ACK COMMIT.
//   slave_ack    — what came back: SYNC_GIVEUP when send_to_bridge exhausted its
//                  retries, SYNC_CRC32_ERR when the slave got a garbled frame,
//                  SYNC_NACK_REFUSED when it answered and refused
//   status_ok    — a CRC-VALID status snapshot was obtained from the slave
//   recorded_ack — that snapshot's last_commit_ack (ignored when !status_ok)
//
// ⚠️ fw_up_active is deliberately NOT an input. finalize clears it whether it
// succeeded or refused, so "cleared" is equally consistent with a refusal and with
// a SUCCESS whose ack was lost in transit — treating that as a refusal turns the
// common lost-ack case into a full re-stream. Only the recorded verdict can tell
// them apart.
enum fw_up_commit_verdict fw_up_classify_commit_failure(uint8_t slave_ack, bool status_ok,
                                                        uint8_t recorded_ack);

// The STATUS snapshot's own integrity check. A snapshot decides whether a flash
// landed, so a corrupt one must be refused rather than believed — the reply rides
// the same un-CRC'd link as everything else.
bool fw_up_status_crc_ok(const fw_staging_status_t *status, uint32_t claimed_crc);
