// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// FW-9 signature gate for the DOOM engine pack: the DECISION, separated from the
// flash reads and the branch into the image so it can be tested on the host.
//
// The pack is executable code loaded over the same unauthenticated HID transport
// as any resource, so `doom_pack_load()` authenticates it before computing the
// entry pointer. What this header decides is the one question that is not a
// cryptographic one: WHAT TO DO when the signature does not check out.
//
// ⚠️ UNSIGNED and INVALID are opposite events, and the asymmetry is the whole
// point — the same rule the firmware image has followed since FW-2. A pack with
// no signature at all is a developer build; offering it a physical yes/no costs
// nothing an attacker could not already do by standing at the keyboard. A pack
// whose signature is present and WRONG is a tampered artifact, and offering a
// keypress there hands an attacker the one thing the physical gate exists to
// withhold. So: unsigned may prompt, invalid never does.
//
// ⚠️ The second axis is whether anybody is THERE. `doom_pack_load()` runs from two
// kinds of entry: a deliberate one (the armed KC_IDDQD item on the utilities
// layer — a keypress, so a finger is on the board) and an automatic one (the idle
// screensaver, and the slave half's mirror session). A prompt raised on the
// automatic path would put a dialog on 72 keycaps with nobody to answer it, in
// place of the screensaver it was supposed to introduce. So only a deliberate
// entry prompts; the idle path keeps refusing outright, exactly as before.
//
// Those two axes are why this is a table rather than an `if`. The FW-9 refusal
// itself has not moved: an unsigned pack that nobody authorises still does not
// run.

#include <stdbool.h>
#include <stdint.h>
#include <assert.h>   // static_assert: a C11 macro, a C++ keyword — the host tests compile this as C++

// What the signature trailer says about the pack sitting in the slot.
enum doom_pack_sig {
    DOOM_PACK_SIG_VALID = 0,  // checks out against FW_SIGNING_PUBKEY
    DOOM_PACK_SIG_BLANK,      // all 0x00/0xFF — never signed (a local build, or erased flash)
    DOOM_PACK_SIG_INVALID,    // present and wrong: a re-targeted or corrupted signed pack
};

// How the load was reached.
enum doom_pack_entry {
    DOOM_PACK_ENTRY_INTERACTIVE = 0,  // the armed KC_IDDQD keypress — a finger is on the board
    DOOM_PACK_ENTRY_AUTOMATIC,        // idle screensaver / slave mirror — nobody is there
};

// What the loader should do about it.
enum doom_pack_verdict {
    DOOM_PACK_LOAD = 0,  // branch into the image
    DOOM_PACK_REFUSE,    // log and fall back (no game; the idle pipeline picks another style)
    DOOM_PACK_PROMPT,    // raise the on-keycap A/ACCEPT — R/REJECT dialog, refuse for now
};

// The physical answer, once given, is remembered for the rest of the BOOT and
// bound to the pack it was given for.
//
// ⚠️ Bound to the image CRC, not a bare flag: accepting pack A must not silently
// authorise pack B flashed over it afterwards. Re-flashing re-prompts, which is
// what a developer iterating on the engine actually wants — each new build is a
// new decision, and it costs one keypress.
//
// ⚠️ RAM-only, deliberately. Persisting it would turn one keypress into a
// permanent bypass of FW-9 that survives power loss, which is the property the
// signature exists to deny. A reboot means a fresh decision.
typedef struct {
    bool     valid;      // an answer has been given this boot
    uint32_t image_crc;  // …for the pack carrying this CRC
} doom_pack_auth_t;

static inline bool doom_pack_authorised(const doom_pack_auth_t *auth, uint32_t image_crc) {
    return auth != 0 && auth->valid && auth->image_crc == image_crc;
}

// The whole gate, as one pure function.
static inline enum doom_pack_verdict doom_pack_gate(enum doom_pack_sig   sig,
                                                    enum doom_pack_entry entry,
                                                    const doom_pack_auth_t *auth,
                                                    uint32_t             image_crc) {
    if (sig == DOOM_PACK_SIG_VALID) {
        return DOOM_PACK_LOAD;
    }
    // Tampered: never negotiable, on either entry path, authorised or not. An
    // authorisation covers "this developer build has no signature", never "this
    // signature is wrong" — those are different claims about the same bytes.
    if (sig == DOOM_PACK_SIG_INVALID) {
        return DOOM_PACK_REFUSE;
    }
    // Unsigned from here on.
    if (doom_pack_authorised(auth, image_crc)) {
        return DOOM_PACK_LOAD;
    }
    return (entry == DOOM_PACK_ENTRY_INTERACTIVE) ? DOOM_PACK_PROMPT : DOOM_PACK_REFUSE;
}

// A signature that is entirely 0x00 or 0xFF was never written: erased flash reads
// 0xFF, and a pack that stops at image_size leaves whatever the slot held. Both
// mean "no signature", which is a different verdict from a wrong one — so this
// classification is load-bearing, not a logging nicety.
//
// ⚠️ Ask this BEFORE spending any crypto. A blank trailer cannot verify, so
// running Ed25519 (a SHA-512 over ~210 KB, ~0.4 s) to discover that is pure
// waste — and it is waste on the half that must answer split transactions
// promptly. On hardware that cost an 8-second split-link outage: the slave
// retried the load every housekeeping pass, each retry blocked it for ~0.5 s,
// and the transactions that failed included the very sync carrying the
// authorisation that would have ended the loop.
static inline bool doom_pack_trailer_is_blank(const uint8_t *sig, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        if (sig[i] != 0x00 && sig[i] != 0xFF) {
            return false;
        }
    }
    return true;
}

// `ok` is the verifier's verdict, and callers only need to compute it when the
// trailer is not blank.
static inline enum doom_pack_sig doom_pack_classify(bool ok, const uint8_t *sig, uint32_t len) {
    if (ok) {
        return DOOM_PACK_SIG_VALID;
    }
    return doom_pack_trailer_is_blank(sig, len) ? DOOM_PACK_SIG_BLANK : DOOM_PACK_SIG_INVALID;
}

// A valid signature outranks everything, including the entry path: a release pack
// must never need a keypress.
static_assert(DOOM_PACK_LOAD == 0, "LOAD is the zero verdict — callers test it as such");
