// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
// See slave_data.h.
#include "slave_data.h"

#include QMK_KEYBOARD_H
#include "quantum.h"
#include "transactions.h"
#include "split_util.h"        // is_transport_connected()
#include "bridge_helper.h"     // is_usb_host_side()
#include "base/crash_record.h"
#include "print.h"           // uprintf (the crash-test pull diagnostic)
#ifdef POLYKYBD_CRASH_TEST
#    include "crash_test.h"
#endif
#ifdef POLYKYBD_LTR559_DRIVE
#    include "ltr559_policy.h"
#endif

#include <string.h>

// Slave side: answer the master's pull for the requested `kind`.
static void user_sync_slave_data_handler(uint8_t in_len, const void *in_data, uint8_t out_len, void *out_data) {
    uint8_t kind = (in_len >= 1) ? ((const uint8_t *)in_data)[0] : SLAVE_DATA_SENSOR;
    switch (kind) {
#ifdef POLYKYBD_LTR559_DRIVE
        case SLAVE_DATA_SENSOR:
            poly_ltr559_slave_sensor_reply(out_data, out_len);
            break;
#endif
        case SLAVE_DATA_CRASH:
            crash_record_slave_reply((uint8_t *)out_data, out_len);
            break;
#ifdef POLYKYBD_CRASH_TEST
        case SLAVE_DATA_CRASH_TEST:
            crash_test_slave_fault();   // does not return: records and reboots
#endif
        default:
            break;  // unknown kind: leave the reply buffer as-is
    }
}

void slave_data_register(void) {
    transaction_register_rpc(USER_SYNC_SLAVE_DATA, user_sync_slave_data_handler);
}

// ---------------------------------------------------------------------------
// The crash-record pull. Once per link establishment: the slave's record is
// captured at ITS boot and only changes at its next boot, which the master sees
// as a link drop + re-establishment. Spaced retries because the slave may still
// be settling right after the transport reports connected.
// ---------------------------------------------------------------------------
#define CRASH_PULL_TRIES       3u
#define CRASH_PULL_SPACING_MS  2000u
#define SLAVE_EMIT_REPEATS     4u
#define SLAVE_EMIT_SPACING_MS  1500u
static uint8_t  s_emits   = 0;
static uint32_t s_emit_at = 0;

#ifdef POLYKYBD_CRASH_TEST
// A crash-test request KNOWS a slave record is coming, so it must not depend on
// the master OBSERVING a link drop: whether a slave reboot presents as a
// disconnect at all depends on SPLIT_MAX_CONNECTION_ERRORS (200) accumulating
// before the slave is back, which is a race. Keep retrying for a bounded window
// instead of spending the three ordinary tries into a half-rebooted slave.
#    define CRASH_FORCE_WINDOW_MS 30000u
static bool     s_forcing     = false;
static uint32_t s_force_start = 0;
#endif

// File scope rather than function statics so the crash-test request below can
// re-arm the pull directly (see slave_data_request_crash_test).
static bool     s_linked = false;
static uint8_t  s_tries  = 0;
static uint32_t s_last   = 0;

void slave_data_crash_pull_tick(void) {
    if (!is_usb_host_side()) return;

    if (s_emits > 0 && timer_elapsed32(s_emit_at) >= SLAVE_EMIT_SPACING_MS) {
        s_emit_at = timer_read32();
        s_emits--;
        crash_record_emit_slave_line();
    }

    const bool linked = is_transport_connected();
    if (!linked) {
        // Re-arm for the next link-up (a slave reboot presents exactly like this).
        s_linked = false;
        s_tries  = 0;
        return;
    }
    if (!s_linked) {
        s_linked = true;
        s_tries  = 0;
        s_last   = timer_read32();
        return;   // give the freshly-linked slave one spacing before the first pull
    }
    bool forcing = false;
#ifdef POLYKYBD_CRASH_TEST
    if (s_forcing && timer_elapsed32(s_force_start) >= CRASH_FORCE_WINDOW_MS) s_forcing = false;
    forcing = s_forcing;
#endif
    if (!forcing && s_tries >= CRASH_PULL_TRIES) return;
    if (timer_elapsed32(s_last) < CRASH_PULL_SPACING_MS) return;
    s_last = timer_read32();
    s_tries++;

    uint8_t kind = SLAVE_DATA_CRASH;
    uint8_t reply[CRASH_HID_BODY_LEN];
    memset(reply, 0, sizeof(reply));
    uint32_t tag = crash_phase_enter(CRASH_PHASE_BRIDGE, USER_SYNC_SLAVE_DATA);
    bool ok = transaction_rpc_exec(USER_SYNC_SLAVE_DATA, sizeof(kind), &kind, sizeof(reply), reply);
    crash_phase_leave(tag);
    const bool noted   = ok && crash_record_note_slave(reply, sizeof(reply));
    const bool present = noted && (reply[0] & CRASH_HID_FLAG_PRESENT) != 0;
#ifdef POLYKYBD_CRASH_TEST
    // One line per forced pull, so a round on hardware says WHICH half of this
    // is failing instead of leaving four indistinguishable silences: the RPC not
    // landing, the slave holding no record, a record that is not fresh, or a
    // record that was reported and the console missed.
    if (forcing) {
        uprintf("crash-test: slave pull #%u ok=%u flags=0x%02X\n",
                (unsigned)s_tries, (unsigned)ok, (unsigned)(ok ? reply[0] : 0u));
    }
#endif
    // ONLY a record actually present ends the pull. crash_record_note_slave()
    // returns true for an empty reply too (it clears the master's cached copy),
    // so testing it alone let a pull that landed BEFORE the slave had archived
    // -- the normal case right after a slave reboot -- count as done and close
    // the window with nothing reported.
    if (present) {
        s_tries = CRASH_PULL_TRIES;   // done for this link-up
        // Schedule a few repeats of the slave's line. It is printed once at the
        // pull and the console can drop a read, which loses the record for good;
        // the master's own line survives that because the boot banner repeats it.
        s_emits    = SLAVE_EMIT_REPEATS;
        s_emit_at  = timer_read32();
#ifdef POLYKYBD_CRASH_TEST
        s_forcing = false;
#endif
    }
}

#ifdef POLYKYBD_CRASH_TEST
void slave_data_request_crash_test(void) {
    // Open the forced retry window (see CRASH_FORCE_WINDOW_MS). Deliberately does
    // NOT touch s_linked/s_tries: clearing s_tries here would spend the three
    // ordinary tries into a slave that is still rebooting, and the drop-detection
    // path already re-arms those correctly if the master does see the disconnect.
    s_forcing     = true;
    s_force_start = timer_read32();
    s_last        = timer_read32();   // give the slave one spacing to reboot first

    uint8_t kind = SLAVE_DATA_CRASH_TEST;
    uint8_t reply[CRASH_HID_BODY_LEN];
    memset(reply, 0, sizeof(reply));
    // Expected to return false: the slave faults inside the handler, so the
    // transaction times out after its retries. Nothing to do with the result.
    (void)transaction_rpc_exec(USER_SYNC_SLAVE_DATA, sizeof(kind), &kind, sizeof(reply), reply);
}
#endif
