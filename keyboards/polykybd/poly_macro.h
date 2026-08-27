// Copyright 2026 Thomas Pollak
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdbool.h>
#include <stdint.h>

// Dynamic macros with a keycap legend.
//
// Storage is QMK's own dynamic-macro buffer (a run of NUL-terminated bodies at
// DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR) plus a fixed-stride label array carved off the top
// of the same region -- see config.h for why the two are separate structures rather
// than one.
//
// Playback is OURS, not QMK's. dynamic_keymap_macro_send() runs the whole macro inline
// and implements its delay as `while (ms--) wait_ms(1)`. On a single-controller board
// that is merely rude; here the same loop scans the matrix, drives the split UART to
// the other half, services USB HID and pushes 72 SPI displays, so a macro with a half-
// second delay would freeze the board, drop the split link and stall the keycaps
// mid-render. poly_macro_tick() executes at most one step per housekeeping pass.

#define POLY_MACRO_NONE 0xFF

// Spacing between steps. TAP_CODE_DELAY is 0 on this board, which would run steps
// back-to-back -- still one per main-loop pass, so still yielding, but fast enough that
// some hosts drop events. 8 ms is comfortably above USB polling without being felt.
#ifndef POLY_MACRO_STEP_MS
#    define POLY_MACRO_STEP_MS 8
#endif

// ---------------------------------------------------------------------------
// Bodies

// Total body bytes available (labels excluded) and how many are in use.
uint16_t poly_macro_capacity(void);
uint16_t poly_macro_bytes_used(void);

// Read/write a window of the body buffer. Both clamp to the body region, so neither
// can reach the labels.
void poly_macro_read(uint16_t offset, uint16_t size, uint8_t *out);
void poly_macro_write(uint16_t offset, uint16_t size, const uint8_t *data);

// Zero every body and every label.
// Written into the buffer's last byte while a body write is in flight. Any non-zero
// value works -- poly_macro_buffer_intact() only asks "is the final byte NUL" -- and it
// is the same marker the host raises before it streams.
#define POLY_MACRO_INCOMPLETE 0xFF

void poly_macro_reset_all(void);

// ---------------------------------------------------------------------------
// Labels

// Labels live in a RAM cache on BOTH halves, not just in EEPROM. Two reasons, and the
// second is the binding one: the render path reads a label for every macro keycap on
// every refresh, and the slave has no other way to know them at all -- the host writes
// macros to the master, and the slave's own EEPROM never sees them.
//
// Copies macro `id`'s label into `out` as a NUL-terminated string. `out` must hold at
// least POLY_MACRO_LABEL_LEN + 1 bytes. An empty label yields "".
void poly_macro_label_get(uint8_t id, char *out);

// Stores up to POLY_MACRO_LABEL_LEN bytes, NUL-padded, and queues it for the slave.
// Non-ASCII bytes are dropped: the _Nano_ face covers 0x20..0x7E only, so anything else
// would render as nothing and read as a bug. Rejecting at the door means what is stored
// is what is drawable.
void poly_macro_label_set(uint8_t id, const char *text);

// Fill the RAM cache from EEPROM. Master only -- called once at boot.
void poly_macro_labels_load(void);

// Slave side: adopt a label pushed over the split link. RAM only; the slave never
// persists a label, because the master is authoritative and re-pushes every boot.
void poly_macro_label_adopt(uint8_t id, const char *text);

// Master side: push at most ONE queued label to the slave. Call from housekeeping --
// never inline in the HID handler, where sixteen bridges of up to ten retries each
// would be seconds of dead main loop on exactly the bad link that needed the retries.
// Returns true when it sent something, so the caller can see the queue draining.
bool poly_macro_label_sync_tick(void);

// Queue every label for the slave. Used at boot, once the link is up: the slave comes
// up with an empty cache and nothing else would ever fill it.
void poly_macro_labels_mark_all_dirty(void);

// ---------------------------------------------------------------------------
// Playback

// Start macro `id`. Returns false when the id is out of range, the macro is empty, or
// the buffer looks mid-write. Aborts any macro already running.
bool poly_macro_start(uint8_t id);

// Stop immediately and release everything the macro registered.
void poly_macro_abort(void);

bool poly_macro_active(void);

// Advance at most one step. Call from housekeeping on the master.
void poly_macro_tick(void);
