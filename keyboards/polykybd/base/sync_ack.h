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

// ── The vocabulary ──────────────────────────────────────────────────────────
// SUCCESS (sync_succeeded() below is the only correct way to test for it):
//   SYNC_ACK      the slave processed the request
//   SYNC_ACK_SIG  processed, and signalling something in-band (the flash-staging
//                 begin uses it for "erase started, keep polling")
// FAILURE — each needs a DIFFERENT response, which is why they are separate values:
//   SYNC_CRC32_ERR     the frame I received did not check out  -> retry, it may
//                      well arrive intact next time
//   SYNC_NACK_REFUSED  I processed it and the answer is NO     -> re-flash; asking
//                      again cannot change what is in my flash
//   SYNC_BUSY          a normal not-yet state (the flash-staging begin re-poll
//                      while the deferred erase runs)          -> keep polling
//   SYNC_GIVEUP        no answer was obtained at all: send_to_bridge exhausted its
//                      retries, or we never even asked         -> retry
//
// ⚠️ SYNC_CRC32_ERR used to mean ALL FOUR of the failure cases. That is what let a
// font-pack COMMIT tell the host "retry me" for a half whose flash was genuinely
// bad — a refusal and a dropped frame are opposite events. SYNC_NACK_REFUSED split
// off the refusal; SYNC_BUSY and SYNC_GIVEUP split off the other two, so the name
// SYNC_CRC32_ERR now finally means only what it says. Do not re-overload it.
#define SYNC_ACK_SIG      0b01001101
#define SYNC_ACK          0b11001010
#define SYNC_CRC32_ERR    0b00110101
#define SYNC_NACK_REFUSED 0b10110010
#define SYNC_BUSY         0b00011011
#define SYNC_GIVEUP       0b11100100

// ── Why these particular bytes ──────────────────────────────────────────────
// The reply carries NO CRC (see the split-link notes in CLAUDE.md), so the values
// are spaced by Hamming distance so a single flipped bit cannot turn one verdict
// into another. The set is built as COMPLEMENT PAIRS, each balanced at popcount 4:
//   SYNC_ACK 0xCA ↔ SYNC_CRC32_ERR 0x35   (distance 8)
//   SYNC_ACK_SIG 0x4D ↔ SYNC_NACK_REFUSED 0xB2
//   SYNC_BUSY 0x1B ↔ SYNC_GIVEUP 0xE4
// All six sit at min pairwise distance 4. 0x00/0xFF are deliberately avoided even
// though they are well spaced: they are exactly what a stuck or floating line reads
// as.
//
// ⚠️ Adding a SEVENTH value: keep the min pairwise distance at 4, or the
// single-bit tolerance degrades for the WHOLE set, silently. A mutually-distance-4
// code containing these six extends to 16 values, so 10 remain (8 after excluding
// 0x00/0xFF) — pick a complement of an unused one to keep the pattern.
// SyncAckTest.AckValuesStayHammingSpaced in base/tests/ enforces this; it is not
// advice, and it fails on a badly-chosen value rather than letting it ship.

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
