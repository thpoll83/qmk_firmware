// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// The decision behind fw_sig_verify(), with the key and the Ed25519 check passed in
// so it can be tested without the keyboard. fw_staging.c calls it with
// FW_SIGNING_PUBKEY and monocypher's crypto_ed25519_check().
//
// ⚠️ The all-zero key is NOT an inert value that simply fails every check. Its
// encoding is y=0, which decodes to a point that is genuinely ON the curve with
// ORDER 4, and crypto_eddsa_check_equation() has no low-order-key rejection. With an
// order-4 A, [h]A depends only on h mod 4, so an attacker can land a forgery in a
// handful of attempts. A placeholder key therefore makes enforcement FORGEABLE
// rather than fail-closed, so it is refused before the crypto is asked at all.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FW_SIG_KEY_LEN 32

// Same shape as monocypher's crypto_ed25519_check(): 0 = valid, -1 = not.
typedef int (*fw_sig_check_fn)(const uint8_t signature[64], const uint8_t public_key[FW_SIG_KEY_LEN], const uint8_t *message, size_t message_size);

// Is a real key compiled in, or the all-zero placeholder fw_pubkey.h ships before
// `gen_signing_key.py` has been run?
static inline bool fw_sig_key_provisioned(const uint8_t key[FW_SIG_KEY_LEN]) {
    uint8_t acc = 0;
    for (size_t i = 0; i < FW_SIG_KEY_LEN; i++) {
        acc |= key[i];
    }
    return acc != 0;
}

// True only for a provisioned key AND a signature the check accepts. The check is
// never called for the placeholder key.
static inline bool fw_sig_verify_with(const uint8_t key[FW_SIG_KEY_LEN], fw_sig_check_fn check, const uint8_t signature[64], const void *msg, size_t len) {
    if (!fw_sig_key_provisioned(key)) return false;
    return check(signature, key, (const uint8_t *)msg, len) == 0;
}
