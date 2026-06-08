// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdint.h>
#include <stdbool.h>

// ── Paged language selection layer (_LL) ──────────────────────────────────────
//
// The language layer mirrors the emoji layer's unified look: a top MRU row of
// recently-used languages, plus page-relative slot keys (KC_LANG_SLOT_BASE + n)
// that page through the full, country-sorted language list — so we can show
// fewer languages at a time (the top row took a slot row) yet still reach all of
// them. Paging wraps around in both directions.
//
// Slots and MRU entries are rendered as country flags by the keymap; this module
// only owns the page state and the slot<->LANG_* index mapping.

#ifndef LANG_SLOTS_PER_PAGE
#  define LANG_SLOTS_PER_PAGE 48
#endif

void lang_init(void);

// Number of pages needed to show every language LANG_SLOTS_PER_PAGE at a time.
uint8_t lang_page_count(void);
uint8_t lang_active_page(void);
void    lang_page_next(void);   // wraps to page 0 past the last
void    lang_page_prev(void);   // wraps to the last page before page 0

// Slave applies the master's page (synced via poly_sync_t.lang_page).
void lang_apply_page_sync(uint8_t page);

// Maps a language slot/MRU keycode to a LANG_* index for rendering/activation.
// Returns -1 when the keycode is not a language key, is an empty MRU slot, or a
// slot beyond the end of the list on the current page.
int16_t lang_index_for_keycode(uint16_t keycode);

// Returns the page-arrow display string for KC_LANG_PAGE_PREV/NEXT, or NULL.
const uint32_t* lang_display_text(uint16_t keycode);
