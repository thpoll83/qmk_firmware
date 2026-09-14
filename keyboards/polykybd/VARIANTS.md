# Keyboard variants and the shared keymap

Extracted from `CLAUDE.md` 2026-09-14. The prose is unchanged; only heading levels
and relative links were adjusted to suit a standalone file.

## Keyboard variants & the shared keymap (`poly_keymap.c`)

Two hardware variants share one firmware: **`split72`** (72-key, RGB matrix,
Cirque trackpad, 128×64 status OLED) and **`split42`** (42-key CRKBD footprint,
no RGB, no trackpad, 128×32 status OLED). **`split42` was renamed from `corne42`
in 2026-06** — same hardware/PID/`LAYOUT_crkbd`; old `corne42` paths are gone.

All behaviour lives in the keyboard-level `poly_keymap.c` (compiled for both via
`rules.mk` `SRC`). Each variant's `<variant>/keymaps/default/keymap.c` is **data
only**: `keymaps[]`, `encoder_map[]`, and (RGB variants) `g_led_config`. Variant
differences resolve at compile time:
- `polykybd.h` `#include`s the active variant header (selected by QMK's
  `-DKEYBOARD_polykybd_<variant>`), so `QMK_KEYBOARD_H` reaches
  `struct display_info` + the `BITMASK*` macros.
- Per-variant header macros: `POLY_DISP_ROW_0/3` (scan-start displays) and
  `POLY_SPLASH_R1/R2/R2_ROW` (boot splash).
- `RGB_MATRIX_ENABLE` / `POINTING_DEVICE_ENABLE` guard the RGB and trackpad paths.

**Consequence:** a feature added to `poly_keymap.c` (e.g. a language via cog)
lands on both keyboards at once — they can't drift apart. Don't re-introduce
per-variant copies of the keymap logic (that drift is exactly what this
extraction fixed: `corne42` had silently fallen ~98 languages behind split72).
`run_cog.sh` targets `poly_keymap.c`.

⚠️ **The two variants also share the MCU SCHEMATIC, so an MCU-level question is
never answered from `variations/poly_corne/` — that directory contains no
processor.** In the hardware repo (`thpoll83/polykybd`) split42's sheets live in
`poly_kybd/variations/poly_corne/` and are the board-specific ones only
(`poly_corne_split42_{left,right}`, `shift_registers`, `ni_buffer2`,
`SSD1306_TO_SPI`); the RP2040 sheet is one level up at `poly_kybd/rp_pico.kicad_sch`
and split42 pulls it in as a hierarchical sheet (`Sheetfile` = `../../rp_pico.kicad_sch`).
Verified 2026-09-09: **exactly one `rp_pico*.kicad_sch` exists in the whole repo**, and
it is the only file containing `VBUS_SENSE`. So `R8 5.6K / R15 10k / D2 1N5819WS` — the
VBUS divider on GP24 — is on **both** boards, identically.
- ⚠️ **A grep over `variations/poly_corne/*.kicad_sch` therefore reports EVERY MCU net
  as absent, and reads as a hardware fact.** That is how "split42 has no VBUS_SENSE net"
  was asserted here (and used to scope a feature to split72) when the boards are
  identical — the search covered five sheets, none of them the processor. **An empty
  grep is evidence only once you have shown the search covered the thing you asked
  about**; one `ls` of the directory settles it. The independent tell was already
  available: split42's keymap `config.h` defines `USB_VBUS_PIN GP24` and its master
  detection works, which cannot be true of an unwired pin.
  ```bash
  # the check that actually answers it, from the hardware repo root
  grep -rl "VBUS_SENSE" --include=*.kicad_sch .        # -> poly_kybd/rp_pico.kicad_sch
  find . -name "rp_pico*.kicad_sch"                    # -> exactly one
  grep -o '"Sheetfile" "[^"]*"' poly_kybd/variations/poly_corne/poly_corne_split42_left.kicad_sch
  ```
- ⚠️ Minor, unresolved: the **right** sheet references a bare `rp_pico.kicad_sch` with no
  `../../`, and no such file exists beside it. Whether KiCad resolves that from the project
  root or the reference is simply stale was **not** established — don't read it as either.

