# split42 split-link diagnostics — investigation record + bench toolkit

The first physical split42 pair has a **dead inter-half UART**: the master (USB half)
types fine, but the slave receives nothing. This doc is the **standalone record** of
that investigation and the **opt-in diagnostic firmware** written to chase it.

The diagnostic **code is deliberately kept OUT of the production tree** (it is bring-up
scaffolding, not a feature). It lives, buildable, on the bring-up branch
**`claude/split42-oled-status-display-8uok79`** — the commit SHAs are listed under each
flag below so it can be cherry-picked back when the boards are on a bench. This doc is
the map to it.

---

## OPEN ISSUE — split link dead on split42 (master TX, slave receives nothing)

**Symptom (field, 2026-07, first physical split42 pair):** the master (USB half)
transmits and types normally, but the **slave receives nothing** — the health counter
reads `transport_fail=100%, crc_err=0, giveup` climbing, the slave RX frame counter
(`get_split_rx_frames()`) stays at `S 0`, slave keys don't register and its keycap
OLEDs never update. **Reproduced on 3 split42 boards; split72 works with byte-identical
firmware.** "It worked once" per the user.

**Status: UNRESOLVED. Every automated/firmware avenue is exhausted and CLEARED — the
residual is the physical build of the split42 boards or something only a bench
instrument can see. Do NOT re-chase the firmware/design; pick up at the bench probe
below.**

### Ruled out (with the evidence — don't re-derive these)
- **Firmware / serial config** — `diff`'d `halconf.h` / `mcuconf.h` / `config.h` split42
  vs split72: identical except the baud value and the I2C bus for the OLED/trackpad. The
  `SERIAL_USART_*` / `SPLIT_TRANSACTION_IDS_USER` / `EE_HANDS` defines all live in the
  **shared** `config.h`. The proven vendor/PIO transport that runs split72 is the *same
  code*.
- **Role / handshake** — forcing roles (`POLYKYBD_MASTER_LEFT`, `POLYKYBD_HIL=left/right`)
  still failed; `PIN_SWAP` is applied by `is_keyboard_master()`, same path both variants.
- **Baud** — failed at 230400, 115200, and 19200 (`POLYKYBD_SERIAL_SPEED=5`).
- **PIO vs bitbang** — failed on PIO full-duplex, PIO half-duplex (`POLYKYBD_HALF_DUPLEX`),
  AND bitbang (`POLYKYBD_SERIAL_BITBANG`). ⚠️ Bitbang is a weak signal (see the flag table).
- **Cable** — the same USB-C bridge cable works on the split72 pair and for HID firmware
  transfer. Good.
- **GP4↔GP5 short** — the **phased** loopback (`POLYKYBD_PIN_LOOPBACK`, GP4 at 0.7 s /
  GP5 at 1.1 s) showed all four level combinations `00/01/10/11` on the reader ⇒ the two
  conductors are independent (a bridge would only ever show `00`/`11`). Also no
  GP4/GP5-to-GND short, no open (DC loopback conducts both directions).
- **Schematic + PCB layout** — investigated with the `investigate-kicad-pcb` skill. The
  COM1/COM2 net is identical between variants: same passives (22 Ω `R1/R2`, 5.1 k CC
  pulldowns, **no filter cap on COM**), and the layout path
  `U10.6/7 (GP4/GP5) → U26 (shunt ESD array) → USB2 (bridge connector)` is the same parts
  / ~48 mm(L) & 58 mm(R) / 0.25 mm width — the **right half is byte-identical**. (Net
  numbers are per-file: split42-left COM = 226/227, right = 400/401 — resolve by NAME.)
  `left2`/`right2` PCBs are unrelated stubs (no U26/USB2).

### Localisation
A **known-good split72 half used as the slave** still failed 100% ⇒ the fault is on the
**split42 master's side** (its GP4/GP5 serial path — TX and/or RX) or, now ruled out, the
cable. The "DC conducts / independent lines, but no UART at any baud down to 15.6 k"
pattern is a low-pass signature, but the schematic shows no filter cap and the layout is
equivalent to split72 — so the mechanism is **not identified**. Do **not** assert a
specific one (a "copper-plane short" and a "cracked QFN joint" were both floated and
neither is verified — BRINGUP.md §4's GP23 open joint is verified for **GP23 only**, says
nothing about GP4/GP5).

### Bench procedure (needs a multimeter / scope — the decisive next step)
1. **Scope GP5 on the split42 master while it is trying to send** (it retries
   continuously): is a UART waveform present at the **RP2040 pad** → at **U26** → at the
   **USB2 pad**? First point where it disappears localises it (like BRINGUP.md §4 did for
   GP23).
2. **Continuity / resistance** GP4 & GP5 from the RP2040 QFN pad (U10 pad 6/7) → U26 →
   USB2 pad — good joint < 1 Ω; flex the board while watching for intermittency.
3. **Role-flip / board-rotation** (no instrument): make a *different* split42 the master,
   rotate pairings. Every split42 pairing failing while split72 never does ⇒ systematic to
   the split42 build; one working pairing localises it.

---

## Diagnostic build flags (bench toolkit — code on the bring-up branch)

Opt-in `-e` flags. All OFF by default; split72 and the normal split42 build are
untouched. Flash **both halves** with the same flag. The commit SHA is the restore
point on `claude/split42-oled-status-display-8uok79`.

| Flag | Commit | What it does / proves |
|------|--------|-----------------------|
| `POLYKYBD_LINK_DIAG=yes` | `0f208611`, `460bf1b6` | Draws role glyph + TX/transport-fail/RX-frame counters on the top keycap row (no console needed). `update_displays()` early-returns under it. Adds `get_split_rx_frames()` (`split_sync.c/.h`) and the link getters in `bridge_helper.c/.h`. |
| `POLYKYBD_SERIAL_SPEED=N` | `c8b7f2b6` | Overrides `SELECT_SOFT_SERIAL_SPEED` for **any** driver. `=5` → 19200 baud on the **proven vendor/PIO** transport — the clean low-baud test. |
| `POLYKYBD_SERIAL_BITBANG=yes\|slow` | `6a330469`, `72745bc0` | Swaps `SERIAL_DRIVER` to QMK's software bitbang on `SOFT_SERIAL_PIN GP5` (no PIO). `slow` forces 64 µs/bit (~15.6 kbaud). ⚠️ **Weak evidence — QMK bitbang is essentially unexercised on RP2040**, so a bitbang failure may be the driver, not the link. Confirm on split72 before drawing a conclusion. |
| `POLYKYBD_HALF_DUPLEX=yes` | `d5b3f93c` | Reverts to single-wire half-duplex on GP5 (drops FULL_DUPLEX/PIN_SWAP/RX). Works even if GP4 is dead. |
| `POLYKYBD_NO_PIN_SWAP=yes` | `d5b3f93c` | Keeps full-duplex but drops the firmware TX/RX crossover (for a physically-crossed bridge). |
| `POLYKYBD_PIN_LOOPBACK=drive\|read` | `dcf0bae3`, `5f7b353f` | Two-board GPIO loopback. Driver toggles GP4 (0.7 s) / GP5 (1.1 s) at **different** rates; reader shows both levels on the top keycap. All of `00/01/10/11` = conductors independent & live; only `00/11` = GP4↔GP5 shorted; stuck 0 = short to GND; stuck 1 = open. |
| `POLYKYBD_GPIO_SHORT_TEST=yes` | `9d959d8a` | Single board: drives GP4/GP5 high, reads back, prints `PINTEST … [1=ok, 0=SHORT to GND]`. |
| `POLYKYBD_MASTER_LEFT=yes` | `a9dd0f76` | Forces the left half master by handedness (bench role-forcing without the USB-VBUS dependence). |
| (baud test) | `e1d3fdbb` | Lowered `split42/halconf.h` `SELECT_SOFT_SERIAL_SPEED` 1→2 (230400→115200). ⚠️ Revert to 1 before shipping. |

### The most reusable snippet — phased GP4/GP5 loopback (`poly_keymap.c`)

Driving the two pins to the **same** level can't distinguish two good independent
conductors from a GP4↔GP5 bridge — both read equal. Phasing them apart is what makes a
short visible (`00`/`11` only, never `01`/`10`):

```c
#ifdef POLYKYBD_PIN_LOOPBACK_DRIVE
    gpio_set_pin_output(GP4);
    gpio_set_pin_output(GP5);
    uint32_t drv_now = timer_read32();
    gpio_write_pin(GP4, (drv_now / 700)  & 1);   // toggles every 700 ms
    gpio_write_pin(GP5, (drv_now / 1100) & 1);   // toggles every 1100 ms
#else /* READER */
    gpio_set_pin_input_high(GP4);
    gpio_set_pin_input_high(GP5);
    int g4 = (int)gpio_read_pin(GP4), g5 = (int)gpio_read_pin(GP5);
    // show '0'+g4, '0'+g5 on the top keycap; all of 00/01/10/11 = independent & live,
    // always-equal (only 00/11) = GP4/GP5 shorted, stuck 0 = short to GND, stuck 1 = open.
#endif
    return;   // own the pins, skip the split transport
```

Full code for every flag is on the bring-up branch at the SHAs above; the PCB side is
the `investigate-kicad-pcb` skill (`kicad_net_diff.py`).
