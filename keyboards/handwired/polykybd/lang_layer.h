// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdint.h>
#include <stdbool.h>

// ── Region-grouped language selection layer (_LL) ─────────────────────────────
//
// The language layer mirrors the emoji layer's unified look. Languages are
// grouped into continent "regions" (the same grouping the PolyHost "Select
// language" menu uses), and the top row carries one tab per region — exactly
// like the emoji layer's category tabs. Within the active region, page-relative
// slot keys (KC_LANG_SLOT_BASE + n) page through that region's languages, and a
// top/bottom MRU recents row holds the most-recently-used languages across all
// regions. Paging wraps in both directions.
//
// Slots and MRU entries are rendered as country flags by the keymap; region tabs
// render as a text label (continent silhouettes later). This module owns the
// region + page state and the slot<->LANG_* index mapping.

// Number of continent regions (matches LANG_REGION_ORDER in the host's
// services/lang_regions.py). Two of them (Africa, Oceania) are currently empty.
#define NUM_LANG_REGIONS 6

#ifndef LANG_SLOTS_PER_PAGE
#  define LANG_SLOTS_PER_PAGE 38
#endif

void lang_init(void);

// Active region tab and the page within it.
uint8_t lang_active_region(void);
uint8_t lang_active_page(void);

// Number of languages in a region, and the number of pages it spans.
uint8_t lang_region_count(uint8_t region);
uint8_t lang_region_page_count(uint8_t region);

// Region tab press: switch to `region` (clamped) and reset its page to 0.
void lang_select_region(uint8_t region);

// Paging within the active region (wraps in both directions).
void lang_page_next(void);
void lang_page_prev(void);

// Region + page packed into one byte for split sync (poly_sync_t.lang_page):
// high nibble = region, low nibble = page.
uint8_t lang_pack_state(void);
void    lang_apply_sync(uint8_t packed);

// Tab label for a region (e.g. U"Europe"), or U"" for an out-of-range region.
const uint32_t* lang_region_label(uint8_t region);

// Maps a language slot/MRU keycode to a LANG_* index for rendering/activation.
// Returns -1 when the keycode is not a language key, is an empty MRU slot, or a
// slot beyond the end of the active region's current page.
int16_t lang_index_for_keycode(uint16_t keycode);

// Returns the page-arrow display string for KC_LANG_PAGE_PREV/NEXT (blank when
// the active region fits on a single page), or NULL for unrelated keycodes.
const uint32_t* lang_display_text(uint16_t keycode);

// Draws the active region tab's 3-px frame (mirror of emj_draw_tab_indicator),
// or the inactive tab's bottom bar. Call after the label but before send_buffer.
void lang_draw_tab_indicator(uint16_t keycode);
void lang_draw_tab_bottom(uint16_t keycode);
