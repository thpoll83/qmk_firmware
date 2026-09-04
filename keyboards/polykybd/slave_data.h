// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// USER_SYNC_SLAVE_DATA: the ONE generic slave->master pull channel (see the
// transaction-table note in config.h). The master's request is a single `kind`
// byte selecting which slave-side payload to return; the slave answers into the
// reply buffer, bounded by out_len. Append a kind + a case in slave_data.c to
// carry more slave-side data over the same split slot -- no new transaction.
//
// Both halves run the same image, so the per-kind payload structs can change
// freely (no split versioning).
#pragma once

#include <stdint.h>

enum slave_data_kind {
    SLAVE_DATA_SENSOR = 0,  // ltr559_sync_t: {avg lux, proximity}   (POLYKYBD_LTR559_DRIVE)
    SLAVE_DATA_CRASH  = 1,  // crash_record: [flags][poly_crash_record_t] (crash_record.h)
};

// Registers the split handler. Call once from keyboard_post_init_user(),
// alongside the other transaction registrations, on BOTH halves (the handler
// only ever runs on the slave; the master initiates the exec).
void slave_data_register(void);

// Master side, every housekeeping pass: pull the slave's crash record once per
// link-up (bounded retries, spaced out) and hand it to crash_record_note_slave().
// No-op on the slave.
void slave_data_crash_pull_tick(void);
