// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// AI mode: the number row 1..9,0 and ESC show the host's agent registry
// (PolyKybdHost docs/agent-registry-plan.md). PROTOTYPE.
//
// - KC_AI (bound in no keymap) enters the mode on its press. Held longer than the
//   tapping term, the mode ends on its release. Tapped, the mode stays on until a
//   number key, another KC_AI press, any other key press (swallowed), or
//   AI_MODE_LATCH_TIMEOUT_MS without a key press.
// - In the mode, a number key press is an event for the host (key 0..9) and ends the
//   mode; ESC is the pager (key 10) and does not. Every press and release is swallowed,
//   except the release of a key that was already down when the mode started, and the
//   release of a modifier or a layer key.
// - The keys are found by their keycode on the active default layer (KC_1..KC_0, KC_ESC), so a remap
//   moves them. split42 has no number row: there only ESC is found.
// - The host sets, per number key, a state (the LED) and the pool slot of a card (the
//   keycap image in AI mode) with cmd 45 op 1, uploads the cards with cmd 46, and reads
//   the events with cmd 45 op 2. All of it is RAM only and synced in poly_sync_t.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "action.h"   // keyrecord_t

// PROTOTYPE value, to be decided by the maintainer (agent-registry-plan.md, open
// question 1): a tapped AI key leaves the mode after this long without a key press.
#define AI_MODE_LATCH_TIMEOUT_MS 10000u

// True while AI mode owns the number row and ESC (both halves; the synced flag).
bool ai_mode_active(void);

// Master: every key event, before the normal handling. Returns true to swallow it.
bool ai_process_record(uint16_t keycode, keyrecord_t *record);

// Master, every housekeeping pass: the latch timeout.
void ai_mode_tick(void);

// Both halves: when AI mode is on and (row, col) is one of its keys, draws the keycap
// into the scratch buffer and returns true. The caller has selected the panel and
// sends the window afterwards. False leaves the key to the normal renderer.
bool ai_render_key(uint8_t row, uint8_t col);

#ifdef RGB_MATRIX_ENABLE
// Both halves, from rgb_matrix_indicators_kb(): paints the number keys that have a
// state, and ESC in AI mode when the host asks for attention. Every other LED keeps
// the running effect.
void ai_rgb_paint(void);
#endif

// HID cmd 45 (AI) and cmd 46 (AI_CARD), protocol v30. Both reply into `data`.
void ai_hid_cmd(uint8_t *data, uint8_t length);
void ai_hid_card(uint8_t *data, uint8_t length);
