// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdint.h>

// OS-semantic action keys: a single keycode that emits the correct key chord for
// whatever host OS is active, so one key means "Copy" / "Lock" / "Search" on
// every OS without per-OS layers. The resident [action][os] table is the solid
// default set.
//
// The module owns its own two index spaces so it depends on no keyboard enum:
// rows are enum polymod_os_action, columns enum polymod_os_action_os. A keyboard
// binds its keycode range and its host-OS identity to them at the call site —
// PolyKybd pins KC_OS_* - KC_OS_ACTION_BASE == OSA_* and POLY_OS_* == OSA_OS_*
// with _Static_asserts in poly_keymap.c, and folds its Linux-DE refinements
// (GNOME/KDE) to OSA_OS_LINUX via poly_os_action_column() before calling in.

// Row order of the chord table. Append-only: a keyboard's keycode range and any
// host-side labels track this order positionally.
enum polymod_os_action {
    OSA_COPY = 0,
    OSA_CUT,
    OSA_PASTE,
    OSA_UNDO,
    OSA_REDO,
    OSA_SELALL,
    OSA_FIND,
    OSA_LOCK,       // lock screen
    OSA_SCRSHOT,    // region screenshot
    OSA_SEARCH,     // launcher / spotlight
    OSA_APP_SWITCH, // application switcher
    OSA_WIN_SWITCH, // window switcher
    OSA_EMOJI,      // emoji picker
    OSA_WORD_LEFT,  // move cursor one word left
    OSA_WORD_RIGHT, // move cursor one word right
    OSA_LINE_HOME,  // move to start of line
    OSA_LINE_END,   // move to end of line
    OSA_ACTION_COUNT
};

// Column order of the chord table. An os value at or past OSA_OS_COUNT falls
// back to the Unknown column, which mirrors the Windows/Linux Ctrl convention
// so an unresolved OS still does the sensible thing.
enum polymod_os_action_os { OSA_OS_UNKNOWN = 0, OSA_OS_WINDOWS, OSA_OS_MACOS, OSA_OS_LINUX, OSA_OS_ANDROID, OSA_OS_IOS, OSA_OS_COUNT };

// emit_os_action() taps the chord for the given action on the given OS. A cell
// with no binding on that OS (e.g. Lock on iOS) is a no-op, as is an action_idx
// past the table.
void emit_os_action(uint16_t action_idx, uint8_t os);

// Number of defined actions (== OSA_ACTION_COUNT; a function so a keyboard can
// size a runtime structure without including this header everywhere).
uint16_t os_action_count(void);
