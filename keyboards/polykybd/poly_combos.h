// Copyright 2026 Thomas Pollak
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Runtime combos and tap dance (HID cmd 48, protocol v33): the firmware side of the
// store. The record format is base/combo_store.h; the implementation, the RAM cost
// and the way to reduce it are described at the top of poly_combos.c.
//
// POLY_RUNTIME_COMBOS is defined by rules.mk for every build except the DOOM
// monolith; without it every call below is an inline no-op.

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "base/combo_store.h"

// poly_combos_commit()'s result bits (defined in every build: hid_com.c reads them).
#define POLY_COMBOS_STORED_HERE  0x01u
#define POLY_COMBOS_STORED_OTHER 0x02u

#ifdef POLY_RUNTIME_COMBOS

// Loads the sector into RAM and builds QMK's tables. Call once at boot, on both halves.
void poly_combos_init(void);

// One record as cmd 48 sends it (POLY_COMBO_REC_BYTES, little-endian). false for a
// kind or index out of range, or (set) a record that fails validation.
bool poly_combo_record_get(uint8_t kind, uint8_t index, uint8_t out[POLY_COMBO_REC_BYTES]);
bool poly_combo_record_set(uint8_t kind, uint8_t index, const uint8_t in[POLY_COMBO_REC_BYTES]);

// Writes the RAM records to this half's flash and sends them to the other half.
// Main loop only. Returns POLY_COMBOS_STORED_HERE when this half's flash read back
// correctly, plus POLY_COMBOS_STORED_OTHER when the other half's flash matches.
uint8_t poly_combos_commit(void);

// The per-dance tapping term for a TD(n) keycode, or 0 for "use the global term".
uint16_t poly_tap_dance_term(uint16_t keycode);

// Slave side: the split handler records, housekeeping writes (poly_combos.c).
bool poly_combos_slave_record(uint8_t kind, uint8_t index, const uint8_t rec[POLY_COMBO_REC_BYTES]);
void poly_combos_slave_commit(void);
bool poly_combos_slave_check(uint32_t master_crc);
// Housekeeping, both halves: the slave's pending flash write, a deferred table
// rebuild, and on the master the periodic check that the other half has the same
// records (sending them again when it has not).
void poly_combos_drain_pending(void);

#else

static inline void poly_combos_init(void) {}
static inline bool poly_combo_record_get(uint8_t kind, uint8_t index, uint8_t out[POLY_COMBO_REC_BYTES]) { (void)kind; (void)index; (void)out; return false; }
static inline bool poly_combo_record_set(uint8_t kind, uint8_t index, const uint8_t in[POLY_COMBO_REC_BYTES]) { (void)kind; (void)index; (void)in; return false; }
static inline uint8_t poly_combos_commit(void) { return 0; }
static inline uint16_t poly_tap_dance_term(uint16_t keycode) { (void)keycode; return 0; }
static inline bool poly_combos_slave_record(uint8_t kind, uint8_t index, const uint8_t rec[POLY_COMBO_REC_BYTES]) { (void)kind; (void)index; (void)rec; return false; }
static inline void poly_combos_slave_commit(void) {}
static inline bool poly_combos_slave_check(uint32_t master_crc) { (void)master_crc; return false; }
static inline void poly_combos_drain_pending(void) {}

#endif
