// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Public interface of poly_keymap.c — the shared keymap logic compiled for every
// variant (rendering, the display refresh pass, idle visuals, layer/keycode
// resolution, and the suspend/flash entry points).
//
// This header exists because it did not, and that cost real safety: poly_keymap.c
// exports ~20 symbols, and before this file each consumer found a declaration
// wherever was convenient — base/update.h (an idle-timestamp module) carried
// poly_prepare_for_flash / reset_idle_jitter / overlay_slot_displayed,
// base/text_helper.h carried poly_lang_code with a comment apologising for it, and
// five .c files simply re-typed a prototype locally. C diagnoses none of that: a
// re-typed prototype that drifts from the definition links happily and misbehaves
// at run time. poly_keymap.c now includes this header, so every definition is
// checked against the declaration its callers see.
//
// QMK's own *_user / *_kb callbacks (process_record_user, housekeeping_task_user,
// keyboard_post_init_user, suspend_power_down_kb, ...) are deliberately NOT listed
// here — quantum declares those, and re-declaring them would be a second source of
// truth for the exact thing this header exists to prevent.

#include QMK_KEYBOARD_H

#include "base/update.h" // enum refresh_mode

#include <stdint.h>
#include <stdbool.h>

// ---------------------------------------------------------------------------
// Display refresh
// ---------------------------------------------------------------------------

// Repaints the per-keycap OLEDs. `mode` splits the pass across housekeeping
// iterations (START_FIRST_HALF / START_SECOND_HALF / DONE_ALL) or does the lot in
// one go (ALL_AT_ONCE). Early-returns while DISP_IDLE is set or while an animation
// (Eden / DOOM) owns the keycaps.
void update_displays(enum refresh_mode mode);

// Diffs local against global state/layer, pushes any difference to the other half,
// and triggers a refresh when something the display depends on moved. The diff IS
// the retry queue: global only advances on a successful sync, so a dropped frame
// re-fires next pass. Called from housekeeping and from HID handlers that mutate
// synced state.
void sync_and_refresh_displays(void);

// Applies `contrast` to every keycap panel on this half; `idle` selects the idle
// (anti-burn-in) path rather than a normal awake set.
void set_displays(uint8_t contrast, bool idle);

// One idle tick: pulses per-key contrast and, in JITTER style, relocates each key's
// legend as that key dims to black. Owns all idle visuals while DISP_IDLE is set.
void kdisp_idle(uint8_t contrast);

// Maps a 0..49 idle pulse phase to a panel contrast value.
uint8_t to_brightness(uint8_t b);

// Clears the per-key idle "was dark" latch so the next idle session starts from the
// centred awake legend and relocates every key cleanly. Call on any wake / suspend /
// stop-idle path.
void reset_idle_jitter(void);

// Erases the drifting idle legend for one keycap so the Eden screensaver can paint
// over it. Called by anim/startup_anim.c.
bool eden_idle_erase_legend(uint8_t disp_idx);

// Draws the boot splash (progressively; see boot_diag.c splash_progress()).
void show_splash_screen(void);

// ---------------------------------------------------------------------------
// Legends and per-key rendering
// ---------------------------------------------------------------------------

// Renders one key's legend into the scratch buffer. Returns false when the keycode
// has nothing to draw. Honours the glyph-script override, the Intl latin-variation
// layer, and the shift/AltGr preview.
bool render_key(uint16_t keycode, led_t state, uint8_t mods);

// The plain text legend for a keycode under `state` (lock LEDs), or NULL. This is
// the cog-generated language table's entry point.
const uint32_t* to_static_text(uint16_t keycode, led_t state);

// The OS-aware shortcut-hint display list for a keycode under the currently held
// modifiers, or NULL when the chord has no hint. The returned string is interpreted
// by kdisp_write_gfx_text_cy(), which understands the HINT_* control-code ops on top
// of plain glyphs — so it is a mini display list, not just text.
const uint32_t* keycode_to_disp_overlay(uint16_t keycode);

// "xx-YY" for a language index (0..NUM_LANG-1), "" when out of range.
const uint32_t* poly_lang_code(uint8_t lang);

// ---------------------------------------------------------------------------
// Keycode / layer resolution
// ---------------------------------------------------------------------------

// The function layer that pairs with `def_layer` (the active default layer).
layer_state_t get_function_layer(layer_state_t def_layer);

// Invalidate the cached F-row-is-pristine answer after any dynamic-keymap write.
void poly_fl_row_cache_invalidate(void);

// The keycode at a matrix position on a layer, resolved through the dynamic keymap.
uint16_t keymap_key_to_keycode(uint8_t layer, keypos_t key);

// ---------------------------------------------------------------------------
// Overlays
// ---------------------------------------------------------------------------

// Blits the overlay image for (keycode, mods) into the scratch buffer. Returns false
// when no overlay is mapped or the slot is unused.
bool copy_overlay_to_buffer(uint16_t keycode, uint8_t mods);

// True if overlay keycode-slot `base_slot` (0..89, pre-modifier/mapping) is currently
// on screen — i.e. some physical key resolves to that keycode under the active layer.
// Rebuilt by every full update_displays() pass; used by the overlay-completion
// visibility gate to skip re-renders for overlays whose key isn't shown.
bool overlay_slot_displayed(uint16_t base_slot);

// ---------------------------------------------------------------------------
// Power / flash transitions
// ---------------------------------------------------------------------------

// Enters the suspend display state (panels off, idle tracking disabled).
void poly_suspend(void);

// Wakes the displays on real user activity. Returns true if this call performed the
// wake (so the caller can swallow the keypress that caused it).
bool display_wakeup(keyrecord_t* record);

// Called just before a font-pack / firmware flash begins (which blocks normal
// interaction): drop to the base/default layer and render it once, so the user can
// still type plain characters and the keycaps show legible legends while the flash
// holds the main loop. Must run before fw_up freezes display updates.
void poly_prepare_for_flash(void);
