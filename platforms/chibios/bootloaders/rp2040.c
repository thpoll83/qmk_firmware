// Copyright 2022 Stefan Kerkmann
// SPDX-License-Identifier: GPL-2.0-or-later

#include "hal.h"
#include "bootloader.h"
#include "gpio.h"
#include "wait.h"
#include "pico/bootrom.h"

#if defined(POLYKYBD_VREG_VSEL)
#    include "hardware/structs/vreg_and_chip_reset.h"
#    if !defined(RP2040_BOOTLOADER_DOUBLE_TAP_RESET)
// The raise lives in __late_init, which only exists in the double-tap build. If
// that ever stops being set, the board would silently boot its raised system
// clock at the default 1.10 V instead — fail the build rather than ship that.
#        error "POLYKYBD_VREG_VSEL requires RP2040_BOOTLOADER_DOUBLE_TAP_RESET"
#    endif
#endif

#if !defined(RP2040_BOOTLOADER_DOUBLE_TAP_RESET_LED)
#    define RP2040_BOOTLOADER_DOUBLE_TAP_RESET_LED_MASK 0U
#else
#    define RP2040_BOOTLOADER_DOUBLE_TAP_RESET_LED_MASK (1U << RP2040_BOOTLOADER_DOUBLE_TAP_RESET_LED)
#endif

__attribute__((weak)) void mcu_reset(void) {
    NVIC_SystemReset();
}
void bootloader_jump(void) {
    reset_usb_boot(RP2040_BOOTLOADER_DOUBLE_TAP_RESET_LED_MASK, 0U);
}

void enter_bootloader_mode_if_requested(void) {}

#if defined(RP2040_BOOTLOADER_DOUBLE_TAP_RESET)
#    if !defined(RP2040_BOOTLOADER_DOUBLE_TAP_RESET_TIMEOUT)
#        define RP2040_BOOTLOADER_DOUBLE_TAP_RESET_TIMEOUT 200U
#    endif

// Needs to be located in a RAM section that is never initialized on boot to
// preserve its value on reset
static volatile uint32_t __attribute__((section(".ram0.bootloader_magic"))) magic_location;
const uint32_t                                                              magic_token = 0xCAFEB0BA;

// We can not use the __early_init / enter_bootloader_mode_if_requested hook as
// we depend on an already initialized system with usable memory regions and
// populated function pointer tables to the optimized math functions in the
// bootrom. This function is called just prior to main.
void __late_init(void) {
#if defined(POLYKYBD_VREG_VSEL)
    // Raise the core voltage BEFORE clocks_init() brings the system PLL up.
    // A system clock above the 133 MHz / 1.10 V operating point is only
    // certified at a higher VREG setting (RP2040 datasheet 2.15.3: 200 MHz
    // needs >= 1.15 V). clocks_init() below is the FIRST thing in the boot to
    // apply SYS_CLK_KHZ, and the double-tap window right after it busy-waits
    // for a full second, so raising the voltage anywhere later would leave the
    // longest stretch of the boot running out of spec.
    hw_write_masked(&vreg_and_chip_reset_hw->vreg, ((unsigned)POLYKYBD_VREG_VSEL) << VREG_AND_CHIP_RESET_VREG_VSEL_LSB, VREG_AND_CHIP_RESET_VREG_VSEL_BITS);
    // Let the regulator settle before the faster clock arrives. The timer is
    // not ticking yet (its tick generator is set up by clocks_init), so this
    // cannot use wait_us — spin instead. We are still on the ~6.5 MHz ROSC
    // here, so this is comfortably longer than the regulator needs.
    for (volatile uint32_t i = 0; i < 1000U; i++) {
        __asm__ volatile("nop");
    }
#endif
    // All clocks have to be enabled before jumping to the bootloader function,
    // otherwise the bootrom will be stuck infinitely.
    clocks_init();

    if (magic_location != magic_token) {
        magic_location = magic_token;
        // ChibiOS is not initialized at this point, so sleeping is only
        // possible via busy waiting.
        wait_us(RP2040_BOOTLOADER_DOUBLE_TAP_RESET_TIMEOUT * 1000U);
        magic_location = 0;
        return;
    }

    magic_location = 0;
    reset_usb_boot(RP2040_BOOTLOADER_DOUBLE_TAP_RESET_LED_MASK, 0U);
}

#endif
