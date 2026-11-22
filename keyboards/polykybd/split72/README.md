# split72/

The 72-key variant: RGB matrix, Cirque trackpad, 128×64 status OLED, 72 per-keycap
OLEDs. Build it with `qmk compile -kb polykybd/split72 -km default`. The keyboard-level
`polykybd.h` includes `split72.h` for this build, so the shared code in `keys_com/` and
`lib/` reads the variant macros and the display functions from here.

## Files

- `keyboard.json`: the QMK data-driven definition. USB VID `0x2021`, PID `0x2007`,
  the `LAYOUT_left_right_stacked` layout (alias `LAYOUT`), the encoder pins (GP25,
  GP29), the enabled QMK features and the community modules from `modules/polykybd/`
  (`polymod_cirque_gestures`, `polymod_core1`, `polymod_crc32`, `polymod_ltr559`,
  `polymod_monocypher`, `polymod_oled_i2c_diag`, `polymod_os_actions`, `polymod_prc`,
  `polymod_rle`).
- `config.h`: the matrix size (`MATRIX_ROWS_PER_SIDE` 5, `MATRIX_COLS` 8), the SPI and
  shift-register pins for the keycap OLEDs (`NUM_SHIFT_REGISTERS` 5), the 72 RGB LEDs
  (`RGB_MATRIX_SPLIT { 36, 36 }`) and their effects, the Cirque settings, the raw HID
  usage, `DYNAMIC_KEYMAP_LAYER_COUNT` (13), the EEPROM layout
  (`DYNAMIC_KEYMAP_EEPROM_ADDR`) and the tap-hold defaults (`TAPPING_TERM`,
  `FLOW_TAP_TERM`, `CHORDAL_HOLD`, `PERMISSIVE_HOLD`).
- `rules.mk`: the QMK build options (split, SSD1306 OLED, RGB matrix with the WS2812
  vendor driver, `cirque_pinnacle_i2c`, raw HID, WPM, dynamic keymap) and the `SRC`
  list of the base, driver and language files this variant compiles. It sets
  `-DPOLYKYBD_LTR559_DRIVE`.
- `split72.c` / `split72.h`: the per-keycap display selection. `key_display[]` maps a
  display index to a shift-register bitmask (`BITMASK1`..`BITMASK5`).
  `key_has_display()`, `key_display_index()`, `display_index_to_matrix()` and
  `invert_display()` convert between matrix and display positions.
  `matrix_scan_kb()` inverts a keycap OLED while its key is held. The header also sets
  the variant macros of the shared code: `POLY_DISP_ROW_0` / `POLY_DISP_ROW_3`,
  `POLY_FL_ALIGN_FROW`, `POLY_SPLASH_*` and `LATIN_PICKER_SLOTS` (12).
- `status_oled.c` / `status_oled.h`: the 128×64 status display. `oled_update_buffer()`
  draws the normal screen. The left half shows the layout panel (with the Num and Caps
  lock icons), the right half the RGB panel (with the speed gauge). The top row shows
  the USB or link role of the half. `oled_draw_pad_view()` draws the trackpad view and
  `oled_update_buffer_fw_update()` the firmware-update screen.
- `halconf.h` / `mcuconf.h`: ChibiOS HAL and RP2040 MCU settings: I2C and SPI on,
  `SERIAL_PIO_USE_PIO1`, `PICO_FLASH_SIZE_BYTES` (8 MB) and the IRQ priorities. Both
  files are identical to the split42 copies.
- `vphya.json`: a keyboard definition in VIA format (`menus`, `customKeycodes`,
  `matrix`, `layouts`). No build step or tool in this repo reads it.

## Subfolders

- [`keymaps/default/`](keymaps/default/README.md): the layer data, the encoder map and
  the RGB LED positions of this variant.

## Core concepts

- This folder holds hardware data and the display wiring only. All key behaviour is in
  `keys_com/poly_keymap.c`, which both variants compile. Do not add per-variant logic
  here.
- The matrix has 74 keys but there are 72 OLEDs. The inner keys at matrix (3,7) and
  (8,0) have no OLED and no RGB LED.
- Matrix and display indices differ on the right half. Matrix rows 5..8 have no col-0
  key, so matrix col `c` is display col `c-1`. `key_display_index()` is the one place
  that does this fold, and `display_index_to_matrix()` is its inverse.
- Each half inverts its own keycap OLED on a key press in `matrix_scan_kb()`. This needs
  no split sync.
- Both halves draw their own status OLED and keycap OLEDs. The slave draws from the
  synced state.

## Important

- ⚠️ Gate every key-to-display mapping on `key_has_display(r,c)`, and address a panel
  through `key_display_index()`, never `LAYOUT_TO_INDEX(r, c)`. `LAYOUT_TO_INDEX()` does
  no fold, so it is one column off on the right half. `dev/tools/check_disp_index.py`
  checks the fold (manual, not in CI).
- ⚠️ No enabled peripheral IRQ may preempt USB. I2C0, SPI0 and SPI1 are at priority 3 in
  `mcuconf.h`. I2C0 at 2 caused the `0x16C1` boot hang.
- ⚠️ `TAPPING_TERM` and `FLOW_TAP_TERM` are only the defaults. They are runtime
  settings (HID cmd 44); read them through `get_poly_tapping_term()`.
  `HOLD_ON_OTHER_KEY_PRESS` is not defined on purpose.
- ⚠️ VIA is not supported, although `vphya.json` has the VIA format. Remapping goes
  through PolyKybdHost.
- ⚠️ Check a change to `status_oled.c` with `dev/tools/status_oled_preview.py --diag`
  before you flash it. It shows every pixel drawn outside the 128×64 panel in red.

## Related

- [`../docs/build/variants.md`](../docs/build/variants.md)
- [`../docs/display/status-oled.md`](../docs/display/status-oled.md)
- [`../docs/display/display-pipeline.md`](../docs/display/display-pipeline.md)
- [`../docs/features/trackpad.md`](../docs/features/trackpad.md)
- [`../docs/features/keymap-storage.md`](../docs/features/keymap-storage.md)
- [`../docs/features/rgb-settings-row.md`](../docs/features/rgb-settings-row.md)
