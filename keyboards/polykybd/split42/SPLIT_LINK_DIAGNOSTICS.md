# split42 split-link diagnostics — bench toolkit

The first physical split42 pair had a dead inter-half UART: the master typed, the slave
received nothing (`transport_fail=100%`, `crc_err=0`, slave RX `S 0`).

**Outcome:** identified via the transparent split72→split42 rebuild + a per-subsystem
bisect (#143) as a firmware split-link *establishment* failure — enabling
**`SPLIT_POINTING_ENABLE`** brings the link up (a no-op `custom` pointing driver
suffices; the trackpad hardware is not needed). The authoritative, still-evolving
analysis of *why* is in the top-level **`CLAUDE.md`** (split-link investigation notes).

This file is just the reusable **bench toolkit** — opt-in diagnostic builds used to
chase the link. They are **not in the shipped tree** (bring-up scaffolding); they live
on the bring-up branches at the SHAs below. All OFF by default; flash both halves with
the same flag.

| Flag | Commit(s) | What it does |
|------|-----------|--------------|
| `POLYKYBD_LINK_DIAG=yes` | `0f208611`, `460bf1b6` | Draws role glyph + TX/transport-fail/RX-frame counters on the top keycap row (no console needed); `update_displays()` early-returns under it. |
| `POLYKYBD_SERIAL_SPEED=N` | `c8b7f2b6` | Overrides `SELECT_SOFT_SERIAL_SPEED` for any driver (`=5` → 19200 on the vendor/PIO transport). |
| `POLYKYBD_SERIAL_BITBANG=yes\|slow` | `6a330469`, `72745bc0` | Swaps `SERIAL_DRIVER` to QMK's software bitbang on GP5 (no PIO). |
| `POLYKYBD_HALF_DUPLEX=yes` / `POLYKYBD_NO_PIN_SWAP=yes` | `d5b3f93c` | Single-wire half-duplex on GP5 / full-duplex without the firmware crossover. |
| `POLYKYBD_PIN_LOOPBACK=drive\|read` | `dcf0bae3`, `5f7b353f` | Two-board GPIO loopback; drives GP4/GP5 at different rates so a GP4↔GP5 short shows as `00/11` only vs `00/01/10/11` for independent lines. |
| `POLYKYBD_NO_STATUS_OLED=yes` | `177ef5eb` | Disables the status OLED + its I2C1 bring-up (removes I2C1 as a variable). |

The PCB side of the toolkit is the **`investigate-kicad-pcb`** skill (in the
`thpoll83/PolyKybd` hardware repo) with `kicad_net_diff.py` for by-name net/variant diffs.
