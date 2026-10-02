// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// When the SLAVE's crash record stops reading "fresh".
//
// A record's FRESH bit means "the boot before this one ended in this crash", and
// the slave's bit is relative to the SLAVE's boot. The master copies it into its
// own cache and into the cmd 39 reply, so when the master alone reboots and the
// slave stays up, the master's next pull gets the same record still marked fresh.
// The master then prints `crash: side=slave` again and the host alerts on a crash
// it already showed.
//
// The fix is an acknowledgement. Once the host has read a fresh slave record over
// cmd 39, the master sends the slave a SLAVE_DATA_CRASH_ACK naming that record by
// its CRC. From then on the slave reports it as not fresh, for the rest of the
// slave's boot. The CRC binds the ack to the record it was given for: an ack that
// arrives after the slave rebooted into a different record changes nothing.
//
// Host-read, not master-pull, is what acknowledges. A master that pulled the
// record while no host was running must not retire it, or a host started after a
// master-only reboot would never hear of the crash at all.
//
// Pure (no quantum.h, no flash), so it is unit-tested in
// base/tests/crash_ack_tests.cpp.
#pragma once

#include <stdbool.h>
#include <stdint.h>

// Slave side: which record, if any, the master has acknowledged this boot.
typedef struct {
    bool     acked;
    uint32_t crc;   // the acknowledged record's crc field
} crash_ack_t;

// Slave side: the FRESH bit to send for its own record.
static inline bool crash_ack_slave_fresh(bool fresh, const crash_ack_t *ack, uint32_t rec_crc) {
    return fresh && !(ack->acked && ack->crc == rec_crc);
}

// Slave side: apply an ack for `acked_crc`. Applied only when it names the record
// this half holds; returns whether it was.
static inline bool crash_ack_apply(crash_ack_t *ack, bool have_record, uint32_t rec_crc,
                                   uint32_t acked_crc) {
    if (!have_record || acked_crc != rec_crc) return false;
    ack->acked = true;
    ack->crc   = rec_crc;
    return true;
}

// Master side: an ack waiting to be sent to the slave.
typedef struct {
    bool     pending;
    uint32_t crc;
} crash_ack_pending_t;

// Master side: the host read the slave's record (cmd 39, half 1). Queue an ack
// only for a record that was present and fresh; returns whether one was queued.
static inline bool crash_ack_note_host_read(crash_ack_pending_t *p, bool have_slave,
                                            bool slave_fresh, uint32_t slave_crc) {
    if (!have_slave || !slave_fresh) return false;
    p->pending = true;
    p->crc     = slave_crc;
    return true;
}
