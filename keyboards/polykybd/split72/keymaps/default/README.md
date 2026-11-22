# split72/keymaps/default/

The default keymap of split72. It holds data only: which keycode is on which key on
each layer, the encoder map and the RGB LED positions. All key behaviour is in
`keys_com/poly_keymap.c`. QMK compiles `keymap.c` inside
`quantum/keymap_introspection.c`.

## Files

- `keymap.c`: three tables.
  - `keymaps[]`: the 13 layers of `enum kb_layers` (`keys_com/layers.h`), each written
    with `LAYOUT_left_right_stacked`. `_L0`..`_L4` are the base layouts Qwerty,
    Qwerty Stag!, Colemak DH, Neo and Workman. Then `_FL` (function), `_NL` (numpad),
    `_UL` (utility), `_ML` (MIDI), `_SL` (settings), `_LL` (language selection),
    `_ADDLANG1` (Intl) and `_EMJ` (emoji). A box drawing above each base layer shows it.
  - `encoder_map[]`: the knob sends `MS_WHLD` / `MS_WHLU` on every layer, and
    `MI_VELD` / `MI_VELU` (MIDI velocity) on `_ML`.
  - `g_led_config`: the matrix-to-LED map, the LED positions (`LX()` halves the x
    value) and the LED flags of the 72 LEDs. `NO_LED` marks the two keys without an
    LED.
- `config.h`: `EECONFIG_USER_DATA_SIZE` (167), `EECONFIG_USER_DATA_VERSION` (159),
  `EMJ_SLOTS_PER_PAGE` and `LANG_SLOTS_PER_PAGE` (38 each),
  `EMJ_SEARCH_RESULT_OF_CAT`, `USB_VBUS_PIN` (GP24) and `ENABLE_COMPILE_KEYCODE`.
- `rules.mk`: `ENCODER_MAP_ENABLE = yes`. Nothing else: `features/emoji/emoji_layer.c`
  and `keys_com/layers.h` are keyboard-level.

## Core concepts

- The firmware runs the dynamic keymap. At the first boot, or after a keymap reset,
  layers 0..8 (`_L0` to `_ML`, `DYNAMIC_KEYMAP_UPDATE_MAX_LAYER_COUNT` = 9) are copied
  from `keymaps[]` into EEPROM. After that the EEPROM copy is used. `_SL` and the layers
  above it are always read from `keymaps[]` in flash.
- The host layout editor (PolyKybdHost) can remap only layers 0..8.
- `keymap.c` must include `keys_com/poly_combos_keymap.h`. It supplies the stub
  `key_combos[]` and `tap_dance_actions[]` that QMK needs when `COMBO_ENABLE` and
  `TAP_DANCE_ENABLE` are on.
- `g_led_config` is placed in `.rodata` with `__attribute__((section(".rodata")))`, so
  the table is in flash, not RAM.

## Important

- ⚠️ A change to a layer below the write cap does not reach a board that has a stored
  keymap: the EEPROM copy takes priority. To ship a new default key to existing boards,
  add a `KEYMAP_MIG_*` value (`state.h`) and a `dynamic_keymap_migrate_poly(old, new)`
  call. `polyctl keymap set <layer> <row> <col> <keycode>` changes one key without a
  reset.
- ⚠️ Do not add, remove or reorder a stored layer without a new `KEYMAP_STORAGE_*`
  value. A new stored layer goes at the end of the stored block, before `_SL`.
- ⚠️ `EECONFIG_USER_DATA_VERSION` is fixed at 159. Never change it: a change zeroes all
  settings and then discards the stored keymap and macros.
- ⚠️ The build prints the warning "setting incorrect section attributes for .rodata"
  against `quantum/keymap_introspection.c`. It comes from `g_led_config` here and is
  expected. Do not remove the attribute and do not make the table `const`.

## Related

- [`../../../docs/features/keymap-storage.md`](../../../docs/features/keymap-storage.md)
- [`../../../docs/build/variants.md`](../../../docs/build/variants.md)
- [`../../../docs/features/keycode-handling.md`](../../../docs/features/keycode-handling.md)
- [`../../../docs/features/intl-layer.md`](../../../docs/features/intl-layer.md)
- [`../../../docs/features/midi.md`](../../../docs/features/midi.md)
