// Copyright 2026 Thomas Pollak
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "base/macro_record.h"

// Recording a macro ON THE KEYBOARD, with no host app: the state machine, the capture
// shim and the chunked commit.
//
// The arithmetic lives in base/macro_record.c and is unit-tested against RAM; this file
// is the half that cannot be — the host-driver wrapper, the EEPROM writes and the
// timers. Same split as base/fw_up_verdict.c beside split_fw_up.c, and for the same
// reason: the part with a bug future is the part that has to be reachable from a test.
//
// The gesture and the decisions behind it are in MACRO_RECORD_DESIGN.md.

// The staging buffer. Nothing reaches EEPROM until the recording stops: a write per
// keystroke is a wear-levelling journal append, and the consolidation erase it
// eventually triggers lands in the middle of live typing -- the documented mechanism
// behind the "slave becomes unresponsive" field bug.
//
// 192 B is roughly 50-90 keystrokes (3 bytes a tap, 6 a hold, plus delays), against a
// ~2.2 KB shared body region. A recording that fills it stops cleanly rather than
// truncating, because the encoder reserves room to close every held key.
#ifndef POLY_MACRO_REC_BYTES
#    define POLY_MACRO_REC_BYTES 192
#endif

// Where the gesture is. Synced (poly_sync_t.rec_state) because the SLAVE draws its own
// half of the slot picker and only ever sees that struct -- the same reason fw_confirm,
// settings_more and the remap prompt are in there.
enum poly_rec_state {
    POLY_REC_IDLE = 0,
    POLY_REC_PICKING,     // the board is a slot picker; pressing a macro key arms it
    POLY_REC_RECORDING,   // keys pass through to the host AND are captured
    POLY_REC_SAVING,      // the splice is being pumped, a chunk per housekeeping pass
    POLY_REC_SAVED,       // a brief "Saved Mn" so the pause reads as a step, not a hang
};

// Toggle the gesture: IDLE -> PICKING -> (a slot) -> RECORDING -> IDLE (via SAVING).
// Pressing REC while PICKING cancels, so the gesture always has a way out on the key
// that started it.
void poly_macro_rec_toggle(void);

// Arm slot `id` from the picker. Ignored unless PICKING.
void poly_macro_rec_pick(uint8_t id);

// Stop and save. An empty recording leaves the slot alone rather than clearing it --
// storing nothing is a destructive answer to an accidental REC.
void poly_macro_rec_stop(void);

// Discard without saving. Refused while SAVING: cancelling mid-splice would leave the
// shared buffer marked incomplete and half moved.
void poly_macro_rec_cancel(void);

enum poly_rec_state poly_macro_rec_state(void);
uint8_t             poly_macro_rec_slot(void);
uint16_t            poly_macro_rec_bytes(void);   // captured so far
uint32_t            poly_macro_rec_elapsed_ms(void);
static inline bool  poly_macro_rec_busy(void) { return poly_macro_rec_state() != POLY_REC_IDLE; }

// Pump: installs the capture shim on the first call, drives the commit a chunk at a
// time, and expires the SAVED banner. Call from housekeeping on the master.
//
// ⚠️ The shim is installed HERE and not in keyboard_post_init_user(), which runs
// BEFORE protocol_post_init() (quantum/main.c) -- so a driver installed there is
// overwritten by the USB stack a moment later and the recording silently captures
// nothing. Installing from the first housekeeping pass is after both.
void poly_macro_rec_tick(void);
