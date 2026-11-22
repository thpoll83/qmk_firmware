# lib/display/

The display output that is not a plain keycap legend: the HID overlay images and their
decode on core1, the core1 command loop, and the status OLED screens. `lib/hid/hid_com.c`
calls `fill_overlay` for the overlay commands, `lib/split_link/split_sync.c` calls it
for the same data on the slave, and `keys_com/poly_keymap.c` calls the core1 suspend
hooks and `status_idle_task()`. QMK calls `oled_task_user()`. The drawing itself goes
through `lib/drivers/disp_array.h`.

## Files

- `fill_overlay.c` / `fill_overlay.h`: receives overlay images into the pool
  (`base/overlay.h`). `fill_overlay_buffer()` takes raw segments (cmd 10),
  `decompress_overlay_buffer()` RLE fragments (cmd 16/17), `fill_roi_overlay_buffer()`
  region updates (cmd 18/19) and `receive_prc_overlay_report()` PRC-coded images
  (cmd 41, decoded on core0). `set_packed_overlay_mapping()` applies the
  display-to-pool mapping (cmd 21 and cmd 33). It also has the overlay action flags
  (`apply_overlay_action_flags()`), the modifier variant (`adjust_overlay_idx_to_mod()`)
  and the slave mapping repair (`note_overlay_map_sync_lost()`,
  `arm_overlay_map_repair()`, `overlay_map_repair_tick()`).
- `multicore_exec.c` / `multicore_exec.h`: core0's side of core1. It queues RLE and ROI
  decode work (`core1_decompress_fragment()`, `core1_update_roi()`) and Eden keycap jobs
  (`core1_eden_key()`) over the SIO FIFO, and `core1_entry()` is core1's command loop.
  `core1_is_busy()` gives backpressure. `raw_hid_pre_receive_kb()` refuses a HID report
  while core1 owes a fragment, and relaunches a core1 that has stalled for
  `CORE1_STALL_MS` (500 ms). Also the USB suspend hold (`core1_suspend_tick()`,
  `core1_resume()`) and the EEPROM-write hooks `wear_leveling_rp2040_flash_quiesce()` /
  `_resume()`.
- `oled_helper.c` / `oled_helper.h`: `oled_task_user()` picks the status OLED screen:
  firmware update, ACCEPT/REJECT prompt, tutorial, macro recording, idle, demo hint,
  telemetry (Settings → More) or the normal status screen. The board-specific screen is
  `oled_update_buffer()` in each variant's `status_oled.c`. It also has the boot
  progress screen (`oled_boot_progress()`), the firmware apply / restart / failed
  notices and helpers for numbers and right-aligned text (`num_to_u32_string()`,
  `oled_draw_text_right()`).
- `status_idle.c` / `status_idle.h`: split72 only (`split72/rules.mk`). The idle screen
  of the status panels: plasma contour bands with "Poly Kybd" typed and deleted across
  both panels, then a poem from `base/idle_poem_plan.c`. `status_idle_screen()` /
  `status_idle_release()` claim and release the panel, `status_idle_task()` composes a
  frame, and `status_idle_holds_bus()` tells Eden to wait while a frame is sent.

## Core concepts

- An overlay upload addresses a pool slot, and the mapping says which display shows
  which slot. `resolve_upload_side()` decides which half stores an image. The bytes for
  the other half go over the split link (`USER_SYNC_OVERLAY_DATA` and the other overlay
  transactions) as the report's bytes plus the target slot.
- `USE_CORE1` is required: `fill_overlay.c` stops the build with `#error` without it. RLE
  and ROI decode run on core1. core1 runs with interrupts masked (`cpsid i` in
  `core1_entry()`) and waits for commands in RAM (`core1_fifo_pop_ram()`).
- Before an EEPROM write, `wear_leveling_rp2040_flash_quiesce()` sends `CORE1_CMD_PARK`
  and waits up to `CORE1_FLASH_WAIT_US` (50 ms) for core1 to acknowledge from RAM. If it
  does not, core1 is forced off for the write and relaunched afterwards.
- The suspend hold and the stall relaunch run only on the master. The slave's split
  thread gives core1 its fragments, so its main loop must not reset core1.
- The status OLED is on I2C, a different bus from the SPI keycap panels. Each half draws
  its own status panel from the synced `poly_sync_t` fields (`fw_confirm`,
  `settings_more`, `rec_state`).
- Unit tests for the pure parts: `make test:polykybd_core1_suspend` (the suspend hold
  and flash-wait decisions, `base/core1_suspend.c`), `polykybd_map_codec` (the mapping
  packer used by the decoder and the repair), `polykybd_prc_record` (the cmd 41 record
  parser), `polykybd_idle_poem_plan` (the poems and their timeline) and
  `polykybd_status_brightness` (the status panel brightness map).

## Important

- ⚠️ A mapping chunk is sent to the slave once. A lost chunk must be repaired from
  housekeeping (`overlay_map_repair_tick()`), never in the HID handler: a full mapping
  is up to 34 reports, each with up to 10 retries.
- ⚠️ Every site that applies a mapping pairs `set_packed_overlay_mapping()` with
  `request_disp_refresh()` on both halves.
- ⚠️ The overlay identity mapping is required for writes. The flat index is the only
  address an upload has. Zeroing the mapping wrote every image to slot 0.
- ⚠️ Classify a bridge result with `sync_succeeded()`. Never test `send_to_bridge()` as a
  bool: every return value is non-zero.
- ⚠️ The refusal in `raw_hid_pre_receive_kb()` must end. A stopped core1 never catches
  up, and the endpoint then NAKs every host write.
- ⚠️ `num_to_u32_string()` and the other `*_u32_string()` helpers write `uint32_t`
  codepoints. `buffer_len` is the size in bytes of a `uint32_t[]` array.

## Related

- [`docs/display/display-pipeline.md`](../../docs/display/display-pipeline.md): the
  render paths and the panel grid.
- [`docs/display/status-oled.md`](../../docs/display/status-oled.md): the status OLED
  screens and their brightness.
- [`docs/display/idle-styles.md`](../../docs/display/idle-styles.md): the idle styles.
- [`docs/design/overlay-icon-library.md`](../../docs/design/overlay-icon-library.md):
  PRC coding (cmd 41) and the icon library (cmd 42).
- [`docs/protocol/split-sync.md`](../../docs/protocol/split-sync.md): the split
  transactions and the mapping repair.
- [`docs/build/upstream-patches.md`](../../docs/build/upstream-patches.md): the
  `wear_leveling_rp2040_flash_quiesce()` hook in QMK's EEPROM driver and the core1 PARK
  word.
