# The settings-layer RGB row

Extracted from `CLAUDE.md` 2026-09-14. The prose is unchanged; only heading levels
and relative links were adjusted to suit a standalone file.

## The settings-layer RGB row (`poly_keymap.c`, `keycode_helper.c`, `split72/config.h`)

⚠️ **A LEGEND IS NOT EVIDENCE A KEYCODE DOES ANYTHING — the four RGB effect presets
drew a keycap for years and were dispatched nowhere.** `RGB_M_P` / `RGB_M_B` /
`RGB_M_R` / `RGB_M_SW` (`0x782B`–`0x782E`) are the legacy **underglow** mode keycodes.
QMK routes that range through `process_underglow()` even on an RGB-matrix-only board,
but its switch covers only toggle / next / previous / hue / sat / val / speed — the four
mode presets have **no case there, none in `process_rgb_matrix()`**, and
`IS_RGB_KEYCODE` / `RGB_KEYCODE_RANGE` are defined in `keycodes.h` and dispatched
nowhere at all. So the keys rendered, felt real, and did nothing (fixed 2026-09-09,
qmk#281). **Before believing a key works because it has a legend, grep for a `case`
that handles its keycode** — the display pipeline and the action pipeline share
nothing, and this repo has now been caught by that seam in both directions (the
settings-gate post-mortem in [`DISPLAY_PIPELINE.md`](DISPLAY_PIPELINE.md) is the
same split with the halves reversed).

- ⚠️ **The effect must be enabled on BOTH variants, because the handler is in the
  shared keymap.** `RGB_MATRIX_CYCLE_SPIRAL` is the closer match for "Swirl" and is
  **split72-only**, so the preset maps to `CYCLE_PINWHEEL` (18) instead; the other
  three are `SOLID_COLOR` (1), `BREATHING` (5), `RAINBOW_MOVING_CHEVRON` (15). Read
  the indices out of the compiled object (`nm -S` + `objcopy`) rather than counting
  the enum by hand — the set depends on which effects each variant compiles in.
- ⚠️ **`val_to_percent()` scaled against 255 while the value is CAPPED at
  `RGB_MATRIX_MAXIMUM_BRIGHTNESS` (100), so a fully-lit matrix reported 39%** and the
  status-OLED row could never reach 100 whatever the user did. It scales against the
  cap now; since `RGB_MATRIX_VAL_STEP` is 1, one step is exactly one percent.
  Saturation genuinely is a `/255` value — the two share a row and do **not** share a
  scale.
- ⚠️ **New RGB defaults reach only a FRESH eeconfig.** QMK writes them in
  `eeconfig_update_rgb_matrix_default()`, so an existing keyboard keeps its stored
  brightness and speed and sees no change; only the corrected percent is immediate.
  Adopting defaults on deployed boards would need a one-time migration sentinel (the
  `idle_style_fmt` shape) — say so in release notes rather than implying the value
  moved for everyone.

- **Macros — storage, the playback state machine, on-keyboard recording, the
  keycap look and the label faces — are
  [`keyboards/polykybd/MACROS.md`](MACROS.md).** HID cmds
  36/37/38 behind ONE `"macros"` feature gate, protocol v15+. Five rules that reach
  beyond that file:
  - ⚠️ **Playback is OURS and must stay a state machine.** QMK's
    `dynamic_keymap_macro_send()` runs the whole macro inline with a
    `while (ms--) wait_ms(1)` delay; here that loop also scans the matrix, drives the
    split UART, services USB HID and pushes 72 SPI displays, so a macro with a
    half-second delay would freeze the board and drop the link. `poly_macro_tick()`
    runs at most ONE step per housekeeping pass. No time-slicing is needed — steps
    have to be SPACED for the host to see distinct events, so the pacing IS the yield.
  - ⚠️ **Capture is installed from HOUSEKEEPING, not `keyboard_post_init_user()`.**
    `protocol_post_init()` runs after the post_init hooks and installs the USB
    driver, so a shim set there is overwritten a moment later and the recording
    captures **nothing** — no error, no missing key, just an empty macro.
  - ⚠️ **`clear_keyboard()` before starting playback and on abort**, the same rule
    the FW-2 prompt and `doom_begin()` follow: a DOWN step leaves a modifier
    registered, and the bank Shift would otherwise leak into the macro's own output.
  - **Swallowed in `process_record_user()`**, never left to the release edge — `_UL`
    is an `OSL()` layer, which re-dispatches a release-edge action up to three times
    (§ *A release-edge action fires up to THREE times*), i.e. the macro plays twice
    or three times over.
- ⚠️ **A settings key whose legend comes from a HELPER CALL renders as its raw
  keycode name in the HOST's layout editor until the helper is registered there.**
  `keycode_to_static_text()` may `return idle_style_legend();`, but the host parses
  that switch statically (`PolyKybdHost/tools/lang_demo.py`,
  `parse_static_text_map`) and cannot execute a call — so it looks the expression up
  in a hand-kept `STATIC_CALL_DEFAULTS` map and, missing an entry, falls back to the
  token text. **Nothing flags it**: the firmware build is green, the keycap on
  hardware is correct, and only the editor is wrong. `KC_IDLE_TIMEOUT` first
  rendered as `idle_t…` exactly this way. ⚠️ The substituted value is the setting's
  BOOT value, which is **not always index 0** — the idle timeout defaults to
  `IDLE_TIMEOUT_2MIN`, so the first row would show a state no keyboard boots with.
- ⚠️ **Measure a new label's WIDTH; do not eyeball it.** `IDLE IN:` put its last lit
  pixel at x=71 of the 72 px window — no margin at all, against 68 for `SCRIPT:`.
  (That label is gone: `KC_IDLE_TIMEOUT` reads `IDLE` plus a half-scale 🕑 now. The
  measuring rule is what generalises.) Render through the real draw model (the
  `keycap-layout-preview` skill, or `lang_demo.py`'s model directly) and read the ink
  box before believing a two-line settings label fits.
- **An ICON can go on a settings label line, but only through `HINT_HALF` + its own
  `HINT_MOVE`.** `HINT_HALF` (`\x0F`) composites the next glyph at half scale at the
  literal cursor and does **not** advance, so it needs an absolute position — the
  mod-tap badge pattern (`MTB_ALT`). 🕑 `U+1F551` is 39x39 full size, taller than the
  whole two-line legend; halved it is 20x20 and fits the label line's right margin
  (`HINT_POS_IDLECLK`, local 48,1 → x 48..67, 4 px clear of the east edge).
  - ⚠️ **`HINT_MOVE` cannot express a ZERO coordinate.** Its two argument bytes are
    guarded by `if (text[1] && text[2])`, so a `y` of 0 makes the whole op a no-op
    and the glyph lands at the wandering cursor instead. Start at 1.
  - ⚠️ **Write the codepoint as a NAMED glyph, never inline.** The host's preview
    parser resolves named macros by name but decodes a raw inline `U"\x1F551"` as
    `\x1F` followed by the literal characters `551` — four glyphs, silently. The
    named-glyph table bypasses that path entirely. The hand-written half of
    `lang/named_glyphs.h` (everything after the cog `[[[end]]]` at line ~1927) takes a
    new `#define` with no xlsx edit and no cog run.
  - ⚠️ **A pack glyph is not resident.** `_EmjClocks_` ships in the `emoji` bundle, so
    a keyboard with no font pack draws this keycap without its clock. Keep the words
    carrying the meaning; the icon decorates.

  - ⚠️ **A new custom keycode with NO legend renders a BLANK KEYCAP**, which is
    indistinguishable from "the feature did not ship" — `KC_MACRO_REC` reached the
    field that way. The build is green either way: a missing legend is a missing
    `case`. **Grep both legend switches** (`to_static_text()` and
    `keycode_to_static_text()`) for a new keycode before calling it done.

