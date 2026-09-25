// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// First-run tutorial — the guided introduction that follows the Eden intro on a new
// board. Full design, and the reasoning behind the parts that are not obvious, in
// anim/TUTORIAL.md.
//
// split72 only (the ripple works in startup_anim_geom.h's board space); split42 gets
// no-op stubs, exactly as the Eden animation does.
//
// ⚠️ This is NOT built on the one-shot Eden renderer. That path renders a whole frame
// per call (~150 ms of CPU) and swallows every key — correct for an intro nobody
// interacts with, fatal for something that has to notice a tap. This uses the SLICED
// pattern the idle screensaver proved: render keycaps until a few ms are spent, return,
// resume at the same keycap next pass.
#pragma once
#include <stdbool.h>
#include <stddef.h>   // NULL, for the split42 stubs too
#include <stdint.h>

#include "base/tutorial_plan.h"   // TUT_SHIFT_STAGES, TUT_SLOT_*

// Begin. On the master `seed` varies the three letters; the slave ignores it and
// follows the synced state. No-op if already running.
void tutorial_start(uint32_t seed);

// Tear down and hand the keycaps back. Safe when not running.
void tutorial_stop(void);

// True while the tutorial owns the keycaps — update_displays() must early-return,
// process_record_user() must swallow, and the idle fade must be held off.
bool tutorial_active(void);

// ---- the two halves (see the architectural note in base/tutorial_plan.h) ----
// EXCLUSIVE: chapter 1. The tutorial's sliced renderer owns every panel, and
// update_displays() must stay out.
// INTRO: chapter 2 onward. The board renders NORMALLY — mods, layers, both halves, all
// stock — and the tutorial only hides the keys outside its lit set and owns the status
// panels. Never both.
bool tutorial_exclusive(void);
bool tutorial_intro_mode(void);

// True while chapter 1 wants caps lock held in the LOCAL led_state snapshot (the host's
// real lock state is never touched). ⚠️ Must be applied immediately after the snapshot is
// read from the host in sync_and_refresh_displays() — anywhere else and the next read
// erases it, which is why an earlier attempt from the tutorial's own branch never showed.
bool tutorial_caps_hold(void);

// Intro mode: should this matrix position be drawn at all? False = the keycap goes dark.
// Both halves answer from their own keymap, so it needs no sync.
bool tutorial_key_visible(uint8_t row, uint8_t col);

// Render one slice; call every housekeeping pass while active (like startup_anim_tick).
void tutorial_tick(void);

// True once the tutorial has finished or been skipped — the caller then stamps the
// marker and calls tutorial_stop().
bool tutorial_finished(void);
// Whether that end came from the skip gesture rather than from completing the steps.
bool tutorial_was_skipped(void);

// A key was pressed, addressed by its packed slot (see TUT_SLOT in tutorial_plan.h).
// Returns true when it was the key being asked for. Master only — the slave's matrix
// is pulled over the split link, so every press arrives at the master's process_record.
bool tutorial_press(uint8_t slot);

// Chapters 2 and 3: the chapter's held key went down or came up. `kind` is a
// tut_hold_kind_t (0 = Shift, 1 = the layer key) and `slot` is that key's display slot
// (TUT_SLOT_NONE if it does not map to one), which is where the repaint wave starts.
// Master only — the slave learns about it through the ordinary ripple sync.
bool tutorial_hold(uint8_t kind, bool pressed, uint8_t slot);

// The key tour: a key was pressed, by slot. True when it is the key being asked for — the
// caller then lets this press AND its release act for real (the tab switches, the layer
// opens); false, and the caller swallows it. Master only.
bool tutorial_tour_press(uint8_t slot);
// The tour step being asked for or just pressed, or -1 outside the tour; and whether it
// has been pressed (the dwell on its result).
int16_t tutorial_tour_step(void);
// The slot of that step's key, or TUT_SLOT_NONE.
uint8_t tutorial_tour_target(void);
// Go back to waiting on tour step `step` (master; see tut_tour_rewind()).
void    tutorial_tour_rewind(uint8_t step);
bool    tutorial_tour_seen(void);

// End it now: the hold-Esc gesture, or a remote disable over HID.
void tutorial_skip(void);

// Chapter 3: the preview item the MASTER should show right now, or -1 for none (also
// -1 on the slave, which only renders what the sync carries).
int16_t tutorial_preview_index(void);
// The preview item being named or shown, as a row of poly_keymap.c's table (0xFF: none).
// Both halves answer: the master from its own state, the slave from the sync.
uint8_t tutorial_preview_entry(void);
// True while the board is dark and spelling the next item's name.
bool tutorial_naming(void);
// True in the (postponed) layer chapter's hold phases — the only time a layer key may act.
bool tutorial_in_layer_chapter(void);
// True while the board spells how many layouts and scripts there are (TUT_LANG_MORE).
bool tutorial_telling_more(void);
// Which of the two: false = the layouts screen, true = the scripts screen.
bool tutorial_more_scripts(void);
// Draw a capital centred on the selected keycap buffer (the name's letters).
bool tutorial_draw_key_letter(uint32_t cp);

// The lesson's chrome, or NULL when it should show nothing: Esc reads "Hold to / skip...",
// and the mirrored top-right outer key shows the chapter ("2/3").
const uint32_t *tutorial_skip_label(void);
const uint32_t *tutorial_progress_label(void);

// ---- split sync -----------------------------------------------------------
// The master owns the step machine; the slave draws the keys that land on its own
// half, so it needs to know what is being asked for and when a ripple started. Both
// halves then run their own clock from receipt — there is no shared time base.
// [0] flags, [1] phase, [2] current slot, [3] ripple seq, [4] ripple origin,
// [5] how far the MASTER's ripple has already run (tut_elapsed_encode) — see the
// cross-half ripple clock in base/tutorial_plan.h.
#define TUTORIAL_SYNC_BYTES 6
// tut[0]'s flag byte — TUT_SYNC_ACTIVE / TUT_SYNC_ARMED / the STEP field, and the
// tut_sync_word_stops() classifier — live in base/tutorial_plan.h, where the unit
// suite can reach them. See the note on tutorial_sync_apply() in tutorial.c.
// Kept in step with poly_sync_t.tut[] by a static_assert in state.h.
void tutorial_sync_fill(uint8_t out[TUTORIAL_SYNC_BYTES]);
// Returns true when anything changed (the caller repaints).
bool tutorial_sync_apply(const uint8_t in[TUTORIAL_SYNC_BYTES]);
// True when the master has state the slave has not been told about yet.
bool tutorial_sync_pending(void);
// True when the incoming sync says the first-run experience is armed on the master.
// Read by the split handler so THIS half can arm its own post-intro hand-off.
bool tutorial_sync_says_armed(const uint8_t in[TUTORIAL_SYNC_BYTES]);
void tutorial_sync_sent(void);

// ---- status OLED ----------------------------------------------------------
// The current line of prose for THIS HALF (0 = upper, 1 = lower), or NULL for none.
// ⚠️ Per-half, not per-board: the two panels say different things and read across the
// keyboard as one phrase, the way the firmware-apply screen already does. Resident-font
// ASCII only: at first boot the font pack may never have been flashed.
const uint32_t *tutorial_line(uint8_t which);

// A single ICONS-FONT codepoint to draw after that line, or 0 for none. ⚠️ It is
// returned SEPARATELY rather than appended to the string because the status face
// (NotoSans_Regular_Small_15px7b) covers 0x20..0x7E and nothing else, and mixing the
// two faces in one array would baseline-align the icon to the small font's yAdvance —
// a 20 px drop straight out of the band. The caller draws each with its own
// single-font array, which is what keeps both on their own baseline.
uint32_t tutorial_line_icon(uint8_t which);

// ---- provided by poly_keymap.c (it owns the keymap and the display map) ----
// Fill `out` with the packed slots of keys hosting a plain A-Z letter on the base
// layer, both halves, skipping keys with no OLED behind them. Returns the count.
uint8_t tutorial_collect_candidates(uint8_t *out, uint8_t max);
// The codepoint to draw on a slot's keycap, or 0 if it hosts no plain letter.
uint32_t tutorial_slot_letter(uint8_t slot);

// Fill `out` with the slots of the left and right Shift keys (TUT_SLOT_NONE when a
// board has no such key). Chapter 2 POINTS at one of them at a time, so it needs the
// position rather than just "a shift was pressed".
void tutorial_shift_slots(uint8_t out[TUT_SHIFT_STAGES]);

// Does this keycode change layer? Derived from QMK's keycode RANGES, never a list of
// the layer keycodes one keymap happens to use.
bool tutorial_is_layer_key(uint16_t kc);

// Draw the BOARD's own legend for `slot` into the currently selected panel's buffer —
// what update_displays() would draw for that key right now. The caller has selected the
// panel and cleared the buffer, and sends afterwards.
//
// ⚠️ Must reproduce the to_static_text() / render_key() PAIRING, not just one of them:
// a key whose legend is static text draws nothing from render_key() alone. Used only by
// chapter 1, which runs on the base layer by construction (tutorial_start clears the
// layer stack), so there is no live-layer question to get wrong here.
void tutorial_draw_board_legend(uint8_t slot);

// Park the user's default LAYOUT (poly's def_layer: Qwerty/Colemak/Neo/…) on _L0 for
// the lesson, and put it back afterwards. Called on BOTH halves from
// tutorial_start()/tutorial_stop(). Never persisted — a tutorial must not change what
// the user types after it.
void tutorial_enter_base_layout(void);
void tutorial_restore_layout(void);

// Is (row, col) in this chapter's lit set? `layer_chapter` picks which: false = the
// letters plus both shifts, true = the letters plus the layer keys.
bool tutorial_key_in_chapter_set(uint8_t row, uint8_t col, bool layer_chapter);

// Does this matrix position resolve to `slot`?
bool tutorial_slot_matches(uint8_t slot, uint8_t row, uint8_t col);

// The packed display slot at a matrix position on THIS half, or TUT_SLOT_NONE.
uint8_t tutorial_slot_at(uint8_t row, uint8_t col);

// Is this one of the two chrome keys with a label to show right now (Esc, or the
// top-right outer key)? And draw that label into the selected, cleared buffer.
bool tutorial_is_chrome_key(uint8_t row, uint8_t col);
void tutorial_draw_chrome(uint8_t row, uint8_t col);

// ---- chapter 3 (master side; the table lives with the fonts in poly_keymap.c) ----
// Build the list of languages and glyph scripts this board can actually draw, and
// return how many. Called once at tutorial_start() on the master.
uint8_t tutorial_preview_prepare(void);
// The slot of the Lang key on the base layer, either half, or TUT_SLOT_NONE.
uint8_t tutorial_lang_slot(void);
// The status-panel name of the preview item being named or shown.
const uint32_t *tutorial_preview_name(void);
// The left panel's lead-in for that item ("How about", "You may speak", ...).
const uint32_t *tutorial_preview_phrase(void);
// Map a position in the renderable subset (master only) to its table row.
uint8_t tutorial_preview_table_row(uint8_t pos);

// ---- the key tour (poly_keymap.c resolves the keys from the keymap) ----
// Fill `out` with the tour's steps in the order they are asked for and return how many.
// Called on BOTH halves at tutorial_start(); `seed` picks the Intl chapter's letter and
// accent on the master (the slave is sent the keys, so its seed does not matter).
uint8_t tutorial_tour_build(tut_tour_step_t out[TUT_TOUR_MAX], uint32_t seed);
// The status prose for a tour step, this half's half of the sentence; `seen` is the
// dwell after the press.
const uint32_t *tutorial_tour_line(uint8_t step, bool left, bool seen);

// A chapter's LIT SET, as a bitmap over THIS HALF's display slots. TUT_SET_SHIFT is the
// plain A-Z keys plus both shifts; TUT_SET_LAYER is the letters plus the layer keys.
//
// ⚠️ ONE set for both chapters was tried and is wrong — chapter 2 then lights the layer
// keys too, and the shift step showed more than the letters it asks you to look at
// (hardware). Swapping is cheap because the renderer acts on a key only when its OWN
// membership changes, so the boundary costs about seven keys rather than a full board. Each half derives its own from its own keymap, so the set costs no
// split-sync bytes. `bytes` is the bitmap size; bits are (slot index) as tut_bit_get.




// The letter to confirm on THIS half's status panel, drawn large, or 0 for none. Only
// the half that is not saying "Good" returns one, so the two panels complement rather
// than repeat each other.
uint32_t tutorial_big_letter(void);

// Compose that letter into the ALREADY-SELECTED scratch buffer, centred in a w x h
// window at buffer column `ox`. Returns false when there is nothing to draw. Lives
// here rather than in the status-screen composer because the font-tier fallback is
// the same one the lit keycap uses, and two copies of it would drift.
bool tutorial_draw_big_letter(int8_t ox, uint8_t w, uint8_t h);
