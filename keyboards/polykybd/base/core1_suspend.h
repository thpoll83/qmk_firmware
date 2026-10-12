// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// core1 across a USB suspend: hold it in reset while the host sleeps and relaunch it
// on wake, so every wake starts with a fresh core1.
//
// Why: twice in the field (2026-10-09, 2026-10-12) core1 stopped inside an Eden idle
// keycap job while no host was attached. The raw-HID gate (multicore_exec.c) recovers
// such a core1, but only 500 ms into the first overlay switch, and the fragment in
// flight is lost. A core1 that died before the host went to sleep now comes back at
// the wake instead.
//
// Before the hold, the suspend checks whether core1 still owes work (a decode fragment
// or an Eden job). A healthy core1 finishes either in a few ms, so work still owed
// after CORE1_SUSPEND_STALL_MS is a stall, and it is recorded and reported like the
// gate's own recoveries. The reset therefore does not hide the evidence.
//
// Pure decision logic, unit-tested (make test:polykybd_core1_suspend). The hardware
// and the record live in multicore_exec.c.
#pragma once

#include <stdbool.h>
#include <stdint.h>

// The same budget as the raw-HID gate's CORE1_STALL_MS.
#define CORE1_SUSPEND_STALL_MS 500u

typedef struct {
    bool     held;     // this suspend holds core1 (released at wake)
    bool     waiting;  // core1 owed work at an earlier tick of this suspend
    uint32_t since;    // when the wait started
    uint32_t waited;   // how long the hold waited for core1 (for a stall report)
} core1_suspend_t;

typedef enum {
    CORE1_SUSPEND_NONE = 0,     // nothing this tick
    CORE1_SUSPEND_HOLD,         // core1 is idle: hold it
    CORE1_SUSPEND_HOLD_STALLED, // core1 owed work for CORE1_SUSPEND_STALL_MS: record a stall, then hold
} core1_suspend_action_t;

// One suspend tick (suspend_power_down_kb() runs every ~17 ms while the host sleeps).
// `eligible`: this half may hold core1 now (the master, core1 runs the overlay service,
// DOOM does not own it, and no other hold is outstanding). An ineligible tick restarts
// the wait, so a stall is only ever measured on core1 running the service.
// `owes`: core1 has a decode fragment or an Eden job it has not finished.
core1_suspend_action_t core1_suspend_step(core1_suspend_t *s, bool eligible, bool owes, uint32_t now);

// The wake. True when this suspend holds core1 and the caller must release it.
bool core1_resume_step(core1_suspend_t *s);

// ---- core1 during an EEPROM write ---------------------------------------------
// Every EEPROM write runs with core0's interrupts disabled (QMK's RP2040 driver), so
// no thread can hand core1 new work while the writer waits. core1 finishes what it
// has (an Eden keycap is ~4.3 ms, a fragment well under 1 ms) and then waits for its
// FIFO in RAM. Work still owed after CORE1_FLASH_WAIT_US means core1 has stopped
// inside flash code: it is forced off for the write and relaunched afterwards.
#define CORE1_FLASH_WAIT_US 50000u

typedef enum {
    CORE1_FLASH_PROCEED = 0,  // core1 owes nothing: write now
    CORE1_FLASH_WAIT,         // core1 is finishing work: poll again
    CORE1_FLASH_FORCE_OFF,    // core1 owed work for the whole budget: stop it
} core1_flash_action_t;

// `waited_us` is the time since the writer started waiting (unsigned difference).
core1_flash_action_t core1_flash_wait_step(bool owes, uint32_t waited_us);
