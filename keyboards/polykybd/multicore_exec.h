// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "base/overlay.h"

#include <stdint.h>
#include <stdbool.h>

// True while core1 still has a previously-dispatched DECOMPRESS or ROI_UPDATE
// in flight. Callers use this for backpressure: instead of busy-waiting inside
// the dispatcher (which would starve core0's keyboard task), they defer the
// next packet until core1 catches up.
bool core1_is_busy(void);

// Print what the last core1 stall recovery found (raw_hid_pre_receive_kb() relaunches
// a core1 that owes a fragment for too long): up to three copies, 10 s apart, each
// with the same recovery ID. Called from the HID path, where a host is attached and
// draining the console; a no-op when nothing happened.
void core1_stall_report(void);

// Hold core1 in reset across a USB suspend and relaunch it at the wake, so a core1
// that stopped while the host was away comes back fresh (base/core1_suspend.h).
// core1_suspend_tick() runs on every suspend tick (suspend_power_down_kb()); it holds
// once, after core1 finishes the work it owes, and records a stall when that work is
// still owed after 500 ms. core1_resume() releases at the wake (suspend_wakeup_init_kb())
// and does nothing when no hold was taken. Master half only. No-ops without USE_CORE1.
void core1_suspend_tick(void);
void core1_resume(void);

// ⚠️ An EEPROM write is a flash program or erase, and QMK's RP2040 wear-levelling
// driver only masks core0's interrupts. If core1 fetches code from XIP while the QSPI
// is out of XIP mode, the fetch never completes: core1 stops for good, or the bus
// stalls and the flash call never returns, so core0 hangs until the watchdog. Field
// 2026-10-12: both, at USB suspends with Eden computing keycaps on core1.
// core1_suspend_flush_ready(): the suspend flush may run now. False on the master
// until core1_suspend_tick() has taken its hold; true where core1 is not ours to
// reset (the slave, DOOM), which keeps the old behaviour there.
bool core1_suspend_flush_ready(void);
// Run `write` (an EEPROM flush) with core1 in reset, then relaunch it. For the HID
// path (cmd 24 display off, cmd 26 flush), where core1 owes no fragment. Runs `write`
// directly where core1 is off already or not ours to reset.
void core1_off_during(void (*write)(void));

// 1 once core1 has entered core1_entry() (IRQs masked); core0 clears it before launch.
extern volatile uint32_t g_core1_entered;

// True when core1 runs its command loop and can take an Eden idle keycap job: it has
// entered core1_entry() and DOOM has not taken it over. False without USE_CORE1.
bool core1_eden_available(void);
// Queue one Eden idle keycap job (anim/startup_anim.c, startup_anim_core1_job): the
// command, then `arg`. Completion is published by the job itself.
void core1_eden_key(uint32_t arg);

// Decompress the supplied buffer on core1, will block if previous decompression is still ongoing
// All fragments have to be processed in order and until the end, no parallel processing possible
// `visible` = this overlay's modifier variant is the one currently on screen; core1 only
// requests a display refresh on completion when it is (see fill_overlay.c overlay_variant_visible).
// `first` = this is an image's first fragment (cmd 16): the decode cursor rewinds to 0.
// Global variables: core0_decomp_count, core1_decomp_count, core1_bit_index, core1_max_bitlen, core1_idx, core1_buffer, hid_modifier
void core1_decompress_fragment(uint8_t keycode, uint8_t mod, uint16_t overlay_idx, const uint8_t* compressed, bool visible, bool first);

// Queues a region-of-interest overlay update for core1 processing, waits if previous update is ongoing.
// `visible`: see core1_decompress_fragment.
// Global variables: core0_decomp_count, core1_decomp_count, core1_bit_index, core1_roi, core1_idx, core1_buffer, hid_modifier
void core1_update_roi(uint8_t keycode, uint8_t mod, uint16_t overlay_idx, const uint8_t* data, const roi_update_data_t* roi, bool visible);

// Signals core1 to reset bit index for next region-of-interest update.
// Global variables: (none - sends FIFO command only)
void core1_roi_start(void);

#if defined(POLYKYBD_CRASH_TEST) && defined(USE_CORE1)
// TEST BUILDS ONLY (diag/crash_test.h): ask core1 to fault, so the crash record can be
// checked on the core that has no console and used to lock up silently. Returns
// as soon as the command is queued -- core1 faults asynchronously and the fault
// handler reboots the whole chip from there.
void core1_crash_test(void);
#endif
