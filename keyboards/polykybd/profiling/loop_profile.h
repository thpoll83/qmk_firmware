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

#else

static inline void loop_profile_tick(void) {}
static inline void loop_profile_note_overlay_cmd(void) {}
static inline void loop_profile_add_bridge_us(uint32_t us) { (void)us; }
static inline void loop_profile_add_render_us(uint32_t us) { (void)us; }

#endif
