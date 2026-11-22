# lib/split_link/

The link between the two halves. The master (the half with USB) sends state, overlays,
keymap writes and macro data to the slave over QMK split RPC transactions. It also
pulls data back from the slave. `keys_com/poly_keymap.c` registers the handlers and
runs the periodic sync. `lib/hid/`, `lib/display/fill_overlay.c`, `lib/fw_update/`,
`keys_com/poly_macro.c`, `keys_com/poly_combos.c` and some features call
`send_to_bridge()`.

## Files

- `bridge_helper.c` / `bridge_helper.h`: `send_to_bridge()`, which writes the CRC32
  into the first 4 bytes of the buffer and calls `transaction_rpc_exec()` up to
  `max_retries` times. It also holds the role (`set_com_state()`,
  `is_usb_host_side()`), `differ()`, `tid_to_str()` and the link counters
  (`poly_link_stats_t`, `poly_get_link_stats()`, `poly_link_err_permille()`). It
  prints a "Split link" line every `LINK_STATS_LOG_EVERY` (200) frames.
- `split_sync.c` / `split_sync.h`: the payload structs (`overlay_sync_t`,
  `compressed_overlay_sync_t`, `roi_overlay_sync_t`, `dynamic_keymap_sync_t`,
  `overlay_map_sync_t`, `mru_sync_t`) and the slave-side `user_sync_*_handler()`
  functions. Each handler checks the size and CRC with `SYNC_VALIDATE_OR_RETURN`. It
  also has the `*_poly` keymap wrappers (`dynamic_keymap_set_keycode_poly()`,
  `dynamic_keymap_set_buffer_poly()`, `dynamic_keymap_reset_poly()`, the migrate
  functions) and the private ops on the dynamic keymap transaction
  (`POLY_KEYMAP_OP_MACRO_LABEL`, `POLY_KEYMAP_OP_COMBO_*`).
- `slave_data.c` / `slave_data.h`: `USER_SYNC_SLAVE_DATA`, the one slave-to-master
  pull channel. The request is a `kind` byte (`enum slave_data_kind`): the LTR-559
  reading, the crash record, and the crash acknowledgement.
  `slave_data_crash_pull_tick()` pulls the slave's crash record once per link-up.
- `side.c` / `side.h`: which side this half is (`enum side_state`, `set_side()`,
  `is_left_side()`) and `enum key_split_pos` with `is_on_current_side()` /
  `is_on_other_side()`. `poly_keymap.c` sets the side from the handedness stamp.

## Core concepts

- There are 12 custom transaction IDs (`SPLIT_TRANSACTION_IDS_USER` in
  [`../../config.h`](../../config.h)). QMK allows at most 32 transactions in total.
  A new message shares an existing ID by a different payload size, as the MRU
  snapshots and the DOOM mirror do on `USER_SYNC_OVERLAY_MAP_DATA`.
- Every payload starts with a `uint32_t crc32` over the bytes after it. The split UART
  has no integrity check of its own, so this CRC is the only one.
- The slave answers with one ack byte (`poly_sync_reply_t`, values in
  `base/sync_ack.h`). The values are Hamming-spaced (distance 4) because the reply has
  no CRC.
- A `user_sync_*` handler runs on the slave's split-protocol thread, concurrently with
  its main loop. Work that takes longer (an Eden replay, a keymap reset, an EEPROM
  write) is recorded and run from housekeeping: `split_sync_drain_anim_replay()`,
  `split_sync_drain_keymap_reset()`, `mark_latin_dirty()`.
- All keymap changes go through a `*_poly` wrapper, so the slave gets the same change.
- The counters in `poly_link_stats_t` are master-only. On the slave every field is 0.
  `nack` (BUSY or REFUSED) is a valid answer and is not counted as a link fault.

## Important

- ⚠️ Never bool-test `send_to_bridge()`. Every return value is non-zero, give-up
  included. Classify it with `sync_succeeded()`. Only a successful sync may advance
  the global copy.
- ⚠️ `RPC_M2S_BUFFER_SIZE` (252) is a silent ceiling. A larger payload is dropped
  before it is sent. Add a `static_assert` for any struct that can grow.
- ⚠️ State that a host HID report sets goes to the slave as that report (in
  `dynamic_keymap_sync_t.commands`), not as new `poly_sync_t` fields.
- ⚠️ `sync_is_link_fault()` is a complement, not a list of fault values. A new ack
  value must keep the Hamming distance of 4.
- ⚠️ Never touch the SPI bus or write EEPROM inside a `user_sync_*` handler.
- ⚠️ split42 needs `POLY_SPLIT_SHMEM_RPC_GUARD`. Do not remove it.

## Related

- [`../../docs/protocol/split-sync.md`](../../docs/protocol/split-sync.md)
- [`../../docs/debugging/crash-diagnostics.md`](../../docs/debugging/crash-diagnostics.md)
- [`../../docs/debugging/investigation-history.md`](../../docs/debugging/investigation-history.md)
