# split42 split-link investigation — master PIO1 RX receive path

> **Clean baseline (2026-07-15):** this branch (`claude/split42-link-diag-minimal`) was cut
> fresh from the default branch (`PolyKybd`) and carries **only the minimal, safe,
> observation-only diagnostics** — the IRQ counters + the benign RX pre-poll measurement +
> the `HS-DIAG`/`HS-OK` console prints (all under `-DPOLY_HANDSHAKE_DIAG` in
> `split42/rules.mk`). It deliberately does **not** include any of the transport *behaviour*
> changes tried on `claude/split42-literal-split72-copy` — the `sync_rx` suspend-slice surgery
> and the blocking-receive loop both **bricked the keyboard** (no USB console, frozen), so
> they were dropped. The commit SHAs referenced below are from that older investigation
> branch and are kept as the historical record of what was ruled out.

**Prior branch:** `claude/split42-literal-split72-copy`
**Status:** the difference between working and broken is **"is the slave's echo byte
cleanly present in the master RX FIFO at receive time"**, NOT the IRQ. The RX-not-empty
IRQ is **dead in BOTH** the working and broken builds (`irq_rxne`≈1/8000) — the receive
succeeds purely by finding the byte already in the FIFO. In the working (pointing)
build it's reliably there; in the broken build the RX SM produces a garbage-decode
storm and the real echo is not cleanly framed/present. **Not yet fixed/shipped.**
Currently reading the *unperturbed* working RX path (pointing on, diagnostic spin
removed) to pin down what makes the echo land cleanly.
**Working fallback for the repo:** PR **#144** (`claude/split42-working-all-subsystems`,
`d74e7e11`) = RGB + pointing[Cirque] + LTR-559, confirmed working.

> This file is the running log of the split42 boot-time split-link failure. It
> supersedes the "why does split42 need `SPLIT_POINTING_ENABLE`" open question in
> the top-level `CLAUDE.md` (that note stays as the higher-level pointer).

---

## 1. The symptom

split42 (42-key CRKBD-footprint PolyKybd) **never established the split link at
boot** unless `SPLIT_POINTING_ENABLE` was defined. The same firmware image is
flashed to **both** halves; the crossover is done at runtime by role
(`SERIAL_USART_PIN_SWAP`, master swaps TX/RX in its init path).

Observed on hardware:
- With pointing **disabled**: the two halves can't talk. The master retries split
  transactions, exhausts `SPLIT_MAX_CONNECTION_ERRORS` (200), times out, and runs
  **solo** — the display sits on the boot splash until a keypress forces a refresh.
- It follows the **master role** (swap which half has USB → the behavior follows the
  new master), not a physical half.
- Enabling the pointing device makes the link come up. **Deterministic, not a flaky
  race.**

Master HID console on the broken build:
`Split link: … crc_err=0 transport_fail=100.0%` climbing to >1.2M frames, all
failing. So the QMK serial transport is dead at the **transport** layer — every
frame times out. It is **NOT** payload/CRC corruption (`crc_err=0`), and the
handshake token can't mismatch (`tid ^ NUM_TOTAL_TRANSACTIONS`, same image both
sides).

---

## 2. What was ruled OUT (earlier work, on-hardware)

| Hypothesis | How ruled out |
|---|---|
| Trackpad hardware / Cirque I2C stall | No-op `custom` pointing driver (zero I2C) + `SPLIT_POINTING_ENABLE` → **still works** (`5de77192`). So the fix is the split transaction, not the I2C. |
| Transaction **count** / handshake-token / table size | Registered 3 dummy split transactions to match `NUM_TOTAL_TRANSACTIONS` of the working build (no pointing) → **still 100% transport_fail** (`e260bcd4`). |
| Every-cycle master→slave **traffic/frequency** | Drove an every-cycle pull over the existing `USER_SYNC_SLAVE_DATA` channel from housekeeping → **still breaks** (`01cb83d0`). Not the traffic. |
| Memory/`.bss`/stack **layout** | `.bss`/`.data`/stacks within ~100 B and stacks at identical addresses between working and broken. |
| **Baud mismatch** | `POLY_FIXED_SERIAL_CLKDIV` (pin the PIO clkdiv to a fixed 125 MHz constant on both halves) was a **no-op** — the master already computes the divisor at 125 MHz; forcing it changed nothing. |
| **Framing errors** (bad stop bit at wrong baud) | `g_rx_framing_errors == 0` across the whole run. Bytes decode cleanly. |
| **Bad RX SM state** (metastable/wedged) | A full RX SM re-init (disable, clear FIFO, restart, jmp to program start, re-enable) did **not** recover the link. Consistent with the RP2040 PIO forum "metastable crash" being a *different* failure (that needs `input_sync_bypass` set; ours has synchronizers **on**, `in_sync_bypass=0`, and a clean PC). |
| PIO resource collision (WS2812/RGB) | Serial is on **PIO1**, WS2812 on **PIO0**. No collision. |

---

## 3. What was proven TRUE (master-console, race-free measurements)

All of the following were measured on the master's USB console (no slave rendering
involved, so no HIGHPRIO-thread SPI race), with `POLY_HANDSHAKE_DIAG`:

1. **The slave physically transmits** valid 230400-baud UART frames on the master's
   RX pin GP5: `min_low_us=4, min_high_us=4` (one bit ≈ 4.34 µs at 230400).
2. **The master RX state machine is perfectly configured** (from a live register
   dump, `serial_debug_dump_rx_sm()`):
   - `PIO1 ctrl=0x3` (both SMs enabled), `rx_sm=1`.
   - RX `pinctrl` IN_BASE = 5 = GP5, `execctrl` JMP_PIN = 5, `clkdiv=0x0043D100`
     (matches the working TX SM's divisor exactly).
   - GP5 `func=7` (PIO1), pad `IE=1`, `padoe` bit5 = 0 (master is **not** driving its
     own RX pin), `in_sync_bypass=0` (synchronizers on), `clk_sys=125 MHz`.
3. **The RX SM decodes cleanly and pushes the echo byte into the FIFO every
   transaction:** `pc_moved>0` (PC leaves the `wait 0 pin` at offset 19, reaches
   push at pc 20..27), `framing_errors=0`, and a direct FIFO pop right before the
   receive returned the **correct** echo byte every time: `direct_hits=500,
   direct_last=0x19` (0x19 = `tid ^ NUM_TOTAL_TRANSACTIONS` for that transaction).
4. **The master's PIO1 rx-not-empty IRQ effectively never fires:** `irq_entries` was
   1–14 across 500 transactions, `irq_rxne` ≈ 0–1 — even though NVIC
   `ISER0=0x0000AA21` shows **PIO1_IRQ_0 (bit 9) enabled**, and `ISPR0=0x00010040`
   shows SIO_IRQ_PROC1 (bit 16) pending.

### The localization

Putting 1–4 together: the echo byte **reaches the master RX FIFO**, but the master's
`sync_rx()` suspends waiting on the PIO1 rx-not-empty IRQ to wake `rx_thread`, and
**that IRQ wake never happens**. So the receive sits until the 20 ms timeout and
fails — even though a valid byte is (or shortly becomes) available in the FIFO.

Why `SPLIT_POINTING_ENABLE` "fixes" it: enabling pointing adds an extra per-cycle
transaction that **shifts timing** so a byte is already sitting in the FIFO at the
moment the receive checks it (the receive's first `pio_sm_is_rx_fifo_empty` check
passes without ever needing the IRQ). This is **masking**, not repairing, the dead
IRQ. (The old `sample_burst` diagnostic did the same by accident — see §5.)

This matches the class of RP2040 PIO/SIO IRQ-delivery quirk already documented in
this repo's **core1-hang** investigation (`CLAUDE.md`): an IRQ that is enabled in
NVIC yet is not actually delivered/taken.

---

## 4. The asymmetry (an open sub-question)

Both halves run the **same code**. The **slave's** RX IRQ works — the slave's
`react_to_transaction()` reaches its echo stage every time (confirmed earlier via
the stage probe: "RX / lock / glyph confirmed working"). Only the **master's** RX
IRQ is dead. The only role-dependent difference in the RX path is the pin swap:
master RX = GP5, slave RX = GP4 (the SM index `rx_sm=1` and IRQ wiring are
identical). Why the master's PIO1_IRQ_0 delivery specifically fails is **not yet
explained** — it is the remaining root-cause question even once the poll bypass is
confirmed.

---

## 5. The fix under test (poll bypass) + a measurement gotcha

**Approach:** don't depend on the IRQ. In `sync_rx()` (serial_vendor.c), before
falling back to the IRQ-suspend, **poll the RX FIFO** (unlocked, IRQs on) for a
bounded window. Because only this thread drains the master RX FIFO, seeing the FIFO
non-empty means the next locked empty-check consumes the byte with no suspend. A
genuinely dead link still falls through to the normal 20 ms IRQ-suspend timeout, so
the poll can only help. Gated by `POLY_RX_POLL_FIX`, window `POLY_RX_POLL_US`.

**Measurement gotcha (important for reading logs):** the `min_low_us`, `fifo_seen`,
`pc_moved`, `direct_hits` fields are populated by the `serial_debug_rx_sample_burst()`
(a ~1–3 ms GP5 sampling loop) and `serial_debug_rx_pop_before_recv()` diagnostics.
When those calls were removed to test the poll cleanly, those fields read back as
**zero** — that is a *measurement artifact*, NOT the slave going silent. Do not
re-interpret zeroed diag fields as "no transmission." Also: `_pop_before_recv()`
**steals the byte** out of the FIFO before the real receive, so it must NOT be left
in when testing the poll (it would defeat it).

**First poll result (1.5 ms window):** link **still dead**, `irq_entries≈14`, and
the poll did not catch the byte. But the earlier `direct_hits=500` proof came only
*after* the ~1–3 ms `sample_burst` loop. ⇒ Working hypothesis: **the echo lands
late (>1.5 ms)**, and the old burst was accidentally providing exactly that delay.

**22 ms-poll build (`bdb71a18`) — measured on hardware (2026-07-15):**
```
HS-DIAG: total=500 timeout=500 … poll_hits=3 poll_miss=500 poll_max_us=426 irq_entries=14 irq_rxne=1
```
- `poll_miss=500`: polling the **full 22 ms**, the echo byte reaches the master RX
  FIFO in only **3 of ~503** transactions (~0.6 %).
- `poll_max_us=426`: when it *does* arrive, it arrives **fast (≤426 µs), not late**.

**⇒ The "byte arrives late" theory is REFUTED. The byte does not arrive late — it
almost never arrives at all.** The earlier `direct_hits=500` ("byte always in the
FIFO") was **entirely an artifact of the `sample_burst` diagnostic loop** that ran
before that measurement: remove the ~1–3 ms burst and the master RX SM captures the
slave's echo ~0.6 % of the time.

**⇒ The poll-bypass fix is DEAD** — you cannot poll a byte out of the FIFO that
never lands there. The failure is **upstream of the FIFO**: the master's RX state
machine is **not reliably capturing the echo**, and the wake-path/IRQ theory
(§3) is downstream of a problem that mostly prevents the byte from being received
at all.

**The load-bearing clue:** running a tight ~2 ms spin (the old `sample_burst`,
which reads GP5 + the RX PC in a loop) right after the send makes capture succeed
**every** time; a 22 ms FIFO-status poll spin does **not**. Register reads have no
documented hardware side effect, so the difference is either (a) *what* is read
(GPIO_IN / the SM ADDR register vs FSTAT) or (b) the exact CPU/bus activity during
the echo window. This is the current thread to pull.

**Control build (`538ccca1`) — measured on hardware (2026-07-15):**
```
HS-DIAG: total=500 timeout=500 … pc_moved=20023 pc=20..27 min_low_us=4 min_high_us=2
         fifo_seen=697269 poll_hits=0 poll_miss=500 poll_max_us=0 irq_entries=140 irq_rxne=0
         fdebug=0x01000002   (RXSTALL bit set)
```
- **The link is STILL DEAD** (`timeout=500`, `USER_SYNC_POLY_DATA failed to send`).
  ⇒ the `sample_burst` spin does **NOT** fix the link — the "spin makes capture
  work" theory is **also refuted**.
- `pc_moved=20023` ≈ **40 decodes per transaction** (only ONE echo is expected),
  `fifo_seen=697269`, `fdebug` **RXSTALL** set ⇒ the RX SM is decoding a **storm of
  bytes and overflowing the FIFO** — far more than the single echo.
- `min_high_us=2` ⇒ **sub-bit-width (2 µs) glitches on GP5** (a bit is 4.34 µs).
- `irq_rxne=0` (the 140 `irq_entries` are TX-not-full wakes from the sends, not RX).

**⇒ The master RX SM is not *failing to capture* — it is capturing GARBAGE
continuously and never cleanly framing the single expected echo.** The earlier
`direct_hits=500 / direct_last=0x19` was mostly this garbage, with `0x19` popping up
occasionally by luck. Every software-side theory so far (IRQ wake, poll bypass, spin
before receive) is now dead. This points at a **signal-integrity / spurious-trigger
problem on the master's GP5 RX line**, or a framing problem that only the
pointing-enabled build's timing/layout happens to avoid.

**Pointing-enabled comparison build (`ed9652fb`, still had sample_burst) — measured
on hardware (2026-07-15). Link comes UP; `HS-OK` success lines:**
```
HS-OK: ok=2000 irq_entries=5051 irq_rxne=1 poll_hits=0 poll_miss=0 poll_max_us=0
HS-OK: ok=8000 irq_entries=7703 irq_rxne=1 poll_hits=0 poll_miss=0 poll_max_us=0
```
Two solid conclusions (robust to the sample_burst confound):
1. **`irq_rxne=1` in 8000 transactions → the RX-not-empty IRQ is DEAD in the WORKING
   build too.** So the dead RX IRQ was **never** the difference between working and
   broken — it is dead in both. The whole IRQ-wake line of inquiry is closed: the
   receive succeeds whenever the echo byte is simply *present* in the FIFO, never via
   the IRQ. (`irq_entries` ≈ 1/txn are TX-not-full wakes from the sends.)
2. **`poll_hits=0 poll_miss=0` → the poll never even entered → the FIFO was ALWAYS
   non-empty at receive time.** In the working build the echo byte is reliably present
   when the receive checks; in the broken build it isn't (garbage storm, FIFO empty).

⇒ **Working vs broken = "is the echo byte cleanly present in the FIFO at receive
time", NOT the IRQ.** What enabling pointing changes must make the echo land cleanly
and reliably.

**Confounds in that build (being removed next):**
- It **still ran `sample_burst`** — a ~2 ms master busy-spin *per transaction*. That
  is almost certainly the **sluggishness + slave→master stalling** the user observed
  (master saturated ~2 ms/txn), AND it provides a ~2 ms delay that itself lets the
  echo arrive — so "byte always present" may be sample_burst, not pointing.

**Clean working read (`78b5b99e`, pointing on, sample_burst removed) — hardware
(2026-07-15). Link works (slave keystrokes came back, briefly); `HS-OK`:**
```
HS-OK: ok=42000 irq_entries=5219 irq_rxne=0 poll_hits=84658 poll_miss=2 poll_max_us=1721
```
- `poll_hits=84658` (~2 per txn), `poll_miss=2` (essentially zero), `irq_rxne=0`.
- **`poll_max_us=1721` — the echo arrives with up to ~1.7 ms latency, and the poll
  catches it essentially every time.** So the byte was NOT instantly present — the
  earlier "always present" (`ed9652fb`) *was* the sample_burst delay. Here, with the
  spin gone, **the poll (`POLY_RX_POLL_FIX`) is the actual receive mechanism, and it
  works** (the dead RX IRQ is bypassed).

**⇒ Crisp, quantitative root-cause probe found: `poll_miss`.**
| Build | pointing | `poll_miss` | echo arrives? |
|---|---|---|---|
| `bdb71a18` | OFF | ~500 / 503 | **no** |
| `78b5b99e` | ON (custom, split) | 2 / 84660 | **yes**, ≤1.7 ms |

**⇒ Pointing makes the slave's echo actually come back; without it the echo doesn't
arrive at all. The poll-fix is a real fix for the dead RX IRQ — but it can only catch
a byte that arrives.** `poll_miss` now discriminates the cause on the master console
with no rendering needed.

**Open thread:** even in the working build the slave keystrokes "worked briefly then
stopped" though `poll_miss≈0` (handshake fine). That is a *separate* higher-level
symptom (key-event transfer / slave scan), or a diagnostic-console-flood artifact —
tracked separately, not the handshake.

**(c)-discriminator build (`77ab181d`, pointing code, NO `SPLIT_POINTING`) — hardware
(2026-07-15):**
```
HS-DIAG: total=500 timeout=500 … poll_hits=3 poll_miss=500 … irq_rxne=1
```
- **`poll_miss=500` — identical to the no-pointing broken build.** Running the pointing
  **code** without `SPLIT_POINTING_ENABLE` does NOT make the echo arrive. **Effect (c)
  — linking/running `pointing_device.c` — is RULED OUT.**

**Candidates now (by elimination):**
| candidate | status |
|---|---|
| (a) transaction **count** / table size | ruled out (`e260bcd4` dummy transactions) |
| (a′) every-cycle **traffic** | ruled out (`01cb83d0` heartbeat) |
| (c) pointing **code** linkage/init/task | **ruled out** (`77ab181d`, `poll_miss=500`) |
| **(b) the `split_shared_memory_t` `pointing` member** | **last one standing** |

The `pointing` member (`split_slave_pointing_sync_t`, ~8 B) sits in `transport.h`
**immediately before the RPC buffers** (`rpc_info` / `rpc_m2s_buffer` / `rpc_s2m_buffer`)
that the poly `USER_SYNC_*` transactions transfer through — so enabling it **shifts the
RPC buffers' offset**. This is the **memory-layout-coincidence / latent-corruption**
hypothesis: enabling pointing shifts the shared-memory layout and masks a real bug.

**(b)-discriminator build (`6ff584d4`, dummy shmem member only) — hardware (2026-07-15):**
```
HS-DIAG: total=500 timeout=500 … poll_hits=0 poll_miss=500 … irq_rxne=0
```
(Verified the pad compiled in: flag in `cflags.txt`; the member is 8 B and shifts the RPC
buffers by 9 B.) **`poll_miss=500` → the shmem member / RPC-offset shift is RULED OUT too.**

**So EVERY isolated component is now ruled out** — (a) count, (a′) traffic, (b) shmem
member, (c) code/task — yet full pointing (all together) works.

**Linker/memory-map diff (working full-pointing vs broken pad-only, hardware-free):**
`.text` grows with pointing (expected); `.bss`/`.data` symbols shift a few dozen bytes;
but **the stacks are at byte-identical addresses** in both (`__main_stack_base__`
0x20040000, `__process_stack` 0x20040400, all core1 stacks identical). **No stack move or
suspicious overlap** → the "layout-coincidence masks a stack/buffer bug" hypothesis is
**weakened**; the cause is more likely **functional**.

### Root-cause chain (current best model) — the dead-IRQ blocking-receive trap

Putting the confirmed facts together:
1. The PIO1 rx-not-empty IRQ **never fires** (`irq_rxne=0` in every build, working or
   broken — silicon/SDK quirk, same family as the core1-hang PIO/SIO IRQ issue).
2. So `osalThreadSuspendTimeoutS(&rx_thread, …)` on that IRQ only wakes on its **timeout**,
   never on a byte. For a **finite** receive that's fine (it times out and retries). For a
   **`TIME_INFINITE`** receive — which is exactly the **slave's id-wait**
   (`serial_transport_receive_blocking`) — it **suspends FOREVER**. Once suspended, a byte
   landing in the FIFO can't wake it: **the slave goes permanently deaf.**
3. `poll_miss=500` on the master = the slave never echoes = the slave isn't receiving ids
   (it's deaf). With pointing, the extra every-cycle transactions keep the slave's receive
   loop **cycling** (each completed transaction re-enters the receive with a fresh poll), so
   it re-polls often enough to catch a master id before going deaf — masking, not fixing,
   the trap. This also explains why no single isolated factor reproduced it (it's about
   keeping the loop *cycling*, an emergent effect of real traffic), and why builds that
   "work" still **degrade after a short time** (the slave eventually loses the race and goes
   deaf).

### The candidate real fix

Never wait on the dead IRQ indefinitely: the slave's blocking (`TIME_INFINITE`) id-receive
must re-poll the FIFO instead of suspending forever.

**Attempt 1 (`01225dc3`) — hand-rolled suspend-slice loop inside `sync_rx`: BRICKED IT.**
Both halves went unresponsive, no USB console, glyphs frozen (a hang before the main loop,
on both halves). The bug was in the bespoke suspend-slice surgery in `sync_rx` (couldn't be
pinned down safely). **Reverted.** Lesson: don't hand-roll the ChibiOS suspend loop.

**Attempt 2 (current) — loop the PROVEN finite receive instead.**
`serial_transport_receive_blocking()` (the slave's 1-byte id-wait, the only `TIME_INFINITE`
caller) now loops the existing, well-tested `receive_impl(..., TIME_MS2I(POLY_RX_BLOCK_SLICE_MS))`
until it succeeds — leaving `sync_rx` **untouched**. Each attempt re-polls the FIFO via
`sync_rx`'s tight poll and yields between attempts via the bounded suspend, so a byte landing
between attempts is always picked up and the slave can never go permanently deaf. It only
ever receives the single-byte id, so a retried attempt never splits a buffer. `POLY_RX_POLL_US`
dropped 22000→3000 (still covers the ~1.7 ms echo; less idle spin). **Tested with NO
pointing, NO pad** (zero pointing symbols):
- **link up, `poll_miss≈0`, slave alive, both-halves typing, stable (no "stops after a
  while")** → confirmed: the dead-IRQ blocking-suspend trap was the root cause; this is the
  real fix and split42 needs neither the trackpad nor `SPLIT_POINTING`. Then extend to
  split72 + drop the diagnostics.
- **still dead / bricked** → the slave-deaf model is wrong or incomplete.

---

## 6. Key files & flags

- `platforms/chibios/drivers/vendor/RP/RP2040/serial_vendor.c`
  - `pio_serve_interrupt()` — the PIO1 IRQ handler; `g_pio_irq_entries` /
    `g_pio_irq_rxne` count whether it fires at all.
  - `sync_rx()` — the receive-wake path; the `POLY_RX_POLL_FIX` pre-poll + poll
    measurement live here.
  - `serial_debug_*()` — the diagnostic accessors (FIFO level/peek, GP5 sample
    burst, RX PC span, framing errors, IRQ counters, poll counters, RX SM dump).
- `platforms/chibios/drivers/serial_protocol.c`
  - `initiate_transaction()` — master handshake; the `HS-DIAG` (fail) + `HS-OK`
    (success) console prints, all under `POLY_HANDSHAKE_DIAG`.
- `keyboards/polykybd/split42/rules.mk`
  - `POLY_HANDSHAKE_DIAG` — master-side diagnostics (on).
  - `POLY_RX_POLL_FIX` + `POLY_RX_POLL_US` — the poll bypass under test (on).
  - `POLY_FIXED_SERIAL_CLKDIV` — the fixed-baud experiment (off, was a no-op).
  - `POLY_SLAVE_STAGE_PROBE` — slave-side keycap stage probe (off; raced the slave's
    own SPI, gave non-deterministic partial fills).

All diagnostic defines are **split42-only** — split72 and normal builds are
unaffected.

---

## 7. Commit trail (this investigation)

Diagnostic + experiment commits on `claude/split42-literal-split72-copy` (most recent
last), each a single change for bisectability:

- `70c7f6af` dump PIO1 padoe/padout — is the master driving its own RX pin GP5?
- `af780752` measure GP5 longest low-run — valid UART start bit vs glitch?
- `503e3472` FIX EXPERIMENT — re-init the master RX state machine after boot
- `5018d666` sample RX PC distribution — never-leaves-wait vs detects-but-misframes
- `241b8152` FIX TEST — pin PIO serial clkdiv to a fixed constant (no-op)
- `52b97b7e` reliable GP5 bit-time (hw timer) + did-the-RX-SM-actually-push probe
- `5d7f2ebe` count RX framing errors (framing_errors=0)
- `eebaff54` pop RX FIFO right before receive (direct_hits=500 → byte reaches FIFO)
- `86da4d85` dump inte0/ints0 + NVIC state (irq_entries≈1 → IRQ never fires)
- `06071ee6` FIX TEST — bypass the dead RX IRQ by polling the FIFO in sync_rx (1.5 ms)
- `bdb71a18` poll the whole 22 ms window + measure echo latency → **byte reaches FIFO
  only 3/503 (poll_max_us=426); the poll bypass is dead, the byte simply isn't captured**
- `57baf658` add this investigation doc
- `538ccca1` control — re-add sample_burst before receive → **link STILL dead; RX SM
  decodes a garbage storm (pc_moved 20023, RXSTALL), sub-bit glitches (min_high_us=2);
  the spin theory is refuted too**
- `ed9652fb` pointing-enabled + diagnostics → **link UP; irq_rxne=1/8000 (RX IRQ dead
  in the WORKING build too), poll never entered (byte always present). But still ran
  sample_burst → sluggish, confounds "byte present".**
- `78b5b99e` pointing on, sample_burst removed → **clean working read: poll_miss=2/84660,
  poll_max_us=1721. The poll IS the working receive path; pointing makes the echo arrive
  (poll_miss≈0 vs ≈500 without). poll_miss is now the root-cause probe.**
- `77ab181d` pointing code, NO SPLIT_POINTING → **poll_miss=500 (same as broken); effect
  (c) code-linkage RULED OUT. By elimination the cause is (b) the shmem `pointing` member.**
- `6ff584d4` dummy shmem member only → **poll_miss=500; (b) shmem member RULED OUT. ALL
  isolated components now ruled out. Map diff: stacks identical → not a layout bug.**
- `01225dc3` REAL-FIX attempt 1 (hand-rolled suspend-slice in sync_rx) → **BRICKED both
  halves (no console, frozen). Reverted.**
- (next) REAL-FIX attempt 2: loop the PROVEN finite receive_impl for the blocking id-wait
  (sync_rx untouched), POLY_RX_POLL_US 22000→3000. NO pointing. poll_miss≈0 + stable
  both-halves typing = the real fix.

> ⚠️ **Environment note:** the remote container was rolled back to an older snapshot
> mid-session once; the **remote branch is the source of truth**. If local `HEAD`
> looks behind, `git fetch origin <branch> && git reset --hard origin/<branch>`
> before continuing.
