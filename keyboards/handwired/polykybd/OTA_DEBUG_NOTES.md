# PolyKybd HID OTA Firmware Update — Debug Session Notes

Branch: `claude/debug-hid-ota-update-stjPG`

---

## Goal

Fix the slave half permanently dying after HID OTA firmware update starts.
After master sends FwUpBegin and slave finishes its deferred flash erase,
the slave must accept subsequent FwUpChunk transactions.

---

## Architecture Recap

- **Master** = USB host side (connected to PC). Runs `fw_staging_begin()` synchronously.
- **Slave** = other half. Runs `fw_staging_begin_deferred()` + `fw_staging_process_deferred()` in housekeeping.
- Split link = UART via `SERIAL_USART_TX_PIN GP5`, timeout `SERIAL_USART_TIMEOUT = 20 ms`.
- RP2040, `USE_CORE1` defined — core1 runs display work.
- Flash is 8 MB (`PICO_FLASH_SIZE_BYTES = 8 * 1024 * 1024`).
- Wear-leveling EEPROM: `WEAR_LEVELING_BACKING_SIZE = 8192` (2 flash sectors, last 8 KB of flash).

### Key files

| File | Role |
|------|------|
| `keyboards/handwired/polykybd/base/fw_staging.c/h` | Core staging logic, flash erase/write, core1 halt/restart |
| `keyboards/handwired/polykybd/hid_fw_up.c` | Master HID handler (CMD_FW_UP_BEGIN/CHUNK/COMMIT) |
| `keyboards/handwired/polykybd/split_fw_up.c` | Slave split-RPC handlers |
| `keyboards/handwired/polykybd/bridge_helper.c` | `send_to_bridge()` retry wrapper |
| `keyboards/handwired/polykybd/split72/keymaps/default/keymap.c` | `housekeeping_task_user()` |
| `keyboards/handwired/polykybd/state.c` | `brightness_save_if_pending()`, `default_layer_save_if_pending()` |
| `keyboards/handwired/polykybd/config.h` | `SPLIT_MAX_CONNECTION_ERRORS`, `RPC_M2S/S2M_BUFFER_SIZE` |
| `quantum/split_common/split_util.c` | `connection_errors`, `is_transport_connected()` |
| `platforms/chibios/drivers/wear_leveling/wear_leveling_rp2040_flash.c` | `backing_store_erase()` = 100 ms IRQ-disabled |

---

## Root Causes Found and Fixed (in order)

### Fix 1 — PSM DONE poll infinite loop
**Commit:** `1ab1cd28`

`while (!(_PSM_DONE & _PSM_PROC1_BIT))` exits immediately because DONE reads
non-zero even during reset. Removed the poll and replaced with a DSB barrier.

```c
_PSM_FRCE_OFF |= _PSM_PROC1_BIT;
__asm volatile ("dsb" ::: "memory");   // DSB: write reaches PSM before flash ops
```

---

### Fix 2 — Per-sector core1 restart → ChibiOS Vector80/NMI hang
**Commit:** `4697f22d`

Original code halted and restarted core1 once per sector erase (~62 restarts
per OTA). Between ROM writing `VTOR=0x10000100` and `cpsid i` in `core1_entry()`,
`SIO_IRQ_PROC1` can fire → ChibiOS NMI context-switch on core1 with no thread
state → hang.

**Fix:** "Keep halted" strategy. Halt core1 **once** in `fw_staging_begin_deferred()`,
keep it halted through all erases, all page writes, and finalize.
- Success path: chip hard-resets via `fw_staging_apply_and_reboot()` — core1 never needs restart.
- Failure path (CRC mismatch): `fw_staging_finalize()` explicitly restarts core1.

Key variables: `s_core1_halted` tracks whether this module holds the PSM reset.
`flush_page()` checks `already_halted` before halt/restart to avoid double-halt.

---

### Fix 3 — `connection_errors` throttle blocking FwUpChunk
**Commit:** `8cdba301`

During deferred erase, each 50 ms sector blackout causes UART timeout →
`connection_errors++` in `split_util.c`. Default threshold = 10; reached in
~4 sectors (200 ms). Once reached, `is_transport_connected()` returns false
and `transaction_rpc_exec()` instant-fails all calls for 500 ms.

**Fix:** `SPLIT_MAX_CONNECTION_ERRORS 200` in `config.h`.

Worst-case: 62 sectors × up to 3 failures each = 186 consecutive failures.
200 > 186, so slave is never declared disconnected during erase.

**Diagnostic added to `bridge_helper.c`:**
```c
bool connected = is_transport_connected();
// ... in retry log:
uprintf("Bridge sync retry %d (tid: %s, success: %d, ack: %d, bytes: %d, conn: %d)\n",
        retry, ..., (int)connected);
```
`conn: 0` = instant fast-fail (connection_errors throttle).
`conn: 1` = real UART attempt made.

---

### Fix 4 — Wear-leveling EEPROM compact races FwUpChunk (LATEST)
**Commits:** `2fc38963`, `2f834d7b`

**Symptom in log:** FwUpChunk retries show `conn: 1, success: 0` — real UART
timeouts, not connection_errors fast-fails. All 10 retries × 20 ms = 200 ms
of timeouts immediately after slave returns SYNC_ACK for FwUpBegin.

**Root cause:** `housekeeping_task_user()` calls:
```c
brightness_save_if_pending();     // can trigger wear-leveling compact
default_layer_save_if_pending();  // can trigger wear-leveling compact
```

`backing_store_erase()` in `wear_leveling_rp2040_flash.c` calls
`flash_range_erase(base, 8192)` = 2 sectors = **~100 ms with IRQs disabled**.
Compact rewrite adds another ~100 ms → total ~200 ms blackout.

**Timing:** The EEPROM debounce timer (`BRIGHTNESS_EEPROM_DEBOUNCE_MS`)
starts during the ~5 s OTA polling phase when master keeps sending
`USER_SYNC_POLY_DATA` → `mark_settings_dirty()` on slave. Debounce expires
at exactly the moment the first FwUpChunk arrives.

**Fix:** New `fw_staging_fw_up_active()` flag. Set `true` in
`fw_staging_begin()` and `fw_staging_begin_deferred()`. Cleared `false` in
`fw_staging_init()` and at end of `fw_staging_finalize()` (both paths).

In `housekeeping_task_user()`:
```c
fw_staging_process_deferred();
if (!fw_staging_fw_up_active()) {
    brightness_save_if_pending();
    default_layer_save_if_pending();
}
```

---

## Current State

All four fixes pushed. The OTA flow should now be:

1. Host sends CMD_FW_UP_BEGIN → master calls `fw_staging_begin()` (sync erase) + sends FW_UP_BEGIN RPC to slave.
2. Slave `user_sync_fw_up_begin_handler()` calls `fw_staging_begin_deferred()`:
   - Sets `s_fw_up_active = true`.
   - Halts core1 via PSM FRCE_OFF (keeps halted entire sequence).
   - Returns SYNC_ACK_SIG.
3. Master polls slave with FW_UP_BEGIN RPCs every ~500 ms. Slave returns SYNC_CRC32_ERR while `s_erase_pending`, SYNC_ACK when done.
4. During polling, slave housekeeping erases one 4 KB sector per 70 ms.
   - `fw_staging_fw_up_active()` returns true → EEPROM saves suppressed → no compact race.
5. Host sends CMD_FW_UP_CHUNK × N. Each chunk goes to slave first (10 retries), then written to master staging.
6. Host sends CMD_FW_UP_COMMIT → `fw_staging_finalize()` on both sides.
   - CRC mismatch: `s_fw_up_active = false`, core1 restarted.
   - CRC match: `s_fw_up_active = false`, `s_commit_pending = true`.
7. `housekeeping_task_user()` detects `fw_staging_commit_pending()` → `fw_staging_apply_and_reboot()` → hard reset.

---

## Remaining Unknowns / Things to Watch

1. **Master-side EEPROM compact**: Master also runs `brightness_save_if_pending()` / `default_layer_save_if_pending()` in housekeeping. After `fw_staging_begin()` restarts master core1, any `backing_store_write_bulk()` → `pico_program_bulk()` → `flash_flush_cache()` with core1 running could cause core1 CPU LOCKUP (bus fault). `fw_staging_fw_up_active()` is set on master too, so the same guard in `housekeeping_task_user()` applies — this should be safe. Worth confirming in logs.

2. **`sync_and_refresh_displays()` with core1 halted**: On slave, `sync_and_refresh_displays()` is still called while core1 is PSM-halted. Check whether it tries to use core1 (e.g. `core1_decompress_fragment()`). If it does, it will deadlock on `core1_is_busy()` spin. The overlay decompress path (`user_sync_compressed_overlay_data_handler`) should only run during overlay upload, not OTA — but worth auditing.

3. **Deferred erase rate-limit**: `fw_staging_process_deferred()` rate-limits to one sector per 70 ms. 70 ms was chosen to leave ~20 ms of UART-responsive time per sector. With `SERIAL_USART_TIMEOUT = 20 ms`, this gives exactly one retry window. If UART proves still flaky between sectors, consider increasing the gap or raising retries.

4. **`RPC_M2S_BUFFER_SIZE = 72`**: FW_UP_CHUNK struct is 4 CRC + 4 offset + 56 data = 64 bytes. Fits fine. No issue here.

5. **Full OTA test still needed**: The EEPROM compact fix has not been tested yet. Next step is a complete OTA run and checking the serial log for any new failure signatures.

---

## How to Continue

```bash
# Clone / switch to branch
cd qmk_firmware
git checkout claude/debug-hid-ota-update-stjPG

# Build
qmk compile -kb handwired/polykybd/split72 -km default

# Flash both halves, then trigger OTA from PolyKybdHost
# Watch serial log (debug_enable = true) for:
#   - fw_staging_process_deferred: erase complete (N sectors)
#   - FwUpChunk retries — should now succeed (ack: 1) not timeout
#   - No "Failed to sync" messages
#   - Final reboot
```

If a new failure appears, look for:
- `conn: 0` → connection_errors throttle (raise `SPLIT_MAX_CONNECTION_ERRORS` further or find new source of errors)
- `conn: 1, success: 0` → UART timeout (another IRQ-disabled blackout; find what's holding IRQs)
- Slave stops responding entirely → core1 interaction with halted PSM (audit `sync_and_refresh_displays`)
