// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

#if defined(POLYKYBD_HIL) || defined(POLYKYBD_MASTER_LEFT)
#    include "usb_util.h"
#endif

#ifdef POLYKYBD_HIL

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

#elif defined(POLYKYBD_MASTER_LEFT)

// split42 bring-up diagnostic (opt-in via `-e POLYKYBD_MASTER_LEFT=yes`).
//
// The split link's full-duplex crossover is decided entirely by role: the master
// half swaps TX/RX (serial_vendor.c serial_transport_driver_master_init), the
// slave does not. So the link only forms when exactly ONE half is master. If both
// halves detect as master (both swap) or both as slave (neither swaps), both drive
// TX on the same physical pin and NOTHING crosses -> the master-side link counter
// shows transport_fail=100%, crc_err=0.
//
// Stock detection makes the role follow USB_VBUS_PIN (GP24). On the current split42
// boards GP24 on the NON-USB half can read high (VSYS reaches it across the bridge
// USB-C), so both halves come up master and the link is dead even with one USB
// cable — the same failure the HIL note above documents for the rig.
//
// This override removes GP24 from the decision: pick the role from HANDEDNESS
// instead (left = master, right = slave), which is provisioned by the EE_HANDS
// marker the `uf2-split-left` / `uf2-split-right` flash targets already write.
// Flash the left half with the split-left image, the right with the split-right
// image, and plug USB into the LEFT — the link then forms regardless of GP24.
//
// Called from split_pre_init() BEFORE split_config.left is populated, so it must
// read handedness through is_keyboard_left_impl() directly, not is_keyboard_left()
// (which returns the not-yet-set split_config.left). is_keyboard_left_impl() is a
// weak core function (split_util.c) with no public header declaration, so forward
// declare it here; for split42 (EE_HANDS) it reads the handedness from EEPROM.
bool is_keyboard_left_impl(void);

bool is_keyboard_master_impl(void) {
    bool left = is_keyboard_left_impl();
    if (!left) {
        // Right half = slave: drop USB so a stray cable in the right port does not
        // enumerate a second keyboard (mirrors the stock weak default's behaviour).
        usb_disconnect();
    }
    return left;
}

#endif // POLYKYBD_HIL / POLYKYBD_MASTER_LEFT
