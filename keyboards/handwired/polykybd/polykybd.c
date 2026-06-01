#include QMK_KEYBOARD_H

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
// Lives here at the keyboard level so split72 and corne42 share one
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
