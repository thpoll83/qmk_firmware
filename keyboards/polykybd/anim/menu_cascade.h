// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The menu CASCADE: whenever the language or emoji layer shows new items (the layer
// is entered, or a tab / the page key is pressed), the three content rows appear key
// by key — left to right across the whole board, row by row — each fading up. The tab
// row and the bottom row stay put. In the first-run tutorial and outside it alike.
// The tutorial borrows it for the Shift chapter's reveal (the letters and both shifts,
// rows 1..3) and for each preview item's name (rows 1..2). Rows are PHYSICAL rows
// (menu_cascade_rows.h, generated from keyboard.json), not display rows.
//
// ⚠️ No sync byte: each half detects the change from state it already has (the synced
// layer, region, category and page), and a key's moment is a pure function of its
// board position, so the two halves run the same cascade from their own clocks.
#pragma once

#include <stdbool.h>
#include <stdint.h>

// Provided by poly_keymap.c.
uint32_t poly_menu_signature(void);    // what the menu shows; 0 = not on a menu layer
uint8_t  poly_panel_full_contrast(void);   // a keycap's normal level right now (0 = off)
bool     poly_render_live(void);       // the last update_displays() pass reached the keycaps
bool     poly_slot_visible(uint8_t slot);   // the tutorial leaves this key lit (true outside it)

// Is this key (matrix row incl. the half's offset, col) still waiting for its turn?
// Asked by update_displays() and the focus ring before drawing a legend. Also where a
// change is NOTICED, so the first render after it already hides the content rows.
bool menu_cascade_hidden(uint8_t row, uint8_t col);
// Draw each due key and fade it up. Housekeeping, both halves; self-gating.
void menu_cascade_tick(void);

// How far this half's key (display index `idx`) has faded in, 0..255, on the cascade
// running now; 255 when no cascade runs or the key does not cascade. For the key LEDs,
// which fade in with the keys (anim/tutorial_rgb.c).
uint8_t menu_cascade_key_level(bool right, uint8_t idx);
