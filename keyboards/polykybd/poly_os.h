// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// The host-OS identity, split out of state.h so code that only needs to KNOW the
// OS (the shortcut-hint table, the OS action chords) does not have to pull in the
// whole synced-state world — state.h includes quantum.h and mru.h, which makes any
// consumer un-testable in the standalone googletest harness. state.h includes this.

#include <stdint.h>

// Sources are reconciled by resolve/get_active_os(): a manual pin wins; otherwise
// host push (cmd 29) beats firmware USB detection (QMK os_detection). Android can
// only ever come from a manual pin — QMK detection reports Android (and ChromeOS)
// as Linux.
enum poly_os {
    POLY_OS_UNKNOWN = 0,
    POLY_OS_WINDOWS = 1,
    POLY_OS_MACOS   = 2,
    POLY_OS_LINUX   = 3,
    POLY_OS_ANDROID = 4,
    // 5 = reserved, **currently not used**. Was POLY_OS_IOS; folded into macOS
    // (identical Cmd/Opt behaviour, and the host never runs on iOS). The slot is
    // kept and never reused, to honour the append-only wire contract (cmd 29 /
    // persisted os_state — a protocol-7 keyboard may still hold this value) and to
    // keep the GNOME/KDE values below pinned at 6/7, matching the host.
    POLY_OS_IOS = 5, // reserved / not used — folded into POLY_OS_MACOS
    // Host-detected Linux desktop environments (from XDG_CURRENT_DESKTOP, pushed
    // over cmd 29). They refine the Super-key shortcut hints — GNOME and KDE bind
    // the launcher/window-switcher differently — and otherwise behave as Linux.
    // Not manually pinnable (the KC_OS_SET_* keys stop at Android); a manual Linux
    // pin or USB detection yields the generic POLY_OS_LINUX.
    POLY_OS_LINUX_GNOME = 6,
    POLY_OS_LINUX_KDE   = 7,
    POLY_OS_COUNT
};

// poly_sync_t.active_os packs the resolved OS in the low bits plus the auto-mode
// flag in bit7, so the slave can render the OS *and* the auto/pin badge from the
// one synced byte. Readers that want the bare OS (e.g. emit_os_action) mask with
// POLY_OS_VALUE_MASK; the icon decodes POLY_OS_AUTO_FLAG for the badge.
#define POLY_OS_VALUE_MASK 0x7Fu
#define POLY_OS_AUTO_FLAG 0x80u

// Map a bare poly_os value (already masked with POLY_OS_VALUE_MASK) to an
// OS-action chord-table column (polymod_os_actions, enum polymod_os_action_os):
// the Linux desktop refinements emit the LINUX chords — GNOME/KDE only refine
// the Super-key *hints*, the poly_os.h note above says they "otherwise behave
// as Linux" — and everything else passes through (values the module doesn't
// know fall back to its Unknown column there). Before this fold existed,
// GNOME/KDE indexed columns the 6-wide table never initialised, so every
// KC_OS_* action key was silently dead exactly when the host had pushed a
// Linux desktop environment over cmd 29 (latent since the DE refinements
// landed; found during the polymod_os_actions extraction). poly_keymap.c
// _Static_asserts pin POLY_OS_* == OSA_OS_* for the six shared values, which
// is what makes the pass-through half of this correct.
static inline uint8_t poly_os_action_column(uint8_t os) {
    return (os == POLY_OS_LINUX_GNOME || os == POLY_OS_LINUX_KDE) ? (uint8_t)POLY_OS_LINUX : os;
}
