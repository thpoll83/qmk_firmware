# split42/

The 42-key variant on the CRKBD (Corne) footprint: 21 keys and 21 keycap OLEDs per
half, a 128×32 status OLED, no trackpad. Build it with
`qmk compile -kb polykybd/split42 -km default`. The keyboard-level `polykybd.h`
includes `split42.h` for this build, so the shared code in `keys_com/` and `lib/` reads
the variant macros and the display functions from here.

## Files

- `keyboard.json`: the QMK data-driven definition. USB VID `0x2021`, PID `0x2008`, the
  `LAYOUT_lr_stacked42` layout, the enabled QMK features and the community modules
  from `modules/polykybd/` (the split72 list without `polymod_cirque_gestures`).
- `config.h`: the matrix size (`MATRIX_ROWS_PER_SIDE` 4, `MATRIX_COLS` 6), the SPI and
  shift-register pins (`NUM_SHIFT_REGISTERS` 3), `POLY_SPLIT42_MIRROR_RIGHT`,
  `DYNAMIC_KEYMAP_LAYER_COUNT` (13), the EEPROM layout, `OLED_DISPLAY_128X32`, and the
  RGB matrix settings (42 LEDs, `RGB_MATRIX_SPLIT { 21, 21 }`, data pin GP16).
- `rules.mk`: the QMK build options and the same shared `SRC` list as split72. It sets
  `-DPOLYKYBD_LTR559_DRIVE`, `-DPOLY_SPLIT_SHMEM_RPC_GUARD`, `-DEMJ_SLOTS_PER_PAGE=18`
  and `-DLANG_SLOTS_PER_PAGE=18`.
- `split42.c` / `split42.h`: the per-keycap display selection. `key_display[]` is a
  6-wide table from display index to shift-register bitmask (`BITMASK1`..`BITMASK3`).
  `key_has_display()` (always true), `key_display_index()`,
  `display_index_to_matrix()` and `invert_display()` convert between matrix and display
  positions. `matrix_scan_kb()` inverts a keycap OLED while its key is held. The header
  sets `POLY_DISP_SELECT_BY_TABLE`, `POLY_DISP_ROW_0` / `POLY_DISP_ROW_3`,
  `POLY_SPLASH_*`, `LATIN_PICKER_SLOTS` (10) and `POLY_KB_NAME` ("Split42").
- `status_oled.c` / `status_oled.h`: the status display. The 128×32 panel is mounted
  rotated by 90°, so `oled_update_buffer()` composes a 32×128 portrait screen and
  `pset()` rotates each pixel into the physical page buffer.
  `oled_update_buffer_fw_update()` draws the firmware-update screen. On the current
  hardware revision the I2C0 bus of the status OLED (GP0/GP1) is not broken out, so the
  panel is not connected (`config.h`).
- `halconf.h` / `mcuconf.h`: ChibiOS HAL and RP2040 MCU settings. Both files are
  identical to the split72 copies.

## Subfolders

- [`keymaps/default/`](keymaps/default/README.md): the layer data, the encoder map and
  the RGB LED positions of this variant.

## Core concepts

- This folder holds hardware data and the display wiring only. All key behaviour is in
  `keys_com/poly_keymap.c`, which both variants compile.
- `POLY_DISP_SELECT_BY_TABLE` makes the shared render and idle scans select each keycap
  straight from `key_display[]`, the same table that `invert_display()` uses. The
  6-column matrix uses only the low 6 bits of each shift register, so the split72 scan
  (one bit shifted per column) would select the wrong panel. `POLY_DISP_ROW_0/3` are
  unused here.
- There is no right-half column fold. `key_display_index()` folds only the matrix row
  into 0..3.
- split42 ships as two left boards. `POLY_SPLIT42_MIRROR_RIGHT` (`config.h`) mirrors the
  right half's columns in the compiled keymap and puts its thumbs at cols 3..5
  (`POLY_LAYOUT` in `keymaps/default/keymap.c`).
- The RGB matrix and the LTR-559 driver are compiled in, although no LEDs are fitted
  and the I2C0 bus is not broken out. The sensor probe fails and the driver turns itself
  off.
- Layout preview of the status screen: `dev/tools/status_oled42_preview.py` (`--diag`
  for the clip check).

## Important

- ⚠️ `POLY_SPLIT_SHMEM_RPC_GUARD` is required for the split link to come up. Do not
  remove it. The writer it guards against was never found.
- ⚠️ `key_display[]` must stay 6 wide (one matrix row per line). Copying split72's
  8-wide table moves every row after row 0 by 2 and inverts the wrong keycap.
- ⚠️ The thumb entries of `key_display[]` for the right half are not verified on
  hardware. The left-half thumbs were checked on the bench on 2026-07-17.
- ⚠️ A change to `POLY_SPLIT42_MIRROR_RIGHT` takes effect only after an EEPROM reset,
  because the firmware runs the dynamic keymap stored in EEPROM.
- ⚠️ No enabled peripheral IRQ may preempt USB. I2C0, SPI0 and SPI1 are at priority 3 in
  `mcuconf.h`.

## Related

- [`../docs/build/variants.md`](../docs/build/variants.md)
- [`../docs/display/status-oled.md`](../docs/display/status-oled.md)
- [`../docs/display/display-pipeline.md`](../docs/display/display-pipeline.md)
- [`../docs/design/split42-eden.md`](../docs/design/split42-eden.md)
- [`../docs/archive/split42-link-status.md`](../docs/archive/split42-link-status.md)
- [`../docs/build/upstream-patches.md`](../docs/build/upstream-patches.md)
