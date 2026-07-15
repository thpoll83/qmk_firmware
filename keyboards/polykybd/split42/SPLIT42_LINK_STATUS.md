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

## Rule for this doc
- Add a row for **every** real boot, pass or fail, with the verbatim banner + USB side.
- Never delete rows. Never conclude from one boot — look for the ratio / the pattern.
- Keep the firmware constant while chasing hardware; keep the hardware constant while
  chasing firmware. Change one variable per row.
