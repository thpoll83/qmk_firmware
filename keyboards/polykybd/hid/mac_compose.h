// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdbool.h>

// MacCompose (HID cmd 20 value 6, protocol v38): in QMK's macOS unicode mode, hold
// F20 instead of Option while the hex digits are typed, so the host types the
// character (base/host_unicode_mode.h). RAM only, defined in hid/hid_com.c. Any
// change of the unicode mode clears it (unicode_input_mode_set_user in
// keys_com/poly_keymap.c).
void mac_compose_set(bool on);
bool mac_compose_active(void);
