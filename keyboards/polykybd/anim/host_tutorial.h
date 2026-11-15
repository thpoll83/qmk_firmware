// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// HOST TUTORIAL (HID cmd 49, protocol v35): the firmware binding. The pure parse,
// matching and watchdog are base/host_tutorial_plan.h, which documents the wire format.
//
// The state is RAM only, on both halves. The slave learns it the way the rules say host
// state reaches it (SPLIT_SYNC.md → "Host state goes to the slave as the report"): the
// master forwards the TEXT, SPOTLIGHT, REVEAL and END reports on
// USER_SYNC_DYNAMIC_KEYMAP_DATA, and both halves apply the same bytes. A forwarded report
// is sent once, so the master resends the last TEXT and SPOTLIGHT state, and an END,
// from housekeeping until the slave acks it.
//
// Conditions (ARM / POLL) live on the master only: only the master runs process_record.
#pragma once
#include <stdbool.h>
#include <stdint.h>

// Master: cmd 49 from raw_hid_receive().
void host_tutorial_hid(uint8_t *data, uint8_t length);

// Slave, on the split-protocol thread: a forwarded cmd 49 report (data[0] 'P'). Copies
// it; host_tutorial_tick() applies it.
void host_tutorial_slave_receive(const uint8_t *data, uint8_t length);

// Housekeeping, both halves: apply a forwarded report (slave), resend to the slave, the
// watchdog and the layer conditions (master), and end a finished reveal.
void host_tutorial_tick(void);

// Master, from process_record_user(): a key event for the KEY and KEYCODE conditions.
void host_tutorial_note_key(uint8_t row, uint8_t col, uint16_t keycode, bool pressed);

// Drawing, both halves.
bool host_tutorial_text_active(void);
// Line i (0 or 1): its bytes and length. Not NUL-terminated.
const char *host_tutorial_line(uint8_t i, uint8_t *len);
// True when the spotlight (or a reveal front that has not reached it) darkens this key.
// (row, col) is the board matrix position.
bool host_tutorial_key_hidden(uint8_t row, uint8_t col);
