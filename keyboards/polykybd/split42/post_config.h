// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// ---------------------------------------------------------------------------
// Status-OLED I2C pins — split42 override.
//
// TEMPORARILY MOVED: the SDA=GP22 / SCL=GP23 / I2CD1 override now lives in the
// shared ../config.h behind `#if defined(KEYBOARD_polykybd_split42)` (a bring-up
// experiment to keep the whole I2C config in one config.h). mcuconf.h still
// selects RP_I2C_USE_I2C1. If reverting, move those three defines back here as
// #undef/#define (post_config.h is processed LAST so it wins cleanly) — a plain
// #define in split42/config.h would be clobbered by the parent, which QMK
// -includes AFTER it.
//
// TODO(v2 hardware): the board's *intended* status-OLED bus is I2C0 on GP0/GP1
// (the SSD1306 I2C_SDA/I2C_SCL net), but on the current rev those RP2040 pins
// are NOT broken out to an accessible pad — only a net label — so the OLED has
// to be wired to the Exp0 expansion header (GP22/GP23) instead. Once a v2 board
// exposes GP0/GP1, drop this override (split42 then inherits the shared I2C0
// GP0/GP1 defaults) and revert split42/mcuconf.h to RP_I2C_USE_I2C0.
// ---------------------------------------------------------------------------

// Bring-up experiment (opt-in, off by default): with -DOLED_I2C_PULLUP the scan in
// poly_keymap.c enables the RP2040 internal pad pull-ups on SDA/SCL. Those are weak
// (~50 kΩ), so also drop the bus to 100 kHz here — 400 kHz has too little rise-time
// margin against a weak pull-up + module capacitance. See split42/BRINGUP.md §4.
#ifdef OLED_I2C_PULLUP
#    undef  I2C1_CLOCK_SPEED
#    define I2C1_CLOCK_SPEED 100000
#endif
