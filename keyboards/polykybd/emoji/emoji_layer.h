// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdint.h>
#include <stdbool.h>

// ── Integration points ──────────────────────────────────────────────────────
//
// In keymap.c to_static_text(), add ONE line before the existing keycode_to_emoji() call:
//     const uint32_t *emj = emj_display_text(keycode);
//     if (emj) return emj;
//
// In keymap.c process_record_user(), add ONE line before process_unicodemap_poly():
//     if (emj_process_keycode(keycode, record->event.pressed)) return false;
//
// In split_sync.c user_sync_poly_data_handler(), after copy_local_state():
//     emj_apply_sync(local_state->emj_category, local_state->emj_page);
//
// In keymap.c display loop, add ONE line before kdisp_send_buffer():
//     emj_draw_tab_indicator(keycode);
//
// ───────────────────────────────────────────────────────────────────────────

// Maximum number of emoji slots that can appear in a keymap layer.
// The split72 layout exposes 50 slots; the split42 exposes 24.
// Override before including this header if needed.
#ifndef EMJ_SLOTS_PER_PAGE
#  define EMJ_SLOTS_PER_PAGE 50
#endif

// Maximum number of category tabs in the keymap.
// split72 has 12 tabs; split42 has 10.
#ifndef EMJ_NUM_CATS_VISIBLE
#  define EMJ_NUM_CATS_VISIBLE 12
#endif

// ── Public API ──────────────────────────────────────────────────────────────

// Call once from keyboard_post_init_user or similar.
void emj_init(void);

// Returns a display string for category tabs, page arrows and emoji slots,
// or NULL if keycode is unrelated to the emoji layer.
// The returned pointer is valid until the next call to this function.
const uint32_t *emj_display_text(uint16_t keycode);

// Handles category tab, page prev/next and emoji slot key events.
// Returns true if the keycode was consumed (caller should return false).
bool emj_process_keycode(uint16_t keycode, bool pressed);

// Called by the slave-side split sync handler to propagate master state.
void emj_apply_sync(uint8_t category, uint8_t page);

// Accessors used by the split sync master side to read current state.
uint8_t emj_active_category(void);
uint8_t emj_active_page(void);
// How many pages category `cat` has (0 for an unknown or empty category).
uint8_t emj_page_count(uint8_t cat);

// Draws a 3-px tab indicator on the active categor.
// Call after kdisp_write_gfx_text but before kdisp_send_buffer.
// No-op for any keycode that is not the currently active category tab.
void emj_draw_tab_indicator(uint16_t keycode);

// similar to above but draws a horizontal bar at the bottom of the tab
void emj_draw_tab_bottom(uint16_t keycode);

// ── Search mode (emoji/EMOJI_SEARCH.md) ─────────────────────────────────────
// KC_EMJ_MODE switches the layer between the category tabs and a keyword search
// through the emojidict bundle (base/emoji_dict.h). In search mode the emoji
// slots type the letters of the base layer in the active language, the category
// tabs show the results, and the page keys (top left, top right) page them.
// Backspace and Space are two fixed emoji slots (EMJ_SEARCH_BKSP_SLOT,
// EMJ_SEARCH_SPACE_SLOT). KC_EMJ_S_CLEAR clears the query, and leaving the layer
// clears it too. The mode key shows an on/off switch, and is blank and inert
// while no dictionary is loaded.

// Result index of each category tab key, by category: the left-to-right order of
// the tabs on the keymap. Identity unless the keymap's config.h says otherwise.
#ifndef EMJ_SEARCH_RESULT_OF_CAT
#  define EMJ_SEARCH_RESULT_OF_CAT { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 }
#endif

// Emoji slots that are Backspace and Space in search mode. Pick slots that are no
// letter on any base layout. 0xFF = none.
#ifndef EMJ_SEARCH_BKSP_SLOT
#  define EMJ_SEARCH_BKSP_SLOT 0xFF
#endif
#ifndef EMJ_SEARCH_SPACE_SLOT
#  define EMJ_SEARCH_SPACE_SLOT 0xFF
#endif

#define EMJ_SEARCH_PAGE 12  // results per page: the category tab keys

// Search mode is on and a dictionary is loaded.
bool emj_search_active(void);
// The typed query (for the status OLED); returns its length.
uint8_t  emj_search_query(uint32_t *out, uint8_t max);
// Results for the query (0 while the query is empty).
uint16_t emj_search_total(void);

// Called when the emoji layer turns off: clears the query (the mode stays).
void emj_search_on_layer_exit(void);

// Split sync: the master exports its search state, the slave applies it and runs
// the same search on its own copy of the dictionary.
void emj_search_export(uint8_t *flags, uint8_t *page, uint8_t *qlen, uint16_t *query);
void emj_search_apply(uint8_t flags, uint8_t page, uint8_t qlen, const uint16_t *query);

