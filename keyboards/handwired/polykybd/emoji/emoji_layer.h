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
// The split72 layout exposes 50 slots; the corne42 exposes 24.
// Override before including this header if needed.
#ifndef EMJ_SLOTS_PER_PAGE
#  define EMJ_SLOTS_PER_PAGE 50
#endif

// Maximum number of category tabs in the keymap.
// split72 has 12 tabs; corne42 has 10.
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

// Draws a 3-px tab indicator on the active categor.
// Call after kdisp_write_gfx_text but before kdisp_send_buffer.
// No-op for any keycode that is not the currently active category tab.
void emj_draw_tab_indicator(uint16_t keycode);

// similar to above but draws a horizontal bar at the bottom of the tab
void emj_draw_tab_bottom(uint16_t keycode);
