# lib/fw_update/

Firmware update over raw HID, and the relay of every staged upload (firmware, font
pack, DOOM slots) from the master to the slave. The host app and the HIL rig send the
commands. `lib/hid/hid_com.c` calls `hid_fw_up_receive()`. `lib/hid/hid_fontpack.c`
and `lib/hid/poly_pipe.c` use the relay functions. The flash writes themselves are
in `lib/drivers/fw_staging.c`.

## Files

- `hid_fw_up.c` / `hid_fw_up.h`: `hid_fw_up_receive()`, the HID commands 0x40 to 0x45.
  `CMD_FW_UP_BEGIN` (size and CRC32, starts the erase on both halves),
  `CMD_FW_UP_CHUNK` (`FW_UP_CHUNK_SIZE`, 56 bytes), `CMD_FW_UP_SIGNATURE` (the 64-byte
  Ed25519 signature in two parts), `CMD_FW_UP_COMMIT` (verify on both halves, no
  reboot), `CMD_FW_UP_APPLY` (install and reboot both halves) and
  `CMD_FW_UP_GET_VERSION` (`FW_VERSION`, size and CRC of the running image).
- `split_fw_up.c` / `split_fw_up.h`: the `USER_SYNC_FLASH_STAGE` transaction. One
  transaction carries all sub-operations, selected by the `op` word
  (`enum flash_stage_op`: BEGIN, CHUNK, COMMIT, STATUS, VERSIONS, HW_UID). Master side:
  `fw_up_xfer_begin()`, `fw_up_xfer_chunk()`, `fw_up_xfer_block()`,
  `fw_up_relay_chunk_to_slave()`, `fw_up_xfer_commit_slave()`,
  `fw_up_slave_refused_commit()`, `fw_up_slave_versions_tick()` and
  `fw_up_query_slave_hw_uid()`. Slave side: `user_sync_flash_stage_handler()`. The file
  also has `USER_SYNC_RESET` (`poly_reset_sync_t`, `RESET_ACTION_APPLY` or
  `RESET_ACTION_REBOOT` with an optional handedness change), sent by
  `fw_up_send_slave_reset()` and handled by `user_sync_reset_handler()`.
- `fw_apply.c` / `fw_apply.h`: the keyboard side of an apply. `fw_apply_tick()` runs the
  apply step machine one stage per housekeeping pass (keys cleared and the board blanked,
  EEPROM flushed with core1 held off, the staged image verified in flash, then
  `fw_staging_apply_and_reboot()`), the reboot path, deferred staging and the slave's
  font-pack reload; it returns true when a stage ran and housekeeping must end the pass.
  It also owns the status-OLED firmware screens and their timers (`poly_fw_screen()`,
  `poly_fw_hold_active()`, `poly_fw_failure_detail()`, `fw_apply_screen_tick()`) and
  `poly_board_unusable_cue()`.

## Core concepts

- The master runs the HID side. The slave stages its own copy from the relayed
  chunks. Each block goes to the slave first, then to the master's flash
  (`fw_up_xfer_block()`), so the two write cursors stay equal. On a failure the reply
  gives the lower of the two cursors as the resume offset.
- A chunk reply (`fw_up_chunk_reply_t`) names the chunk it acknowledges. A stale ACK
  from the previous chunk is not counted as a delivery.
- A relay message is `FW_UP_CHUNK_HDR` (16) plus up to `FW_UP_RELAY_CHUNK_MAX` (236)
  bytes, so a full bulk-pipe chunk uses all 252 bytes of `RPC_M2S_BUFFER_SIZE`.
  `static_assert`s in `split_fw_up.h` check both limits.
- COMMIT replies `'.'` (both halves verified), `'?'` (an unsigned image waits for the
  ACCEPT/REJECT prompt on the keyboard; re-poll), `'S'` (refused: not validly signed)
  or `'!'` (staged CRC mismatch). A COMMIT with `data[2] == 'x'` cancels the prompt.
- APPLY sends `RESET_ACTION_APPLY` to the slave, then calls `fw_staging_arm_apply()`.
  Both halves install and reboot from housekeeping, so they restart together.
- `fw_up_slave_refused_commit()` separates a slave refusal from a lost ack. The
  decision is `fw_up_classify_commit_failure()` in `base/fw_up_verdict.c`, tested by
  `make test:fw_up_verdict`.

## Important

- ⚠️ The slave-side handlers run inside a split transaction (about 20 ms). Heavy work
  is armed there and done in housekeeping: the apply, the font reload after a
  font-pack COMMIT, and the handedness write (`poly_hand_set_pending()`).
- ⚠️ While the unsigned-image prompt is up, COMMIT does not re-run the slave's
  finalize. Each re-run would erase and stamp the slave's staging header sector again.
- ⚠️ `fw_up_active` alone cannot tell a refusal from a success whose ack was lost.
  Read the recorded verdict (`fw_up_query_slave_status()`).
- ⚠️ Every size taken from the top of the staging region shrinks `FW_UP_MAX_SIZE`
  (`lib/drivers/fw_staging.h`). The host's `hid_fw_up.py` has a copy of that value.
- ⚠️ A release is published only after a green firmware-APPLY run on the rig
  (`dev/tools/require_fwapply_run.py`). That run applies the image built from the released commit on the
  rig's keyboard.

## Related

- [`../../docs/fw-update/fw-staging.md`](../../docs/fw-update/fw-staging.md)
- [`../../docs/fw-update/signing.md`](../../docs/fw-update/signing.md)
- [`../../docs/protocol/bulk-pipe.md`](../../docs/protocol/bulk-pipe.md)
- [`../../docs/archive/fw-up-baseline.md`](../../docs/archive/fw-up-baseline.md)
