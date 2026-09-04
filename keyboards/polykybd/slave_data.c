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

void slave_data_crash_pull_tick(void) {
    static bool     s_linked = false;
    static uint8_t  s_tries  = 0;
    static uint32_t s_last   = 0;

    if (!is_usb_host_side()) return;

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
    if (s_tries >= CRASH_PULL_TRIES) return;
    if (timer_elapsed32(s_last) < CRASH_PULL_SPACING_MS) return;
    s_last = timer_read32();
    s_tries++;

    uint8_t kind = SLAVE_DATA_CRASH;
    uint8_t reply[CRASH_HID_BODY_LEN];
    memset(reply, 0, sizeof(reply));
    uint32_t tag = crash_phase_enter(CRASH_PHASE_BRIDGE, USER_SYNC_SLAVE_DATA);
    bool ok = transaction_rpc_exec(USER_SYNC_SLAVE_DATA, sizeof(kind), &kind, sizeof(reply), reply);
    crash_phase_leave(tag);
    if (ok && crash_record_note_slave(reply, sizeof(reply))) {
        s_tries = CRASH_PULL_TRIES;   // done for this link-up
    }
}

#ifdef POLYKYBD_CRASH_TEST
void slave_data_request_crash_test(void) {
    uint8_t kind = SLAVE_DATA_CRASH_TEST;
    uint8_t reply[CRASH_HID_BODY_LEN];
    memset(reply, 0, sizeof(reply));
    // Expected to return false: the slave faults inside the handler, so the
    // transaction times out after its retries. Nothing to do with the result.
    (void)transaction_rpc_exec(USER_SYNC_SLAVE_DATA, sizeof(kind), &kind, sizeof(reply), reply);
}
#endif
