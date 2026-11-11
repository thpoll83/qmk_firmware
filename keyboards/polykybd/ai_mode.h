// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// AI mode: the number row 1..9,0 and ESC show the host's agent registry
// (PolyKybdHost docs/agent-registry-plan.md). PROTOTYPE.
//
// - KC_AI (bound in no keymap) is a tap-hold key. TAPPED, it sends a Hyper tap
//   (Ctrl+Shift+Alt+GUI pressed and released), as KC_HYPR does when tapped; on
//   Windows that opens Copilot. A tap never shows the cards. HELD past the tapping term, or with another key pressed while it is down,
//   it turns the mode on. Released after a key was pressed in the mode, the mode
//   ends; released with nothing pressed (a long press), the mode stays on until a
//   number key, '-', '+', any other key or AI_MODE_LATCH_TIMEOUT_MS, so the same hand
//   can reach 6..0 (the AI key is on the right half).
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
// question 1): after a long press, the mode ends after this long without a key press.
#define AI_MODE_LATCH_TIMEOUT_MS 10000u
// The host sends the agent keys every second. With no SET_KEYS for this long (the
// host process was killed, the PC stays awake) the keys are cleared, so no light
// stays on that nothing updates.
#define AI_KEYS_HOST_TIMEOUT_MS 30000u

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

// Housekeeping, both halves (split72; nothing on split42): while an agent waits,
// the Eden intro's focus ripple (anim/focus_ring.h) runs from its number key, once
// every AI_ATTENTION_PERIOD_MS, taking turns when several wait. It never starts over
// a running ripple, during the tutorial, or while the keycaps are not being drawn.
void ai_attention_tick(void);

// HID cmd 45 (AI) and cmd 46 (AI_CARD), protocol v31. Both reply into `data`.
void ai_hid_cmd(uint8_t *data, uint8_t length);
// Clears the agent states, cards and flags and leaves the mode (master only; a no-op
// when there is nothing to clear). Called at USB suspend: the host that set them is
// gone or asleep, and it sends them again on its next pass.
void ai_reset_keys(const char *why);
void ai_hid_card(uint8_t *data, uint8_t length);
