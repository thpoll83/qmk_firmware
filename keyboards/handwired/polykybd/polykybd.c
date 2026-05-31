#include QMK_KEYBOARD_H

#ifdef POLYKYBD_HIL
#    include "usb_util.h"
#    include "eeconfig.h"

// HIL test station only (enabled by the CI build via `-e POLYKYBD_HIL=yes`).
//
// On the test rig both halves are cabled to the host's USB, so each half reads
// 5 V on USB_VBUS_PIN (GP24). QMK's default master detection is purely VBUS
// based, so *both* halves detect themselves as master and the split link never
// forms. The Raspberry Pi 4's built-in USB ports cannot drop that VBUS via
// uhubctl, so power switching can't disambiguate the halves either.
//
// Override the (weak) master-detection hook so the role is decided from the
// EE_HANDS handedness marker instead of VBUS: the left half is always master,
// the right half always slave. This runs in split_pre_init() before
// split_config.left is populated, so read the handedness directly rather than
// calling is_keyboard_left().
//
// Lives here at the keyboard level so split72 and corne42 share one
// implementation. Normal keyboards never define POLYKYBD_HIL and keep stock
// VBUS detection, so plugging USB into either half still makes that half the
// master.
bool is_keyboard_master_impl(void) {
    if (!eeconfig_is_enabled()) {
        eeconfig_init();
    }
    bool is_left = eeconfig_read_handedness();
    if (!is_left) {
        // Right half is the slave: drop its USB so it does not enumerate as a
        // second keyboard on the test host and confuse HID discovery.
        usb_disconnect();
    }
    return is_left;
}
#endif // POLYKYBD_HIL
