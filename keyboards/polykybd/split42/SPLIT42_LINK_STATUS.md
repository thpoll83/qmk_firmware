# split42 split-link status — RESTART (2026-07-15 evening)

Fresh, disciplined tracking of what actually works on real split42 hardware. Started
over because the investigation had drifted into firmware theory-chasing while the
decisive evidence points the other way (see below). **One row per real hardware test.
Record the build banner verbatim + the master side + the result. No conclusions from a
single boot** (the same firmware has produced different results on different boots).

## ⚠️ PIVOTAL FINDING — the firmware is (almost certainly) NOT the variable

**Known-good firmware that worked earlier the SAME day now fails.**
- `5de77192` (FW 0.9.51) — confirmed **"this works indeed"** earlier this session →
  now **fails on both sides** (user rebuilt + a fresh build from here).
- `9253a328` (merge of #144 *split42-working-all-subsystems*, the real-Cirque
  confirmed-working config) — user rebuilt → **also fails now**.

When a *constant* (a fixed commit) changes behaviour between the morning and the
evening, the variable is not in that constant. ⇒ **Suspect the physical layer**, not
the firmware:
- The split link counter reads `crc_err=0 transport_fail=100%` → **no bytes cross the
  inter-half UART at all** (not corruption — total silence). That is the signature of a
  broken physical link (cable / connector / GP4-GP5 solder), not a firmware bug.
- `transport_connected=1` in the boot banner is a red herring: it is the flag's value at
  the single instant the one-shot banner prints, before the main loop starts; it reads 1
  even in runs that then log 100% transport_fail.

**What is already ruled out as the cause (this session):**
- Master side (left vs right) — fails on **both**.
- Build environment — the **same source fails whether the user or I compile it**.
- The boot splash (progressive vs blocking) — `b5bcf07a SPLASH=blocking` still fails.
- The RX pre-poll / dead-IRQ "master receive" theory — a no-delay + pre-poll build
  fails at the handshake (`transport_fail=100%`); and the working restore needed no
  pre-poll at all. (Full write-up in the top-level CLAUDE.md.)
- Handedness — user confirms correct side on the key displays.
- split72 — works with its cable (but NOTE: this validates split72's cable, **not**
  necessarily split42's inter-half cable — see the diagnostics).

## Established facts (hardware)
- split72 works.
- split42 handedness (EE_HANDS) resolves correctly (key-display legends confirm).
- Every failing split42 run shows `crc_err=0 transport_fail=100%` — zero bytes across
  the split UART (GP4 RX / GP5 TX, full-duplex, straight cable, crossover by role via
  `SERIAL_USART_PIN_SWAP`).

## Self-describing builds
Every diag build from 0.9.62+ prints a banner second line — record it verbatim:
```
build: <git-hash> | SPLIT_POINTING=<0/1> RX_PREPOLL=<us> BOOT_DELAY=<ms> SPLASH=<progressive|blocking>
```
(0.9.51 builds like `5de77192` have NO banner line — full-logo splash, that's how you
know it's the genuine old one.)

## Test log — one row per real hardware boot

| # | when | build / commit | FW | pointing | delay | splash | prepoll | USB side | RESULT | notes |
|---|------|----------------|----|----------|-------|--------|---------|----------|--------|-------|
| 1 | 07-15 AM | `5de77192` (KNOWN-GOOD) | 0.9.51 | custom | 400 | blocking | no | ? | ✅ WORKS | "this works indeed" |
| 2 | 07-15 AM | restore `27de2637` (=PR#151) | 0.9.61 | custom | 400 | progressive | no | ? | ✅ WORKS | "it works" |
| 3 | 07-15 PM | `a9e3b1e3` | 0.9.62 | custom | 400 | progressive | no | right, then left | ❌ FAIL | both sides |
| 4 | 07-15 PM | `2a08a7bb` default branch (my build) | 0.9.63 | custom | 400 | progressive | no | left | ❌ FAIL | |
| 5 | 07-15 PM | `2a08a7bb` default branch (user build) | 0.9.63 | custom | 400 | progressive | no | ? | ❌ FAIL | |
| 6 | 07-15 PM | pre-poll `87f15bec` | 0.9.61 | custom | **0** | progressive | **3000us** | ? | ❌ FAIL | expected (no delay) |
| 7 | 07-15 PM | `b5bcf07a` | 0.9.63 | custom | 400 | **blocking** | no | right | ❌ FAIL | user's splash hypothesis — refuted |
| 8 | 07-15 PM | `5de77192` (KNOWN-GOOD, rebuilt) | 0.9.51 | custom | 400 | blocking | no | both | ❌ **FAIL** | **worked in AM, fails now** |
| 9 | 07-15 PM | `9253a328` (#144, real Cirque) | 0.9.5x | cirque_i2c | 400 | blocking | no | ? | ❌ **FAIL** | was confirmed working; fails now |
| 10 | 07-15 PM | `981e5158` EEPROM-diag | 0.9.63 | custom | 400 | progressive | no | right & left | ❌ FAIL | **EEPROM CLEAN on both halves** — see below |
| 11 | 07-16 eve | `7ddc2f49` RX-line probe | 0.9.63 | custom | 400 | progressive | no | **left & right, HOST QUIT, qmk console** | ❌ FAIL | **`rx_bytes=0 rx_clr=0` BOTH orientations** — see below |

### Row 11 — ZERO bytes reach the master's RX, either orientation (2026-07-16)
Clean test (PolyKybdHost quit, `qmk console` only). Both sides identical:
```
Split link: 201 tx crc_err=0 transport_fail=201 giveup=67 err=100.0% rx_bytes=0 rx_clr=0
eeprom: clean on both halves again (hands correct, no stall, no reformat)
```
- **Not one byte ever entered the master's RX FIFO** across 201 transactions, from
  either side. This is the wire-dead signature, not a wake/IRQ problem (nothing to
  wake for) and not host interference (host was off; result unchanged).
- The slave half boots + renders while powered only via the split cable ⇒ the cable's
  **power conductors are fine**; the failure is specifically the **data path**.
- Remaining candidates: the GP4 conductor/joints (master→slave direction — the master
  always TXs on GP4 via the swap), the GP5 conductor/joints (slave→master echo path —
  the slave always TXs on GP5), a dead pad/pin on one half, or the slave never
  transmitting. The next probe (below) splits these without instruments.

### Row 10 — EEPROM ruled out (2026-07-15 ~21:50)
Both halves read out healthy and consistent; EEPROM is **not** the cause:
```
right: eeprom: hands_en=1 hands_raw=0 hands_ms=0 post_en=1 load_ms=0 init_ran=0 bright=50 lang=0   (+ "USER_SYNC_POLY_DATA failed to send")
left:  eeprom: hands_en=1 hands_raw=1 hands_ms=0 post_en=1 load_ms=0 init_ran=0 bright=50 lang=0
```
- `hands_en=1` / `post_en=1` both → eeconfig valid on both halves.
- `hands_raw`: right=0 (right side), left=1 (left side) → **handedness correct on both**.
- `hands_ms=0`, `load_ms=0` → **no blocking wear-leveling consolidation** at boot.
- `init_ran=0` → EEPROM was valid, NOT reformatted this boot.
- `bright=50 lang=0` identical on both → no EEPROM divergence.
⇒ The EEPROM/wear-leveling hypothesis is dead. Nothing persistent-in-software is
wrong. The master still logs `USER_SYNC_POLY_DATA failed to send` = the split bridge
gets no response (consistent with the `transport_fail=100%` earlier).

**The jump from rows 1–2 (works, AM) to rows 3–9 (fail, PM) with overlapping/identical
firmware is the whole story: something changed on the bench between AM and PM.**

## Next: physical-layer diagnostics (hold firmware constant at a known-good build)

Run these with the **known-good `5de77192`** flashed to both halves, changing ONE
physical thing at a time and logging a row above for each:

1. **Swap the inter-half split cable.** Use the *exact* cable that works between the
   split72 halves on split42 (or a known-good spare). transport_fail=100% is textbook
   bad-cable/bad-connector. This is the #1 suspect and the cheapest test.
2. **Reseat / inspect the split42 inter-half connector** (both ends). Look for a cracked
   solder joint on the TRRS/JST and on GP4/GP5 at each RP2040.
3. **Continuity + wiggle test** GP4↔GP4 and GP5↔GP5 end-to-end across the cable while
   flexing it (an intermittent open reads as transport_fail=100%).
4. **Try each half alone as master into a scope/LA on GP5 (TX):** power one half as USB
   master and watch GP5 — does it drive the ~230400-baud handshake frames? Then the
   other half. Isolates a dead TX PIO on one half from a cable fault.
5. **Swap which physical half is which** (if mechanically possible) to see if the fault
   follows a half or the cable.

If a cable swap or reseat brings `5de77192` back to ✅, the firmware chase is over — it
was a degrading physical connection all along, and rows 3–9 were all the same cable
fault, not the configs we were varying.

## Re-analysis with fresh eyes (2026-07-16, pre-probe) — three overlooked things

A from-scratch re-read of the whole investigation, verifying the load-bearing claims
against the code instead of against earlier conclusions. Three findings, one big.

### 1. The "dead RX IRQ" was a MEASUREMENT ARTIFACT (observer effect)

The cornerstone claim — "the PIO rx-not-empty IRQ never fires, even in a working
link (`irq_rxne≈0`)" — does not survive a code read. In `sync_rx()`
(`serial_vendor.c`) the RXNEMPTY interrupt source is **only enabled inside the
FIFO-empty loop**:

```c
while (pio_sm_is_rx_fifo_empty(...)) {
    pio_set_irq0_source_enabled(..., true);   // <-- only reached if FIFO empty at check
    osalThreadSuspendTimeoutS(...);
}
```

If the byte is **already in the FIFO** when `sync_rx` is called — which the working-era
diag runs themselves reported ("poll never entered (byte always present)",
`poll_hits=84658`) — the IRQ source is **never enabled**, so it never fires, so
`irq_rxne` reads ~0 **in a perfectly healthy system**. And the diag pre-poll made this
worse: it deliberately waited for the byte *before* the locked check, guaranteeing the
IRQ was never needed. In broken runs `irq_rxne=0` simply because **no bytes ever
arrived** (nothing to interrupt about). Either way, `irq_rxne≈0` proved nothing.

**Decisive counter-evidence that the IRQ path actually works:** the morning restore
build (row 2) had **no pre-poll at all** and worked. With a genuinely dead IRQ, every
first-byte wait would have suspended into the 20 ms timeout and the link could never
have functioned. It did. ⇒ The IRQ wakes fine when bytes arrive. The entire
"dead-IRQ blocking-receive trap" root-cause chain in SPLIT42_LINK_INVESTIGATION.md is
built on this artifact and is hereby superseded.

### 2. Permanent 100 % transport_fail is INCOMPATIBLE with every timing/boot-order theory

Verified in `split_util.c`/`transactions.c`: after `SPLIT_MAX_CONNECTION_ERRORS` the
master does NOT stop — it keeps probing (reduced cadence, `num_retries` drops 10→1,
`SPLIT_CONNECTION_CHECK_TIMEOUT` backoff) and a single success resets
`connection_errors` to 0. The slave's main loop demonstrably runs (keypress inversion).
So ANY theory of the form "the halves missed each other at boot" (delay, splash churn,
slow slave, busy master) predicts **eventual recovery** — the master offers a
handshake ~forever and a live slave eventually answers one. We observe 1005+/1005
failures over minutes, repeatedly. ⇒ The failure is **structural** for the whole
session: the id byte doesn't reach the slave, or the echo doesn't reach the master.
Bytes are simply not crossing.

### 3. The pointing "fix" had an unexamined hole: POINTING_DEVICE_RIGHT + orientation

Verified in `quantum/split_common/transactions.c` `pointing_handlers_master()`: with
`POINTING_DEVICE_RIGHT`, **if the master is the right half it returns immediately — no
pointing transaction at all**. The extra "healing" split traffic only exists when the
**left** half is master. No working-era test recorded which half was USB master (the
banner didn't exist yet). So the pointing hypothesis was only ever coherent for
left-master boots, and was never actually tested against orientation.

### The synthesis (hypothesis, to be tested — not a conclusion)

One mechanism explains EVERY row of this table plus the entire historical bisect:
**a marginal physical inter-half contact (TRRS jack / solder joint on GP4/GP5) whose
state flips across replug/flash handling cycles.** Every firmware test involved
physical handling (bootloader replug, cable flex). A contact lottery:
- mints false bisect signals (pointing "needed", delay "needed" — whichever build was
  in hand when the contact happened to seat),
- explains why NO software mechanism was ever found (traffic/count/shmem/linkage/
  layout/EEPROM all individually refuted — because there isn't one),
- explains morning-works → evening-fails on identical firmware (contact finally went
  fully open mid-day, after dozens of handling cycles),
- explains the symmetric failure (either conductor open kills both orientations:
  master TXs on GP4 and RXs on GP5 regardless of which half is master),
- survives every reflash and a clean EEPROM.

### What tonight's RX probe (`7ddc2f49`) actually discriminates — THREE ways

This build has **no pre-poll**, so the IRQ path runs un-observed for the first time
while the counters watch the FIFO independently:
- **`rx_bytes=0 rx_clr=0`** while tx climbs → nothing arrives → **wire/contact open**
  (hardware). Next: the slave-blink probe below to localize the direction/conductor.
- **`rx_clr` climbing** (bytes arrive but land in the *next* transaction's clear) →
  bytes DO arrive but the suspend never woke → **the first real evidence of a broken
  IRQ path** (this time without the observer effect). Firmware angle reopens.
- **`rx_bytes≈tx`, failures low** → it works (the contact re-seated — also
  informative: marginal, not open).

### Prepared next step if rx=0: the slave-blink probe (no console needed on the slave)

A build where the slave visibly flashes its keycap displays on ANY received byte.
Plug USB into the left half, watch the right: flashing = master→slave conductor alive
(the cut is the echo path); dark = master→slave conductor dead. Localizes the broken
direction with zero instruments.

### Bench answers (2026-07-16, user) — two questions closed

1. **Orientation: works-era builds worked on BOTH sides.** The user routinely swapped
   the USB side on working AND failing builds. Since a right-master boot has **zero**
   pointing split traffic (§3), a build that worked in both orientations cannot have
   been fixed by pointing traffic. ⇒ **The pointing-traffic mechanism is now
   definitively dead** (the user had independently suspected POINTING_DEVICE_RIGHT).
   Whatever `SPLIT_POINTING_ENABLE` correlated with, it was not the transactions.
2. **PolyKybdHost was ALWAYS running (it autostarts) — in every test, works and
   fails.** Consequences:
   - As a *constant* it is not the morning→evening flip variable by itself, BUT the
     **one-time first successful P11 connect (the morning restore) would have kicked
     the silent font-pack auto-flash** — ~500 KB+ to the 4–8 MB region, every chunk
     bridged to the slave, **completely invisible on split42** (no status OLED, no
     RGB). Replug mid-flash = partial slot (CRC-invalid ⇒ skipped at boot, in theory
     harmless — but this is the one persistent non-EEPROM state that changed exactly
     in the works window).
   - Worse for test hygiene: **every evening test was polluted** — the host↔master
     HID link works fine (protocol matches), so on every enumeration the host
     connects and **re-attempts the font-pack flash against the DEAD slave bridge**,
     each chunk eating full bridge-retry timeouts on the master, invisibly.
   - ⇒ **Protocol from now on: quit PolyKybdHost (tray Quit / kill the daemon too —
     daemon mode keeps a headless process) and use `qmk console` only.** The
     `Split link:` line prints unconditionally (not debug-gated), so `qmk console`
     sees it.
   - **Forensics TODO:** the host writes logs (log viewer / `daemon_log.txt`). The
     MORNING session's host log should show connect events, the fontpack autocheck
     decision, flash progress/errors — potentially pinning the exact minute the link
     flipped and what the host was doing at that moment. Also `polyctl fontpack
     status` (or the GET_ID v6 block) on each half shows whether bundles landed.

### Still open (bench)

- **"Cables work fine on split72" — which cable?** If the *split42's* cable was
  cross-tested on split72, the cable is exonerated and the fault localizes to
  split42's jacks/pads. If split72's own cable was meant: swap it onto split42.

## Rule for this doc
- Add a row for **every** real boot, pass or fail, with the verbatim banner + USB side.
- Never delete rows. Never conclude from one boot — look for the ratio / the pattern.
- Keep the firmware constant while chasing hardware; keep the hardware constant while
  chasing firmware. Change one variable per row.
