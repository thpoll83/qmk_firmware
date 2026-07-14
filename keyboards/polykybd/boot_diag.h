// Boot diagnostics — the HID-console identification banner and the per-milestone
// boot-splash progress. Both live here (out of poly_keymap.c) because they are
// self-contained boot instrumentation; poly_keymap.c just calls into them.
#pragma once

#include <stdint.h>

// SPLASH_DONE draws the whole splash AND performs the final dwell + legend
// handoff (see splash_progress() in boot_diag.c). Any step 1..7 draws that
// milestone's frame.
#define SPLASH_DONE 0xFF

// Boot identification banner — printed to the HID console (`qmk console`) so it
// shows which board, firmware and role a half is, plus its split-link state.
void emit_boot_banner(void);

// Throttled re-emit of the boot banner, called once per housekeeping pass. The
// one-shot print in keyboard_post_init_user fires before a console is usually
// attached, so this re-emits it a bounded number of times over the first ~half
// minute to catch a console attached shortly after boot.
void boot_banner_housekeeping_tick(void);

// Draws the boot-splash frame for milestone `step` (1..7); SPLASH_DONE draws the
// finished splash then dwells and hands the keycaps over to the real legends.
void splash_progress(uint8_t step);
