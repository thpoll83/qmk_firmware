// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// USB bus-reset race stress (TEST BUILDS ONLY: -e POLYKYBD_USB_STRESS=yes).
//
// Reproduces what a flash erase does to the USB interrupt: interrupts masked for
// tens to hundreds of milliseconds (BY25Q64ES tSE 35 ms typ / 300 ms max per 4 KB
// sector; the wear-levelling area is one 8 KB masked erase). If the host resets
// the bus and sends its next SETUP inside one such window, the USB ISR sees both
// flags in a single pass, and the ORDER it handles them in decides whether
// enumeration survives. See UPSTREAM_PATCHES.md -> "ChibiOS-Contrib RP2040 USB
// driver" and tools/hil_probes/usb_reset_race.py.
//
// The windows are a busy-wait with interrupts disabled, NOT a real erase: the
// USB ISR cannot tell the difference (the USB controller runs on regardless), and
// a real erase would wear a flash sector on every rig boot.
//
// Not compiled into any normal or release build.
#pragma once

#ifdef POLYKYBD_USB_STRESS
// keyboard_pre_init_user(): start the stress thread (master only). Runs before
// protocol_pre_init() connects the pull-up, so the first windows cover the host's
// first bus reset.
void usb_stress_start(void);
// housekeeping_task_user(): prints the `usbdiag:` console line every 2 s for the
// first minute, and reconnects USB once if the host gave up enumerating.
void usb_stress_task(void);
#else
static inline void usb_stress_start(void) {}
static inline void usb_stress_task(void) {}
#endif
