# split42/keymaps/default/

The default keymap of split42. It holds data only: which keycode is on which key on
each layer, the encoder map and the RGB LED positions. All key behaviour is in
`keys_com/poly_keymap.c`. QMK compiles `keymap.c` inside
`quantum/keymap_introspection.c`.

## Files

- `keymap.c`: the `POLY_LAYOUT` macro and three tables.
  - `POLY_LAYOUT`: with `POLY_SPLIT42_MIRROR_RIGHT` (`split42/config.h`) it expands
    directly to the `[MATRIX_ROWS][MATRIX_COLS]` initializer. It reverses the columns
    of each right-half letter row and puts the right thumbs at cols 3..5 of row 7.
    Without the switch it passes its arguments to `LAYOUT_lr_stacked42`. The argument
    order is: left rows 0..2 (6 keys each), left thumbs (3), then the same for the
    right half.
  - `keymaps[]`: the 13 layers of `enum kb_layers` (`keys_com/layers.h`). `_L0`..`_L4`
    are the base layouts (Qwerty, Qwerty Staggered, Colemak DH, Neo, Workman) on the
    Corne footprint. Then `_FL`, `_NL`, `_UL`, `_ML`, `_SL`, `_LL`, `_ADDLANG1` and
    `_EMJ`. On `_LL` the left half has the six region tabs (`LCAT()`), the Unicode
    input mode keys and the MRU row (`LMRU()`); the right half has 18 language slots
    (`LSLOT()`).
  - `encoder_map[]`: `MS_WHLD` / `MS_WHLU` on every layer, `MI_VELD` / `MI_VELU` (MIDI
    velocity) on `_ML`.
  - `g_led_config`: the map of 42 LEDs in matrix order (left 0..20, right 21..41). No
    LEDs are fitted, so the positions are taken from `keyboard.json` and have no effect.
- `config.h`: `EECONFIG_USER_DATA_SIZE` (167), `EECONFIG_USER_DATA_VERSION` (159),
  `USB_VBUS_PIN` (GP24), `EMJ_SLOTS_PER_PAGE` and `LANG_SLOTS_PER_PAGE` (18 each: the
  right half has 3 rows of 6 keys) and `ENABLE_COMPILE_KEYCODE`.
- `rules.mk`: `ENCODER_MAP_ENABLE = yes`. Nothing else: `features/emoji/emoji_layer.c`
  and `keys_com/layers.h` are keyboard-level.

## Core concepts

- The firmware runs the dynamic keymap. At the first boot, or after a keymap reset,
  layers 0..8 (`_L0` to `_ML`) are copied from `keymaps[]` into EEPROM. After that the
  EEPROM copy is used. `_SL` and the layers above it are always read from `keymaps[]`
  in flash.
- The shared code is compiled at the keyboard level and does not see this `config.h`.
  So `split42/rules.mk` also passes `EMJ_SLOTS_PER_PAGE=18` and `LANG_SLOTS_PER_PAGE=18`
  as `OPT_DEFS`.
- `keymap.c` must include `keys_com/poly_combos_keymap.h` for the stub combo and
  tap-dance tables.
- `g_led_config` is placed in `.rodata`, so the table is in flash, not RAM.

## Important

- ⚠️ A change to `POLY_SPLIT42_MIRROR_RIGHT` or to a layer below the write cap does not
  reach a board that has a stored keymap. Reset the dynamic keymap (EEPROM) after
  flashing, or ship the key change with a `KEYMAP_MIG_*` migration (`state.h`).
- ⚠️ Every position that holds Ctrl or Shift on any base layer must be transparent
  (`_______`) on `_ADDLANG1`. The Intl picker opens while Ctrl (`LATIN_PICKER_MOD`) is
  held, and Ctrl is at [1,0] on `_L0` but at [3,3] on `_L1`..`_L4`. No key in this
  keymap selects `_ADDLANG1` yet.
- ⚠️ `EECONFIG_USER_DATA_VERSION` is fixed at 159. Never change it.
- ⚠️ The build warning "setting incorrect section attributes for .rodata" against
  `quantum/keymap_introspection.c` comes from `g_led_config` and is expected. Do not
  remove the attribute and do not make the table `const`.

## Related

- [`../../../docs/features/keymap-storage.md`](../../../docs/features/keymap-storage.md)
- [`../../../docs/build/variants.md`](../../../docs/build/variants.md)
- [`../../../docs/features/intl-layer.md`](../../../docs/features/intl-layer.md)
- [`../../../docs/features/keycode-handling.md`](../../../docs/features/keycode-handling.md)
