# PolyKybd HID fw_up — Working Baseline

Branch: `claude/fw-up-baseline-2o51C`

This branch is the clean starting point for the HID firmware-update feature.
It contains only what has been verified end-to-end on hardware, with all
the dead-end commits from the earlier debug branch (`claude/debug-fw-up-slave-hang-2o51C`,
`claude/debug-hid-ota-update-stjPG`) left behind.

## What works on this branch

1. **Host → master HID protocol.**  All four fw_up commands
   (`CMD_FW_UP_GET_VERSION 0x43`, `CMD_FW_UP_BEGIN 0x40`,
   `CMD_FW_UP_CHUNK 0x41`, `CMD_FW_UP_COMMIT 0x42`) are routed through
   `hid_fw_up_receive()` from `hid_com.c`.

2. **Master ⇄ slave split RPCs.**  Five new transaction IDs
   (`USER_SYNC_FW_UP_QUERY`/`BEGIN`/`CHUNK`/`COMMIT`/`STATUS`) registered
   on both halves.  `SPLIT_MAX_CONNECTION_ERRORS = 200` so the slave's
   deferred sector-by-sector erase doesn't make QMK declare the link
   "disconnected".

3. **Slave deferred erase.**  `fw_staging_begin_deferred()` halts core1
   via PSM reset, schedules a 70 ms-rate-limited erase of the staging
   region (header + ⌈image/4096⌉ sectors), runs through
   `housekeeping_task_user()`.  Master polls `FW_UP_BEGIN` with a
   1-retry probe until the slave answers `SYNC_ACK` (≈ 4–5 s).
   Verified: 62/62 sectors erased every run, slave responsive
   throughout.

4. **Full chunk stream through the split transport.**  4458 × 64-byte
   `USER_SYNC_FW_UP_CHUNK` RPCs in ~37 s, every one ACK'd.  Verified
   end-to-end with no transport failure.

5. **Slave-side diagnostic state RPC.**  `USER_SYNC_FW_UP_STATUS`
   returns slave's `fw_staging` internal counters (begin/chunk call
   counts, `next_offset`, erase progress, `last_chunk_ack`).  Master
   queries it once when the slave first reports ready (`begin-ready`
   tag) and once on first chunk failure (`chunk-fail` tag) and
   `uprintf`s the result.  Critical for further bisection.

6. **`fw_up_active` housekeeping suppression.**  While the flag is set,
   `housekeeping_task_user()` skips `brightness_save_if_pending()`,
   `default_layer_save_if_pending()`, and `sync_and_refresh_displays()`
   on both halves.  Removes three sources of split-UART starvation
   (wear-leveling consolidate ≈ 100 ms IRQ-off, slave SPI display
   refresh ≈ 50–100 ms, master state-push ≈ 10 retries × 80 ms).

7. **Real `serial_transport_driver_clear()` cleanup between retries.**
   Already in tree via commit `68fd2039` in the parent branch
   (PolyKeyboard).  `pio_sm_restart` cleans PIO RX state; the function
   plus `enter_rx_state()` restart the SM after each failed receive.

8. **Bridge helper retry-log fix.**  `send_to_bridge` now resets
   `reply.ack = SYNC_CRC32_ERR` at the top of each retry iteration so
   the `ack:` field in retry-log lines is no longer stale data from a
   previous successful call.

## What is intentionally stubbed (so fw_up does NOT yet program firmware)

These two stubs together kept the transport happy and let chunks stream
through.  They must be removed in order — re-introducing flash work
piecewise — before fw_up actually updates either half.

1. **Master is a pure relay** (`hid_fw_up.c`):
   - `CMD_FW_UP_BEGIN`: no `fw_staging_begin()` — master never erases
     its own staging.  Just relays the BEGIN RPC and sets `fw_up_active`
     via `fw_staging_set_fw_up_active(true)`.
   - `CMD_FW_UP_CHUNK`: no `fw_staging_write_chunk()` — master never
     writes the chunk to its staging.
   - `CMD_FW_UP_COMMIT`: no `fw_staging_finalize()` — master never
     verifies its CRC and never arms `s_commit_pending`.  Just relays
     COMMIT and clears `fw_up_active`.

2. **Slave dummy-accepts chunks** (`split_fw_up.c`
   `user_sync_fw_up_chunk_handler`):
   - CRC32 of the incoming chunk is verified.
   - `fw_staging_write_chunk()` is NOT called — no `s_page_buf` copy,
     no `s_next_offset` advance, no `flush_page()`.
   - Reply is `SYNC_ACK` (or `SYNC_CRC32_ERR` on CRC mismatch).
   - `fw_staging_note_chunk_call()` still bumps the counter so the
     status RPC reflects reality.

The slave's BEGIN and COMMIT handlers do **real** work — only CHUNK is
stubbed.  Because chunks never wrote anything to slave staging, slave's
`fw_staging_finalize()` in COMMIT computes the CRC of an all-0xFF
region and fails — that's why the latest run reported "FW_UP_COMMIT
failed — CRC mismatch on keyboard".  Expected.

## How to extend, in order

### Step 1 — re-introduce slave-side chunk write

Replace the dummy in `user_sync_fw_up_chunk_handler` with the real
`fw_staging_write_chunk(msg->offset, msg->data, FW_UP_CHUNK_SIZE)`.
This will:
- copy 56 bytes per chunk into `s_page_buf`
- every 5th chunk (when `s_buf_fill` hits `FLASH_PAGE_SIZE = 256`)
  trigger `flush_page()`, which on slave is a `flash_range_program`
  call that **temporarily halts core1 via PSM and disables IRQs for
  ~5 ms**.

Expected risk: if the page flush wedges the slave's transport for the
next chunk RPC, we know the slave flash op is the culprit — the
status RPC counters will show whether `chunk_handler_calls` keeps
growing or stalls.

Once this works, slave's commit CRC should match the master's
expected CRC and `FW_UP_COMMIT` should succeed.

### Step 2 — re-introduce master-side chunk write

Add `fw_staging_write_chunk(offset, chunk_data, FW_UP_CHUNK_SIZE)` to
the master path *after* the slave ACK.  This is the smallest possible
master flash workload — no big erase, just per-chunk write.

### Step 3 — re-introduce master-side staging erase

Add `fw_staging_begin()` back to `CMD_FW_UP_BEGIN`.  This is the
biggest, riskiest change: 62 × `flash_range_erase` × 4096 B (~50 ms
IRQ-off each) plus core1 halt/restart.  Earlier runs showed this is
what wedged the master.

### Step 4 — re-introduce master finalize + apply-and-reboot

Add `fw_staging_finalize()` back to `CMD_FW_UP_COMMIT` and confirm the
deferred `fw_staging_apply_and_reboot()` runs from
`housekeeping_task_user()`.

## Files in this baseline

New:
- `base/fw_staging.{c,h}` — staging buffer, deferred-erase scheduler,
  RAM-resident `fw_staging_do_apply()` for the final copy + reboot.
- `hid_fw_up.{c,h}` — master HID handlers + `fw_up_log_slave_status()`.
- `split_fw_up.{c,h}` — slave split handlers (query, begin, chunk,
  commit, status).

Modified:
- `config.h` — added 5 fw_up TIDs, `SPLIT_MAX_CONNECTION_ERRORS = 200`.
- `bridge_helper.c` — fw_up TID names + `reply.ack` reset between retries.
- `hid_com.c` — fall-through dispatch into `hid_fw_up_receive()`.
- `rules.mk` / `split72/rules.mk` — added `hid_fw_up.c`,
  `split_fw_up.c`, `base/fw_staging.c`.
- `split72/keymaps/default/keymap.c` — 5 `transaction_register_rpc()`
  calls, `fw_staging_init()`, and `if (!fw_staging_fw_up_active())`
  guard around brightness/default-layer saves + display refresh.

## How to test

```bash
# Build
qmk compile -kb handwired/polykybd/split72 -km default

# Flash both halves with the same firmware
# Trigger fw_up from PolyKybdHost
# Expected serial log on master:
#   FW_UP_BEGIN ... (relay-only master)
#   slave status (begin-ready): init=1 active=1 erase_pending=0 erase=62/62 ...
#   ~4458 × "FW_UP_CHUNK: offset=N", every one slave_ack=0xca
#   FW_UP_COMMIT: relay-only master slave_ack=0x?? (will fail — expected)
```
