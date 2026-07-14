# split42 split-link diagnostics — investigation record + bench toolkit

The first physical split42 pair had a **dead inter-half UART**: the master (USB half)
typed fine, but the slave received nothing (`transport_fail=100%`, `crc_err=0`, slave RX
counter `S 0`). This doc is the standalone record of that investigation, its **corrected
outcome**, and the opt-in diagnostic firmware written to chase it.

> **Authoritative, still-evolving narrative lives in the top-level `CLAUDE.md`** —
> § "Troubleshooting principle: don't take shortcuts" and the split-link investigation
> notes (the exact resting config, the transport-establishment analysis, and the
> remaining `(b)` shmem-layout vs `(c)` code-linkage discrimination). This file is the
> bring-up-side summary + the bench toolkit; defer to `CLAUDE.md` for the live detail.

---

## Outcome — IDENTIFIED (firmware dependency), and the big correction

**It was never a board fault.** The productive path was **not** more bench probing of
GP4/GP5 — it was the transparent split72→split42 rebuild + a per-subsystem bisect
(merged in #143; confirmed-working config in #144). Result:

- **The symptom is a split-link *establishment* failure** at boot, not a wire/PCB
  problem and not the "slave hangs mid-render / core1 hang" originally framed here. With
  the failing config the two halves (same image on both) can't establish the link: the
  master retries split transactions, exhausts `SPLIT_MAX_CONNECTION_ERRORS` (200), times
  out, and runs **solo** — the display sits on the boot splash until a keypress forces a
  refresh. It follows the **master role** (swap USB → the behaviour moves), not a
  physical half.
- **The fix is enabling `SPLIT_POINTING_ENABLE`** (split72 always had the Cirque
  trackpad, which hid the dependency). The trackpad *hardware*/I2C is **not** needed — a
  no-op `custom` pointing driver works too, so it is the pointing feature's split
  machinery, not the sensor, that matters.
- **The exact mechanism is still being narrowed** in `CLAUDE.md` (transaction count and
  gross memory layout are ruled out; it's down to the `split_shared_memory_t` `pointing`
  member shifting the RPC buffer offsets, vs merely linking `pointing_device.c`).

### ⚠️ Key learning — the earlier conclusion in this very file was WRONG

The original version of this doc concluded *"UNRESOLVED — every firmware avenue is
exhausted; the residual is the physical build of the split42 boards; pick up at the
bench scope."* **That was a wrong turn.** Nothing on the bench was needed; the cause was
a **disabled subsystem** in the firmware. Two lessons (both now in `CLAUDE.md`):

- **Suspect what's *absent/disabled*, not only what's present.** Reading the *enabled*
  code paths can never find a bug caused by a *missing* one. Diff a broken variant
  against a working one for **removed** config, and re-add it to test.
- **Don't upgrade a plausible hardware theory into a verdict.** "Copper-plane short",
  "cracked QFN joint", "physical build fault" were all floated for this and **none was
  real** — exactly the trap the `investigate-kicad-pcb` skill and the CLAUDE.md
  methodology note warn about. A layout/DC check that exonerates the design is a
  *result*; it is not a licence to invent a physical mechanism the instruments haven't
  shown.

---

## What was ruled out (all correct — the fault was in none of these)

These checks were sound; the mistake was the *conclusion drawn from them* (that the
residual must be hardware), not the checks themselves.

- **Firmware / serial config** — `diff`'d `halconf.h` / `mcuconf.h` / `config.h` split42
  vs split72: identical apart from the OLED/trackpad I2C bus. The `SERIAL_USART_*` /
  `SPLIT_TRANSACTION_IDS_USER` / `EE_HANDS` defines all live in the **shared** `config.h`;
  the vendor/PIO transport is the *same code* that runs split72.
- **Role / handshake** — forcing roles (`POLYKYBD_MASTER_LEFT`, `POLYKYBD_HIL=left/right`)
  still failed; `PIN_SWAP` is applied by `is_keyboard_master()`, same path both variants.
- **Baud** — failed at 230400, 115200, and 19200.
- **PIO vs bitbang** — failed on PIO full-duplex, PIO half-duplex, AND bitbang.
- **Cable** — the same bridge cable works on the split72 pair and for HID firmware
  transfer.
- **GP4↔GP5 short** — the phased loopback showed all four level combinations
  `00/01/10/11` ⇒ the two conductors are independent (a bridge would only show `00`/`11`).
- **Schematic + PCB layout** — via the `investigate-kicad-pcb` skill: the COM1/COM2 net
  is equivalent between variants (same passives, same
  `U10.6/7 (GP4/GP5) → U26 → USB2` path, right half byte-identical).

> The one thing the "ruled out" list got *right* and important: because every hardware
> avenue truly was equivalent to the working split72, the fault **had** to be in the
> firmware config difference — which is precisely what the rebuild-and-bisect then found.
> The list pointed at the answer; the old prose pointed away from it.

---

## Diagnostic build flags (bench toolkit — code on the bring-up branches)

These opt-in `-e` flags were the instruments used during the investigation. They are
**deliberately not in the shipped tree** (bring-up scaffolding, not features); they live
on the bring-up branches at the SHAs below, to cherry-pick back for the next hard
split-link bug. All OFF by default; the normal split42/split72 builds are untouched.

| Flag | Commit(s) | What it does / proves |
|------|-----------|-----------------------|
| `POLYKYBD_LINK_DIAG=yes` | `0f208611`, `460bf1b6` | Draws role glyph + TX/transport-fail/RX-frame counters on the top keycap row (no console needed); `update_displays()` early-returns under it. Adds `get_split_rx_frames()` + the link getters. **This is the readout that showed `S 0` / `transport_fail=100%`.** |
| `POLYKYBD_SERIAL_SPEED=N` | `c8b7f2b6` | Overrides `SELECT_SOFT_SERIAL_SPEED` for **any** driver (`=5` → 19200 on the proven vendor/PIO transport). |
| `POLYKYBD_SERIAL_BITBANG=yes\|slow` | `6a330469`, `72745bc0` | Swaps `SERIAL_DRIVER` to QMK's software bitbang on GP5 (no PIO). ⚠️ Weak evidence — QMK bitbang is essentially unexercised on RP2040. |
| `POLYKYBD_HALF_DUPLEX=yes` / `POLYKYBD_NO_PIN_SWAP=yes` | `d5b3f93c` | Single-wire half-duplex on GP5 / full-duplex without the firmware crossover. |
| `POLYKYBD_PIN_LOOPBACK=drive\|read` | `dcf0bae3`, `5f7b353f` | Two-board GPIO loopback; driver toggles GP4/GP5 at **different** rates so a GP4↔GP5 short is visible (`00/11` only) vs independent (`00/01/10/11`). |
| `POLYKYBD_NO_STATUS_OLED=yes` | `177ef5eb` | Disables the status OLED + its I2C1 bring-up entirely (removes I2C1 as a variable; clean cross-flash). |

The most reusable of these is the **phased GP4/GP5 loopback** — driving the two pins to
the *same* level can't distinguish two good independent conductors from a bridge (both
read equal), so phase them apart:

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

The PCB side of the toolkit is the **`investigate-kicad-pcb`** skill (now in the
`thpoll83/PolyKybd` hardware repo) with `kicad_net_diff.py` for by-name net/variant diffs.

---

## If a split-link bug recurs — the order that actually worked

1. **Cross-flash the other variant's firmware onto the suspect hardware first** (the
   O(1) firmware-vs-board isolation — see the CLAUDE.md "cheap cross-checks" note).
2. **Diff the broken variant's config/rules against the working one for what's
   *removed/disabled*** — re-add it (one subsystem per commit) and bisect.
3. Only reach for the scope/multimeter/kicad-tools once a cheap cross-check has
   *justified* a hardware hypothesis. Here, it never did.
