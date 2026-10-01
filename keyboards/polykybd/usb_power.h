// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Running on POWER ONLY: a charger or a power bank, with no host behind the cable.
//
// The RP2040 reports an idle bus as USB SUSPEND whether a host went to sleep or there
// never was one, and QMK's main loop then parks in its suspend loop: poly_suspend()
// switches every display off and the matrix is no longer scanned. So without this a
// keyboard on a charger is dark and deaf, and the showroom demo (anim/demo_mode.h)
// could not even be started, let alone run.
//
// The rule: a USB half that NO host has configured since power-on is on power only and
// skips the suspend loop, so it runs exactly like an awake keyboard (keys, legends, idle,
// the demo). Once any host has configured it, a suspend is a real one again and the
// board sleeps as it always did — a PC that sleeps or shuts down keeps the board dark.
// The veto is QMK's weak usb_suspend_allowed_kb() (UPSTREAM_PATCHES.md, chibios.c).
//
// ⚠️ A board plugged into a PC that is OFF but supplies standby power looks the same as
// a charger: it stays lit, then follows the normal idle timeout, idle style and the
// TURN_OFF_TIME switch-off, instead of going dark within a second of boot.
#pragma once
#include <stdbool.h>

// True once any host has configured this half since power-on (latched).
bool poly_usb_host_seen(void);
// The USB (master) half, and no host has configured it since power-on.
bool poly_power_only(void);
