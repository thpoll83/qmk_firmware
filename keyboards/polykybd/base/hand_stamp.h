// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Handedness that survives an EEPROM wipe.
//
// EE_HANDS stores the left/right marker in the emulated EEPROM, and
// `is_keyboard_left()` is literally `!!eeprom_read_byte(EECONFIG_HANDEDNESS)`
// (split_util.c -> nvm_eeconfig.c). That byte has no "unknown" state: a cleared
// store reads zero, and zero is a perfectly valid `right`. So an EEPROM loss does
// not present as an error, it presents as a half that quietly comes up on the
// wrong side.
//
// And the store CAN be lost whole. QMK's wear-levelling recovery is
// all-or-nothing: a checksum mismatch on the consolidated area calls
// wear_leveling_clear_cache(), and a torn log entry returns WEAR_LEVELING_FAILED
// which makes wear_leveling_init() clear it too. Either way all 8 KB read back as
// zeros. The firmware's one routine EEPROM write is save_all_dirty() at the end of
// suspend_power_down_kb(), which fires when the USB bus goes idle -- including
// when a hub drops, a few milliseconds before the rail collapses. That is the
// 2026-09-07 field report: hub interrupted, left half came up as `right master`
// with the RGB matrix on and the dynamic keymap reset, all three from one wipe.
//
// The fix is to keep the authoritative copy somewhere the wear-levelling driver
// cannot reach: one flash sector we own, below the crash archive
// (FW_HAND_STAMP_OFFSET). Handedness changes essentially never -- only when
// somebody assigns the sides -- so a dedicated sector costs one erase per sixteen
// changes and no wear worth thinking about.
//
// Written APPEND-style, a page at a time, for the torn-write case specifically:
// a partially programmed page fails its CRC and the scan falls back to the
// previous good page, whereas erase-then-rewrite-in-place would leave nothing at
// all if power died in the window. The failure this module exists to survive is
// exactly a write interrupted by power loss, so the write itself must survive it.
#pragma once

#include <stdbool.h>
#include <stdint.h>

// One page holds one stamp; 16 pages per 4 KB sector.
typedef struct __attribute__((packed)) {
    uint32_t magic;    // POLY_HAND_STAMP_MAGIC
    uint8_t  is_left;  // 1 = this half is the left one
    uint8_t  pad[3];   // 0 -- keeps `crc` word-aligned and the layout explicit
    uint32_t crc;      // crc32 over the bytes before it
} poly_hand_stamp_t;

#define POLY_HAND_STAMP_MAGIC 0x48414E44UL  // 'HAND'

// --- the decision -----------------------------------------------------------
// Pure, so the ordering can be tested without a flash part (the same seam as
// base/fw_up_verdict.c and base/map_codec.h). Everything below it is I/O.
typedef struct {
    bool is_left;    // the handedness to use for this boot
    bool migrate;    // no stamp yet and the EEPROM looks trustworthy: stamp it now
    bool repair_ee;  // the stamp disagrees with EEPROM: rewrite the EEPROM byte
} poly_hand_decision_t;

// `ee_enabled` is eeconfig_is_enabled() read BEFORE split_pre_init() gets a
// chance to run eeconfig_init(). It is the only thing that separates "this board
// is genuinely right-handed" from "this store was just wiped", because the
// handedness byte reads zero in both cases.
static inline poly_hand_decision_t poly_hand_decide(bool stamp_valid, bool stamp_is_left, bool ee_enabled, bool ee_is_left) {
    poly_hand_decision_t d = {.is_left = ee_is_left, .migrate = false, .repair_ee = false};
    if (stamp_valid) {
        // The stamp outranks EEPROM unconditionally -- that IS the feature. A
        // disagreement means the EEPROM byte was lost (or reset by
        // eeconfig_init()), so put it back rather than leaving the two to
        // disagree for every stock QMK path that reads it directly.
        d.is_left   = stamp_is_left;
        d.repair_ee = (ee_is_left != stamp_is_left);
    } else if (ee_enabled) {
        // No stamp yet: every board flashed before this existed. Adopt what the
        // EEPROM says WHILE it still looks healthy -- this is the one chance to
        // capture the value before a wipe makes it unrecoverable.
        d.migrate = true;
    }
    // else: no stamp AND the store is blank or wiped. Whatever the byte says is a
    // guess, so use it (unchanged behaviour) but do NOT stamp it -- cementing a
    // wrong side is worse than leaving the board where it already is.
    return d;
}

// Where this boot's handedness came from. Reported by the boot banner rather than
// printed when it is worked out: that happens in pre_init, long before a console
// can be attached, and this is diagnostic information for exactly the failure the
// module exists to survive -- printing it where nobody can read it is no use.
typedef enum {
    POLY_HAND_SRC_STAMP    = 0,  // read back from the flash stamp
    POLY_HAND_SRC_MIGRATED = 1,  // no stamp yet; adopted from a healthy EEPROM and stamped
    POLY_HAND_SRC_EEPROM   = 2,  // no stamp AND the store looked blank: an unstamped guess
} poly_hand_source_t;

poly_hand_source_t poly_hand_source(void);
bool               poly_hand_ee_repaired(void);

// --- I/O --------------------------------------------------------------------
// Resolve + migrate. Call once from keyboard_pre_init_user(), BEFORE
// split_pre_init() runs eeconfig_init(). No core1 lockout is taken because core1
// has not been launched yet -- the same constraint crash_record_init() documents.
void poly_hand_boot_init(void);

// Rewrite the EEPROM handedness byte when it disagreed with the stamp. Deferred
// to keyboard_post_init_user() on purpose: split_pre_init() calls
// is_keyboard_left_impl(), whose EE_HANDS branch runs `if (!eeconfig_is_enabled())
// eeconfig_init();` -- an erase of the whole store. A repair written before that
// would be wiped by it.
void poly_hand_post_init(void);

// The resolved handedness. Safe to call at any time: it resolves on first use, so
// it is correct even if something reads it before poly_hand_boot_init().
bool poly_hand_is_left(void);

// Record a handedness change (HID cmd 25 on the master, the reset-sync carrier on
// the slave). Deliberately does no flash or EEPROM work: the slave's caller is a
// split-transaction handler with a ~20 ms budget. poly_hand_flush_pending() does
// the writing.
void poly_hand_set_pending(bool is_left);

// Write a pending handedness change to the EEPROM byte and the stamp. Call from
// the main loop, before the reboot that adopts it. No-op when nothing is pending.
void poly_hand_flush_pending(void);
