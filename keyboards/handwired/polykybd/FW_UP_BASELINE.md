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
- `chconf.h` — **new**: overrides `CORTEX_ALTERNATE_SWITCH = TRUE` so the
  ChibiOS context-switch trap is PendSV (maskable by PRIMASK) instead of
  NMI (not maskable).  Required for slave-side flash erase to be safe — see
  "Step-1 regression" below.
- `config.h` — added 5 fw_up TIDs, `SPLIT_MAX_CONNECTION_ERRORS = 200`.
- `bridge_helper.c` — fw_up TID names + `reply.ack` reset between retries.
- `hid_com.c` — fall-through dispatch into `hid_fw_up_receive()`.
- `rules.mk` / `split72/rules.mk` — added `hid_fw_up.c`,
  `split_fw_up.c`, `base/fw_staging.c`.
- `split72/keymaps/default/keymap.c` — 5 `transaction_register_rpc()`
  calls, `fw_staging_init()`, `fw_staging_process_deferred()` and
  `fw_staging_commit_pending()` in `housekeeping_task_user`, and
  `if (!fw_staging_fw_up_active())` guard around brightness/default-layer
  saves + display refresh.

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

## Step-1 regression — flash erase hang on slave

Adding the slave-side `fw_staging_write_chunk()` in step 1 surfaced a
**pre-existing latent bug** in the architecture: the slave's
`flash_range_erase` in `fw_staging_process_deferred()` was hanging,
not advancing `s_erase_pending`, BEGIN polls timing out at 90 s.

### Root cause

`save_and_disable_interrupts()` sets PRIMASK=1 — masks all
configurable-priority exceptions but **NOT NMI or HardFault**.
`flash_range_erase()` then exits XIP for ~50 ms while the bootrom
operates the SSI.  In the previous (working-by-accident) firmware,
nothing tried to fire NMI on slave's core0 during that window.

After the upstream merge that landed `g_led_config` and `ALL_FONTS` in
`.rodata` (commit `66cfc10c`), flash layout shifted enough that the
combination of:

- ChibiOS RP2 port's `NMI_Handler` (the SMP context-switch handler when
  `CORTEX_ALTERNATE_SWITCH=FALSE`, the default)
- Vector80 (SIO_IRQ_PROC1) `CH_IRQ_EPILOGUE` writing `ICSR.NMIPENDSET`
- the [pico-sdk SIO FIFO IRQ quirk](https://github.com/raspberrypi/pico-sdk/issues/284)
  (FIFO IRQ keeps firing despite NVIC ISER being clear)

...started reliably firing NMI on slave's core0 during the erase
window.  NMI handler lives at flash address `0x10011649` (see CLAUDE.md
"Bug: core1 hangs ..." vector address table).  Fetching from flash with
XIP off → hang.

The slave appears "alive" because its high-priority `SlaveThread`
(handling RPCs) is on a separate stack and can still respond to BEGIN
polls — it returns `SYNC_CRC32_ERR` ("erase in progress") because
`s_erase_pending` is still true, since slave's main thread is stuck in
the bootrom.  Master polls forever, host times out at 90 s.

### Fix in this commit

`chconf.h`: set `CORTEX_ALTERNATE_SWITCH = TRUE`.  Moves the ChibiOS
context-switch trap from NMI to PendSV.  PendSV is held off by PRIMASK,
so during `flash_range_erase`'s IRQ-disabled window the context-switch
can't fire — and even when it does fire (after `restore_interrupts`),
PendSV is dispatched normally.

The existing `cpsid i` in `core1_entry` (`multicore_exec.c`) continues
to protect core1 from the same SIO FIFO IRQ quirk; this chconf change
is purely about giving core0 a maskable preemption trap during fw_up.

### What this does NOT fix

- The fundamental architectural issue: in-application flash on dual-core
  RP2040 with an SMP RTOS is hostile (see web research summary in chat
  history).  The proper long-term answer is a separate bootloader stage
  (picowota-style) entered via reboot.
- `flash_range_program` (used in `flush_page()` every 5 chunks) has the
  same IRQ-disabled window and the same risk.  Should also be safer
  now with the chconf change, but watch for chunk-time hangs.
- Master-side flash erase (currently stubbed in baseline; needed for
  step 3).  Same risk profile when re-introduced.

### Sources

- [pico-sdk flash.c](https://github.com/raspberrypi/pico-sdk/blob/master/src/rp2_common/hardware_flash/flash.c)
- [pico-sdk #679 — bootrom block-erase hang](https://github.com/raspberrypi/pico-sdk/issues/679)
- [pico-sdk #284 — SIO FIFO IRQ quirk](https://github.com/raspberrypi/pico-sdk/issues/284)
- [FreeRTOS SMP RP2040 thread](https://forums.freertos.org/t/freertos-smp-on-rp2040-caused-flash-erase-and-write-fails/15891)
- CLAUDE.md "Bug: core1 hangs ..." (this repo)

## 2026-05-29 — chconf reverted, slave still hangs, diagnostic counters added

### What was tried
`chconf.h` override set `CORTEX_ALTERNATE_SWITCH = TRUE` on the
hypothesis that an unmaskable NMI fires on the slave's core0 during
`flash_range_erase`'s IRQ-disabled window (bootrom XIP-off ~50 ms) and
the NMI handler — at flash address `0x10011649` — wedges the core when
the bus tries to fetch it. Moving the trap to PendSV (maskable by
PRIMASK) should have made the erase window safe.

### What happened on hardware
The regression got **worse**, not better:

- Master log shows BEGIN polling still timing out at 90 s.
- New `slave status (begin-pending)` snapshots (added in commit
  `0a663aa6`) show `erase=0/62` snapshot after snapshot — the slave's
  erase progress never advances at all. Before the chconf change at
  least some sectors were erasing before the hang.
- COMMIT therefore fails on the master with "CRC mismatch on keyboard"
  for the same reason as the previous run (slave staging is all 0xFF).
- A restart (power-cycle of either half) clears the state; nothing
  bricked. The slave is responsive enough between hangs that the
  status RPC still answers.

The chconf change is therefore reverted in `c52b0b0d`. It either had
no effect (the macro is a ChibiOS port-internal symbol whose actual
build-time activation we couldn't verify locally) or it had the
opposite effect of what was intended.

### What was added in its place
Two new diagnostic counters in `base/fw_staging.c`, surfaced through
the existing `USER_SYNC_FW_UP_STATUS` RPC:

| Field | Meaning |
|---|---|
| `process_deferred_calls` | bumped on every entry into `fw_staging_process_deferred()` — proves the slave's `housekeeping_task_user()` is actually scheduling us. |
| `process_deferred_advances` | bumped after `flash_range_erase` *returns* — proves the bootrom call completed and didn't hang in XIP-off. |

Master's `fw_up_log_slave_status()` now prints them as
`pd_calls=N pd_advances=M`. The periodic `begin-pending` snapshot
fires every 16 BEGIN polls (~4 s at our 250 ms cadence) so a 90 s
timeout gives us ~22 snapshots.

### Decision tree for the next session

Flash the new firmware and read the `pd_calls` / `pd_advances` /
`erase_sector_next` trajectory across snapshots:

| Pattern | Conclusion | Where to look next |
|---|---|---|
| `pd_calls=0` throughout | Slave's `housekeeping_task_user` is never scheduled — or our `fw_staging_process_deferred()` call inside it is being skipped. | `split72/keymaps/default/keymap.c` housekeeping — check fw_up suppression guard order, verify `fw_staging_init()` ran on slave. |
| `pd_calls` grows steadily, `pd_advances=0`, `erase_sector_next=0` | First call to `flash_range_erase` enters bootrom and never returns. | The NMI-from-flash hypothesis is still alive, but chconf didn't move the handler. Try moving `fw_staging_process_deferred` body (or at least the `flash_range_erase` wrapper) into RAM with `__attribute__((section(".time_critical")))` or `__not_in_flash_func`. Or put the slave into a real lockout (multicore_lockout request → core0 alone running RAM-resident code) for the erase. |
| `pd_calls` grows, `pd_advances` grows, but `erase_sector_next` doesn't keep up | Counter/state-machine bug — sector erase succeeded but the index isn't advancing. | Inspect the post-erase block in `fw_staging_process_deferred` — `s_erase_sector_next++` is conditional on something. |
| `pd_advances` grows to 62 but `erase_pending` stays `1` | State-machine bug in the "all done" branch. | Same file, the `if (s_erase_sector_next >= s_erase_sector_count)` block. |
| `pd_calls` and `pd_advances` both grow to 62, `erase_pending=0`, but BEGIN poll still returns `SYNC_CRC32_ERR` | Erase finished but the BEGIN-poll handler is reading stale state — race between status update and reply. | `split_fw_up.c` `user_sync_fw_up_begin_handler` — make sure it re-reads `erase_pending` after invoking process_deferred. |

The most-likely outcome is row 2 (pd_calls grows, pd_advances=0).
That definitively localises the hang to the bootrom call itself and
points at the same NMI-during-XIP-off mechanism — but rules out the
chconf approach. The next move would then be RAM-locating the
critical handlers, not chasing more ChibiOS config knobs.

### What's in tree as of `c52b0b0d`
- `chconf.h` removed (chconf override reverted).
- `base/fw_staging.{c,h}`: new `process_deferred_calls` /
  `process_deferred_advances` counters, exposed in
  `fw_staging_status_t`.
- `hid_fw_up.c`: master's `fw_up_log_slave_status()` prints the new
  fields as `pd_calls=N pd_advances=M`.
- Step-1 slave-side write (`fw_staging_write_chunk` in chunk handler)
  is still active — chunks attempted to write are still going through
  `flush_page` every 5 chunks. If `pd_advances` stays 0 but BEGIN
  somehow gets past erase, watch chunk processing too: the
  `flush_page` path uses `flash_range_program`, identical risk
  surface.

### Open questions / things not tried yet
1. **RAM-locate the erase scheduler.** Mark
   `fw_staging_process_deferred` (or at minimum the bootrom call
   wrapper) with `__not_in_flash_func(...)` so that whatever fires
   during the XIP-off window, it doesn't need flash to find a
   handler. Same for `flush_page` once chunks become real.
2. **Confirm the slave actually rebuilt with the new code.** The
   build artifacts for split72 and the per-half flashing flow matter
   here — if only master got the new firmware, slave is still on
   pre-counter code and snapshots will look bogus. Verify both
   halves' build dates / git hash via the existing version RPC
   before trusting the counter values.
3. **Try the real multicore lockout API.** The current
   `fw_staging_begin_deferred` halts core1 via PSM reset before the
   erase loop; replace that with the SDK's
   `multicore_lockout_start_blocking` (which is the supported API)
   and see if the erase completes. PSM reset is heavy-handed and
   leaves core1 in an undefined state.
4. **Revisit the 70 ms rate limit.** If `pd_calls` is high but
   `pd_advances` stays at 0, the gate `timer_elapsed32(s_last_sector_ms)
   < 70` is fine. But if `pd_advances` advances and then stops, check
   whether `s_last_sector_ms` got corrupted.

### How to continue from a fresh session
The new session will start on `claude/debug-fw-up-slave-hang-2o51C`
(mirrored to baseline's tip after this commit). First steps:

1. Build split72 default for both halves, flash both.
2. Trigger fw_up from PolyKybdHost.
3. Capture master serial log including all `slave status
   (begin-pending)` lines.
4. Read the `pd_calls` / `pd_advances` / `erase_sector_next` columns
   across snapshots and match against the decision table above.
5. Pick the matching follow-up and start there.
