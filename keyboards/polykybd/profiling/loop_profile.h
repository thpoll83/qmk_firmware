// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Main-loop timing instrumentation — compile-time gated, OFF by default.
//
// Purpose: answer ONE question with facts instead of theory — does handling an
// overlay transfer stall the QMK main loop long enough to miss a matrix scan?
// QMK scans the matrix exactly once per keyboard_task() iteration, and
// housekeeping_task_user() runs once per iteration too, so the wall-clock time
// between two housekeeping calls IS the matrix-scan interval. A key tap that both
// begins and ends inside one long iteration is never sampled — a missed keystroke.
//
// This module measures that per-iteration time in microseconds, buckets it, and
// SPLITS the histogram by whether the iteration handled a bulk overlay/mapping HID
// command — so an overlay-handling iteration that runs long shows up distinctly
// from a normal one. It also accounts, per iteration, the blocking time spent inside
// send_to_bridge() (the master->slave UART relay) and inside update_displays() (the
// per-keycap OLED re-render) — the two suspected culprits — and ACCUMULATES both as
// running TOTALS across every overlay iteration (not just the single worst one). The
// summary then attributes the overlay-iteration wall time to bridge / render / rest,
// which is what tells apart a transfer-bound stall (revives the baked-resource idea)
// from a render-bound one.
//
// Enable:  qmk compile -kb polykybd/split72 -km default -e POLYKYBD_LOOP_PROFILE=yes
//          (rules.mk then adds loop_profile.c + -DPOLYKYBD_LOOP_PROFILE)
//
// In a NORMAL build POLYKYBD_LOOP_PROFILE is undefined and every hook below is an
// empty static inline — zero code, zero image growth, no timer reads. So the call
// sites can stay unconditional (no #ifdef clutter in poly_keymap.c / hid_com.c).

#include <stdint.h>
#include <stdbool.h>

// Wire format of the binary snapshot read over HID cmd 32 (see loop_profile.c
// loop_profile_snapshot). Bumped only if the field layout changes; the reader
// (polykybd-ctnd station/perf.py) refuses a version it does not know rather than
// mis-decoding a reordered struct.
#define LOOP_PROFILE_SNAPSHOT_VERSION 1u
// Number of snapshot pages a full read returns (page 0 = scalars, page 1 = the
// two histograms).
#define LOOP_PROFILE_SNAPSHOT_PAGES   2u
// Iteration-time buckets: <1ms, 1-2, 2-5, 5-10, 10-20, 20-50, >=50ms.
#define LOOP_PROFILE_NBUCKET          7

#ifdef POLYKYBD_LOOP_PROFILE

// Call once per main-loop iteration (top of housekeeping_task_user). Closes the
// previous iteration's measurement, updates the histograms, and emits a summary
// line every LOOP_PROFILE_LOG_EVERY iterations.
void loop_profile_tick(void);

// Mark the current iteration as one that handled a bulk overlay/mapping HID
// command (called from raw_hid_receive()).
void loop_profile_note_overlay_cmd(void);

// Add blocking microseconds spent in send_to_bridge() during the current
// iteration (called from send_to_bridge() with its measured transport cost).
void loop_profile_add_bridge_us(uint32_t us);

// Add microseconds spent inside update_displays() (the per-keycap OLED re-render)
// during the current iteration (called from sync_and_refresh_displays() around the
// update_displays() calls).
void loop_profile_add_render_us(uint32_t us);

// --- On-demand control (HID cmd 32), so a host/rig can measure ONE workload ---
//
// The periodic LOOP_PROFILE_LOG_EVERY summary is fine for a human watching the
// console, but it cannot bound a measurement: the counters are cumulative from
// boot and `worst` is an all-time maximum, so an automated run could only diff
// two blocks and would never get a windowed worst-iteration. These let a caller
// zero the counters, run a defined workload, then read the window back exactly.

// Zero every counter and re-arm the iteration boundary. The iteration that is in
// flight when this runs (i.e. the one handling the reset command itself) is
// deliberately DROPPED rather than attributed to the new window — its cost is HID
// dispatch, not the workload under test.
void loop_profile_reset(void);

// Fill one page of the binary snapshot at `out` (a 64-byte HID report body, with
// the 3-byte "P<cmd><status>" reply header already written and `out` pointing at
// data[3]). Returns the number of bytes written, or 0 for an unknown page.
// Page 0: [version][flags][2 reserved] then u32 LE: iters, ovl_iters, max_us,
//         max_bridge_us, max_render_us, ovl_wall_us, ovl_bridge_us, ovl_render_us.
//         flags bit0 = the worst iteration was an overlay-handling one.
// Page 1: u32 LE bkt_norm[7] then bkt_ovl[7].
uint8_t loop_profile_snapshot(uint8_t page, uint8_t *out, uint8_t cap);

// Emit the periodic summary block to the HID console immediately, without waiting
// for the next LOOP_PROFILE_LOG_EVERY boundary.
void loop_profile_log_now(void);

#else

static inline void loop_profile_tick(void) {}
static inline void loop_profile_note_overlay_cmd(void) {}
static inline void loop_profile_add_bridge_us(uint32_t us) { (void)us; }
static inline void loop_profile_add_render_us(uint32_t us) { (void)us; }

// NOTE: loop_profile_reset/_snapshot/_log_now deliberately have NO stubs here.
// The four hooks above are stubbed because their call sites are unconditional in
// poly_keymap.c / hid_com.c / bridge_helper.c and must compile to nothing. The
// control API is different: hid_com.c guards command 32 with
// `#ifdef POLYKYBD_LOOP_PROFILE`, so on a normal build the command falls through
// to the dispatcher's default branch and NACKs. That NACK is the contract — it is
// how the rig tells "this firmware has no profiler" apart from "the profiler
// answered", instead of silently reading back a page of zeros.

#endif
