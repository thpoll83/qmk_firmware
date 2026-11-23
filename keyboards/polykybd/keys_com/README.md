# keys_com/

The key handling and keycap legends that every variant compiles. The keyboard-level
`rules.mk` adds these files to `SRC` (`POLY_SRC`), so `split72` and `split42` run the
same code. Each variant's `keymaps/default/keymap.c` includes `layers.h`,
`keycode_helper.h`, `lang_layer.h` and `poly_combos_keymap.h` from here.
`lib/hid/hid_com.c`, `lib/split_link/split_sync.c`, `lib/display/fill_overlay.c`, the
status OLED files and `features/` call into this folder.

## Files

- `poly_keymap.c` / `poly_keymap.h`: the shared keymap logic. It holds the QMK
  callbacks (`process_record_user()`, `housekeeping_task_user()`,
  `keyboard_post_init_user()`), the display refresh (`update_displays()`,
  `sync_and_refresh_displays()`), idle and suspend (`kdisp_idle()`, `poly_suspend()`,
  `display_wakeup()`), the overlays (`copy_overlay_to_buffer()`), the language
  selection, and the resting legend of each key (`to_static_text()`). `poly_ime_family()`
  and `poly_keycode_at()` are here too. `poly_keymap.h` declares its exports; QMK's own
  `*_user` / `*_kb` callbacks are not in it.
- `render_key.c`: `render_key()`, which draws the keys that need more than a text
  legend, and the menu, macro-picker, macro, MRU and region-tab renderers
  (`render_menu_key()`, `render_macro_picker_key()`, `draw_mru_top_bar()`). It reaches
  the fonts through `extern` declarations.
- `latin_picker.c` / `latin_picker.h`: the Intl latin-variation picker on `_ADDLANG1`.
  It resolves a picker keycode to a slot (`latin_picker_slot()`), pages the picker row
  (`latin_page_count()`), returns the variation a key shows (`latin_variation()`) and
  runs the remap mode (`latin_remap_apply()`, `latin_remap_cancel()`). It also handles
  the Intl keys for `process_record_user()` (`latin_picker_record_early()`,
  `latin_picker_record_pressed()`) and owns the picker's Ctrl latch
  (`latin_picker_latched()`, `latin_picker_latch_release()`).
- `tutorial_board.c`: the keyboard side of the first-run tutorial. It has the callbacks
  that `features/anim/tutorial.c` calls (`tutorial_slot_letter()`,
  `tutorial_draw_board_legend()`, `tutorial_tour_build()`), chapter 3's language and
  glyph-script preview (`s_tut_preview_all[]`, `poly_tutorial_apply_preview()`), the
  name spelled on the keys, and the tutorial branch of `process_record_user()`
  (`poly_tutorial_record()`). It also holds the preview's language bookkeeping:
  `poly_reported_lang()`, `poly_persisted_lang()` and `poly_set_host_lang()`.
- `tap_hold.c`: QMK's tap-hold hooks. `get_tapping_term()`, `get_quick_tap_term()` and
  `get_flow_tap_term()` return the terms set over HID cmd 44; `caps_word_press_user()`
  tests letters in the active language; `chordal_hold_handedness()` derives the hand
  from the row.
- `custom_keys.c`: the settings and utility keycodes. `poly_custom_key_action()` is
  called from `process_record_user()` and hands each keycode to one handler per
  family: settings toggles, base layer, brightness, OS selection, display style,
  Eden and the demos, and the language layer. Each handler owns both edges of its keys
  and acts on the release.
- `housekeeping.c`: phases of `housekeeping_task_user()` that need nothing private to
  `poly_keymap.c`. `macro_rec_sync_tick()` and `fw_confirm_sync_tick()` copy the macro
  recorder and the FW-2 / FW-9 confirm prompt into the synced state.
  `ltr559_housekeeping_tick()` logs the LTR-559 reading and drives auto-brightness.
  `idle_fade_tick()` runs the idle fade, the screensaver entry and the `TURN_OFF_TIME`
  suspend. `master_state_sync_tick()` writes the master-authoritative fields (active OS,
  glyph script, size and placement, idle style and timeout, Doom state).
  `housekeeping_task_user()` calls them in that order.
- `rgb.c`: the key-matrix LEDs, compiled only with `RGB_MATRIX_ENABLE`.
  `rgb_matrix_indicators_kb()` picks what the LEDs show each frame: the flash cue, the
  Doom cue, the slave's bootloader and display-off states, the tutorial and AI mode.
  `flash_rgb_tick()`, called from `housekeeping_task_user()`, drives the flash cue.
  `rgb_adjust_key_down()` / `rgb_adjust_key_up()` apply the `RM_*` adjust keys for
  `process_record_user()` and repeat them while held. `poly_keymap_internal.h` declares
  these three.
- `poly_keymap_internal.h`: helpers that `poly_keymap.c` shares with the files split
  out of it (`render_key.c`, `latin_picker.c`, `tutorial_board.c`, `housekeeping.c`,
  `custom_keys.c`): `latin_publish()`, the `housekeeping.c` phases,
  `render_lang_flag_key()`, `plan_main_legend()`, `poly_keycode_at()`,
  `display_keycode_at()`, the `poly_tutorial_*()` calls, `keymaps[]`,
  `FW_CONFIRM_ROW`/`FW_CONFIRM_COL` and `LATIN_PICKER_MOD`. Only files in `keys_com/`
  include it.
- `keycode_helper.c` / `keycode_helper.h`: the custom keycode enum (`enum my_keycodes`:
  `KC_LANG`, `KC_LAT0`..`KC_LAT11`, the `KCL_*` language keys, the emoji and language
  layer ranges, `KC_IME`) and `keycode_to_static_text()`, the legend for each keycode.
  The `static_assert`s at the end of the enum pin the values that a stored keymap holds.
- `layers.h`: `enum kb_layers`, from `_BL`/`_L0` to `_EMJ` (13 layers).
- `layer_names.c` / `layer_names.h`: one record per base layout with three name forms:
  the split72 status OLED (`poly_layout_name()`), the split42 status OLED
  (`poly_layout_name_short()`, at most 5 characters) and the HID cmd 35 wire form
  (`poly_layer_name_wire()`, at most `POLY_LAYER_NAME_MAX` = 8). `poly_layer_names_payload()`
  builds the cmd 35 payload.
- `lang_layer.c` / `lang_layer.h`: the language selection layer `_LL`. Languages are
  grouped into `NUM_LANG_REGIONS` (6) regions (`REGION_OFFSET`, `REGION_LANGS`), one
  tab per region, paged by `LANG_SLOTS_PER_PAGE`. `lang_index_for_keycode()` maps a
  slot or MRU keycode to a `LANG_*` index.
- `mru.c` / `mru.h`: the most-recently-used lists of the emoji and language layers,
  `MRU_CAP` (12) entries each. Emoji entries are 14-bit category/offset codes, language
  entries are `LANG_*` indices.
- `poly_combos.c` / `poly_combos.h`: runtime combos and tap dance (HID cmd 48). The
  records are in two flash sectors (`FW_COMBO_STORE_OFFSET`), and QMK reads the tables
  through `combo_get()` / `tap_dance_get()`. Without `POLY_RUNTIME_COMBOS` every
  function is an inline no-op.
- `poly_combos_keymap.h`: one-entry stub tables `key_combos[]` and `tap_dance_actions[]`
  that QMK's `keymap_introspection.c` needs. Every `keymap.c` includes it.
- `poly_macro.c` / `poly_macro.h`: dynamic macros with a keycap label. The bodies use
  QMK's dynamic-macro buffer, and the labels are a fixed-stride array after it
  (`POLY_MACRO_LABEL_ADDR`). Playback is our own: `poly_macro_tick()` runs one step per
  housekeeping pass, `POLY_MACRO_STEP_MS` (8 ms) apart. `poly_macro_banked_id()` maps a
  slot plus Shift to a macro id (`POLY_MACRO_BANK` = 12).
- `poly_macro_record.c` / `poly_macro_record.h`: recording a macro on the keyboard
  (`enum poly_rec_state`, `poly_macro_rec_toggle()`, `poly_macro_rec_stop()`). Keys go
  into a `POLY_MACRO_REC_BYTES` (192) staging buffer, and the commit writes EEPROM in
  `POLY_REC_COMMIT_CHUNK` (32) byte steps.
- `poly_os.h`: `enum poly_os`, the `POLY_OS_VALUE_MASK` / `POLY_OS_AUTO_FLAG` packing of
  `poly_sync_t.active_os`, and `poly_os_action_column()`. `state.h` includes it.
- `matrix_helper.c` / `matrix_helper.h`: `get_split_matrix_pos()` and
  `get_split_matrix_side()` find a keycode on a layer and report the half it is on.
- `uni.h`: `unicode_map[]`, the Unicode map of the latin letters and the private emoji.

## Core concepts

- Both halves run this code. The master processes the keys and the HID commands. The
  slave draws its own keycaps from the state it receives: `poly_sync_t` (for example
  `lang_page`, `active_os`, `rec_state`), the latin table (`latin_publish()`), the MRU
  lists and the macro labels.
- Legend flow: `update_displays()` walks the keymap and resolves each key with
  `display_keycode_at()`, which calls `poly_keycode_at()`. It asks `to_static_text()`
  for a text legend. Only when that returns NULL does it call `render_key()`.
- `poly_keycode_at()` is the one resolver for the legend and the key event
  (`keymap_key_to_keycode()`). Layers below `DYNAMIC_KEYMAP_UPDATE_MAX_LAYER_COUNT` (9)
  come from the dynamic keymap in EEPROM. `_SL` and the layers above it come from the
  compiled `keymaps[]` in flash.
- Generated code: cog blocks in `poly_keymap.c`, `keycode_helper.h` and `uni.h` are
  generated from `lang/lang_lut.xlsx`. Do not edit between `[[[cog` and `[[[end]]]` by
  hand. Regenerate with `dev/scripts/run_cog.sh`.
- Storage: macros, macro labels and the MRU lists are in the emulated EEPROM. The MRU
  lists are written only at suspend or on a host save, and only when `mru_dirty()`.
  Combos are in their own flash sectors and are copied to the other half by
  `poly_combos_commit()`.
- Build switches: `POLY_RUNTIME_COMBOS` (every build except the DOOM monolith),
  `LATIN_PICKER_SLOTS` (12 on split72, 10 on split42, from the variant header),
  `LANG_SLOTS_PER_PAGE` / `EMJ_SLOTS_PER_PAGE` (38 on split72, 18 on split42).
- Tests: `make test:polykybd_layer_names` links `layer_names.c`. The pure parts these
  files call are tested in `base/`: `polykybd_macro_decode`, `polykybd_macro_record`,
  `polykybd_combo_store`, `polykybd_legend_plan`.

## Important

- ⚠️ `to_static_text()`, `poly_ime_family()` and `update_displays()` must stay in
  `poly_keymap.c`. PolyKybdHost `tools/lang_demo.py` parses `to_static_text()` and
  `poly_ime_family()` from that file, and `dev/tools/check_disp_index.py` parses
  `update_displays()` and `kdisp_idle()` there. `lang_demo.py` also parses
  `keycode_helper.c`.
- ⚠️ `render_key()` and `to_static_text()` are a pair and must normalise keycodes the
  same way (for example `poly_mt_tap()` for a mod-tap). Grep `update_displays()` for
  your keycode before you believe a legend edit does anything.
- ⚠️ Handle a custom keycode in `process_record_user()` and return false, never on the
  release edge: on an `OSL()` layer a release action fires up to three times. A hidden
  key must be blank and inert.
- ⚠️ Never insert into `enum my_keycodes`. A stored keymap holds these values. Append
  new keycodes at the end, as `KC_LAT10`, `KC_LAT11` and `KC_IME` were. So
  `KC_LAT0`..`KC_LAT11` are not contiguous: use `latin_picker_slot()`. The `KCL_*` block
  is frozen at `KCL_FROZEN_COUNT` (160); later languages go in the tail block.
- ⚠️ The font headers define their `GFXfont` data, so only `poly_keymap.c` includes
  them. Other files declare the fonts `extern`.
- ⚠️ `get_local_layer()` is the synced snapshot and lags a layer change by up to one
  housekeeping pass. Do not gate rendering on it.

## Related

- [`../docs/display/legend-rendering.md`](../docs/display/legend-rendering.md)
- [`../docs/display/legend-layout.md`](../docs/display/legend-layout.md)
- [`../docs/display/display-pipeline.md`](../docs/display/display-pipeline.md)
- [`../docs/features/keycode-handling.md`](../docs/features/keycode-handling.md)
- [`../docs/features/keymap-storage.md`](../docs/features/keymap-storage.md)
- [`../docs/features/intl-layer.md`](../docs/features/intl-layer.md)
- [`../docs/features/macros.md`](../docs/features/macros.md)
- [`../docs/design/macro-record.md`](../docs/design/macro-record.md)
- [`../docs/protocol/layer-names.md`](../docs/protocol/layer-names.md)
- [`../docs/protocol/split-sync.md`](../docs/protocol/split-sync.md)
