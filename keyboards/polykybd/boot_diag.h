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
// Milestones a boot passes through: steps 1..7 plus SPLASH_DONE, reported as
// "Boot n/8" on the status OLED and as the CRASH_PHASE_BOOT argument.
#define POLY_SPLASH_STEPS 8

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

// A SUB-milestone inside the current step, for a gap that turned out to be too
// coarse to localise a hang in.
//
// ⚠️ It does NOT renumber the percentages, and that is the whole point. "63%" has
// named step 5 for longer than the splash letters have existed, the same number is
// the CRASH_PHASE_BOOT argument, and this board's boot hangs are reported in that
// vocabulary — so a finer split has to append rather than renumber. The panel keeps
// the percent on its own line and reads the sub-step as a fraction under it ("63%"
// over "2 / 4"); the breadcrumb becomes 0x0502 (step<<8 | sub), which a bare
// milestone never produces because it stamps the step alone (0x0005). So old
// records keep their meaning and new ones are distinguishable by the high byte.
//
// `sub_total` is how many pieces this milestone was split into — it is only ever
// shown, never stored, so it can change without invalidating a single report.
//
// Cheap: it repaints the status OLED's percent line only, and does not touch the
// keycaps (the splash letters stay where the step left them).
void boot_substep(uint8_t sub, uint8_t sub_total);

// ── The FINAL boot render (the 100% step) ───────────────────────────────────
// Called by update_displays() once per key, and a no-op at every other time. It
// is armed only for the ONE update_displays(ALL_AT_ONCE) at the SPLASH_DONE tail
// — the largest unwatched span in boot, and the one a MacBook cold boot has been
// seen to hang in with the splash still reading 100%.
//
// ⚠️ That span is invisible by construction: it runs before crash_watchdog_start()
// (so a stall is permanent — no reset, no record) and before the main loop (so
// console_task() never flushes a word of it, and nothing drains the USB event
// queue). Each keycap is a blocking spi_transmit() -> spiSend(), i.e. an
// osalThreadSuspendS() with NO timeout, so one lost SPI/DMA completion parks the
// main thread there for good.
//
// While it is armed this does three things per key: feed the watchdog (armed
// across the render by splash_progress, so a STALL resets and a merely slow render
// does not), stamp the breadcrumb with the key index (step 8, so `phase=1:0x08NN`,
// NN = row*MATRIX_COLS + col + 1), and — once per ROW, not per key — repaint the
// status panel as "100%" over "NN / <keys on this half>". So the panel names the
// row on a board nobody can attach to, and the archived crash record names the
// exact key.
void boot_render_mark(uint8_t row, uint8_t col);

// Per-milestone elapsed times for THIS boot, printed once the boot completes.
//
// ⚠️ This is what has to exist before anyone arms a watchdog across post_init. The
// watchdog is off through the whole of it because "the steps above it may block for
// seconds" — and nobody has ever measured WHICH steps, or how close to
// CRASH_WATCHDOG_MS the worst one runs. Arming it on a guess turns an occasional
// hang into a permanent boot loop, which is far worse than the hang.
void emit_boot_timing_line(void);
