// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

#include "base/hand_stamp.h"

// QMK's weak is_keyboard_left() hands back split_config.left, which split_pre_init()
// would fill from the EEPROM handedness byte -- and a wiped store makes that byte
// read zero, i.e. a confident "right". Route every reader through the flash stamp, so
// an EEPROM loss cannot move a half to the other side. This is the ONE choke point:
// QMK's matrix row ownership, the RGB clipping range and our own set_side() geometry
// all come through here, so nothing can be left reading the old source by accident.
// hand_stamp.h has the failure this defends against.
bool is_keyboard_left(void) {
    return poly_hand_is_left();
}

// ...and the impl QMK caches into split_config.left at split_pre_init(), so the
// cached copy agrees with the answer above instead of holding a dead, wrong one.
// Overriding this is also what lets config.h drop EE_HANDS: the stock body's
// EE_HANDS branch runs `if (!eeconfig_is_enabled()) eeconfig_init()` -- an erase
// of the whole store -- on its way to a decision the stamp has already made.
// Both are overridden rather than just this one, because is_keyboard_left() is
// the weak accessor over a value that is only filled at split_pre_init: anything
// reading it before then would get the zero-initialised `right`, which is the
// exact class of silent wrong answer this module exists to remove.
bool is_keyboard_left_impl(void) {
    return poly_hand_is_left();
}

#ifdef POLYKYBD_HIL
#    include "usb_util.h"

// HIL test station only (enabled by the CI build via `-e POLYKYBD_HIL=left|right`).
//
// On the test rig both halves are cabled to the host's USB, so each half reads
// 5 V on USB_VBUS_PIN (GP24). QMK's default master detection is purely VBUS
// based, so *both* halves detect themselves as master and the split link never
// forms. The Raspberry Pi 4's built-in USB ports cannot drop that VBUS via
// uhubctl, so power switching can't disambiguate the halves either.
//
// The two halves are identical hardware and the rig provisions no handedness at
// all, so the role cannot be read from stored state (an unprovisioned half reads
// as "not left", which would make *both* halves slaves and leave zero masters). Instead the role is fixed at compile time per side: the
// station flashes the POLYKYBD_HIL=left image (master) to the left board and the
// POLYKYBD_HIL=right image (slave) to the right board. This runs in
// split_pre_init(), before the split transport is up.
//
// Lives here at the keyboard level so split72 and split42 share one
// implementation. Normal keyboards never define POLYKYBD_HIL and keep stock
// VBUS detection, so plugging USB into either half still makes that half the
// master.
bool is_keyboard_master_impl(void) {
#    ifdef POLYKYBD_HIL_SLAVE
    // Right half: drop its USB so it does not enumerate as a second keyboard on
    // the test host and confuse HID discovery.
    usb_disconnect();
    return false;
#    else
    // Left half: always master.
    return true;
#    endif
}
#else
#    include "usb_util.h"

// Normal builds. A rev3+ board keeps the stock rule (USB_VBUS_PIN, GP24): the half
// that sees VBUS is the master. A Rev.2 board passes VBUS over the split cable, so
// GP24 reads high on both halves and both would be master; its stamp carries
// POLY_HAND_FLAG_SPLIT_VBUS (hand_stamp.h), and then the half that ENUMERATES is the
// master. That is QMK's SPLIT_USB_DETECT loop, with its default 2 s timeout, so on a
// Rev.2 board the slave waits 2 s at boot before it gives up. Rev3 boards never wait.
#    define POLY_SPLIT_USB_TIMEOUT_MS 2000
#    define POLY_SPLIT_USB_POLL_MS 10

static bool usb_enumerated_within_timeout(void) {
    for (uint16_t i = 0; i < (POLY_SPLIT_USB_TIMEOUT_MS / POLY_SPLIT_USB_POLL_MS); i++) {
        if (usb_connected_state()) return true;
        wait_ms(POLY_SPLIT_USB_POLL_MS);
    }
    return false;
}

bool is_keyboard_master_impl(void) {
    bool split_vbus = (poly_hand_board_flags() & POLY_HAND_FLAG_SPLIT_VBUS) != 0;
    bool is_master  = split_vbus ? poly_master_decide(true, false, usb_enumerated_within_timeout())
                                 : poly_master_decide(false, usb_vbus_state(), false);
    // Same as the stock body: the detection can leave USB enabled on the slave.
    if (!is_master) {
        usb_disconnect();
    }
    return is_master;
}
#endif // POLYKYBD_HIL
