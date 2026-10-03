/*
    PolyKybd addition to the vendored ChibiOS-Contrib RP2040 USB driver.
    SPDX-License-Identifier: Apache-2.0 (same as the driver it instruments)

    Counters for the bus-reset / SETUP ordering race in the USB interrupt.
    Compiled only with -DPOLYKYBD_USB_RACE_DIAG (rules.mk: POLYKYBD_USB_STRESS=yes);
    a normal build contains none of it. Written only by the USB ISR, read by
    keyboards/polykybd/base/usb_stress.c.
*/
#pragma once

#include <stdint.h>

typedef struct {
    uint32_t bus_resets;       /* ISR passes that saw USB_INTS_BUS_RESET              */
    uint32_t setups;           /* ISR passes that saw USB_INTS_SETUP_REQ              */
    uint32_t reset_with_setup; /* passes that saw BOTH: the case the ordering decides */
    uint32_t ep0_stalls;       /* EP0 STALLs armed (also legitimate: unknown requests) */
    uint32_t first_reset_ms;   /* uptime of the first bus reset, 0 = none yet          */
    uint32_t last_reset_ms;    /* uptime of the latest bus reset                       */
} poly_usb_diag_t;

extern volatile poly_usb_diag_t poly_usb_diag;

/* 1 when the ISR handles BUS_RESET before SETUP_REQ (the fix), 0 for Contrib's
   original order. Printed in every `usbdiag:` line, so a rig log says by itself
   which driver produced it. */
extern const uint8_t poly_usb_diag_reset_first;
