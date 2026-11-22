# lib/drivers/

Hardware drivers for one keyboard half: the per-keycap OLED array on SPI and its
shift-register chip-select chain, the external QSPI flash (firmware staging, the crash
archive, the flash unique ID) and the default-layer EEPROM slot. `keys_com/poly_keymap.c`
initialises and uses all of them. `lib/display/`, `features/anim/` and both
`status_oled.c` files draw through `disp_array`. `lib/fw_update/`, `lib/hid/` and
`doom/` write flash through `fw_staging`.

## Files

- `disp_array.c` / `disp_array.h`: the keycap OLED driver (SSD1306-compatible panels on
  one SPI bus). It draws into a 128×64 scratch buffer (`get_scratch_buffer()`), of which
  the 72×40 window at column `BUFFER_X` (28) is shown. Text goes through
  `kdisp_write_gfx_text_cy()`, which walks a display list: control codepoints such as
  `\x10` (small), `\x16` (mid face), `\x14` (erase) and the composite ops (half, thin,
  rotate, badge, frame). `kdisp_send_window()` sends the window; after
  `kdisp_track_panel(idx)` it sends only the union of the old and new bounding box.
  Also the panel setup (`kdisp_hw_setup()`, `kdisp_init()`, `kdisp_setup()`), contrast,
  invert, scroll, bitmaps, courtyards and the tab chrome of the emoji and language
  layers.
- `shift_reg.c` / `shift_reg.h`: the shift-register chain that drives the panels'
  chip-select lines (`SR_CLK_PIN`, `SR_DATA_PIN`, `SR_LATCH_PIN`).
  `sr_shift_out_buffer_latch()` latches a select mask, `sr_shift_once_latch()` shifts
  one bit to select the next panel (the split72 render walk), and `sr_shift_out_0_latch()` selects every panel of
  the half.
- `spi_helper.c` / `spi_helper.h`: `spi_prepare_commands()` and `spi_prepare_data()` set
  `SPI_DC_PIN` low or high. The body of `spi_hw_setup()` is commented out, and
  `spi_reset()` is declared but has no definition.
- `helpers.c` / `helpers.h`: `peripherals_reset()` pulses `HW_RST_PIN` low for 2 µs, and
  the `PK_MIN`, `PK_MAX`, `PK_ABS` and `PK_POW` macros.
- `e2prom.c` / `e2prom.h`: `persistent_default_layer_get()` / `_set()` read and write
  the default layer through QMK's `eeconfig`.
- `fw_staging.c` / `fw_staging.h`: streams an image into flash in chunks and applies it.
  The header holds the authoritative flash map (`FW_STAGING_OFFSET`,
  `FW_RESOURCE_OFFSET`, the apply log, crash log, handedness stamp and combo store
  carved from the top of staging, `FW_UP_MAX_SIZE`). Targets are `fw_target_t`:
  `FW_TARGET_FIRMWARE`, `FONTPACK`, `DOOMWAD` and `DOOMPACK`. The main calls are
  `fw_staging_begin*()`, `fw_staging_write_chunk()`, `fw_staging_finalize()` and
  `fw_staging_apply_and_reboot()`. It also verifies the Ed25519 signature
  (`fw_sig_verify()`) and runs the ACCEPT/REJECT prompt for an unsigned image
  (`fw_staging_confirm_*`).
- `crash_record.c` / `crash_record.h`: the fault handlers (`HardFault_Handler`,
  `_unhandled_exception`) record a 48-byte `poly_crash_record_t` into a NOLOAD RAM
  block and reboot. `crash_record_init()` archives it to `FW_CRASH_LOG_OFFSET` at the
  next boot. Also the phase breadcrumb (`crash_phase_enter()` / `_leave()`), the cmd 39
  reply body, the slave record pull and its acknowledgement, and the watchdog
  (`crash_watchdog_arm()`, `_start()`, `_feed()`, `_stop()`, 8 s).
- `hw_uid.c` / `hw_uid.h`: `poly_hw_uid_boot_init()` reads the 8-byte flash unique ID
  once at `keyboard_setup()`. `poly_hw_uid()` returns the copy in RAM for cmd 50.

## Core concepts

- Both halves run these drivers for their own panels and their own flash chip. The
  master forwards firmware chunks, crash pulls and the slave's hardware ID over the
  split link (`lib/fw_update/split_fw_up.c`, `lib/split_link/slave_data.c`).
- Flash map (8 MB): 0–2 MB running firmware, 2–4 MB staging, 4 MB to `0x7FE000`
  resources (font packs, DOOM data), and the top 8 KB is QMK's wear-levelling EEPROM
  (`FW_EEPROM_RESERVE_SIZE`). `_Static_assert`s in `fw_staging.c` keep the regions apart.
- A flash erase or program flushes the XIP cache. core1 must not run code from flash at
  that time. `fw_staging.c` and `crash_record.c` hold core1 with `core1_hold()` /
  `core1_release()` (`polymod_core1_owner.h`). Before core1 is launched
  (`crash_record_init()`, `poly_hw_uid_boot_init()`), they must not take a hold.
- The slave starts an erase with `fw_staging_begin_deferred*()` and erases one unit per
  `fw_staging_process_deferred()` call from housekeeping. The firmware image erases in
  64 KB blocks, a resource slot in 4 KB sectors, with `FW_ERASE_GAP_MS` between them.
- The SPI source files are listed per variant in `split72/rules.mk` and
  `split42/rules.mk`. `crash_record.c` and `hw_uid.c` are in the shared `POLY_SRC` list.
  `FW_REQUIRE_SIGNATURE` makes an unsigned image raise the prompt.
- The hardware code has no unit test. The pure parts it calls are tested on the host:
  `make test:polykybd_fw_stage` (the IDLE / ERASING / RECEIVING stage machine in
  `base/fw_stage.c`), `polykybd_fw_sig_policy` (the placeholder-key refusal),
  `polykybd_crash_ack` (when a slave record is no longer fresh), `polykybd_hand_stamp`
  (the handedness decision) and `polykybd_font_bbox` (the measurement side of the
  display list, `base/font_lookup.c`).

## Important

- ⚠️ The self-apply page buffer in `fw_staging_do_apply()` must be `uint32_t`. A
  `uint8_t` buffer copied word by word through a cast HardFaults the M0+ inside a
  function that never returns. It bricked boards.
- ⚠️ COMMIT must not block while it waits for the ACCEPT/REJECT keypress. It runs on the
  loop that scans the matrix, so it answers `?` until the answer is known.
- ⚠️ Each sector taken from the top of staging needs an alignment assert as well as an
  overlap assert, and shrinks `FW_UP_MAX_SIZE`. The host's `hid_fw_up.py` mirrors that
  value.
- ⚠️ A new display-list op needs the same change in three walkers: the draw in
  `disp_array.c`, the measurement in `base/font_lookup.c` and the host's
  `oled_preview.py`. Render every legend you change and require 0 pixels outside the
  72×40 window.
- ⚠️ The scratch buffer has no slack above the window (`BUFFER_SLACK_N` is 0). A legend
  may leave the window to the left, right and bottom, never to the top.
- ⚠️ Arm a boot-window watchdog with `crash_watchdog_arm()`, never `crash_watchdog_start()`.
  `_start()` declares that the boot finished.

## Related

- [`docs/display/display-pipeline.md`](../../docs/display/display-pipeline.md): the
  panel grid, chip-select mapping and render paths.
- [`docs/display/legend-rendering.md`](../../docs/display/legend-rendering.md) and
  [`docs/display/legend-layout.md`](../../docs/display/legend-layout.md): the display-list
  ops and legend placement.
- [`docs/fw-update/fw-staging.md`](../../docs/fw-update/fw-staging.md) and
  [`docs/fw-update/signing.md`](../../docs/fw-update/signing.md): staging, the apply and
  the signature prompt.
- [`docs/debugging/crash-diagnostics.md`](../../docs/debugging/crash-diagnostics.md):
  the crash record, the watchdog and the boot window.
