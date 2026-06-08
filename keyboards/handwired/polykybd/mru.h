// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdint.h>
#include <stdbool.h>

// ── Most-Recently-Used lists for the emoji and language selection layers ──────
//
// Both the emoji layer (_EMJ) and the language layer (_LL) share a unified top
// row that shows the user's most-recently-selected items:
//   * emoji recents store Unicode codepoints
//   * language recents store LANG_* enum indices
//
// The lists live in RAM, are synced master->slave (both halves render the top
// row), and are persisted to EEPROM only on a power-suspension event (and on an
// explicit host save command) — and only when they actually changed since the
// last save (the `dirty` flag), to avoid pointless flash wear.

#define MRU_CAP          12u    // visible MRU slots per layer (top row minus 2 ends)
#define MRU_EMOJI_EMPTY  0u
#define MRU_LANG_EMPTY   0xFFu

// Clears both lists (RAM only). Call once at init.
void mru_init(void);

// ── Emoji recents (Unicode codepoints) ───────────────────────────────────────
// Insert `cp` at the front, de-duplicating (moves an existing entry to front)
// and evicting the oldest when full. No-op for cp == 0.
void     mru_emoji_push(uint32_t cp);
// Codepoint at MRU position `pos` (0 == most recent), or 0 if empty.
uint32_t mru_emoji_get(uint8_t pos);
void     mru_emoji_clear(void);
void     mru_emoji_preset(void);   // load the built-in default recents

// ── Language recents (LANG_* indices) ────────────────────────────────────────
void     mru_lang_push(uint8_t lang);
uint8_t  mru_lang_get(uint8_t pos);   // MRU_LANG_EMPTY if empty
void     mru_lang_clear(void);
void     mru_lang_preset(void);

// ── Persistence (EEPROM is written by state.c) ───────────────────────────────
// True when the lists differ from what was last loaded/saved to EEPROM.
bool            mru_dirty(void);
void            mru_clear_dirty(void);
// Raw backing arrays for (de)serialisation by state.c.
const uint32_t* mru_emoji_array(void);
const uint8_t*  mru_lang_array(void);
// Replace the lists from an EEPROM blob (clears dirty, schedules a slave resync).
void            mru_load(const uint32_t emoji[MRU_CAP], const uint8_t lang[MRU_CAP]);

// ── Split sync (master pushes to slave) ──────────────────────────────────────
bool mru_sync_pending(void);
void mru_clear_sync_pending(void);
// Slave applies an incoming snapshot (does not mark dirty — slave never saves).
void mru_apply_sync(const uint32_t emoji[MRU_CAP], const uint8_t lang[MRU_CAP]);
