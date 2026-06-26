// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdint.h>

// OS-semantic action keys: a single keycode (KC_OS_*) that emits the correct key
// chord for whatever host OS is active, so one key means "Copy" / "Lock" / "Search"
// on every OS without per-OS layers. The resident [action][os] table is the solid
// default set; the host can later refine specific cells (e.g. Linux-DE specifics).
//
// emit_os_action() taps the chord for the given action on the given OS (enum
// poly_os). action_idx = keycode - KC_OS_ACTION_BASE. A cell with no binding on
// that OS (e.g. Lock on iOS) is a no-op. os == POLY_OS_UNKNOWN falls back to the
// Windows/Linux Ctrl convention.
void emit_os_action(uint16_t action_idx, uint8_t os);

// Number of defined actions (KC_OS_ACTION_END - KC_OS_ACTION_BASE).
uint16_t os_action_count(void);
