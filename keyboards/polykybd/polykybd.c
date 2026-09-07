// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

#include "base/hand_stamp.h"

// QMK's weak is_keyboard_left() hands back split_config.left, which split_pre_init()
// filled from the EE_HANDS byte -- and a wiped EEPROM makes that byte read zero,
// i.e. a confident "right". Route every reader through the flash stamp instead, so
// an EEPROM loss cannot move a half to the other side. This is the ONE choke point:
// QMK's matrix row ownership, the RGB clipping range and our own set_side() geometry
// all come through here, so nothing can be left reading the old source by accident.
// hand_stamp.h has the failure this defends against.
bool is_keyboard_left(void) {
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
// The two halves are identical hardware and the rig does not provision an
// EE_HANDS handedness marker, so the role cannot be read from EEPROM (a fresh
// EEPROM reads back as "not left", which would make *both* halves slaves and
// leave zero masters). Instead the role is fixed at compile time per side: the
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
#endif // POLYKYBD_HIL
