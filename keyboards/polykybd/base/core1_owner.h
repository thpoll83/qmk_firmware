// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The one owner of core1 at runtime.
//
// core1 runs one of two things: the overlay-RLE "service" (core1_entry in
// polymod_core1), or a custom entry such as the Doom engine. Anything that
// rewrites flash must also keep core1 in PSM reset, because a core1 fetch from
// XIP while the QSPI is out of XIP mode stalls the bus for good.
//
// Before this module, five files drove the PSM and the launch handshake
// directly, and each decided on its own whether core1 was free. Every core1
// bug of 2026-10-07 had the same shape: one party released core1 while another
// still needed it held (the Doom engine's start, its stop, an unbounded launch).
// A HOLD COUNT makes that impossible by construction: core1 leaves reset only
// when the last holder lets go, and nothing may launch it while any hold is
// outstanding.
//
// Not covered, on purpose: the boot-time launch in post_init (no holder can
// exist yet, and a boot failure has no fallback) and fw_staging_do_apply(),
// which runs from RAM, never returns, and must not call into flash.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    CORE1_TENANT_SERVICE = 0, // the overlay-RLE service (core1_entry)
    CORE1_TENANT_CUSTOM  = 1, // a core1_run() entry, e.g. the Doom engine
} core1_tenant_t;

// Take a hold: core1 is forced into PSM reset and stays there until every
// hold is released. Nests. Safe from any thread, including the slave's split
// thread.
void core1_hold(void);

// Drop a hold. When the last one goes, core1 leaves reset and the RLE service
// is relaunched (bounded). A release with no hold outstanding does nothing.
void core1_release(void);

// True while any hold is outstanding.
bool core1_held(void);

// Number of holds outstanding (diagnostics and tests).
uint8_t core1_hold_count(void);

// Hand core1 to a custom entry with its own stack. Refused (false) while a
// hold is outstanding, or when the bounded launch handshake times out because
// a hold landed during it. The check and the reset are one critical section.
bool core1_run(void (*entry)(void), uint32_t *stack_bottom, size_t stack_bytes);

// Hand core1 back to the RLE service, e.g. when the Doom engine stops. While a
// hold is outstanding nothing is launched: the last release restarts the
// service itself, so this returns false and the service still comes back.
// Otherwise up to three reset-and-launch attempts; false if all time out.
bool core1_restore_service(void);

// What core1 is running, or will run once the last hold is released.
core1_tenant_t core1_tenant(void);

// ---------------------------------------------------------------------------
// Hardware seam. core1_owner.c holds only the logic; core1_owner_hw.c
// implements these for the RP2040 and ChibiOS, and the unit tests fake them.
// ---------------------------------------------------------------------------
void core1_hw_lock(void);
void core1_hw_unlock(void);
// Force core1 into PSM reset and wait until the reset has landed.
void core1_hw_force_off(void);
// Release the PSM force-off. core1 then sits in the bootrom launch wait loop.
void core1_hw_release(void);
// Bounded launch handshakes; false when core1 did not answer in time.
bool core1_hw_launch_service(void);
bool core1_hw_launch(void (*entry)(void), uint32_t *stack_bottom, size_t stack_bytes);
// Log a launch that timed out (uprintf on the keyboard, a counter in tests).
void core1_hw_report(const char *what);
