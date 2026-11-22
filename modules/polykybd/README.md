# modules/polykybd/

The PolyKybd QMK community modules. Each `polymod_*` folder is self-contained code that
has no display, HID protocol or keymap coupling, so it can be built and tested away from
`keyboards/polykybd/`. The firmware in `keyboards/polykybd/` calls these modules; the
modules never include files from there, except `polymod_os_actions/tests`, which reads
`keys_com/poly_os.h`.

## What a community module is

- A module is enabled by listing it in the `"modules"` array of a variant's
  `keyboard.json` (`keyboards/polykybd/split72/keyboard.json`,
  `keyboards/polykybd/split42/keyboard.json`). This fork uses the keyboard-level array,
  not `keymap.json`, so a module is part of every keymap of that board. split72 lists all
  nine modules. split42 lists eight: it has no `polymod_cirque_gestures`.
- Listing a module is the whole enable. The build defines
  `COMMUNITY_MODULE_<NAME>_ENABLE` (the upper-cased folder name, for example
  `COMMUNITY_MODULE_POLYMOD_LTR559_ENABLE`) and `COMMUNITY_MODULES_ENABLE`. Consumer code
  gates on that define. There is no `SRC +=` line and no separate `-D` switch.
- `modules/polykybd/<name>/<name>.c` is compiled automatically. Any other source file
  needs `SRC +=` in the module's own `rules.mk`.
- `qmk_module.json` names the module (`module_name`), its maintainer and its license.
- Module hooks run before the keyboard and keymap hooks. `quantum/keyboard.c` calls
  `keyboard_post_init_modules()` before `keyboard_post_init_kb()`, and
  `housekeeping_task_modules()` before `_kb` and `_user`.
- For each hook in the module API, `lib/python/qmk/cli/generate/community_modules.py`
  generates three weak functions per listed module (`<api>_<module>()`,
  `<api>_<module>_kb()`, `<api>_<module>_user()`) and one call per module in
  `<api>_modules()`. So every listed module costs flash, also a library module that
  defines no hook (`polymod_crc32`, `polymod_rle`). Measured for `polymod_oled_i2c_diag`
  (qmk_firmware#453): 456 B on split72 (16 hooks) and 368 B on split42 (14 hooks), no
  RAM.
- This fork is on module API 1.1.2. The available hooks are in
  `data/constants/module_hooks/*.hjson`.

## Subfolders

- [`polymod_cirque_gestures/`](polymod_cirque_gestures/README.md): the Cirque
  absolute-mode gesture layer (tap, corner right-click, scroll dial). split72 only.
- [`polymod_core1/`](polymod_core1/README.md): the RP2040 core1 launcher and the core1
  owner (`core1_hold()` / `core1_release()`).
- [`polymod_crc32/`](polymod_crc32/README.md): table-driven CRC-32 (`crc32_1byte()`,
  `crc32_large()`).
- [`polymod_ltr559/`](polymod_ltr559/README.md): the optional LTR-559 light and
  proximity sensor driver.
- [`polymod_monocypher/`](polymod_monocypher/README.md): vendored Monocypher, used for
  the Ed25519 firmware signature check.
- [`polymod_oled_i2c_diag/`](polymod_oled_i2c_diag/README.md): diagnostics for failed
  OLED I2C writes.
- [`polymod_os_actions/`](polymod_os_actions/README.md): the per-OS chord table
  (`emit_os_action()`).
- [`polymod_prc/`](polymod_prc/README.md): the PRC 1-bit image decoder, its table and
  the Python trainer.
- [`polymod_rle/`](polymod_rle/README.md): the bit-level RLE decoder for keycap
  overlays (`rle_decompress()`).

## Important

- ⚠️ A module that defines the non-suffixed hook (for example
  `housekeeping_task_polymod_ltr559()`) replaces the top of the generated chain. It must
  call `<api>_<module>_kb()` itself, or the keyboard and keymap versions are never called.
- ⚠️ A module that probes or polls in its own hooks must not also be initialised from
  `keyboards/polykybd/`. `poly_keymap.c` does not call `ltr559_init()` or
  `ltr559_task()`; adding those calls back probes the part twice.
- ⚠️ `-Wcast-align` applies to every file under `modules/polykybd/` except
  `polymod_monocypher/` (`keyboards/polykybd/rules.mk`).
- ⚠️ A test suite is registered in two files: `builddefs/testlist.mk` includes the
  module's `tests/testlist.mk`, and `builddefs/build_test.mk` includes its
  `tests/rules.mk`. `make test:<name>` with an unregistered name exits 0 and prints
  nothing. Check for the `[  PASSED  ] N tests.` line.

## Related

- [Community modules and the LTR-559 sensor](../../keyboards/polykybd/docs/build/community-modules.md)
- [Unit tests](../../keyboards/polykybd/docs/build/unit-tests.md)
