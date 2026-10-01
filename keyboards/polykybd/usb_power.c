// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#include "usb_power.h"

#include "quantum.h"
#include "hal.h"     // USBD1 — the same driver object boot_diag.c samples

static bool s_host_seen = false;
static bool s_logged    = false;

bool poly_usb_host_seen(void) {
    // configuration != 0: a host sent SET_CONFIGURATION. saved_state == USB_ACTIVE: the
    // bus was configured when it went to sleep — read directly so a suspend that lands
    // before housekeeping ever ran this still counts as a host. Latched, because a bus
    // reset clears both, and a host that reset and then slept is still a host.
    if (!s_host_seen &&
        (USBD1.configuration != 0 || USBD1.state == USB_ACTIVE || USBD1.saved_state == USB_ACTIVE)) {
        s_host_seen = true;
    }
    return s_host_seen;
}

bool poly_power_only(void) {
    return is_keyboard_master() && !poly_usb_host_seen();
}

// Overrides QMK's weak default (tmk_core/protocol/chibios/chibios.c): called on every
// main-loop pass while the bus reads SUSPENDED, and on every turn of the suspend loop.
bool usb_suspend_allowed_kb(void) {
    if (!poly_power_only()) return true;
    if (!s_logged) {
        s_logged = true;
        uprint("USB: no host has configured this half: power only, running without suspend\n");
    }
    return false;
}
