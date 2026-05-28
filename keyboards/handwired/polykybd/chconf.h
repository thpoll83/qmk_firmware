// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// PolyKybd-local ChibiOS configuration override.
//
// CORTEX_ALTERNATE_SWITCH=TRUE moves ChibiOS's context-switch trap from NMI
// to PendSV on the ARMv6-M-RP2 port.  NMI is unmaskable by PRIMASK; PendSV
// is maskable.  This matters during fw_up: the slave's deferred flash erase
// calls `save_and_disable_interrupts()` (sets PRIMASK=1) and then runs
// `flash_range_erase`, which exits XIP for ~50 ms.  If anything triggers an
// NMI during that window, the CPU fetches the NMI handler from XIP-off
// flash and hangs — and the ChibiOS strong NMI_Handler lives at a flash
// address (see CLAUDE.md "Bug: core1 hangs ..." vector address table).
// With CORTEX_ALTERNATE_SWITCH=TRUE the SMP/preemption trap is PendSV, which
// is held off by PRIMASK and fires after `restore_interrupts()`.
//
// This complements the existing `cpsid i` in core1_entry (multicore_exec.c)
// which masks all configurable-priority exceptions on core1 — that workaround
// keeps protecting core1 regardless of which vector ChibiOS picks.
//
// See FW_UP_BASELINE.md "Step-1 regression — flash erase hang" for the
// research and reasoning.
#pragma once

#define CORTEX_ALTERNATE_SWITCH TRUE

#include_next <chconf.h>
