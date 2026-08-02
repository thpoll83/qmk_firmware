// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdint.h>

// ── Firmware-image signing public key (FW-2) ────────────────────────────────
// Ed25519 public key the firmware uses to verify a signed firmware image before
// applying it (see fw_staging.c / crypto/monocypher-ed25519.h). The matching
// PRIVATE key signs releases and must NEVER live in this repo.
//
// ⚠️ THIS IS A PLACEHOLDER (all-zero) KEY. No real signature can verify against
// it. Generate your real keypair ONCE with:
//
//     python3 keyboards/polykybd/tools/gen_signing_key.py
//             --out-pubkey  keyboards/polykybd/base/fw_pubkey.h
//             --out-privkey fw_signing_key.bin
//
// Keep `fw_signing_key.bin` secret (store it as the GitHub Actions secret
// FW_SIGNING_KEY for release signing, and locally for dev signing). Commit only
// the regenerated public-key header.
//
// Phase A (current): signature verification runs in warn-only mode — a missing
// or bad signature is logged but does NOT block the update, so flashing keeps
// working with this placeholder. Define FW_REQUIRE_SIGNATURE (see config.h /
// rules.mk) only AFTER a real key is provisioned and releases are signed.

static const uint8_t FW_SIGNING_PUBKEY[32] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
