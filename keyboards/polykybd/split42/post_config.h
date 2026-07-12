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

// ---------------------------------------------------------------------------
// split42 split-link bring-up overrides (bring-up diagnostics only).
//
// The shared config.h runs the split UART full-duplex two-wire with a firmware
// TX/RX crossover (SERIAL_USART_PIN_SWAP). On the first split42 boards the
// master->slave direction carries no data (the slave receives 0 frames — see the
// LINK_DIAG keycap readout), while split72 works with the identical firmware. The
// two remaining suspects are a swapped bridge conductor pair or a dead GP4 (RX)
// path. These opt-in overrides A/B-test each without touching split72. post_config.h
// wins because it is processed AFTER the shared config.h.
//
//   -e POLYKYBD_NO_PIN_SWAP=yes : keep full-duplex two-wire but DROP the firmware
//       crossover. If the split42 bridge already crosses GP4/GP5 physically, the
//       swap was double-crossing it; removing it restores the link.
//   -e POLYKYBD_HALF_DUPLEX=yes : revert to single-wire half-duplex on GP5 only
//       (the original PolyKybd link). Uses NO GP4 at all, so it works even if the
//       GP4 conductor/buffer is dead — proving GP5 is the good wire.
// ---------------------------------------------------------------------------

#ifdef POLYKYBD_NO_PIN_SWAP
#    undef SERIAL_USART_PIN_SWAP
#endif

#ifdef POLYKYBD_HALF_DUPLEX
#    undef SERIAL_USART_FULL_DUPLEX
#    undef SERIAL_USART_PIN_SWAP
#    undef SERIAL_USART_RX_PIN
#endif

// ---------------------------------------------------------------------------
// Bitbang split-transport override (bring-up diagnostics only).
//
//   -e POLYKYBD_SERIAL_BITBANG=yes : SERIAL_DRIVER = bitbang (set in rules.mk).
//
// The bitbang driver is a pure-software, single-wire half-duplex UART on one pin
// (SOFT_SERIAL_PIN). It does NOT use the RP2040 PIO block or any of the
// SERIAL_USART_* config, so drop those defines and point it at GP5 (the wire that
// is straight-through on the split cable, GP5<->GP5). If the master->slave link
// comes alive on bitbang while it is dead on the PIO vendor driver, the PIO
// peripheral — not the board wiring — is the fault. Requires PAL_USE_WAIT +
// PAL_USE_CALLBACKS (already enabled in split42/halconf.h).
// ---------------------------------------------------------------------------
#ifdef POLYKYBD_SERIAL_BITBANG
#    undef SERIAL_USART_FULL_DUPLEX
#    undef SERIAL_USART_PIN_SWAP
#    undef SERIAL_USART_TX_PIN
#    undef SERIAL_USART_RX_PIN
#    undef SOFT_SERIAL_PIN
#    define SOFT_SERIAL_PIN GP5
#endif
