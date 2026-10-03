// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
// See usb_stress.h. Compiled only with -e POLYKYBD_USB_STRESS=yes.
#include "usb_stress.h"

#include "quantum.h"
#include "print.h"
#include "usb_main.h"            // USB_DRIVER, restart_usb_driver()
#include "hardware/sync.h"       // save_and_disable_interrupts()
#include "hardware/structs/timer.h" // timer_hw->timerawl, the raw 1 MHz counter
#include "poly_usb_diag.h"       // chibios_overrides/USBDv1, the ISR's counters

// Masked windows, cycled. 35 = one typical sector erase, 70 = the typical 8 KB
// wear-levelling erase, 150 and 300 cover the slow end of the datasheet. The gap
// lets the ISR run between windows -- which is when a reset and a SETUP that
// both arrived inside the previous window are handled together.
static const uint16_t k_window_ms[] = {35u, 70u, 150u, 300u};
#define USB_STRESS_GAP_MS 40u
// Keep stressing this long after the first bus reset the ISR saw. Linux
// enumerates within ~0.3 s; 3 s covers its quick retries without stretching the
// boot into the 8 s watchdog guard (which this file deliberately does NOT feed).
#define USB_STRESS_AFTER_FIRST_RESET_MS 3000u
// Hard stop if no bus reset ever arrives (no host attached).
#define USB_STRESS_CAP_MS 12000u
// If USB is still not ACTIVE this long after the stress ended, the host has
// given up on the port; reconnect once so the rig can reach the board again.
#define USB_STRESS_HEAL_AFTER_MS 15000u
#define USB_DIAG_PRINT_EVERY_MS 2000u
#define USB_DIAG_PRINT_UNTIL_MS 60000u

static volatile uint32_t s_windows, s_masked_ms, s_stress_end_ms;
static uint32_t          s_reconnects, s_last_print_ms;

// Interrupts are off, so neither the ChibiOS clock nor a sleep can be used: spin
// on the free-running hardware counter, which keeps counting regardless.
static void spin_us(uint32_t us) {
    const uint32_t t0 = timer_hw->timerawl;
    while ((uint32_t)(timer_hw->timerawl - t0) < us) {}
}

static uint32_t uptime_ms(void) {
    return (uint32_t)TIME_I2MS(chVTGetSystemTimeX());
}

static bool stress_done(uint32_t now) {
    const uint32_t first = poly_usb_diag.first_reset_ms;
    if (now >= USB_STRESS_CAP_MS) return true;
    return first != 0u && now >= first + USB_STRESS_AFTER_FIRST_RESET_MS;
}

static THD_WORKING_AREA(s_wa_usb_stress, 512);
static THD_FUNCTION(usb_stress_thread, arg) {
    (void)arg;
    chRegSetThreadName("usb_stress");
    unsigned i = 0;
    while (!stress_done(uptime_ms())) {
        const uint32_t w   = k_window_ms[i++ % (sizeof k_window_ms / sizeof k_window_ms[0])];
        const uint32_t irq = save_and_disable_interrupts();
        spin_us(w * 1000u);
        restore_interrupts(irq);
        s_windows++;
        s_masked_ms += w;
        chThdSleepMilliseconds(USB_STRESS_GAP_MS);
    }
    s_stress_end_ms = uptime_ms();
}

void usb_stress_start(void) {
#ifndef POLYKYBD_HIL_SLAVE
    // The HIL slave never connects USB (usb_disconnect() in polykybd.c); masking
    // it would only starve its split link and blur the comparison.
    chThdCreateStatic(s_wa_usb_stress, sizeof(s_wa_usb_stress), HIGHPRIO, usb_stress_thread, NULL);
#endif
}

void usb_stress_task(void) {
    if (!is_keyboard_master()) return;
    const uint32_t now = uptime_ms();

    const uint8_t state = (uint8_t)USB_DRIVER.state;
    if (s_stress_end_ms != 0u && s_reconnects == 0u && now >= s_stress_end_ms + USB_STRESS_HEAL_AFTER_MS &&
        state != USB_ACTIVE && state != USB_SUSPENDED) {
        uprintf("usbdiag: not enumerated %lu ms after the stress -- reconnecting\n",
                (unsigned long)(now - s_stress_end_ms));
        s_reconnects++;
        restart_usb_driver(&USB_DRIVER);
    }

    if (now >= USB_DIAG_PRINT_UNTIL_MS || now - s_last_print_ms < USB_DIAG_PRINT_EVERY_MS) return;
    s_last_print_ms = now;
    // One line, key=value, parsed by tools/hil_probes/usb_reset_race.py.
    uprintf("usbdiag: up=%lu reset_first=%u windows=%lu masked_ms=%lu stress_end=%lu "
            "resets=%lu setups=%lu both=%lu ep0_stalls=%lu first_reset=%lu last_reset=%lu "
            "state=%u reconnects=%lu\n",
            (unsigned long)now, (unsigned)poly_usb_diag_reset_first, (unsigned long)s_windows,
            (unsigned long)s_masked_ms, (unsigned long)s_stress_end_ms,
            (unsigned long)poly_usb_diag.bus_resets, (unsigned long)poly_usb_diag.setups,
            (unsigned long)poly_usb_diag.reset_with_setup, (unsigned long)poly_usb_diag.ep0_stalls,
            (unsigned long)poly_usb_diag.first_reset_ms, (unsigned long)poly_usb_diag.last_reset_ms,
            (unsigned)state, (unsigned long)s_reconnects);
}
