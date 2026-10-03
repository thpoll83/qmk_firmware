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

/* One ISR pass that touched EP0 control traffic: a bus reset, a SETUP, or a
   buffer completion on EP0. The first POLY_USB_EV_MAX of a boot are kept. */
typedef struct {
    uint32_t t_us;      /* chVTGetSystemTimeX(), 1 MHz                          */
    uint32_t ints;      /* USB->INTS as the pass saw it                         */
    uint32_t bufstatus; /* USB->BUFSTATUS at entry (bit 0 = EP0 IN, 1 = EP0 OUT) */
    uint8_t  st_before; /* usbp->ep0state on entry ...                          */
    uint8_t  st_after;  /* ... and on exit                                      */
    uint8_t  bmrt;      /* SETUPPACKET[0..1], when SETUP_REQ was pending        */
    uint8_t  breq;
    uint16_t wvalue;
    uint16_t wlength;
    uint8_t  stalls;    /* ep0_stalls after the pass (low byte)                 */
    uint8_t  addr;      /* USB->DEVADDRCTRL & 0x7F after the pass               */
} poly_usb_ev_t;

#define POLY_USB_EV_MAX 48u
extern volatile poly_usb_ev_t poly_usb_ev[POLY_USB_EV_MAX];
extern volatile uint32_t      poly_usb_ev_count;   /* passes logged (may exceed MAX) */

/* 1 when the ISR handles BUS_RESET before SETUP_REQ (the fix), 0 for Contrib's
   original order. Printed in every `usbdiag:` line, so a rig log says by itself
   which driver produced it. */
extern const uint8_t poly_usb_diag_reset_first;
