// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Showroom DEMO MODE: KC_DEMO (settings layer, behind More) starts a ~15 minute loop of
// typing, menus, a language and glyph-script tour and the idle animation, which repeats
// until someone holds Esc for DEMO_EXIT_HOLD_MS. The playlist and its timing are the
// pure base/demo_plan.c; this file binds it to layers, previews and keycaps.
//
// KC_DEMO_KEYS starts the KEY DEMO: the same loop, and each key the board shows pressed
// in a typing segment also reaches the host as a plain keystroke (plus an Enter per
// line), so a notepad fills with text. It never sends a modifier or a layer key, so
// the run cannot switch away from the editor; see demo_host_usage() in demo_plan.h.
//
// What neither variant does: send a modifier to the host, persist anything, or change
// the user's layout. The plain demo sends no keystroke at all. Every key event is
// swallowed while either runs (process_record_user), the language is a board-only
// preview (poly_reported_lang() keeps answering the real one), and the layer stack is
// put back on exit.
//
// Split: the MASTER owns the segment machine and publishes {active, segment} on
// poly_sync_t.demo. Each half then times the segment from its own receipt of that index
// and inverts the keycaps on its OWN half, the same "derive it locally from synced
// state" shape the Eden idle loop uses. A typed key therefore costs no split traffic,
// and no repaint: only the press highlight moves.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "action.h"   // keyrecord_t

// poly_sync_t.demo[0]
#define DEMO_SYNC_ACTIVE 0x01u
#define DEMO_SYNC_KEYS   0x02u   // the key demo: typed keys also reach the host
#define DEMO_SYNC_BYTES  2u   // {flags, segment index}

// Master: start / stop the loop. demo_start() refuses (and returns false) while another
// mode owns the board — the tutorial, Eden, a firmware screen, a macro recording.
// send_keys = the key demo (KC_DEMO_KEYS) rather than the plain one (KC_DEMO).
bool demo_start(bool send_keys);
void demo_stop(void);

// True on the master while the loop runs.
bool demo_active(void);
// True on EITHER half while the synced state says the loop runs (the slave's only view).
bool demo_sync_active(void);
// True on either half while the synced state says the running loop is the key demo.
bool demo_sync_sends_keys(void);

// Housekeeping, every pass, on both halves: the master advances the playlist; both
// halves move their own press highlights.
void demo_tick(void);

// Master, from process_record_user(): true = swallow the event. The key demo swallows
// too: the host receives only the demo's own keystrokes, never a visitor's. Tracks the Esc hold
// that ends the demo, and swallows that Esc's release after the exit.
bool demo_process_record(uint16_t keycode, keyrecord_t *record);

// The modifiers the demo is "holding" — OR'd into the synced mods snapshot so the
// legends change with Shift. Display only; nothing is registered with QMK.
uint8_t demo_display_mods(void);

// The legend override the current segment asks for: true with a language index
// (*script == false) or a glyph script (*script == true); false = the board's own.
bool demo_preview(bool *script, uint8_t *value);
