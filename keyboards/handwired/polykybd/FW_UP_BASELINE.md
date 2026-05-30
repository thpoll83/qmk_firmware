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

## 2026-05-29 (run 2) — erase FIXED; slave hard-locks on first chunk; core1-restart probe

### What the hardware actually did (refutes the row-2 prediction above)
Build from `c52b0b0d`/`a37fb58a`, both halves flashed. Master log:

- `FW_UP_GET_VERSION: 0.7.2`; new image `size=249628 crc=0x37b9f444`.
- `begin-pending`: `erase=40/62 … pd_advances=40`; then `begin-ready`:
  `erase=62/62 erase_pending=0 … pd_advances=62`, BEGIN returns
  `slave_ack=0xca` (SYNC_ACK). **The deferred erase completes in full and
  the slave stays alive throughout** (every poll answers). `pd_calls=65535`
  is just the uint16 counter saturating over ~8 s — not significant.

So the previous session's "most-likely row 2 (pd_advances=0)" prediction was
**wrong**: reverting the chconf change fixed the erase path. That branch is
closed.

- **The failure moved to the first chunk.** `FW_UP_CHUNK: offset=0` → all 10
  relay retries `success: 0` (transport dead, not a rejection) → `slave_ack=0x35`.
  The slave then goes **permanently dark**: ~3 minutes of `Failed to execute
  slave_matrix` + failed `UserPoly`/`UserRoi`/`UserOverlayMap`/`UserCompressed`,
  ending in `Target disconnected, throttling connection attempts`. The slave
  never recovers (replug/reflash needed). **The master stays fully healthy** —
  keeps servicing HID and even live keystrokes. This is the "second half
  unresponsive" / Cortex-M0+ CPU-LOCKUP mode.

### Ruled out (with evidence)
| Hypothesis | Verdict | Evidence |
|---|---|---|
| Chunk (64 B) > M2S buffer → local reject | refuted | `config.h` `RPC_M2S_BUFFER_SIZE 72` ≥ 64 |
| `FW_UP_CHUNK` not registered on slave | refuted | `keymap.c` registers it unconditionally beside BEGIN/STATUS |
| Slave `uprintf` blocks in handler | refuted | `process_deferred` uprintfs every sector; erase reached 62/62 |
| Slave pushes to halted core1 via display path | refuted | display refresh gated on `!fw_up_active` (`keymap.c`); the later `UserRoi`/`UserCompressed` failures are *after* the lock, not its cause |

### The crux
`offset=0` does **no flash op** (`flush_page` only fires at 256 B, ~chunk 5) and
**no core1 work** — so a flash/XIP fault cannot explain the chunk-0 lock. The one
thing abnormal for the preceding 8 s is **core1 held in PSM reset**
(`fw_staging_begin_deferred` halts it and keeps it halted for the whole sequence).
Leading theory: once the chunk phase resumes normal scheduling, the slave's
ChibiOS-SMP core0 signals core1 over the SIO FIFO; core1 (PSM-reset, not draining)
never empties it; `multicore_fifo_push_blocking` blocks core0 **forever**.

Note the master-side `slave status (chunk-fail)` snapshot **cannot** resolve this:
the slave is already dead, so the STATUS RPC returns `RPC FAILED` and `chunk_calls`
is unreadable. (In run 1 it was also dropped by console-TX overflow from the 10×
retry burst.) The "is the chunk handler even reached?" question is unanswerable
from the master once the slave locks.

### The probe in this commit
`fw_staging_process_deferred()` now restarts core1 the instant erase completes
(before the first chunk), gated on `USE_CORE1`. Read the chunk acks on the master
log (`0xca` = accepted, `0x35` = failed):

| Probe result | Conclusion | Next move |
|---|---|---|
| chunk 0 still `0x35`, slave locks | core1-PSM-hold is **not** the cause | transport-level: slave RX of the 64 B M2S during fw_up, or ChibiOS-SMP state — instrument the slave's own console |
| chunks 0–3 `0xca`, dies at `offset=224` (first `flush_page`) | the per-flush PSM halt/restart + `flash_range_program` cycle is the killer | **cooperative core1 park** (RAM-resident FIFO-draining spin loop instead of PSM reset) + RAM-locate `flush_page` |
| all chunks `0xca`, fw_up completes | restarting core1 after erase is itself the fix | keep it, clean up, done |

Expected: the **middle** row. With the probe, `flush_page` will halt+restart core1
on every page (~976×, since core1 is now alive between flushes), re-exposing the
Vector80/NMI restart window — so a lock at the first flush both confirms the
mechanism and motivates the cooperative-park redesign.

### What's in tree as of this commit
- `base/fw_staging.c`: `fw_staging_process_deferred()` restarts core1 at
  erase-complete (diagnostic probe; see inline comment dated 2026-05-29).
- No other changes; erase/chunk/flush logic otherwise unchanged from `a37fb58a`.

## 2026-05-29 (run 3) — core1 AND payload-size both exonerated; bracket-probe added

### Probe result on hardware
Flashed the run-2 core1-restart probe. Master log:

- `begin-ready: … erase=62/62 erase_pending=0 … pd_advances=62`, BEGIN
  `slave_ack=0xca`, and the begin-ready **status read succeeded** (printed all
  counters) — slave provably alive here.
- `FW_UP_CHUNK: offset=0` → **all 10 retries `success:0`, all stamped at the
  same millisecond**, `slave_ack=0x35`, then ~3 min of `Failed to execute
  slave_matrix` + failed `UserPoly`. Master stays healthy (USB briefly
  re-enumerated, then recovered).

**Identical to run 2.** Restarting core1 the instant erase completes changed
nothing.

### Two suspects eliminated
1. **core1 is exonerated.** The chunk-0 lock is identical whether core1 is held
   in PSM reset through the chunk phase (run 2) or restarted alive at
   erase-complete (run 3). Core1 state is not the variable. The whole
   NMI/Vector80/PSM line of inquiry is irrelevant to the chunk-phase hang.
2. **Payload size is exonerated.** Normal overlay sync routinely pushes *larger*
   M2S transactions to the slave and they work (overlays render):
   `overlay_sync_t`=67 B, `compressed_overlay_sync_t`=69 B, `roi_overlay_sync_t`=69 B,
   `dynamic_keymap_sync_t`=68 B — all > the **64 B** fw_up chunk, and all ≤
   `RPC_M2S_BUFFER_SIZE`=72. So 64 B is not too big.

### What the transport numbers say
`send_to_bridge` → `transaction_rpc_exec(tid, 64, buf, sizeof(poly_sync_reply_t), …)`.
Sizes pass (64 ≤ 72; reply tiny), so `success:0` is **not** a size reject — it's a
failed UART round-trip. The slave answered a STATUS RPC microseconds before and
is dead microseconds after, so either the first chunk transaction itself kills
the slave, or the slave hangs in its main loop the instant erase finishes
(before any chunk). And `fw_staging_write_chunk` at offset 0 is a **single 56 B
`memcpy` into a static buffer** (no flash, no core1) — so the handler body is
exonerated too.

**Net: the failure is specific to the `FW_UP_CHUNK` transaction in the post-erase
fw_up state — not core1, not size, not the handler body, not the erase (which
completes).** This is the same shape as the long-standing note that
`UserCompressed`/`UserRoi` fail to the slave in some states.

### Bracket-probe added (this commit)
`hid_fw_up.c` `CMD_FW_UP_CHUNK`, gated to `offset==0`, before relaying: a small
status read (`fw_up_log_slave_status("pre-chunk")`) followed by a **64 B-M2S**
status read (the status handler ignores `in_len`, so a padded request is benign
and read-only). Decision tree for the next run:

| Master log at offset 0 | Conclusion | Next move |
|---|---|---|
| `pre-chunk` OK **and** `64B-M2S status xfer -> OK` | transport fully alive post-erase → the `FW_UP_CHUNK` transaction itself is the culprit | inspect FW_UP_CHUNK registration/ID + slave chunk handler entry vs the working overlay handlers |
| `pre-chunk` OK, `64B-M2S status xfer -> FAILED` | large-M2S transport is broken **only in the post-erase fw_up state** | what the deferred erase leaves changed for large transfers (IRQ/DMA/serial state); compare with normal overlay path |
| `pre-chunk` … `RPC FAILED` | slave is already hung the instant erase finishes, before any chunk | the hang is in the slave's post-erase main loop / housekeeping, not the chunk at all |

### What's in tree as of this commit
- `hid_fw_up.c`: offset-0 bracket probe (small + 64 B-M2S status reads).
- `base/fw_staging.c`: run-2 core1-restart probe still present (harmless now that
  core1 is exonerated; left in so run-4 conditions match run-3).

## 2026-05-30 (run 4) — bracket probe lands on Row 1: the FW_UP_CHUNK txn itself

### Probe result on hardware
```
slave status (begin-ready): … erase=62/62 … pd_calls=35992 pd_advances=62
FW_UP_CHUNK: offset=0
slave status (pre-chunk):   … erase=62/62 … pd_calls=35995 pd_advances=62
FW_UP probe: pre-chunk 64B-M2S status xfer -> OK
Bridge sync retry 0 (tid: FwUpChunk, success: 0, ack: 0, bytes: 64)   … ×10 → slave dead
```
This is **Row 1, unambiguous**:
- `pre-chunk` small status read **OK**, and `pd_calls` advanced 35992→35995 between
  the two reads — the slave is alive and running its housekeeping loop.
- A **64 B-M2S `FW_UP_STATUS` transaction succeeds** right before the chunk.
- The **64 B `FW_UP_CHUNK`** transaction — identical M2S size, same transport,
  same millisecond — fails `success:0` and the slave hard-locks.

So the failure is **specific to the `FW_UP_CHUNK` transaction**. Definitively
*not* transport, *not* payload size, *not* post-erase transport state, *not*
core1 (all eliminated runs 2–4).

### Why runs 2–4 were identical (the thing I missed)
Across runs 2–4 the **slave's chunk-handling code never changed**: run 2 was the
core1-restart-at-erase (slave, but erase-only), runs 3–4 were master-side
instrumentation (`hid_fw_up.c` relay/probe). The slave `user_sync_fw_up_chunk_handler`
+ `fw_staging_write_chunk` path has been byte-identical the whole time, so an
identical failure was guaranteed. **To change the outcome we must change the
slave chunk path and flash the SLAVE half.**

### Run-4 experiment: skip ONLY the slave staging write
Note: the 2026-05-20 "verified transport baseline" (`b4a939f6`) already ran a
no-op chunk handler and streamed every chunk — but that predates step-1's real
write (`a0510144`, 05-22) and the erase-fix (`c52b0b0d`, 05-29), so no-op-vs-real
has never been A/B'd on today's code. `config.h` `#define FW_UP_CHUNK_NOOP_PROBE`
makes `user_sync_fw_up_chunk_handler` skip **only** `fw_staging_write_chunk`
(keeping the `in_len` guard + CRC) — i.e. exactly that streaming baseline applied
to current code, so the sole variable is the staging write. **Must flash the
SLAVE.** Decision tree:

| Master log on first chunk | Conclusion | Next move |
|---|---|---|
| `FwUpChunk … success:1` + `slave_ack=0xca`, fw_up runs to COMMIT (which fails CRC — nothing was staged) | FW_UP_CHUNK transport/dispatch is fine → the hang is in the **handler body** (`fw_staging_write_chunk` / its CRC) | re-enable the body incrementally (CRC only, then memcpy, then flush) to find the wedge |
| `FwUpChunk … success:1` but `ack` ≠ 0xca | slave/master `fw_up_chunk_sync_t` size mismatch (in_len guard rejected) → **halves are not the same build** | rebuild + reflash both halves, re-test |
| `FwUpChunk … success:0`, slave dies (unchanged) | the FW_UP_CHUNK **transaction/dispatch itself** wedges, independent of handler — or the slave isn't actually running this build | confirm the slave was reflashed; then diff `USER_SYNC_FW_UP_CHUNK` enum/registration vs the working `FW_UP_STATUS` |

A no-op that changes nothing is itself informative: it means the slave didn't get
this build (verify the slave flash) or the wedge is before handler dispatch.

### What's in tree as of this commit
- `config.h`: `FW_UP_CHUNK_NOOP_PROBE` defined (diagnostic — remove later).
- `split_fw_up.c`: under that macro, `fw_staging_write_chunk` is replaced by
  `ok = true` (the `in_len` guard + CRC check still run).
- Bracket probe (`hid_fw_up.c`) and run-2 core1-restart (`fw_staging.c`) still present.

## 2026-05-30 (run 5) — ROOT CAUSE FOUND & FIXED: uint8_t overflow in write_chunk

### Probe result
With `FW_UP_CHUNK_NOOP_PROBE` (skip only `fw_staging_write_chunk`), the master log
streamed **all 4458 chunks** `slave_ack=0xca` from offset 0 to the end, reached
COMMIT, and COMMIT failed CRC ("CRC mismatch on keyboard") — exactly the expected
"success" outcome (nothing was actually staged). That conclusively places the hang
**inside `fw_staging_write_chunk`**, not the transaction/transport/erase/core1.

### Root cause
`fw_staging_write_chunk`'s page-copy loop declared the working counts as **`uint8_t`**:
```c
uint8_t space = FLASH_PAGE_SIZE - s_buf_fill;   // FLASH_PAGE_SIZE == 256
uint8_t copy  = (remaining < space) ? remaining : space;
```
`FLASH_PAGE_SIZE` is **256**. When the page buffer is empty (`s_buf_fill == 0`),
`256 - 0 = 256` **truncates to 0** in a `uint8_t`, so `copy = min(remaining, 0) = 0`,
`memcpy` copies nothing, `remaining` never decreases, and the loop **spins forever**.
The slave's core0 is stuck in the chunk handler → never replies → master sees
`success:0` → "slave hard-lock". It triggers on the **first chunk** (offset 0,
`s_buf_fill==0`) and would also hit at every 256 B page boundary.

This single bug explains **every** observation across runs 2–5: dies exactly on
chunk 0; the "lock" is an infinite loop, not a fault (so no `_unhandled_exception`,
nothing on the link); the no-op handler streams perfectly; and erase, core1 (NMI/
Vector80/PSM), payload size, and the FW_UP_CHUNK transaction id were **all red
herrings**.

### Why it stayed hidden for ~6 weeks
- The 2026-05-20 "verified transport baseline" (`b4a939f6`) used the no-op chunk
  handler — `fw_staging_write_chunk` was never called, so the loop never ran.
- Step-1 (`a0510144`, 05-22) introduced the real write **and** the bug, but the
  slave-erase hang (the chconf saga) stopped execution at BEGIN — chunks were
  never reached.
- Only after the erase was fixed (chconf revert, run 2) did execution finally
  reach chunk 0 — and immediately hit the overflow loop.

### Fix (this commit)
- `base/fw_staging.c`: widen `space`/`copy` to `uint32_t` (with a comment).
- `config.h`: `FW_UP_CHUNK_NOOP_PROBE` disabled so the real write runs again.
- Builds clean (`split72:default` → `.uf2`, exit 0).

### Expected on next hardware run (flash BOTH halves)
Chunks stream `slave_ack=0xca` AND actually land in slave staging, so
`FW_UP_COMMIT` should pass CRC and the update should complete. If COMMIT now
passes, the diagnostics still in tree (bracket probe in `hid_fw_up.c`, core1
restart in `fw_staging.c`, the status counters, and this disabled no-op probe)
can be removed in a cleanup commit.

## 2026-05-30 (run 6) — chunk path FIXED end-to-end; new hang is the self-apply

### Result
The uint8_t fix is confirmed on hardware. The master log streamed **all 4458
chunks `slave_ack=0xca`** (offset 0 → 249256, full 243 KB) with the real
`fw_staging_write_chunk` active — no chunk-0 hang. The entire image transferred
and was written to slave staging.

The slave's `finalize()` CRC over the full 244 KB staged image **matched** — we
can infer this: on a CRC *mismatch* `finalize()` restarts core1 and returns
(slave stays alive), but the slave instead goes *dead*, which only happens on
the CRC-*match* path (writes header magic, arms `s_commit_pending`, returns ACK,
then housekeeping calls the apply). So the transferred+staged image is
byte-correct. **The whole transfer/staging path is verified working.**

### New failure: COMMIT → self-apply
```
Bridge sync retry 0..9 (tid: FwUpCommit, success: 0, bytes: 4) → Failed to sync
Failed to execute slave_matrix    (slave dead, 48 s+, does not return)
```
- `user_sync_fw_up_commit_handler` runs `fw_staging_finalize()` **synchronously**
  in the split-transaction context: flush final page + **CRC over 244 KB** +
  header program. That almost certainly exceeds the split-transaction timeout, so
  the master sees COMMIT `success:0` (reported to host as "CRC mismatch") even
  though finalize actually succeeds.
- finalize then arms `s_commit_pending`; housekeeping calls
  `fw_staging_apply_and_reboot()` → `fw_staging_do_apply()` — the RAM-resident
  routine that **erases the slave's own flash from offset 0** (boot2 + app),
  copies staging in, and `NVIC_SystemReset`s. The slave never comes back, so the
  self-flash hangs or bricks (this code had never executed before run 6).

### Where this leaves us
- Transfer + staging + slave-side CRC verify: **working** (huge progress from
  the chunk-0 hang).
- Remaining blocker is the **self-apply+reboot** — a distinct, delicate subsystem
  (RP2040 self-reflash from offset 0, boot2/XIP hazards), and currently
  asymmetric: the master is relay-only (baseline steps 2–4 not done), so only the
  **slave** self-applies+reboots while the master stays on old firmware.
- `finalize()` should also be split: return the ACK *before* the long CRC/apply
  so the COMMIT transaction doesn't time out and mis-report.

### Options for next step (pick one)
1. **Lock in the win:** make COMMIT verify-CRC + report success *without*
   auto-applying/rebooting (gate the apply behind a separate explicit step), so
   we have a stable "data path 100%, CRC verified on slave" baseline and stop
   bricking the slave while the apply is designed carefully.
2. **Harden/debug the self-apply now:** check the staged image begins with valid
   boot2, verify the RAM-resident apply + pico-sdk boot2-copyout path, and split
   finalize so COMMIT ACKs before the slow work. Riskier (may brick repeatedly).

### Resolution (chose option 1 — decouple, lock in the win)
`fw_staging_finalize()` now:
- verifies via a **running CRC** (`s_staged_crc`, accumulated byte-for-byte in
  `fw_staging_write_chunk`) instead of re-scanning 244 KB — so the COMMIT handler
  returns in ~one page-flush time and the split transaction no longer times out
  (fixes the false "CRC mismatch" report);
- does **NOT** arm `s_commit_pending` / write the staging header, so the
  housekeeping auto-apply (`keymap.c` → `fw_staging_apply_and_reboot`) never
  fires — **no self-flash, no reboot, no brick**;
- **restarts core1** (held in PSM reset since `begin_deferred`) because we are no
  longer hard-resetting the chip — otherwise the slave would resume with a dead
  core1 (no RLE overlay decompression).

Expected on hardware now: all 4458 chunks stream `0xca`, `FW_UP_COMMIT` **ACKs**
(host shows success), and the slave **stays alive** on its existing firmware
(staging written + received-CRC verified, but intentionally not applied). This is
the stable "transfer + stage + verify = 100%" baseline.

`fw_staging_apply_and_reboot()` / `fw_staging_do_apply()` remain in the tree,
now dead code (never invoked), ready to be wired to a separate explicit
apply step later — at which point the offset-0 self-erase + boot2/XIP handling
must be designed/verified carefully (and the master made non-relay-only so both
halves update coherently).
