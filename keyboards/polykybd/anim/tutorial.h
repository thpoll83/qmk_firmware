// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// First-run tutorial — the guided introduction that follows the Eden intro on a new
// board. Full design, and the reasoning behind the parts that are not obvious, in
// anim/TUTORIAL.md.
//
// split72 only (the ripple works in startup_anim_geom.h's board space); split42 gets
// no-op stubs, exactly as the Eden animation does.
//
// ⚠️ This is NOT built on the one-shot Eden renderer. That path renders a whole frame
// per call (~150 ms of CPU) and swallows every key — correct for an intro nobody
// interacts with, fatal for something that has to notice a tap. This uses the SLICED
// pattern the idle screensaver proved: render keycaps until a few ms are spent, return,
// resume at the same keycap next pass.
#pragma once
#include <stdbool.h>
#include <stddef.h>   // NULL, for the split42 stubs too
#include <stdint.h>

// Begin. On the master `seed` varies the three letters; the slave ignores it and
// follows the synced state. No-op if already running.
void tutorial_start(uint32_t seed);

// Tear down and hand the keycaps back. Safe when not running.
void tutorial_stop(void);

// True while the tutorial owns the keycaps — update_displays() must early-return,
// process_record_user() must swallow, and the idle fade must be held off.
bool tutorial_active(void);

// Render one slice; call every housekeeping pass while active (like startup_anim_tick).
void tutorial_tick(void);

// True once the tutorial has finished or been skipped — the caller then stamps the
// marker and calls tutorial_stop().
bool tutorial_finished(void);
// Whether that end came from the skip gesture rather than from completing the steps.
bool tutorial_was_skipped(void);

// A key was pressed, addressed by its packed slot (see TUT_SLOT in tutorial_plan.h).
// Returns true when it was the key being asked for. Master only — the slave's matrix
// is pulled over the split link, so every press arrives at the master's process_record.
bool tutorial_press(uint8_t slot);

// End it now: the hold-Esc gesture, or a remote disable over HID.
void tutorial_skip(void);

// ---- split sync -----------------------------------------------------------
// The master owns the step machine; the slave draws the keys that land on its own
// half, so it needs to know what is being asked for and when a ripple started. Both
// halves then run their own clock from receipt — there is no shared time base.
#define TUTORIAL_SYNC_BYTES 5
// Kept in step with poly_sync_t.tut[] by a static_assert in state.h.
void tutorial_sync_fill(uint8_t out[TUTORIAL_SYNC_BYTES]);
// Returns true when anything changed (the caller repaints).
bool tutorial_sync_apply(const uint8_t in[TUTORIAL_SYNC_BYTES]);
// True when the master has state the slave has not been told about yet.
bool tutorial_sync_pending(void);
void tutorial_sync_sent(void);

// ---- status OLED ----------------------------------------------------------
// The current line of prose (0 = upper, 1 = lower), or NULL for none. Resident-font
// ASCII only: at first boot the font pack may never have been flashed.
const uint32_t *tutorial_line(uint8_t which);

// ---- provided by poly_keymap.c (it owns the keymap and the display map) ----
// Fill `out` with the packed slots of keys hosting a plain A-Z letter on the base
// layer, both halves, skipping keys with no OLED behind them. Returns the count.
uint8_t tutorial_collect_candidates(uint8_t *out, uint8_t max);
// The codepoint to draw on a slot's keycap, or 0 if it hosts no plain letter.
uint32_t tutorial_slot_letter(uint8_t slot);
