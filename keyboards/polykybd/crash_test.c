// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
// See crash_test.h.
#include "crash_test.h"

#ifdef POLYKYBD_CRASH_TEST

#include "quantum.h"
#include "split_util.h"   // is_keyboard_master()
#include "print.h"
#include "base/crash_record.h"
#include "multicore_exec.h"
#include "slave_data.h"

// The chord: Ctrl AND Shift AND Alt held, then a digit. Tested per FAMILY --
// MOD_MASK_CTRL covers left and right, so `(mods & (CTRL|SHIFT|ALT)) == that`
// would demand all SIX keys at once and could never fire off a normal keymap.
static bool chord_held(void) {
    const uint8_t m = get_mods();
    return (m & MOD_MASK_CTRL) && (m & MOD_MASK_SHIFT) && (m & MOD_MASK_ALT);
}

// Every address below is laundered through a volatile so the compiler cannot
// reason about it: a plain `*(uint32_t *)1 = x` is undefined behaviour GCC is
// entitled to delete, and a deleted fault is a test that silently proves nothing.
static volatile uintptr_t s_addr;
static volatile uint32_t  s_target;

// Cortex-M0+ NVIC, by address rather than CMSIS name: this file has no business
// pulling in a core header for two stores.
#define POLY_NVIC_ISER (*(volatile uint32_t *)(uintptr_t)0xE000E100u)
#define POLY_NVIC_ISPR (*(volatile uint32_t *)(uintptr_t)0xE000E200u)
// RP2040 IRQ 31 is one of the six SPARE lines (26..31): nothing drives it, so it
// reaches the weak vector, i.e. vectors.S's shared body -> _unhandled_exception.
#define SPARE_IRQ 31u

// A crash is a path that does not return, so the host would keep auto-repeating
// whatever is held -- and the watchdog trigger holds it for a full 8 s before the
// board even reboots. Same rule the FW-2 prompt and doom_begin() follow: release
// everything first, and give USB a moment to actually ship the report.
static void release_the_chord(void) {
    clear_keyboard();
    wait_ms(25);
}

// --- 1: unaligned 32-bit store ---------------------------------------------
// The qmk#258 brick, reproduced: ARMv6-M has no unaligned access, so a word STR
// to an odd address is a HardFault inside whatever function issued it. The
// stacked PC names that instruction, which is the whole point of the record.
static void __attribute__((noreturn)) fault_unaligned_store(void) {
    s_addr = (uintptr_t)&s_target + 1u;
    volatile uint32_t *p = (volatile uint32_t *)s_addr;
    *p = 0xDEADBEEFu;
    for (;;) {}
}

// --- 2: branch to a bad address ---------------------------------------------
// Bit 0 clear means ARM state, which the M0+ does not have: the BLX faults on
// entry, so the stacked PC is the TARGET (0x20000000) and LR points back here.
// That pairing is what tells a "jumped somewhere wrong" fault apart from a
// "faulted where it stood" one in the record.
static void __attribute__((noreturn)) fault_bad_branch(void) {
    s_addr = 0x20000000u;
    void (*fn)(void) = (void (*)(void))s_addr;
    fn();
    for (;;) {}
}

// --- 3: main-loop hang ------------------------------------------------------
// Interrupts stay ON deliberately: USB, the ChibiOS tick and the split link all
// keep running, and only the main loop stops -- which is exactly the hang class
// the watchdog exists for. No frame is left behind, so the phase breadcrumb is
// the entire diagnosis. ~8 s to the reboot (CRASH_WATCHDOG_MS).
static void __attribute__((noreturn)) fault_hang(void) {
    for (;;) {
        __asm volatile("" ::: "memory");
    }
}

// --- 4: deliberate halt -----------------------------------------------------
static void __attribute__((noreturn)) fault_halt(void) {
    crash_record_halt(0xC0DEu);   // the arg lands in the record's phase_arg
}

// --- 5: an unhandled vector -------------------------------------------------
// Not a HardFault: this takes the OTHER funnel, vectors.S's shared body ->
// _unhandled_exception, where there is no EXC_RETURN to say which stack the
// frame is on. ICSR.VECTACTIVE (47 = 16 + IRQ 31) is what names it.
static void __attribute__((noreturn)) fault_unhandled_vector(void) {
    POLY_NVIC_ISER = (1u << SPARE_IRQ);
    POLY_NVIC_ISPR = (1u << SPARE_IRQ);
    __asm volatile("dsb \n isb" ::: "memory");
    for (;;) {}
}

// --- 6: a fault on core1 ----------------------------------------------------
// The vector table is shared, so HardFault_Handler takes it there too (PRIMASK=1
// on core1 does not mask a HardFault). The record's `core` field should read 1,
// and this used to be an instant silent lockup -- core1 has no console.
static bool fault_core1(void) {
#ifdef USE_CORE1
    core1_crash_test();
    return true;
#else
    uprintf("crash-test: no USE_CORE1 in this build\n");
    return false;
#endif
}

// --- 7: a fault on the SLAVE half -------------------------------------------
// Asked for over the split link. The master's pull times out, the slave reboots,
// the link drops and re-establishes, and slave_data_crash_pull_tick() then pulls
// the record and prints it as `side=slave` -- the one path that cannot be reached
// from this half's keyboard at all, since only the master runs process_record.
static bool fault_slave(void) {
    if (!is_keyboard_master()) return false;
    slave_data_request_crash_test();
    return true;
}

void crash_test_slave_fault(void) {
    fault_unaligned_store();
}

// ---------------------------------------------------------------------------
bool crash_test_process_record(uint16_t keycode, bool pressed) {
    if (keycode < KC_1 || keycode > KC_7) return false;
    if (!chord_held()) return false;
    if (!pressed) return true;   // swallow the release of a chord we consumed

    const uint8_t which = (uint8_t)(keycode - KC_1 + 1);
    uprintf("crash-test: trigger %u armed\n", (unsigned)which);
    release_the_chord();

    switch (which) {
        case 1: fault_unaligned_store();
        case 2: fault_bad_branch();
        case 3: fault_hang();
        case 4: fault_halt();
        case 5: fault_unhandled_vector();
        case 6: (void)fault_core1(); break;   // core1 faults; core0 carries on until the reboot
        case 7: (void)fault_slave(); break;   // the SLAVE faults; this half stays up
        default: break;
    }
    return true;
}

void crash_test_announce(void) {
    uprintf("   CRASH-TEST BUILD -- hold LCtrl+LShift+LAlt and press:\n");
    uprintf("     1 unaligned store   2 bad branch     3 main-loop hang (~8s)\n");
    uprintf("     4 deliberate halt   5 unhandled vec  6 core1 fault  7 SLAVE fault\n");
}

#endif  // POLYKYBD_CRASH_TEST
