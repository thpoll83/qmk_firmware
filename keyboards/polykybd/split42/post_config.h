// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// ---------------------------------------------------------------------------
// Status-OLED I2C pins — split42 override.
//
// The shared ../config.h sets the status OLED (SSD1306) on I2C0, GP0/GP1 — that
// is the split72 wiring. On split42 the status OLED is wired to a DIFFERENT bus:
//   SDA -> GP22   (RP2040 I2C1 SDA)
//   SCL -> GP23   (RP2040 I2C1 SCL)
// GP22/GP23 are the SDA/SCL pins of the RP2040's *second* I2C block, so the
// driver must be I2CD1 (RP_I2C_USE_I2C1 is enabled in split42/mcuconf.h).
//
// This override lives in post_config.h (NOT split42/config.h) on purpose:
// QMK -include's split42/config.h BEFORE the shared polykybd/config.h, so a
// plain #define there would just be clobbered by the parent. post_config.h is
// processed LAST, so the #undef/#define below wins cleanly for split42 only,
// leaving split72's GP0/GP1 (I2C0) untouched.
//
// TODO(v2 hardware): the board's *intended* status-OLED bus is I2C0 on GP0/GP1
// (the SSD1306 I2C_SDA/I2C_SCL net), but on the current rev those RP2040 pins
// are NOT broken out to an accessible pad — only a net label — so the OLED has
// to be wired to the Exp0 expansion header (GP22/GP23) instead. Once a v2 board
// exposes GP0/GP1, drop this whole override (split42 then inherits the shared
// I2C0 GP0/GP1 defaults) and revert split42/mcuconf.h to RP_I2C_USE_I2C0.
// ---------------------------------------------------------------------------

#undef  I2C_DRIVER
#define I2C_DRIVER   I2CD1

#undef  I2C1_SDA_PIN
#define I2C1_SDA_PIN GP22

#undef  I2C1_SCL_PIN
#define I2C1_SCL_PIN GP23
