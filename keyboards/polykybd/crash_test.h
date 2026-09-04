// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Deliberate crashes, for exercising the crash record (base/crash_record.h) on
// real hardware. TEST BUILDS ONLY -- `qmk compile ... -e POLYKYBD_CRASH_TEST=yes`.
// A normal build compiles the inline no-ops below and pays nothing.
//
// Why it exists: every OTHER path in the crash record has been driven end to end
// (boot capture, the console line, the slave pull, HID cmd 39, the archive), but
// the FAULT HANDLERS themselves never have -- the only records the hardware has
// ever produced came from the bootrom's post-UF2 watchdog reboot. So the naked
// HardFault_Handler, the stacked-frame read, the `_unhandled_exception` funnel,
// the watchdog-timeout synthesis and the core1/slave paths are all unproven.
// This makes each of them reachable from a key chord.
//
// The chord is LCtrl+LShift+LAlt held + a digit. That combination is not a
// shortcut anything types, needs no keymap edit (so the keycaps keep their real
// legends) and works on every base layout, since KC_1..KC_9 sit on all of them.
// The digit event is SWALLOWED, so the host never sees it.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef POLYKYBD_CRASH_TEST

// Call from the top of process_record_user(). Returns true when the event was a
// crash-test chord and must not be processed further -- though for every trigger
// but an unrecognised digit, it does not return at all.
bool crash_test_process_record(uint16_t keycode, bool pressed);

// One console line listing the chords. Called from keyboard_post_init_user(),
// beside crash_record_emit_lines(), so a test build says what it is.
void crash_test_announce(void);

// Slave side: the fault the master asks for over SLAVE_DATA_CRASH_TEST.
void crash_test_slave_fault(void) __attribute__((noreturn));

#else

static inline bool crash_test_process_record(uint16_t keycode, bool pressed) {
    (void)keycode;
    (void)pressed;
    return false;
}
static inline void crash_test_announce(void) {}

#endif
