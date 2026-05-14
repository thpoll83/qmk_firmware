#include QMK_KEYBOARD_H

#include "hid_com.h"

void housekeeping_task_kb(void) {
    if (is_keyboard_master()) {
        hid_notify_firmware_ready();
    }
}
