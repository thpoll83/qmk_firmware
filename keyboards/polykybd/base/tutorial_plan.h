// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Pure timeline / step logic for the first-run tutorial (see anim/TUTORIAL.md).
//
// Deliberately free of quantum.h, the display stack and EEPROM: the arithmetic here
// (phase transitions, the fade curve, the ripple envelope, letter selection) is the
// part with a bug history and the only part reachable from a unit test. The firmware
// binding lives in anim/tutorial.c. Same seam as base/legend_plan.c and
// base/macro_decode.c.
#pragma once
#include <stdbool.h>
#include <stdint.h>

// ---- timeline (ms) --------------------------------------------------------
#define TUT_BLANK_MS      600u   // after Eden: everything dark and still
#define TUT_TEXT_MS       900u   // status line up, keycaps still dark
#define TUT_LETTER_IN_MS  500u   // a letter fades in (contrast 0 -> full)
#define TUT_RIPPLE_MS     400u   // ripple expands across the board and is gone
#define TUT_GAP_MS        400u   // stillness before the next letter fades in
#define TUT_SKIP_HOLD_MS 1000u   // hold Esc this long to skip

#define TUT_LETTERS 3            // step 1 asks for three keys

// A chosen key: side (0 = left, 1 = right) packed above the display index 0..39.
#define TUT_SLOT(side, idx) ((uint8_t)(((side) ? 0x80u : 0u) | ((idx) & 0x3Fu)))
#define TUT_SLOT_RIGHT(s)   (((s) & 0x80u) != 0u)
#define TUT_SLOT_IDX(s)     ((uint8_t)((s) & 0x3Fu))
#define TUT_SLOT_NONE       0xFFu

typedef enum {
    TUT_BLANK = 0,      // dark, settling out of Eden
    TUT_TEXT,           // status line up, keycaps dark
    TUT_LETTER_IN,      // current letter fading in
    TUT_LETTER_WAIT,    // lit, waiting for the press (no timeout)
    TUT_RIPPLE,         // ripple expanding, letter settling out
    TUT_GAP,            // stillness between letters
    TUT_DONE,           // finished or skipped — the caller tears down
} tut_phase_t;

typedef struct {
    uint8_t  phase;             // tut_phase_t
    uint8_t  step;              // 0..TUT_LETTERS-1
    uint32_t phase_start;       // ms stamp of the current phase
    uint8_t  slots[TUT_LETTERS];// chosen keys, in the order they are asked for
    uint8_t  ripple_seq;        // bumped per accepted press; the slave starts on receipt
    uint8_t  ripple_slot;       // where the live ripple came from
    bool     skipped;           // DONE was reached by the skip gesture, not by finishing
} tut_state_t;

// Reset to the start of the tutorial. `slots` is copied verbatim; pass what
// tut_choose_slots() produced.
void tut_init(tut_state_t *st, const uint8_t slots[TUT_LETTERS], uint32_t now);

// Advance the timed phases. Returns true when the phase changed (the caller then
// repaints / pushes state to the other half). Never leaves TUT_LETTER_WAIT — only a
// press does.
bool tut_tick(tut_state_t *st, uint32_t now);

// A key was pressed. Returns true if it was the key being asked for (which starts the
// ripple); false means "not the one" and by design nothing at all should happen.
bool tut_press(tut_state_t *st, uint8_t slot, uint32_t now);

// Force the end (the skip gesture, or a remote disable).
void tut_skip(tut_state_t *st, uint32_t now);

// The key currently being asked for, or TUT_SLOT_NONE outside the letter phases.
uint8_t tut_current_slot(const tut_state_t *st);

// 0..255 through the current phase; 255 for a phase with no duration.
uint8_t tut_phase_progress(const tut_state_t *st, uint32_t now);

// ---- curves ---------------------------------------------------------------

// Fade-in contrast for the lit letter: eased, so it emerges rather than ramps.
// p 0..255 -> OLED contrast 0..255.
uint8_t tut_fade_contrast(uint8_t p);

// Ripple radius in BOARD units (the SA_GEOM space) at progress p. Starts at the
// 5px disc and expands past the far corner so it leaves the board cleanly.
uint16_t tut_ripple_radius(uint8_t p);

// Ripple ink density 0..255 at progress p: solid at the start, thinning to nothing.
uint8_t tut_ripple_density(uint8_t p);

// ---- letter selection -----------------------------------------------------

// Choose TUT_LETTERS distinct keys from `cand` (packed slots), forcing at least one
// on each half when both are represented — the ripple crossing the split is the point.
// Deterministic in `seed`. Returns the number chosen (< TUT_LETTERS only when the
// candidate list is too small, which the caller must treat as "no tutorial").
uint8_t tut_choose_slots(const uint8_t *cand, uint8_t n_cand, uint32_t seed,
                         uint8_t out[TUT_LETTERS]);
