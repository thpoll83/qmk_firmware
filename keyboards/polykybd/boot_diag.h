// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
// Boot diagnostics — the HID-console identification banner and the per-milestone
// boot-splash progress. Both live here (out of poly_keymap.c) because they are
// self-contained boot instrumentation; poly_keymap.c just calls into them.
#pragma once

#include <stdbool.h>
#include <stdint.h>

// SPLASH_DONE draws the whole splash AND performs the final dwell + legend
// handoff (see splash_progress() in boot_diag.c). Any step 1..7 draws that
// milestone's frame.
#define SPLASH_DONE 0xFF

// Boot identification banner — printed to the HID console (`qmk console`) so it
// shows which board, firmware and role a half is, plus its split-link state.
void emit_boot_banner(void);

// The configured idle (anti-burn-in) style and the timings driving the idle state
// machine. Separate from the banner because the style is only known after the
// EEPROM config load; call it once from post_init after that load (the banner tick
// re-emits it alongside the banner for a late console).
void emit_idle_config(void);

// Records what keyboard_post_init_user() found in poly_eeconf_t.keymap_layers_fmt and
// whether that forced a dynamic-keymap discard (and how long the discard blocked).
void note_keymap_storage(uint8_t stored_fmt, bool reset_ran, uint32_t elapsed_ms);

// The stored dynamic-keymap format version + whether this boot discarded the keymap.
// Re-emitted by the banner tick, like emit_idle_config().
void emit_keymap_storage_line(void);

// What the previous self-apply reached, if this boot followed one (watchdog-scratch
// breadcrumb). Silent when no apply preceded this boot.
void emit_apply_breadcrumb_line(void);

// Throttled re-emit of the boot banner, called once per housekeeping pass. The
// one-shot print in keyboard_post_init_user fires before a console is usually
// attached, so this re-emits it a bounded number of times over the first ~half
// minute to catch a console attached shortly after boot.
void boot_banner_housekeeping_tick(void);

// Draws the boot-splash frame for milestone `step` (1..7); SPLASH_DONE draws the
// finished splash then dwells and hands the keycaps over to the real legends.
void splash_progress(uint8_t step);
