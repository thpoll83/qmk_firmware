// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// "Mines": a mine sweeper on the keycaps, one key per cell, reachable only from the
// idle screen. Holding Hyper for MINES_START_HOLD_MS while an idle animation runs
// (DISP_IDLE: pulse, jitter, Eden) starts the game, or resumes the board that was
// hidden last. A shorter tap only wakes the board, so the game does not start by
// accident. The Hide key, or MINES_INACTIVE_MS without a key press, hands the board
// back to the idle animation.
// Nothing reaches the host while the game runs.
//
// The board lives in poly_sync_t.mines (base/mines_plan.h), so the game costs no RAM
// beyond that struct and a few statics here. The game logic is base/mines_plan.c.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "action.h"   // keyrecord_t

// Back to the idle animation after this long without a key press in the game.
#define MINES_INACTIVE_MS 120000u
// How long Hyper must be held on the idle screen to start the game.
#define MINES_START_HOLD_MS 1000u

// True while the game owns the keycaps (both halves; read from the synced state).
bool mines_active(void);

// Master: the start gesture (a Hyper hold on the idle screen; mines_tick() starts the
// game when the hold is long enough). Returns true when the event belongs to the
// gesture, in which case the caller swallows it.
bool mines_try_start(uint16_t keycode, keyrecord_t *record);

// Master: every key event while mines_active(). Always swallows.
void mines_process_record(keyrecord_t *record);

// Master, every housekeeping pass: starts the game once Hyper was held long enough,
// holds the normal idle countdown off while the game runs and hides the game after
// MINES_INACTIVE_MS.
void mines_tick(void);

// Both halves: draw the keycap at matrix (row, col) into the scratch buffer. The caller
// has selected the panel and sends the window afterwards.
void mines_render_key(uint8_t row, uint8_t col);
