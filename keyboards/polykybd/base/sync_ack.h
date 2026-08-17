// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// The split-link ACK VOCABULARY, on purpose in a header of its own.
//
// These four byte values plus sync_succeeded() are a protocol CONTRACT, not part
// of the sync payload structs. They used to live in split_sync.h, which needs
// config.h (BYTES_PER_SEGMENT), state.h (poly_sync_t) and mru.h (MRU_CAP) for its
// struct definitions — so a caller that only wanted to ask "is this ack a
// success?" had to drag in the entire keyboard configuration. That is why
// sync_succeeded(), which guards every send_to_bridge() call site and is itself
// the subject of a field bug (see the "never bool-test send_to_bridge()" note in
// CLAUDE.md), had no unit test for years.
//
// This header depends on stdint/stdbool and nothing else, so the contract can be
// tested standalone. split_sync.h includes it, so every existing consumer is
// unchanged.

#include <stdint.h>
#include <stdbool.h>

#define SYNC_ACK_SIG    0b01001101
#define SYNC_ACK        0b11001010
#define SYNC_CRC32_ERR  0b00110101
// The slave ANSWERED and refused the request (its own validation failed), as
// opposed to SYNC_CRC32_ERR, which cannot say which of three things happened:
//   * "still erasing"        — flash_stage_begin's re-poll state, a NORMAL state
//                              hid_fw_up actually counts to log erase progress
//   * "your frame arrived garbled" — a request-CRC mismatch, which SHOULD be retried
//   * "no answer at all"     — send_to_bridge exhausting its retries
// A refusal needs the opposite response from all three: re-flash, don't retry.
// Collapsing it into SYNC_CRC32_ERR is why a font-pack COMMIT could tell the host
// "retry me" for a half whose flash was genuinely bad. Do NOT repurpose
// SYNC_CRC32_ERR for this — the begin re-poll depends on its existing meaning.
//
// The value is not arbitrary: the reply carries NO CRC (see the split-link notes in
// CLAUDE.md), so these bytes are spaced by Hamming distance to survive a flipped
// bit. 0xB2 is the complement of SYNC_ACK_SIG, making the set two complement pairs;
// it ties for the best achievable min-distance (4) from all three existing values
// and is balanced (popcount 4), unlike the equally-spaced 0x00/0xFF, which are
// exactly what a stuck or floating line reads as.
//
// ⚠️ Adding a fifth value: keep the min pairwise Hamming distance at 4 or the
// single-bit-error tolerance above degrades for the WHOLE set, silently. There are
// 12 spare codewords that preserve it (0x00 and 0x1B among them, though avoid the
// all-zero/all-one ones for the stuck-line reason above). SyncAckTest.
// AckValuesStayHammingSpaced in base/tests/ enforces this — it is not advice.
#define SYNC_NACK_REFUSED 0b10110010

typedef struct _poly_sync_reply_t {
    uint8_t ack;
} poly_sync_reply_t;

// send_to_bridge() returns the slave's reply ack byte, or SYNC_CRC32_ERR after
// exhausting its retries. SYNC_NACK_REFUSED needs no case here — it is not an ACK,
// so every existing "not ACK == failure" caller already treats it correctly; only a
// caller that wants to tell a refusal from a link drop looks at it specifically.
// ALL of SYNC_ACK / SYNC_ACK_SIG / SYNC_CRC32_ERR are
// non-zero, so a bare truthiness test — `if(!send_to_bridge(...))` — is NOT a
// failure check: it is false for the failure value too, so the caller would
// treat a give-up as success and advance its global snapshot, never re-firing
// the lost sync. Always classify the return through this helper.
//
// It is deliberately a WHITELIST (fail-closed): a new failure value is a failure
// at all 14 existing call sites the moment it exists, with no edits. Never invert
// it into a blacklist of known failures.
static inline bool sync_succeeded(uint8_t ack) {
    return ack == SYNC_ACK || ack == SYNC_ACK_SIG;
}
