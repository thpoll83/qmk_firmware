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

// The emoji recents are stored as a compact 16-bit code rather than the raw
// 32-bit codepoint: 4 bits select the emoji category (tab) and 12 bits index the
// glyph within that category (EMJ_CATEGORIES[cat].codepoints[offset]). The
// emoji layer owns the pack/unpack; mru.c just stores the opaque code. This is
// safe because the EEPROM is reset on every firmware update (build-date magic +
// EECONFIG size), so a stored code is always interpreted against the same table
// layout that produced it.

#define MRU_CAP          12u    // visible MRU slots per layer (top row minus 2 ends)
#define MRU_EMOJI_EMPTY  0xFFFFu  // cat 15 / offset 0xFFF — never a valid code
#define MRU_LANG_EMPTY   0xFFu

// Clears both lists (RAM only). Call once at init.
void mru_init(void);

// ── Emoji recents (packed 16-bit category|offset codes) ──────────────────────
// Insert `code` at the front, de-duplicating (moves an existing entry to front)
// and evicting the oldest when full. No-op for code == MRU_EMOJI_EMPTY.
void     mru_emoji_push(uint16_t code);
// Code at MRU position `pos` (0 == most recent), or MRU_EMOJI_EMPTY if empty.
uint16_t mru_emoji_get(uint8_t pos);
void     mru_emoji_clear(void);
// Replace the whole emoji list (used by the layer's "Preset" key).
void     mru_emoji_set_all(const uint16_t codes[MRU_CAP]);

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
const uint16_t* mru_emoji_array(void);
const uint8_t*  mru_lang_array(void);
// Replace the lists from an EEPROM blob (clears dirty, schedules a slave resync).
void            mru_load(const uint16_t emoji[MRU_CAP], const uint8_t lang[MRU_CAP]);

// ── Split sync (master pushes to slave) ──────────────────────────────────────
bool mru_sync_pending(void);
void mru_clear_sync_pending(void);
// Slave applies an incoming snapshot (does not mark dirty — slave never saves).
void mru_apply_sync(const uint16_t emoji[MRU_CAP], const uint8_t lang[MRU_CAP]);
