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

// Render sub-phases inside update_displays()'s per-keycap main path — the finer
// breakdown of where a full re-render's time actually goes. Defined unconditionally
// so the LP_RP_* names resolve in a normal build too (the hooks below discard them).
//   CLEAR   — kdisp_set_buffer(0) (wipe the 1 KB scratch)
//   LEGEND  — base legend + tab/MRU chrome (glyph lookup across g_all_fonts + raster)
//   OVERLAY — copy_overlay_to_buffer() bitmap blit + any hint text
//   SEND    — kdisp_send_window() (the 360-byte SPI push to the panel)
//   LEG_LOOKUP / LEG_RASTER — a finer split of the LEGEND phase, measured inside
//   kdisp_write_gfx_char(): the g_all_fonts font-resolution scan (LOOKUP) vs the
//   per-pixel bitmap plot that reads the packed glyph from XIP flash (RASTER).
//   Reported on the `rlegend` line so we can tell whether a legend fix is a
//   font-lookup cache or an unchanged-legend cache.
enum loop_profile_rphase { LP_RP_CLEAR = 0, LP_RP_LEGEND, LP_RP_OVERLAY, LP_RP_SEND,
                           LP_RP_LEG_LOOKUP, LP_RP_LEG_RASTER, LP_RP_COUNT };

#ifdef POLYKYBD_LOOP_PROFILE

// Call once per main-loop iteration (top of housekeeping_task_user). Closes the
// previous iteration's measurement, updates the histograms, and emits a summary
// line every LOOP_PROFILE_LOG_EVERY iterations.
void loop_profile_tick(void);

// Accumulate microseconds spent in one render sub-phase (see enum above) into an
// all-time running total, reported on the `rphase` summary line. Called from the
// per-keycap main path in update_displays().
void loop_profile_add_render_phase(uint8_t phase, uint32_t us);

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

// Raw 1 MHz microsecond counter (timer_hw->timerawl). Exposed so hot draw code in
// disp_array.c can bracket a sub-phase without pulling in the pico timer header
// itself. Returns 0 in a normal (non-profiling) build so the call sites fold away.
uint32_t loop_profile_now_us(void);

#else

static inline void loop_profile_tick(void) {}
static inline void loop_profile_note_overlay_cmd(void) {}
static inline void loop_profile_add_bridge_us(uint32_t us) { (void)us; }
static inline void loop_profile_add_render_us(uint32_t us) { (void)us; }
static inline void loop_profile_add_render_phase(uint8_t phase, uint32_t us) { (void)phase; (void)us; }
static inline uint32_t loop_profile_now_us(void) { return 0; }

#endif
