# Investigation history (PolyKybd firmware)

Closed investigations, moved out of `CLAUDE.md` on 2026-09-10 so they stop costing
context in every session. Everything here is **dated and closed** — a resolved bug,
a superseded theory, a measurement of a past event. The **standing rules** they
produced stayed behind in `CLAUDE.md` § *Rules that came out of closed
investigations*; this file is the narrative evidence for them.

⚠️ **Read a note here as HISTORY, not as current behaviour.** Several entries are
explicitly superseded by later ones (the split42 saga's whole "dead PIO1 RX-IRQ"
and "the pointing transaction is required" trail; the HIL language-timeout note's
"flaky rig link" premise). `CLAUDE.md` wins on any disagreement.

Come here when you need the *why* behind a rule, a root cause to compare a new
symptom against, or the evidence for a decision somebody is about to reverse.

---

> ## ✅ RESOLVED (2026-07-17): the split42 split-link saga — two compounding root
> causes, neither of which was the pointing feature or the boot delay.
> **Authoritative record: `keyboards/polykybd/split42/SPLIT42_LINK_STATUS.md`**
> (the per-boot test log, rows 1–24 + resolution summary). The narrative below this
> banner is the HISTORICAL investigation trail — its conclusions about
> `SPLIT_POINTING_ENABLE` being required, the `wait_ms(400)` being load-bearing,
> the "dead PIO1 RX-IRQ", and the heartbeat/dummy-transaction refutations are all
> **SUPERSEDED** (they were taken through an unrecognized hardware coin-flip).
> The two real causes:
> 1. **Hardware:** the split42-left v1.0 board leaves the flipped-orientation
>    link-USB-C data pads copper-orphaned behind U26 (ESD array). With U26's
>    bridge broken and both halves being left boards (no right boards were ever
>    fabbed), only **1 of 4 plug-orientation combinations** links — the source of
>    every works↔dead flip on identical firmware. Bench fix: reflow/populate U26
>    or bodge `USB2.8→6` + `USB2.5→7`; next-rev items in the hardware repo's
>    `SPLIT42_REDESIGN_NOTES.md`.
> 2. **Firmware:** with the orientation controlled, split42 needs exactly an
>    **8-byte pad at the pointing member's position in `split_shared_memory_t`**,
>    in front of the RPC buffers — now shipped explicitly as
>    `POLY_SPLIT_SHMEM_RPC_GUARD` (`transport.h`, tracked in
>    `UPSTREAM_PATCHES.md`). The pointing subsystem and the 400 ms delay were
>    both removed from split42. **Open follow-up:** find the latent writer the pad
>    guards against (canary plan in the status doc, row 24) — until then do NOT
>    remove the guard.

**Bisect result (2026-07-14): split42 needs `SPLIT_POINTING_ENABLE`'s periodic split
transaction — NOT the trackpad, NOT its I2C.** Two-stage bisect:
- RGB + pointing device + LTR-559 (`d74e7e11`) → **works**; drop only the pointing
  device (`b25f2045`) → **breaks**. ⇒ the pointing device is the required piece (RGB +
  LTR-559 were both on in the broken build, so they're cleared; kept on anyway,
  harmless when unpopulated).
- Enabling the pointing device had **two** effects: (1) an extra periodic master→slave
  split transaction (`SPLIT_POINTING_ENABLE` → the master pulls `GET_POINTING_CHECKSUM`/
  `GET_POINTING_DATA` from the slave every cycle, `quantum/split_common/transactions.c`
  `pointing_handlers_master/_slave`), and (2) a per-cycle slave I2C read that could stall
  up to `CIRQUE_PINNACLE_TIMEOUT` (20 ms) since GP0/GP1 aren't broken out. Swapping the
  Cirque driver for QMK's **no-op `custom` driver** (weak hooks do zero I2C) while keeping
  `SPLIT_POINTING_ENABLE` (`5de77192`) → **still works**. ⇒ the fix is **effect (1), the
  split transaction**, not the I2C stall and not the trackpad hardware.

**⚠️ SYMPTOM CORRECTED (2026-07-14, after careful hardware observation):** the earlier
"slave hangs mid-render / core1 hang" framing was WRONG (that was a misread of an
un-refreshed splash). The real symptom is a **split-link establishment failure at boot**:
with pointing disabled, the two halves (same image on both) can't talk — the master
retries split transactions, exhausts `SPLIT_MAX_CONNECTION_ERRORS` (200), **times out**,
then runs **solo** (the display stays on the boot splash until a **keypress** forces a
refresh to the default layer). It follows the **master role** (swap USB → the behavior
moves to the new master), not a physical half. Enabling the pointing feature makes the
link come up; it is deterministic (not a flaky race). Consequently the heartbeat test
result is NOT evidence about traffic — a housekeeping heartbeat can't rescue a link that
never *establishes*. The core1 / render-hang lines above are superseded for this bug.

**Resting config:** split42 keeps `SPLIT_POINTING_ENABLE` + `POINTING_DEVICE_DRIVER =
custom` (no-op) — same fix as the real trackpad but with no dead I2C on the un-broken-out
bus. **ROOT CAUSE STILL OPEN:** *why* the shared PolyKybd firmware depends on that
periodic slave-pull transaction. split72 always had it (real trackpad), which hid the
dependency. Leading theory: split42's only other regular master→slave traffic is QMK's
built-in matrix pull + the poly custom syncs (which fire on *diffs*), so on an idle
freshly-booted split42 the slave may go too long without being serviced by the transport
in the way the poly split state machine expects; the pointing transaction restores a
guaranteed every-cycle pull. **Heartbeat test (2026-07-14, `01cb83d0`) — REFUTES the frequent-pull theory.** Disabled
the pointing device entirely and instead drove an **every-cycle** master→slave pull over
the existing `USER_SYNC_SLAVE_DATA` channel (reused, so no new transaction / no shmem
change — pure traffic) from `housekeeping_task_user()`. Result: split42 **still breaks**.
So an every-cycle slave pull is **not** what split42 needs — the dependency is **not the
traffic/frequency**.

⇒ **The dependency is structural to *enabling the pointing feature itself*, or a
memory-layout coincidence.** Enabling `POINTING_DEVICE_ENABLE`+`SPLIT_POINTING_ENABLE`
does several things a reused-transaction heartbeat does NOT: (a) adds 3 transaction IDs
(`GET_POINTING_CHECKSUM`/`GET_POINTING_DATA`/`PUT_POINTING_CPI`) → shifts the USER
transaction-id numbering and bumps `NUM_TOTAL_TRANSACTIONS` (the poly table is near the
32 cap); (b) adds a `pointing` member to `split_shared_memory_t` → changes shmem
size/offsets; (c) links `pointing_device.c` + runs `pointing_device_init/_task` → shifts
image/RAM layout. Any of (a)–(c) could be the real cause, **including the possibility
that "enable pointing" merely perturbs memory layout and masks a latent bug** (a
stack/buffer/uninitialised-use error) — the same *class* of coincidence the I2C-timing
red herring was. **Resting fix stays `SPLIT_POINTING_ENABLE` + no-op `custom` driver
(`5de77192`)** — that is the last confirmed-working config; the heartbeat commit
`01cb83d0` is an experiment, to be reverted to `5de77192` if the investigation doesn't
supersede it. NEXT: get the exact failure symptom (slave-dead vs no-USB vs display vs
boot-hang), then discriminate (a)/(b) from (c) by adding a **dummy split transaction +
shmem member with no task** (tests transaction-count/shmem-layout alone) and by an
**`-Wl,-Map` layout/`.bss` diff** of the working vs broken image (tests the
layout-coincidence hypothesis). Do NOT ship split42 off `01cb83d0`.

**Transport-level findings (2026-07-14, cont.) — it's a split-link *establishment*
failure, and the transaction COUNT is ruled out.**
- **Master HID console (broken build):** `Split link: … crc_err=0 transport_fail=100.0%`,
  climbing to >1.2M frames all failing. So the QMK **serial transport is dead** — every
  frame times out at the transport layer; this is **NOT** payload/CRC corruption
  (`crc_err=0`), and the handshake token can't mismatch (`tid ^ NUM_TOTAL_TRANSACTIONS`,
  same image both sides). The master exhausts `SPLIT_MAX_CONNECTION_ERRORS` (200), gives
  up, and runs solo; the "stuck splash" is just the un-refreshed screen until a keypress
  forces `update_displays`.
- **Corollary:** a dead transport can't be fixed by the pointing *transactions* riding it,
  so enabling pointing must fix the transport via a **side effect**.
- **Transaction count RULED OUT (`e260bcd4`):** registered **3 dummy split transactions**
  (no pointing) so `NUM_TOTAL_TRANSACTIONS` matched the working build — **still 100%
  transport_fail**. So it is NOT the count / handshake token / transaction-table size.
- **Memory layout ruled out earlier:** `.bss`/`.data`/stacks are within ~100 B and the
  stacks sit at identical addresses between working and broken (`5de77192` vs `01cb83d0`).
- **Narrowed to two candidates**, both present only when `SPLIT_POINTING_ENABLE` is set:
  **(b)** the `split_shared_memory_t` `pointing` member — it sits **immediately before the
  RPC buffers** (`transport.h`: `pointing` at line ~210, then `rpc_info`/`rpc_m2s_buffer`/
  `rpc_s2m_buffer`), so it **shifts the RPC buffers' offset** the poly `USER_SYNC_*`
  transactions transfer through; vs **(c)** merely linking `pointing_device.c` + running
  its init/task (a layout/init side effect). Discriminator flashed but not yet read back:
  **`0e04469d`** = `POINTING_DEVICE_ENABLE` with the no-op `custom` driver but **without**
  `SPLIT_POINTING_ENABLE` (pointing code linked/run, but no shmem member, no transactions).
  *Link revives → (c) code-linkage (coincidental); still dead → (b) the shmem `pointing`
  member specifically.*
- **Working config shipped for the repo:** PR **#144** (branch
  `claude/split42-working-all-subsystems`, cut at **`d74e7e11`** = RGB + pointing[Cirque] +
  LTR-559, confirmed working) captures the working split42 while this root-cause work
  continues on `claude/split42-literal-split72-copy`.

**⚠️ The DEFAULT branch (`PolyKybd`) regressed split42 again — TWO unreverted
experiment commits (found + FIXED 2026-07-15, confirmed on hardware).** After the
above work, the split42 on the `PolyKybd` tip was itself broken (a fresh build from
the default branch didn't come up). The working commit **`5de77192`** (FW 0.9.51,
`SPLIT_POINTING_ENABLE` + no-op `custom` driver) *is an ancestor* of the tip, so a
`git diff 5de77192..PolyKybd` restricted to split-relevant code isolated it — the
entire split **transport** (`serial_vendor.c`, `serial_protocol.c`, `split_sync.c`,
`bridge_helper.c`, `base/`) is **byte-identical** working↔tip, so it was neither a
transport nor a hardware regression. Two breaks, both from root-cause EXPERIMENT
commits committed straight onto `PolyKybd` and never reverted:
- **Break #1 (config):** `SPLIT_POINTING_ENABLE` was removed from
  `split42/config.h` by `01cb83d0` (heartbeat experiment) and only
  `POINTING_DEVICE_ENABLE` was re-added by `0e04469d` — leaving the exact broken
  **(c)** state the bisect condemned (pointing code linked, **no split transaction
  registered**). The whole `01cb83d0…0e04469d` experiment series (heartbeat, boot
  traces, `SERIAL_DEBUG`, 3 dummy transactions, pointing-code-only) landed on the
  default branch; only the final one's config state survived, and it was the broken
  one. **Lesson: revert experiment commits, or run them on a throwaway branch — do
  NOT commit a discriminator series onto the release branch and walk away.**
- **Break #2 (boot timing):** the progressive boot-splash rework (PR #138 + merges
  #143/#144: `show_splash_screen()` → `splash_progress()`, `boot_diag.c`) **removed
  the pre-init `wait_ms(400)` delay** the old blocking splash had (where it was
  purely a **logo dwell**, now served by the progressive reveal), deferring the
  dwell to the *end* of `post_init` and adding ~7 per-`post_init` keycap renders +
  a boot banner. Fine on split72 (real trackpad → robust link) but split42's marginal
  link (the dead PIO1 RX-IRQ) does not come up without that pre-init delay. This is
  the only live shared-code delta working↔tip (the other two diffs are inert: a
  `POLY_DUMMY_TXN` macro gated off, and a `POLY_KB_NAME` GET_ID string).
  - ⚠️ **The delay is EMPIRICALLY load-bearing but its MECHANISM is unknown — do NOT
    invent one.** A clean single-variable A/B on hardware (2026-07-15 eve) settled it:
    the working restore with **only** the `wait_ms(400)` removed (identical config,
    `SPLIT_POINTING_ENABLE` on) **fails** — the slave runs just its own local scan
    (keypress display inversion works) while the rest of the split link never
    establishes. So the delay genuinely fixes something on the link; *why* a boot
    delay affects it is not understood. An earlier version of this note (and the
    in-code comment) claimed a "settle window so the slave comes up before the master
    hammers transactions" — that was a **fabricated mechanism**, corrected here. The
    value 400 is inherited from the old splash (known-good, not a measured minimum).
  - **RULED OUT — it is NOT the pointing driver / per-cycle I2C (2026-07-15 eve).**
    Follow-up A/B: split42 built with the **real `cirque_pinnacle_i2c` driver** (like
    split72, so each pointing cycle does a real I2C read — GP0/GP1 aren't broken out so
    it just times out ~20 ms) **and no delay** → **still broken**, same symptom (no
    split link, slave only doing its autonomous local invert). So split42 needs the
    delay **regardless of pointing driver** (no-op `custom` and real Cirque both fail
    without it), and split72 needs no delay — the split72↔split42 difference that
    requires the delay is therefore **not** the pointing driver and **not** the
    per-cycle I2C activity. ⚠️ This also means the earlier bisect's "the fix is the
    split transaction, not the I2C stall" was tested *with the delay present*, which
    masked this — the I2C stall is now cleanly ruled out on its own. Leading remaining
    hypothesis (UNPROVEN): split72 simply **boots slower** (36 vs 21 keycap OLEDs/side,
    real RGB, bigger status OLED), and with the dead PIO1 RX-IRQ making establishment a
    *polling race*, a slower master boot wins the race — the 400 ms stands in for that.
    The proper fix is the IRQ-independent pre-poll (`claude/split42-link-diag-minimal`),
    not more delay tuning.
- **Fix (branch `claude/split42-fresh-rebuild-4m3vip`, restarted from the tip; one
  restore commit — PR #141 on it was closed unmerged, so the stale rebuild history
  was discarded per the restart-from-default procedure):**
  (1) restore the Cirque/pointing block + `SPLIT_POINTING_ENABLE` in
  `split42/config.h`; (2) restore the pre-init `wait_ms(400)` delay in
  `show_splash_screen()`, **split42-only** (`#if defined(KEYBOARD_polykybd_split42)`)
  so the progressive reveal is kept and split72 is untouched. Confirmed working on
  hardware 2026-07-15. The delay is a **stopgap** (empirically needed, mechanism
  unknown — see the ⚠️ above) — drop it once the IRQ-independent link fix lands (the
  deeper "why does the marginal link need the pointing transaction *and* a boot
  delay" root cause is still open; see the `claude/split42-link-diag-minimal`
  pre-poll work, where `poll_miss` — pointing OFF ≈500, pointing ON ≈2 — is the
  definitive probe of the dead RX-IRQ).

## Bug: second half of keyboard becomes unresponsive (slave stops sending key events)

**Symptom**: Intermittently, the right/slave half stops recognising keystrokes. Only keys on the master (USB) side still work. Reconnecting (replugging) or reflashing restores it. Happens "once in a while", not on every boot.

**Root cause identified (2026-04-29)**: Two separate EEPROM-write paths can block the slave's UART long enough to miss a split transaction response window, causing the master to declare the slave unresponsive. Both were introduced in commit `98ed47612d` ("eeprom refactoring — still needs testing", 2026-04-24). On the RP2040, EEPROM is wear-leveled flash; most writes are fast journal appends, but when the journal fills the firmware does a page consolidation (~50 ms blocking erase) — which is when the symptom occurs.

**Path 1 — blocked inside sync transaction handler (rare: only on default-layer change)**:
`eeconfig_update_default_layer()` was called directly inside `user_sync_layer_data_handler()`, a split UART transaction callback. Blocking there guaranteed a UART timeout whenever `def_layer` changed.

**Path 2 — blocked in housekeeping on the slave side (normal typing, brightness keys)**:
`mark_settings_dirty()` was called on the slave from `user_sync_poly_data_handler()` whenever the master synced a contrast or lang change (i.e. after any brightness key press). Five seconds later `brightness_save_if_pending()` fired on the slave in `housekeeping_task_user()`, writing EEPROM. The slave has no need to persist these values — the master is the authoritative owner and syncs them on every boot. This is the likely cause of occurrences during normal typing days with no layout switch.

**Fix applied (2026-04-29)**:
- `split_sync.c` `user_sync_layer_data_handler()`: replaced blocking `eeconfig_update_default_layer()` with `defer_default_layer_save()` — moves the flash write out of the UART transaction callback into housekeeping.
- `state.c` / `state.h`: added `defer_default_layer_save(layer_state_t)` and `default_layer_save_if_pending()`.
- `keymap.c` `housekeeping_task_user()`: added `default_layer_save_if_pending()` call on both sides.
- `brightness_save_if_pending()` was already deferred (5 s debounce, housekeeping) — no change needed there.

**Superseded (2026-06, PR #63 "unify emoji & language layers")**: persistence moved to a
**suspend-only dirty-flag model**. `defer_default_layer_save()` now just sets `g_def_layer_dirty`
(+ pending value) and the actual write is folded into the centralized `save_all_dirty()` in
`state.c`, which flushes every dirty block (settings / latin / default layer / MRU) at the real
flush points only: USB suspend (`suspend_power_down_kb`), the host shutdown signal
(`shutdown_user`), the firmware-update / `mcu_reset` paths in housekeeping, and the manual store
key (`KC_STORE_EE` → `request_eeprom_save` → `save_all_if_requested`). Consequently
`default_layer_save_if_pending()` was **removed** and is no longer called from
`housekeeping_task_user()` — do NOT re-add a per-housekeeping default-layer drain (that was the
old model and reintroduces the frequent in-housekeeping EEPROM write this very bug was about).
Base-layer changes apply immediately and persist on the next suspend/reset/store.

**How to confirm the fix worked**: reproduce by switching the default layer while typing on both halves. If the slave stays responsive, the sync-handler path is fixed.

**If the bug reappears after this fix**, the remaining risk is the RP2040 wear-leveling consolidation (~50 ms page erase) coinciding with a split UART transaction window, triggered by `brightness_save_if_pending()` firing in housekeeping 5 s after a brightness key press. This is a statistical coincidence, not a guaranteed block. Mitigations to try in order:

1. Also defer `save_user_latin()` in `user_sync_latin_ex_data_handler()` — still a direct EEPROM write inside a sync handler (triggered on language changes).
2. `eeprom_update_block()` in `dynamic_keymap_set_buffer_poly()` — also inside a sync handler, only during keymap remapping, lowest priority.
3. **Proper fix: offload EEPROM writes to core 1.** The keyboard already uses core 1 for RLE decompression via `multicore_exec.c` and the FIFO dispatch. Instead of calling `save_user_settings()` / `save_user_latin()` / `eeconfig_update_default_layer()` directly on core 0, post the write as a job to core 1 via the FIFO. Core 1 does the blocking flash operation while core 0 (QMK main loop, UART, USB) keeps running uninterrupted — eliminating the framing-corruption risk entirely. Main caveat: core 1 is currently single-purpose (RLE decompression), so the two job types must not collide; check that core 1 is idle before posting, or add a small job queue. EEPROM writes and RLE decompression are unlikely to overlap in practice since both are rare and burst-style.

**Relevant files**:
- `keyboards/polykybd/split_sync.c` — all `user_sync_*_data_handler` functions
- `keyboards/polykybd/state.c` / `state.h` — deferred-write helpers
- `keyboards/polykybd/poly_keymap.c` — `housekeeping_task_user()`

---

## Bug: key displays turn on when keyboard is suspended/sleeping

**Symptom**: Per-keycap OLED displays briefly light up (or stay lit) when the keyboard should be in suspend/sleep state.

**Root cause identified (2026-04-29)**: `poly_suspend()` in `keymap.c` clears `STATUS_DISP_ON` and `DISP_IDLE` but did not clear `IDLE_TRANSITION`. If the keyboard was in the fade-out phase (IDLE_TRANSITION set in local_state but not yet propagated to global_state) when USB suspend was triggered, `sync_and_refresh_displays()` — called immediately after `poly_suspend()` from `suspend_power_down_kb()` — detects `back_from_idle_transition = true` (IDLE_TRANSITION in local but not global) and restores `contrast = ee.brightness` from EEPROM, overwriting the `DISP_OFF` value `poly_suspend()` had just set. This triggers `contrast_changed = true` → `set_displays(ee.brightness, false)` → `kdisp_enable(true)` on both master and slave — keycap displays turn on for one suspend cycle before the next iteration corrects it.

**Fix applied (2026-04-29)**: Added `IDLE_TRANSITION` to the flags cleared in `poly_suspend()`:
```c
local_state->flags &= ~((uint8_t)STATUS_DISP_ON) & ~((uint8_t)DISP_IDLE) & ~((uint8_t)IDLE_TRANSITION);
```

**If the bug reappears**: Check whether a split transport failure (the other bug above) is preventing the suspend state from reaching the slave — if the slave never receives `STATUS_DISP_ON=0` it will keep its displays on indefinitely. The two bugs can look identical from the outside.

**Relevant files**:
- `keyboards/polykybd/poly_keymap.c` — `poly_suspend()`, `suspend_power_down_kb()`, `sync_and_refresh_displays()`
- `keyboards/polykybd/base/com.h` — flag bit definitions (`STATUS_DISP_ON`, `IDLE_TRANSITION`, `DISP_IDLE`)

---

## Bug: core1 hangs whenever overlay/ROI data is processed (post-merge regression)

**Symptom**: After merging upstream QMK master into the `PolyKybd` branch (May 2026), the master half hangs whenever the host sends overlay/ROI data over HID. Simple HID commands (GET_ID, brightness, language) still work. Core0 pushes a `CORE1_CMD_*` to the FIFO successfully; core1 starts processing then stops mid-work; core0 blocks in its busy wait for `core1_decomp_count` to catch up, which never happens; that wait loop starves the USB main loop on master, freezing master entirely. Slave keeps running because slave is autonomous (its own scan loop) — slave keypress inversion still works while master is frozen.

**Status (2026-05-15): WORKAROUND APPLIED, ROOT CAUSE NOT YET IDENTIFIED.**

**Workaround (currently in tree)**: `__asm volatile("cpsid i" ::: "memory");` at the top of `core1_entry` in `multicore_exec.c`. Sets PRIMASK=1 — masks all configurable-priority exceptions on core1. Empirically eliminates the hang completely; ROI and DECOMPRESS commands process across many keys/mods with steady `tick` growth and matching counts. Safe because core1 in this codebase has no IRQ-driven work — `multicore_fifo_pop_blocking` polls FIFO_ST, doesn't need an IRQ to wake.

**What we know decisively**:
- `cpsid i` is the actual cure — replacing it with a pure memory clobber (`__asm volatile("" ::: "memory")`) does NOT fix the hang. So PRIMASK=1 is doing the work, not compiler ordering.
- An explicit clear of `NVIC->ICER[0] = 0xFFFFFFFF`, `NVIC->ICPR[0] = 0xFFFFFFFF`, `SysTick->CTRL = 0`, and `ICSR PENDSV/SysTick CLR bits` at core1 entry — without setting PRIMASK — does NOT fix the hang. So the offending exception is NOT one we can prevent by disabling/clearing the standard sources.
- The hang point is **deterministic per build** but **shifts with the workload per inner-loop iteration** (heavier loop body → earlier stop). E.g. tick=101 with full inner loop, tick=143 with writes stubbed, tick=493 in `rle_decompress`. That points to a wall-clock-time-driven event, not iteration count.
- Our state captures (live, sampled inside the inner loop on every iteration) consistently show `ISER=0`, `SysTick CTRL=0`, `ICSR.PENDSVSET=0`, `ICSR.PENDSTSET=0`, `VECTACTIVE=0`. The exception fires and clears between samples — invisible in pre/post snapshots.
- ICSR bit 22 (`ISRPENDING`) is set and ISPR shows many IRQs pending at entry (`0x818a61` = TIMER_IRQ_0, USBCTRL_IRQ, XIP_IRQ, PIO1_IRQ_0, DMA_IRQ_0, SIO_IRQ_PROC0, SIO_IRQ_PROC1, I2C0_IRQ). With ISER=0, none should fire.
- An override of `_unhandled_exception` (the ChibiOS weak fallthrough used by all unhandled vector entries) never fires (`core1_fault_signal=0`). So whatever fires has a *strong* handler installed elsewhere.

**The contradiction**: PRIMASK=1 masks configurable-priority exceptions (NVIC IRQs, SysTick, PendSV). It does NOT mask NMI or HardFault. cpsid eliminates the hang ⇒ the exception is maskable ⇒ SysTick / PendSV / NVIC IRQ. But we've ruled out all three at sample time. The exception must fire so briefly between our inner-loop captures that pending/active bits aren't observable, and the handler that runs must come from a *strong* override (not falling through to our `_unhandled_exception` shim).

**Top remaining suspect — ChibiOS context switch via NMI**. ChibiOS `ARMv6-M-RP2` port with `CH_CFG_SMP_MODE=TRUE` (set in `platforms/chibios/boards/GENERIC_RP_RP2040/configs/chconf.h`) and `CORTEX_ALTERNATE_SWITCH=FALSE` (default) uses **NMI as the context-switch vector** (strong `NMI_Handler` in `lib/chibios/os/common/ports/ARMv6-M-RP2/chcore.c`) and a strong `Vector80` (SIO_IRQ_PROC1) FIFO drain in the same file whose `CH_IRQ_EPILOGUE` triggers NMI via `__port_exit_from_isr` writing `ICSR.NMIPENDSET`. But: NMI is unmaskable by PRIMASK, so this *can't* be what cpsid is preventing — unless the chain is "Vector80 fires (IRQ 16, maskable) → handler triggers NMI". In that case, masking Vector80 (the IRQ) prevents NMI from being triggered. PRIMASK=1 would do that. Catch: `NVIC->ISER` bit 16 is consistently 0 in our captures, meaning Vector80 shouldn't fire. Either our capture has a timing gap that misses a transient ISER bit being set, or some other path triggers it.

**What's currently in tree (post-cleanup, 2026-05-15; updated 2026-05-16)**:
- `keyboards/polykybd/multicore_exec.c` — `__asm volatile("cpsid i" ::: "memory");` at the top of `core1_entry` (with an explanatory comment pointing at this doc). Also: `core0_decomp_count` changed from plain `static uint32_t` to `static volatile uint32_t` (real correctness fix — the compiler could otherwise hoist the load out of the wait loops).
- `keyboards/polykybd/base/multicore/core1.c` — `CORE1_STACK_SIZE` set to 384 (originally 256, briefly bumped to 1024 during the investigation, then sized based on measurement: peak observed ~164 bytes via the `CORE1_STACK_HWM` probe — see `keyboards/polykybd/readme.md` "For developers" → "Diagnostics"). The same file ships an `#ifdef CORE1_STACK_HWM` painting/walking probe that is off by default.
- All diagnostic instrumentation has been removed from `multicore_exec.c`, `base/overlay.c`, and `base/rle.c` apart from the gated HWM probe.
- **Reverted as not-actually-a-race (2026-05-16)**: an earlier cleanup added a `core0_decomp_count != core1_decomp_count` wait to `core1_roi_start()` framed as a "race fix". On re-analysis it was redundant — `CORE1_CMD_RESET_BIT_IDX` only mutates `core1_bit_index`, FIFO ordering guarantees any in-flight DECOMPRESS/ROI_UPDATE finishes atomically before RESET runs, and every caller immediately follows `core1_roi_start()` with `core1_update_roi()` which has its own wait + buffer-write + dmb + push. Now reduced to the bare `multicore_fifo_push_blocking(CORE1_CMD_RESET_BIT_IDX)`.

**Local divergence from SDK that may matter**:
- `keyboards/polykybd/base/multicore/core1.c` reimplements `multicore_launch_core1_*` locally and does NOT call `irq_init_priorities()` (the post-merge SDK version of `core1_wrapper` does, see `lib/pico-sdk/src/rp2_common/pico_multicore/multicore.c:89`). Unverified whether this matters — `irq_init_priorities` only sets `NVIC->IPR` priorities and doesn't enable IRQs, but the priorities affect handler interaction.
- The local `core1_wrapper` has `runtime_run_per_core_initializers()` commented out (function doesn't exist in post-merge SDK anyway).

**Vector address lookup completed (2026-05-15)**. With `cpsid i` reinstated (firmware working), captured the handler address at each vector slot from the live `VTOR=0x10000100` and resolved against the `.elf` symbol table:

| Vector slot | Captured addr | Symbol |
|---|---|---|
| NMI | `0x10011649` | `NMI_Handler` (ChibiOS RP2 port, `lib/chibios/os/common/ports/ARMv6-M-RP2/chcore.c:85`) — strong override; the context-switch handler when `CORTEX_ALTERNATE_SWITCH=FALSE` |
| HardFault, SVC, PendSV, SysTick (and all weak vectors 0x20–0x78) | `0x100002c7` | shared body in `lib/chibios/os/common/startup/ARMCMx/compilers/GCC/vectors.S` that does `bl _unhandled_exception` |
| SIO_IRQ_PROC1 (Vector80) | `0x10011721` | `Vector80` (ChibiOS RP2 port, same file:167) — strong override; drains FIFO_RD, calls `CH_IRQ_EPILOGUE` which can trigger NMI via `__port_exit_from_isr` writing `ICSR.NMIPENDSET` |
| `_unhandled_exception` | `0x10001858` | OUR strong override in `multicore_exec.c` that increments `core1_fault_signal` and infinite-loops |

`core1_fault_signal` stays at 0 across ALL tests. That proves no exception going through the weak `bl _unhandled_exception` shared body ever fires on either core — eliminating HardFault, SVC, PendSV, SysTick, BusFault, MemManage, UsageFault, and Vector20–78. The cure must therefore be masking one of the two strong overrides: **`NMI_Handler`** (unmaskable by PRIMASK — ruled out) or **`Vector80`** (NVIC IRQ 16, maskable by PRIMASK).

**That makes Vector80 the only candidate consistent with `cpsid i` being the fix.** The mystery: every live capture of `NVIC->ISER` reads bit 16 as 0, which says Vector80 should not be deliverable to core1. Either there's a transient enable between our inner-loop samples (e.g. something in the Vector80 handler chain re-enables itself, or a fast handler that runs and finishes between two captures), or RP2040 silicon delivers SIO_IRQ_PROC1 via a path that bypasses ISER (check the FIFO/SIO interrupt model in the RP2040 datasheet — there are NVIC `FORCE` registers and `IPSR` semantics worth re-examining). The chain we suspect: Vector80 fires → its `CH_IRQ_EPILOGUE` writes `ICSR_NMIPENDSET` (see `chcoreasm.S:142–147` `__port_exit_from_isr` for `CORTEX_ALTERNATE_SWITCH=FALSE`) → NMI fires on return → ChibiOS NMI handler runs context-switch logic on a core with no thread state → hang.

**Web search hint (pico-sdk issue #284, "Unable to disable FIFO_IRQ_PROC0")**: there's a known quirk where the SIO FIFO interrupt on RP2040 behaves abnormally — "FIFO_IRQ_PROC (15) keeps firing continuously, and disabling it from the NVIC seems to ignore it", plus "writing `1<<15` to NVIC ISER causes a hard fault". This is exactly the behaviour pattern that fits our observations (`ISER` bit 16 reads 0 in every sample, yet PRIMASK=1 is the only thing that stops the IRQ from being taken). It looks like an SDK / silicon oddity around the SIO FIFO IRQs, not something specific to PolyKybd. That makes `cpsid i` the right shape of fix — there isn't a cleaner per-IRQ disable available.

**If revisiting**:
1. Consider whether the local copy of `multicore_launch_core1_*` in `base/multicore/core1.c` should be replaced with the pico-sdk one (or at least updated to call `irq_init_priorities()`). Unclear it matters given the `cpsid i` mask, but it's a known divergence from the SDK.
2. Try setting `CORTEX_ALTERNATE_SWITCH=TRUE` in the polykybd chconf — that moves ChibiOS's context-switch handler from NMI to PendSV. Wouldn't change whether Vector80 fires on core1, but would make the trap go through PendSV (a maskable exception) instead of NMI, making the failure mode more predictable.
3. If the FIFO IRQ behaviour is investigated further, search for the RP2040 silicon errata / pico-sdk discussions around how `SIO_IRQ_PROC0` / `SIO_IRQ_PROC1` are enabled — they may need to be cleared/disabled via a peripheral-side register rather than NVIC alone.

**Separate but related issue surfaced during this debugging**: when the slave half is flashed with the same firmware as master, master → slave UART split-sync repeatedly fails ("Bridge sync retry … Failed to sync … for transaction UserCompressed / UserRoi"). Flashing slave with a *known-working* firmware (older) cleans up these retries. Deferred — this is a different code path (split_sync.c / split UART transport) from the core1 hang. Worth investigating but out of scope for the core1 fix.

---

## Bug: key display brightness drops to 0 on boot / wake (post-PR-#63 regression)

**Symptom**: Keycap OLED brightness intermittently comes up as 0 on keyboard start and after wake from suspend, without the user having set it to 0.

**Root cause (2026-06-10 — FIXED)**: PR #63's suspend-only persistence flushes `save_user_settings()` at exactly the moments `l_state.contrast` holds a *transient* value, persisting it as the user brightness:
- `suspend_power_down_kb()` calls `poly_suspend()` (sets `contrast = DISP_OFF`) **before** `save_all_dirty()` — a dirty flag set any time since boot persisted brightness 0.
- The slave was hit on *every* suspend: the master syncs `contrast = 0` before the flush, and `user_sync_poly_data_handler()` marked settings dirty on any contrast diff — including the suspend sync itself — then copied 0 into local state.
- The idle paths (`TURN_OFF_TIME` → `poly_suspend()`, fade transition, 0–49 pulsing) also leave transients in `contrast` that a later flush persisted.

**Fix**: `state.c` keeps a `g_user_brightness` snapshot that is updated **only** at deliberate set-points — `inc/dec_brightness()`, the new `set_user_brightness()` (used by the `KC_D*` preset keys and HID cmd 13), `note_user_brightness()` at boot-time EEPROM load, and on the slave when adopting an *awake* master's synced contrast (`contrast > DISP_OFF` and `DISP_IDLE|IDLE_TRANSITION` clear). `save_user_settings()` persists `~g_user_brightness` instead of `~l_state.contrast`. All idle/suspend *restore* paths (`back_from_idle_transition`, fade target, `display_wakeup()`, `suspend_wakeup_init_kb()`, HID stop-idle) now read `get_user_brightness()` instead of re-loading EEPROM — which also means an unflushed brightness change survives an idle/wake cycle (EEPROM was stale there under the suspend-only flush model). The suspend-only flush model itself is unchanged.

**Relevant files**:
- `keyboards/polykybd/state.c` / `state.h` — `g_user_brightness`, `set/note/get_user_brightness()`
- `keyboards/polykybd/split_sync.c` — `user_sync_poly_data_handler` awake-guard
- `keyboards/polykybd/hid_com.c` — cmd 13 (set brightness), cmd 15 (stop idle)
- `keyboards/polykybd/poly_keymap.c` — preset keys, idle/wake restore paths, boot seeding (shared by split72 + split42)

**Follow-up (2026-06-23): host-auto state now persists across reboots.** The
`g_user_brightness` model above keeps the *manual* brightness clean, but it is
**only** updated at deliberate set-points — host-auto/daylight (VOLATILE) pushes
never touch it. So once `g_user_brightness` held a low value (e.g. an old
pre-v5 host that pushed daylight values as plain *persisted* sets wrote a
night-time `2`, or the `KC_DMIN` preset), auto mode *masked* it at runtime but
every reboot re-exposed it: the keyboard boots in **manual** mode (auto is
RAM-only) at the stale `~g_user_brightness` until the host re-engages — "both
halves came up at 2 after a firmware reboot" (field, 2026-06-23). Fix: the
**host-auto mode + last auto value are now persisted** in the freed
`poly_eeconf_t.auto_brightness` byte (`pack_auto_brightness`/`load_auto_brightness`
in `state.c`, bit7 = mode engaged, **bit6 = a real host value is known**, bits0-5 =
value). The known bit is essential: engaging auto *before* the host pushes a value
must NOT persist the default `g_last_auto_brightness` as if real — else the next
boot snaps to it (the FULL_BRIGHT jump `get_active_brightness` guards at runtime).
On load, auto-on-but-not-known comes up in auto mode but falls back to the manual
brightness until the host pushes. `set_brightness_auto_mode` /
`set_auto_brightness_value` set `g_brightness_dirty` so the state flushes at the
next suspend/store; `keyboard_post_init_user` calls `load_auto_brightness()` so a
reboot while host-auto was engaged comes up at the **last auto value** (with
`g_auto_value_known` set) instead of the stale manual one — `set_displays()` now
uses `local_state->contrast` (the restored active brightness), not `ee.brightness`.
The stale `g_user_brightness` stays in EEPROM but is no longer shown while auto is
on. Old EEPROMs read the byte as 0 (auto off) — clean migration. ⚠️ This is the
**one** place an auto-derived value is persisted; it is kept SEPARATE from
`g_user_brightness` (the manual value), so the brightness-0 separation above is
intact. Also: the slave's `user_sync_poly_data_handler` adopt no longer
`mark_settings_dirty()` — it tracks the master's awake contrast in RAM (for
idle/wake restore) but never persists it (the master is authoritative and syncs
brightness every boot), so the slave can't independently bank a stale auto value.

---

## Bug: slave does not show overlay icons after MRU program switch until modifier change

**Symptom**: After the host switches to a new program using the MRU overlay path, the slave half's keycap OLEDs do not display overlay icons. Keys on the master half show correctly. A layer or modifier change (which triggers a full display refresh) makes them appear.

**Root cause (2026-05-17 — FIXED)**: Two missing `request_disp_refresh()` calls, plus `DISPLAY_OVERLAYS` not being included in `OVERLAY_SYNCED_STATE_FLAGS`.

**Fix 1 — slave mapping handler** (`split_sync.c` `user_sync_overlay_map_data_handler`): when the master bridges an overlay mapping chunk to the slave, the slave called `set_10bit_overlay_mapping()` (setting usage bits and pool→display mappings) but never called `request_disp_refresh()`. Added the call so the slave redraws after each mapping chunk lands.

**Fix 2 — master mapping handler** (`hid_com.c` case 21): symmetric gap — the master also called `set_10bit_overlay_mapping()` without a following `request_disp_refresh()`. Added it.

**Fix 3 — ESC (and any key in a later mapping chunk) not appearing** (`base/com.h`): The MRU host sends overlay mappings in chunks of 24 pairs per HID report. For programs with many overlays (e.g. an IDE with all A–Z + numbers), ESC (display_flat_idx=37) falls in the second chunk. Fix 1's per-chunk refresh fires after chunk 1 lands — at that point ESC's usage bit is still 0 — so ESC shows fallback text. Chunk 2 fires another refresh and should correct it, but this creates a transient window. The reliable fix: add `DISPLAY_OVERLAYS` to `OVERLAY_SYNCED_STATE_FLAGS` so that `enable_overlays()` (called by the host after **all** mapping chunks are confirmed ACK'd) force-syncs state to the slave via case 11. The slave detects `state_diff`, calls `request_disp_refresh()`, and renders with all chunks already in place — guaranteed final correct refresh.

**Relevant files**:
- `keyboards/polykybd/split_sync.c` — `user_sync_overlay_map_data_handler`
- `keyboards/polykybd/hid_com.c` — case 21
- `keyboards/polykybd/base/com.h` — `OVERLAY_SYNCED_STATE_FLAGS`

---

## Bug: one keycap's overlay missing on the SLAVE half after an app switch, fixed by switching away and back

**Symptom (field, 2026-08-01)**: intermittently one keycap on the link-side half
falls back to its plain legend while the rest of the app's overlay set renders
(observed on **Esc**, Explorer). Switching to another app and back fixes it.

**Root cause**: every phase of an app switch bridges to the slave with the return
value **DISCARDED** — prepare (cmd 11), image uploads (`fill_overlay.c`), mapping
chunks (cmd 21), enable (cmd 11). `send_to_bridge()` returns the slave's ACK byte
*or* `SYNC_CRC32_ERR` once its retries are exhausted, so a give-up was
indistinguishable from success: the master applied the change to its own tables and
moved on, halves diverged, **no log line anywhere**. ⚠️ This is the *discarding*
sibling of the documented "never bool-test `send_to_bridge()`" rule — classify
every ack with `sync_succeeded()`, including the fire-and-forget bulk sends.

A **mapping chunk** is the one that bites: it is **one-shot** — nothing re-fires it,
unlike the periodic state syncs where the diff *is* the retry queue — and the
slave's render gate is the usage bit that `set_10bit_overlay_mapping()` sets. So a
lost chunk blanks exactly the positions it carried. Esc is display index 37, which
at 24 pairs/report lands in **chunk 2** (the same "later chunk" position as the
2026-05-17 ESC bug).

**⚠️ The differential that identifies WHICH bridge dropped** — `resolve_upload_side()`
means the master keeps **no copy of an other-side overlay image** (`is_on_current_side()`
is false → the local `memcpy` is skipped, the bytes go only over the wire), while the
host's MRU cache records that image as resident and will **not** re-send it:

| lost bridge | symptom | recovers on next app switch? |
|---|---|---|
| prepare (reset) | previous app's icons | yes |
| **image** | blank/stale keycap | **NO** — MRU hit, never re-sent; sticks until the cache resets |
| **mapping chunk** | **missing icon, others fine** | **yes** — full mapping re-sent every switch |
| enable | *all* icons missing on that half | yes |

Self-healing therefore points at the **mapping**, and rules the image path out.

**Fix (2026-08-01)**: all four acks classified with `sync_succeeded()` + a named
warning. The mapping is additionally **repaired**: the master holds the authoritative
`overlay_map[]` + `use_overlay[]` (it applies every chunk locally either way), so a
loss arms a repair that rebuilds the slave's view from the master's own tables.
⚠️ The repair **drains from `housekeeping_task_user()`**, 2 reports/tick from a saved
cursor — **never inline in the HID handler**: a full mapping is up to 34 reports and
each bridge can burn 10 retries × the bridge timeout, i.e. *seconds* of dead main loop
on exactly the bad link that triggered the repair. The 10-bit packer is the inverse of
`set_10bit_overlay_mapping()`'s decode — verify any change to it by round-tripping
through that decoder, not by eye.

The two **image** bridges are checked and logged but deliberately **not repaired** —
per the table above the master cannot: it never had the bytes. Closing that would need
a master-side shadow copy (RAM it does not have) or a host-visible failure signal (a
protocol change). Do it only if the logs show it actually happens.

**Relevant files**: `keyboards/polykybd/hid_com.c` (cases 11, 21),
`keyboards/polykybd/fill_overlay.c` (`arm_overlay_map_repair`,
`overlay_map_repair_tick`, `resolve_upload_side`), `keyboards/polykybd/poly_keymap.c`
(housekeeping drain)

---

## Bug: slave half stuck in the idle pulsing frame — keypress/shift won't wake it, only a brightness key does

**Symptom (field, 2026-06-18)**: After the displays went into the idle *pulsing*
animation, the **slave** half froze on one pulse frame ("some keycaps off, others
very dim") and **did not update at all** — neither a keypress nor Shift brought it
back. The master woke normally and kept logging key events. Pressing a manual
brightness key restored the slave.

**Root cause**: `send_to_bridge()` returns the slave's reply ack **byte**, or
`SYNC_CRC32_ERR` once it exhausts its retries. **All three returns are non-zero**
(`SYNC_ACK 0xCA`, `SYNC_ACK_SIG 0x4D`, `SYNC_CRC32_ERR 0x35`), but three callers in
`poly_keymap.c` `sync_and_refresh_displays()` tested it as a bool —
`if(!send_to_bridge(...))`. `!0x35 == false`, so the failure branch
(`state_diff/layer_diff = false`, "failed to send") was **dead code**: on a
give-up the master fell through, ran `copy_global_state()`/`copy_global_layer()`,
**advanced `global` to `local`**, and so produced no diff next pass → the lost
sync was **never re-fired**. (The accompanying comment block — "the diff IS the
retry queue; global only advances on a successful sync" — described the *intended*
behaviour that the `!` test silently defeated.)

Why it only bit the *pulsing→awake* transition: the pulsing contrast changes every
housekeeping pass, so a dropped frame is replaced by the next fresh diff and is
invisible. **Wake-from-idle is single-shot** (`display_wakeup()` clears
`DISP_IDLE` + restores `contrast` once). If that lone sync's give-up was
mis-classified as success, the master stopped re-sending and the slave — which
only pulses because the master *tells* it to, the idle math is `is_usb_host_side()`
only — stayed on its last received pulse frame indefinitely. A brightness key
mutates `contrast` again → a brand-new diff → fresh send → recovery (matching "the
manual brightness control brought it back"). Also explains why no
`USER_SYNC_POLY_DATA failed to send` line ever appeared in the logs.

**Fix (2026-06-18)**: added `static inline bool sync_succeeded(uint8_t ack)`
(`split_sync.h`, by the `SYNC_*` defines) returning `ack == SYNC_ACK || ack ==
SYNC_ACK_SIG`, and routed all `sync_and_refresh_displays()` send sites through it
(POLY / LAYER / LASTKEY, plus the already-correct MRU send for uniformity). A
genuine give-up now keeps the diff so the send re-fires next pass, as the comments
always claimed. ⚠️ Never bool-test `send_to_bridge()` directly — every return value
is non-zero; classify it with `sync_succeeded()`.

**Relevant files**:
- `keyboards/polykybd/poly_keymap.c` — `sync_and_refresh_displays()` send sites; `display_wakeup()`, `housekeeping_task_user()` (the single-shot wake)
- `keyboards/polykybd/split_sync.h` — `sync_succeeded()` helper + `SYNC_*` values
- `keyboards/polykybd/bridge_helper.c` — `send_to_bridge()` (returns the ack byte / `SYNC_CRC32_ERR`)

---

## Bug: idle mode sometimes never starts; host "start idle" (cmd 15) is a no-op right after boot

**Symptom**: (1) Once in a while the keycaps never enter the idle
fade/pulse/turn-off animation at all — the displays just stay at full brightness
until suspend. (2) The host-side "start idle" HID command (cmd 15, payload ≠ 0)
does nothing when sent within the first ~2 minutes after the keyboard powers on —
the keyboard keeps waiting the full idle timeout instead of idling immediately.

**Root cause (2026-07-07 — FIXED)**: both trace to `base/update.c`'s activity
timestamp `last_update` being a **signed `int32_t` that overloaded a `uint32_t`
timestamp with sentinels** (`-1` = "idle tracking off"), and the housekeeping loop
gating idle on `if(get_last_update() >= 0)`.
- **(1) The 24.86-day sign-bit window.** `update_performed()` stores
  `timer_read32()` (a `uint32_t` ms counter) into the signed `last_update`. Once
  uptime passes ~24.86 days (`timer_read32() ≥ 2³¹`), that value reads back
  **negative**, so `if(update >= 0)` is false and the **entire idle/turn-off block
  in `housekeeping_task_user()` is skipped** — idle silently stops working for the
  ~25-day window until the 49.7-day `uint32` wrap. Intermittent, uptime-dependent →
  "sometimes it doesn't idle".
- **(2) Backdating underflow near boot.** `hid_com.c` case 15's "start idle" set
  `last_update = timer_read32() - FADE_OUT_TIME` to make idle begin one fade-out
  interval "ago". In the first `FADE_OUT_TIME` (120 s) of uptime `timer_read32() <
  120000`, so the signed subtraction went **negative and was clamped to 0** — which
  reads as "just became active", not "idle now", so the fade never triggered. (The
  code even logged `Starting idle in N msec` and then didn't.)

**Fix**: separate the "idle tracking enabled" state from the timestamp.
`base/update.c` now stores `last_update` as a real **`uint32_t`** plus a distinct
`bool idle_tracking` flag; `get_time_since_last_update()` uses `timer_elapsed32()`
(correct modular `uint32` arithmetic at any uptime, including across the wrap).
Housekeeping gates on **`is_idle_tracking()`** instead of the sign of the
timestamp, so idle works for the full 49.7-day timer range. The host "start idle"
path calls the new **`backdate_last_update(FADE_OUT_TIME)`** — modular
`timer_read32() - ms`, correct even when `now < ms`, so idle begins on the next
pass regardless of uptime. The old `set_last_update(-1)` "idle off" calls are now
the clearer **`disable_idle_tracking()`** (suspend / host display-off cmd 24 /
turn-off-reached); `set_last_update(int32_t)` is kept as a thin compat shim (`<0`
disables, `≥0` sets+enables). No wire-protocol change (cmd 15 payload identical),
so no `PROTOCOL_VERSION`/`__protocol__` bump.

**Relevant files**:
- `keyboards/polykybd/base/update.c` / `update.h` — `uint32_t last_update` +
  `idle_tracking`; `is_idle_tracking()`, `disable_idle_tracking()`,
  `backdate_last_update()`
- `keyboards/polykybd/poly_keymap.c` — `housekeeping_task_user()` idle gate
  (`is_idle_tracking()`), the turn-off + `suspend_power_down_kb()` disable calls
- `keyboards/polykybd/hid_com.c` — cmd 15 start branch (`backdate_last_update`),
  cmd 24 display-off (`disable_idle_tracking`)

---

## Bug: keyboard hangs on the boot splash after a firmware apply (slave not rebooted)

**Symptom (field, 2026-06-22)**: After a successful HID firmware flash + apply, the
master rebooted onto the new firmware but **hung on the boot splash** ("SPLIT 72");
no USB enumerated for minutes (`No Interface` in the host log) until the **slave
half was replugged**. Afterwards the split link showed a high steady error rate
(`err=36%`) because master ran new firmware while the slave still ran the old one.

**Root cause**: `CMD_FW_UP_APPLY` (`hid_fw_up.c`) tells the slave to install its
staged image and reboot in lockstep via `send_to_bridge(USER_SYNC_FW_UP_APPLY, …)`,
then arms the master's own reboot **regardless of the slave's ack**. That bridge was
sent with only **5 retries**, so one unlucky drop on this single critical
transaction left the slave on old firmware; the rebooted master then waits for a
slave handshake at split init that never comes → hang. (The master booting alone
into mismatched firmware is exactly why the apply bridges to the slave at all.)

**Fix (2026-06-22)**: bump the slave-apply bridge to **20 retries** and **re-fire the
whole round once** if it still hasn't acked. Safe: the slave apply handler is
idempotent (validates the staged image + arms a *deferred* reboot), and
`send_to_bridge` is **synchronous** (returns only after the slave handled the
message), so by the time the master proceeds to reboot the slave has already armed
its own. Worst case adds ~1 s, only on a bad link.

**Recovery if it recurs**: re-run the flash + **Apply** (re-bridges the install to
the slave, which already has the image staged), or flash the slave directly via
BOOTSEL/UF2. The high `err%` clears once both halves run matching firmware.

**Relevant files**:
- `keyboards/polykybd/hid_fw_up.c` — `CMD_FW_UP_APPLY` (slave bridge retries)
- `keyboards/polykybd/split_fw_up.c` — `user_sync_fw_up_apply_handler` (deferred, ACK-first)

---

## Split-link integrity: wire noise, the app-level CRC32, retries, and the health counter

> **RESOLVED (2026-06-16): migrated the split UART to full-duplex two-wire — the
> ongoing corruption is gone.** `config.h` now sets `SERIAL_USART_FULL_DUPLEX` +
> `SERIAL_USART_TX_PIN GP5` / `SERIAL_USART_RX_PIN GP4` + `SERIAL_USART_PIN_SWAP`.
> GP4 was always wired (a second conductor) but unused — there was no PIO
> full-duplex when the board was brought up; the vendor PIO driver supports it
> now. The cable is **straight** (GP5↔GP5, GP4↔GP4); `SERIAL_USART_PIN_SWAP`
> gives the crossover by swapping TX/RX **only on the master half's init path**
> (`serial_vendor.c`: `serial_transport_driver_master_init` swaps,
> `..._slave_init` does not), so **one identical image** produces the logical
> crossover at runtime by role — no per-side build, no EEPROM handedness. Works
> for the normal single image (USB half = master) and the HIL rig (roles forced
> per image via `POLYKYBD_HIL`, same `is_keyboard_master()`).
>
> **Measured result** via the health counter below: half-duplex was corrupting the
> small frequent syncs (`Failed to sync … UserLayer/UserPoly` lines — i.e. exactly
> the layer-drop + RGB-flash symptoms). On full-duplex, across **858 tx including
> deliberate heavy overlay/RGB load, `crc_err`/`giveup` stayed frozen at the
> boot-only burst (39/13) with `transport_fail=0`** — i.e. **zero** steady-state
> errors; `err%` only decays as the boot burst dilutes (14.3 → 4.5 % and falling).
> The boot burst is unmonitored (it precedes HID-console attach — the counter
> caught what the live log couldn't) and harmless (persistent state, re-delivered
> by the diff re-fire once the link settles). Why it works: full-duplex removes the
> single-wire **bus-turnaround/line-float** hazard and drives push-pull both ways
> (no pull-up), and gives the reply direction its own clean line.
>
> **Consequently the transport-level CRC patch and the upstream QMK PR are SHELVED**
> — they would have fixed *ongoing* payload corruption, which no longer occurs. The
> app-level CRC32 + `PERIODIC_SYNC_RETRIES=3` stay as the cheap backstop that
> absorbs the boot burst. Reopen only if `crc_err`/`giveup` start climbing in
> *steady state* (watch the counter). The analysis below is retained as the record
> of why the link behaves as it does — note the "half-duplex/single-wire/230400/
> 12 mA" descriptions are now historical (pre-2026-06-16).

**The split UART has no payload integrity check of its own — the per-transaction
CRC32 in `split_sync.c` is the only thing that catches a bit flipped by wire
noise in flight.** This is the single most important fact about the link, and
the reason the CRC32 was added (intermittent sync corruption that looked random).

**The link, pre-migration (HISTORICAL — the half-duplex setup in use until the
2026-06-16 full-duplex switch in the RESOLVED note above)**: `SERIAL_DRIVER = vendor` → the RP2040 **PIO
half-duplex, single-wire** driver (`serial_vendor.c`) on **`SERIAL_USART_TX_PIN
GP5`** (no RX pin, no `SERIAL_USART_FULL_DUPLEX` → one shared wire). Baud is
**230400** (`SELECT_SOFT_SERIAL_SPEED 1` in both variants' `halconf.h` →
`serial_usart.h` maps that to 230400; 8× PIO oversampling). TX is driven at
**12 mA** (`GPIO_DRIVE_STRENGTH_12MA`, `serial_vendor.c`) — fast, strong edges
that ring/reflect on a longer split cable.

**What QMK's transport guarantees (almost nothing)** — traced in
`platforms/chibios/drivers/serial_protocol.c`:
- A **1-byte handshake token**: master sends the transaction id, slave echoes
  `tid ^ NUM_TOTAL_TRANSACTIONS`. Proves *a* transaction of that id is starting —
  says nothing about the data bytes.
- A **20 ms** receive timeout (`SERIAL_USART_TIMEOUT`).
- The actual `initiator2target` / `target2initiator` **payload buffers travel
  raw** — no CRC, no checksum, not even parity. A flipped bit inside the 64-byte
  buffer is delivered to the slave callback and the transaction reports
  **success**. Without the app-level CRC32 the slave applies garbage state
  (contrast/flags/layer/overlay bytes) silently. ⚠️ Do **not** remove the CRC32
  thinking the transport covers it — it does not.
- Note the **reply** (`poly_sync_reply_t`, 1 ACK byte) has **no CRC** either; a
  corrupted reply can turn a real `SYNC_ACK` into a non-ACK → master retries (safe,
  idempotent) or, ~1/256, into a false ACK. Low impact, but it's why a tiny
  fraction of `crc_err` counts can be reply corruption rather than payload.
  - **That missing CRC is why the ack BYTE VALUES are Hamming-spaced**, and the
    vocabulary lives in dependency-free **`base/sync_ack.h`** (re-exported by
    `split_sync.h`, so consumers are unchanged) with tests enforcing it:
    `SyncAckTest.AckValuesStayHammingSpaced` requires min pairwise distance **4**
    across all six values, `EveryAckValueIsDistinct` forbids a duplicate, and
    `NoAckValueIsAStuckLineReading` forbids `0x00`/`0xFF` (what a stuck or floating
    line reads as). The set is built as **complement pairs**, each balanced at
    popcount 4: `SYNC_ACK 0xCA ↔ SYNC_CRC32_ERR 0x35`, `SYNC_ACK_SIG 0x4D ↔
    SYNC_NACK_REFUSED 0xB2`, `SYNC_BUSY 0x1B ↔ SYNC_GIVEUP 0xE4`.
    ⚠️ **Adding a seventh value must keep distance 4** or the single-bit tolerance
    degrades for the *whole* set. A mutually-distance-4 code containing these six
    extends to **16**, so 10 remain (8 excluding `0x00`/`0xFF`) — take the complement
    of an unused one to keep the pattern. `sync_succeeded()` is a deliberate
    **whitelist** so a new failure value is a failure at all 14 existing call sites
    with no edits; `SyncSucceededIsFailClosedAcrossEveryByte` sweeps all 256 bytes to
    pin that, because a blacklist implementation passes every other test.
  - ✅ **`SYNC_CRC32_ERR` is DE-OVERLOADED — it now means exactly one thing: "the
    frame I received did not check out".** It used to mean four: that, plus "still
    erasing", "no answer at all", and "I refuse". Each now has its own value —
    `SYNC_BUSY` (the `flash_stage_begin` re-poll while the deferred erase runs),
    `SYNC_GIVEUP` (`send_to_bridge` exhausted its retries, or we never asked), and
    `SYNC_NACK_REFUSED` (processed and declined: an unknown bundle id, a rejected
    chunk write, an apply with no valid staged image, an unknown reset action).
    The audit that keeps it true: **every remaining `= SYNC_CRC32_ERR` sits directly
    on a `crc32 != …->crc32` (or magic) check** —
    `grep -rn -B3 "ack = SYNC_CRC32_ERR" --include=*.c keyboards/polykybd/` should
    show no exceptions.
    - **Relabelling was behaviourally inert, which is why it was safe**: every
      consumer tests `== SYNC_ACK` / `sync_succeeded()`, i.e. ACK-or-not, so no
      decision changed — only what the logs and the COMMIT classifier can tell apart.
    - ⚠️ **The one place needing a compat guard is `hid_fw_up.c`'s erase-progress
      counter**, which matches on the begin re-poll value. It accepts **both**
      `SYNC_BUSY` and the legacy `SYNC_CRC32_ERR`, because the two halves can
      transiently run different firmware (a fw apply reboots the master first — the
      2026-06-22 boot-splash hang). Mismatched halves are safe in both directions
      *because* the functional decision is ACK-or-not.

**How CRC32 + retries + noise interact** (the model that drives the retry-count
choice). With `p` = probability a single frame is corrupted (and caught by CRC32),
`N` independent attempts fail this housekeeping pass with prob ≈ `p^N`:
- **CRC32 detects** corruption → slave returns `SYNC_CRC32_ERR` (not `SYNC_ACK`).
- **Retries recover** → `send_to_bridge` re-sends; all handlers are idempotent.
- Retries trade latency/CPU for resilience; **they do not reduce `p`.** A high
  `SPLIT_MAX_CONNECTION_ERRORS` (200, raised for the fw-update erase) is itself a
  tell that `p` is non-trivial.

**Periodic syncs use `PERIODIC_SYNC_RETRIES` (=3)** in `poly_keymap.c`
`sync_and_refresh_displays()` (poly/MRU/layer/last-key). Was briefly cut to **1**
(to avoid the ~400 ms main-loop stall that 10 retries × ~40 ms timeout costs once
`SPLIT_MAX_CONNECTION_ERRORS=200` stops failures fast-failing). **1 was too few**:
the diff re-fire only guarantees eventual delivery of state that *persists* (it
re-sends the current snapshot; global advances only on success), so a *transient*
that reverts to == global before the next successful sync is dropped, and even a
persistent transition leaves the slave visibly stale for a pass+. Field symptoms
at retries=1: layer updates occasionally not propagating (briefly-held momentary
layer lost), and the RGB matrix flashing on the slave for a fraction of a second
(stale disp/RGB until the deferred sync lands). 3 rides through a single glitch
within the same pass while bounding the worst-case stall to ~3 × 40 ms (the active
fw-update path skips this code).

**Measuring `p` — the split-link health counter** (`bridge_helper.c`,
master-side, added 2026-06-16). Every `send_to_bridge` frame is counted and
classified: `ok` / `crc_err` (slave NACK or corrupted reply — payload integrity
miss) / `transport_fail` (timeout/handshake) / `giveup` (retries exhausted).
`send_to_bridge` emits a compact summary every `LINK_STATS_LOG_EVERY` = 200
frames (count-based, no timer — the cadence follows real traffic, so it's dense
during overlay bursts and silent when idle; gated on `debug_enable`):

```text
Split link: 12345 tx crc_err=4 nack=17 transport_fail=1 giveup=0 err=0.0%
```

`err%` is the all-time detected-error rate over all frames — a direct read on the
wire. **Use it to validate any link change** (baud/cable/drive/termination) by
watching the number move, instead of by feel. `giveup` should stay ~0 with
retries=3; if it climbs, attack `p` at the source.

⚠️ **`giveup` counts only calls that ended on a LINK fault, and `nack` is EXCLUDED
from `err%`** — both decided by the one shared predicate `sync_is_link_fault(got_reply,
ack)` (`base/sync_ack.h`), so the two numbers can never disagree about what a bad wire
is. A link fault is exactly *nobody answered* or *the slave says what reached it was
corrupt* (`SYNC_CRC32_ERR`); every other byte means the wire delivered a frame and the
slave answered with a verdict of its own.
- ⚠️ **It is deliberately NOT an enumeration of the non-fault values.** Listing the
  siblings (`ack == SYNC_BUSY || ack == SYNC_NACK_REFUSED || …`) is the guard shape that
  goes stale — a seventh ack value would be misclassified until someone remembered to
  add it. `SyncAckTest.AnUnknownReplyValueIsNotMistakenForALinkFault` sweeps all 256
  bytes to pin that, and it fails against the enumerating implementation.
- **Why `giveup` needed this:** the `flash_stage_begin` re-poll runs with
  `max_retries=1`, so **every** poll of a deferred erase exhausted its retries with a
  perfectly good `SYNC_BUSY` answer and counted as a give-up. Measured on hardware
  (2026-08-18) a healthy font-pack sync read `nack=11 transport_fail=1 giveup=12` — one
  real fault, twelve reported give-ups. `giveup` is read as "the link is failing", so
  that is the same category error that had `err%` reading 6.0% on that link instead of
  0.5%.

⚠️ **`nack` is EXCLUDED from `err%` on purpose** — it counts valid non-ACK answers
(`SYNC_BUSY`, `SYNC_NACK_REFUSED`), where the wire worked and the slave simply said
something other than yes. Only `crc_err` (a corrupted frame) and `transport_fail`
(no answer) are link faults. Before the split, every non-ACK incremented
`crc_err` — and since `SYNC_BUSY` now arrives on **every erase re-poll of a flash**,
a single font-pack update would otherwise have added hundreds of phantom "errors"
to the one number used to judge cable/baud changes.

⚠️ **`send_to_bridge()` returns what the slave SAID; it returns `SYNC_GIVEUP` only
when the slave never answered.** It used to return a *constant* on give-up,
discarding `reply.ack` — and that worked only by **coincidence**, because the
constant was `SYNC_CRC32_ERR`, which happened to equal what the slave sent in every
case that mattered. Distinguishing the failure values exposed the discard, and with
it **`fw_up_slave_refused_commit()`'s "a refusal is self-describing, so don't spend
a STATUS RPC" short-circuit, which was dead code** — a refusal arrived as the
give-up constant, never as `SYNC_NACK_REFUSED`, so every refusal paid for a probe
(found in review, 2026-08-17). **Generalise: a sentinel that happens to equal a
real value hides the fact that the real value is being thrown away.**
- ⚠️ **The near-miss is the more instructive half, and it was initially reported
  here as a second dead-code case — wrongly.** `hid_fw_up.c`'s erase-progress
  counter kept firing throughout, just not for the reason it reads as: its guard
  accepts `SYNC_BUSY` **or** `SYNC_CRC32_ERR`, a compat arm added for transiently
  mismatched halves, and that arm also matched the give-up constant. A defensive
  clause written for one hazard quietly covered the discard, so the counter fired
  on a value the slave never sent. Verified on hardware 2026-08-18: it logs
  `begin-pending` at poll 17 and 33 of a 117-sector erase — as it did before.
  **Check a dead-code claim against the guard's OTHER arms before making it**; the
  git history of the condition settles it in one `git show`.

**Reducing `p` at the source (the real root fix), in order of leverage**:
1. **Lower the baud** — biggest, cheapest software lever. 230400 → 115200
   (`SELECT_SOFT_SERIAL_SPEED 2`) roughly doubles the per-bit sampling margin.
   Cost: overlay transfers (the bulk of UART bytes) ~2× slower; tiny state/layer
   syncs imperceptibly. A/B-test it against the health counter before keeping it.
2. **Driver edge rate** — the 12 mA TX drive in `serial_vendor.c` is strong; a
   slower edge helps signal integrity but lives in QMK core (would be a tracked
   local divergence, not a config knob).
3. **Hardware** — single-wire half-duplex over a TRRS-style cable is the classic
   culprit: ~100 Ω series resistor near the driver (damp reflections), a ground
   conductor twisted with the data line, shorter/shielded cable, solid common
   ground, good connector contact; rule out RGB/SPI/I²C coupling.
4. **Full-duplex two-wire** ✅ **DONE (2026-06-16)** — see the RESOLVED note at the
   top of this section. Removed the single-wire bus-turnaround hazard and drove the
   steady-state error rate to zero, so options 1–3 above were never needed.

**Relevant files**:
- `keyboards/polykybd/split_sync.c` — per-transaction CRC32 (the only payload check)
- `keyboards/polykybd/bridge_helper.c` / `.h` — `send_to_bridge` retries + the link health counters / `LINK_STATS_LOG_EVERY` summary
- `keyboards/polykybd/poly_keymap.c` — `PERIODIC_SYNC_RETRIES`, `sync_and_refresh_displays()`
- `keyboards/polykybd/config.h` — `SPLIT_MAX_CONNECTION_ERRORS`; the full-duplex defines (`SERIAL_USART_FULL_DUPLEX`, `SERIAL_USART_TX_PIN GP5`, `SERIAL_USART_RX_PIN GP4`, `SERIAL_USART_PIN_SWAP`)
- `<variant>/halconf.h` — `SELECT_SOFT_SERIAL_SPEED`. ⚠️ **Currently `0`, i.e. 460800 baud** in both variants — *not* the 230400 quoted in the historical half-duplex paragraph above, which describes the pre-2026-06-16 setup and is the figure a reader otherwise carries forward. The mapping lives in `platforms/chibios/drivers/serial_usart.h` (0→460800, 1→230400, 2→115200, …) and nothing in `keyboards/polykybd/` overrides `SERIAL_USART_SPEED` directly. It matters for any wire-time estimate: at 460800 8N1 one byte is **21.7 µs**, so the per-scan split transactions (slave matrix + pointing, unconditional in `transactions_master()`) are a fixed cost that does **not** shrink when the CPU clock rises — measured at ~473 µs, about half of an idle main-loop iteration (see the 200 MHz measurement in PR #187).
- `platforms/chibios/drivers/serial_protocol.c`, `drivers/vendor/RP/RP2040/serial_vendor.c` — QMK transport (no payload integrity)

---

## Bug: HIL "get current language" (cmd 7) times out once early in the run — boot-time busy window stalling the main loop

> **⚠️ CORRECTION (2026-06-27): the "flaky rig link" premise this note was written
> on is WRONG.** The rig runs the **same clean full-duplex two-wire split link as a
> shipping keyboard** (identical `config.h` defines; the crossover is done by role
> at runtime via `SERIAL_USART_PIN_SWAP`, not a different cable). There is **no
> "flaky / slow-ACK rig link"** — that phrasing below is superseded. The real
> differentiator is **timing/readiness, not link quality**: the rig fires its first
> HID queries within ~2 s of the master booting, inside the master's boot-time busy
> window (initial 72-keycap OLED render + the one-shot split sync to the
> just-booted slave), and the slave — independently flashed and rebooted on the rig
> (`usb_disconnect()` image) — can still be coming up then. A human user never pokes
> the keyboard that early. The forced-resync analysis below is also partly stale:
> the one-shot gate (`fc6ee693`, `is_transport_connected()`-gated, cleared even on a
> drop) already removed the per-pass spin. Treat the boot-window timing as the cause;
> the exact internal mechanism for the multi-second silence is unconfirmed (no trace).
> The rig-side mitigations live in `polykybd-ctnd` (sustained settle #37, packed-list
> headroom #38).

**Symptom (HIL rig, 2026-06-24)**: The `get current language` test (cmd `0x07`)
times out (`GET_LANG response: None`) and **fails the run**, while the *same*
command answers fine everywhere else in the *same* run — 3× during the runner's
settle phase and again in the later language round-trip read-back. It reproduced
**identically across two consecutive runs** (always test #4, right after the three
GET_IDs), so it is not pure randomness.

**Root cause (diagnosis, not yet fixed)**: the boot-time **forced layer-resync**
in `poly_keymap.c` `sync_and_refresh_displays()`. `g_force_layer_resync` starts
`true` and the master re-sends `USER_SYNC_LAYER_DATA` **every housekeeping pass
until the slave ACKs**, at `PERIODIC_SYNC_RETRIES` (3) per attempt
(`send_to_bridge`). On the rig the slave (the `*_hil_right` image) is **slow to ACK
at boot** (it is independently flashed + rebooted and still coming up — NOT a link
problem; see the correction banner above), so the resync **spins and blocks the master
main loop** for ~3 × the bridge timeout per pass, right in the early window where
the host is issuing its first HID queries — deterministically landing on cmd 7
(test #4). Once the slave finally ACKs, `g_force_layer_resync` clears and the loop
is responsive again (the later `GET_ID stress` shows 0 retries / 3–7 ms latency).

**Why it is almost certainly rig-only (and why it did not block the merge)**: on
real hardware the split link is the reliable **full-duplex two-wire** setup (see
the split-link RESOLVED note above — zero steady-state errors). There the slave
ACKs the **first** attempt, so `g_force_layer_resync` clears on pass 1 with
negligible stall and no HID command is delayed. The flake only manifests on the
rig because of *when* it queries (mid-boot) and the slave's boot latency — not link
quality. PR #85 merged with this HIL test red for exactly this reason.

**What the forced resync is and why it exists** (don't remove it blindly): each
half loads its **own** default layer from EEPROM, and the master only pushes
`USER_SYNC_LAYER_DATA` on a *diff*. So when the active default layer equals the
master's last-synced `global` (e.g. `_L0`/Qwerty = all-zero `global` after a fresh
boot or a fw-apply reboot), a slave that came up with a **stale** default layer
would never be corrected until the next manual layer change. The one-shot resync
forces a single push to fix that. It is gated by `g_force_layer_resync` (set at
boot, cleared on the first successful push); on failure the flag stays set so the
push re-fires — which is exactly the spin that stalls the rig.

**If hardening is wanted** (so it can't spin/stall even when the slave is slow to
come up at boot, without losing the fresh-boot correction): make the forced push a true
one-shot — attempt it **once** (ideally gated on the split transport being
connected so the single try has a real chance) and clear the flag regardless of
ACK, rather than re-firing every pass; or back off its retry cadence instead of
hammering each housekeeping pass. A genuine slave-stale case would then still be
corrected by the next real layer diff. Not done — left optional since real hardware
is unaffected.

**Relevant files**:
- `keyboards/polykybd/poly_keymap.c` — `g_force_layer_resync`, the forced-push branch in `sync_and_refresh_displays()` (`if ( layer_diff || g_force_layer_resync )`)
- `keyboards/polykybd/bridge_helper.c` — `send_to_bridge()` (per-attempt blocking cost = `PERIODIC_SYNC_RETRIES` × bridge timeout)
- `polykybd-ctnd` `station/hil_tests.py` — the `get current language` test (no miss-tolerance, unlike `test_get_id_stress`)
