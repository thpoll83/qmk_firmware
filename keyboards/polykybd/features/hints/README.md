# features/hints/

The OS shortcut-hint table: the icon a keycap shows while a modifier is held (Win+L lock,
Cmd+Tab app switch, Alt+Left word left), and the modifier badge on a mod-tap key. One
caller: `keycode_to_disp_overlay()` in `keys_com/key_render_helpers.c`, which passes the local
modifier bits and `poly_sync_t.active_os`.

## Files

- `os_hints.c` / `os_hints.h`: `os_hint_for_keycode(keycode, mods_raw, active_os_packed)`
  returns a display list for `kdisp_write_gfx_text_cy()`, or NULL when the chord has no
  hint. A shortcut hint is one `ICON_HINT_*` glyph of the PolyHintIcons pack font. A
  mod-tap key (`IS_QK_MOD_TAP`) gets a badge of up to four modifier marks
  (`MTB_*` macros), from one 16-entry table per OS (`MT_BADGE_TABLE`).

## Subfolders

- [`tests/`](tests/): the `polykybd_os_hints` unit test and the pre-extraction
  reference table it compares against.

## Core concepts

- The function is pure. It reads no global state, and it includes only
  `quantum/quantum_keycodes.h`, `quantum/modifiers.h`, `keys_com/poly_os.h` and
  `lang/named_glyphs.h`, not `QMK_KEYBOARD_H`. This lets the unit test link it alone.
- Left and right modifiers are collapsed to one set (bit0 Ctrl, bit1 Shift, bit2 Alt,
  bit3 GUI). A hint matches the exact set, so an extra modifier gives no hint.
- App shortcuts get no hint here: a letter or digit with Ctrl or Alt (Shift may be
  added), or with Cmd on macOS. Those keys stay free for the host's app overlay.
  F2 and F5 have no hint for the same reason.
- macOS screenshots are Cmd+Shift+3/4/5. The table answers them before the
  app-shortcut rule, which would otherwise mask Cmd+Shift+digit. Ctrl+Cmd+Shift+3/4
  (copy to clipboard) get no hint: the exact-set rule forbids a wider chord showing
  the same icon as a narrower one.
- The OS comes from `active_os_packed`. The function masks `POLY_OS_AUTO_FLAG` with
  `POLY_OS_VALUE_MASK` itself. `POLY_OS_UNKNOWN` gets the
  Windows hints.
- The icons are drawn by `dev/tools/hint_icons.py` and shipped in `symbol.plyf`. With no
  font pack flashed the glyph is missing and no hint is drawn.
- Compiled into both variants through `POLY_SRC` in `rules.mk`. Tests:
  `make test:polykybd_os_hints`.

## Important

- ⚠️ `QK_MOD_TAP_GET_MODS()` returns QMK's 5-bit form: bit 4 is the left/right flag, not
  a modifier. Normalise it as `quantum/keymap_common.c` does before you test it against
  `MOD_MASK_*`.
- ⚠️ The badge is anchored at the bottom right, because `render_key()` draws a key's
  shift preview in the upper right. A top-anchored badge covers it.
- ⚠️ Compare hints by content, never by pointer. Two identical string literals may or
  may not share one address.
- ⚠️ A change to which chord shows which icon must also update `kExpected` in
  `tests/os_hints_tests.cpp`. The `add-polykybd-shortcut-hint` skill has the full steps.

## Related

- [`../../docs/display/hint-icons.md`](../../docs/display/hint-icons.md): the icon design and where each part is
- [`../../docs/display/legend-rendering.md`](../../docs/display/legend-rendering.md): the display-list ops
- [`../../docs/build/unit-tests.md`](../../docs/build/unit-tests.md)
