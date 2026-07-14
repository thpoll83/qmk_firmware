# split42 hardware bring-up — handoff notes

**Status: brought up and working.** The first physical split42 (42-key CRKBD
footprint) is up. The variant was rebuilt transparently from the working split72 —
one commit per step, every pin re-derived from the authoritative KiCad schematic —
and merged in **PR #143**; the confirmed full-subsystem working config is captured in
**PR #144**. The one bring-up bug that resisted the obvious theories (a dead inter-half
split link) turned out to be a **firmware** dependency, not a board fault — see §9 and
[`SPLIT_LINK_DIAGNOSTICS.md`](SPLIT_LINK_DIAGNOSTICS.md).

> **Living source of truth for the split-link investigation is the top-level
> `CLAUDE.md`** (§ "Troubleshooting principle: don't take shortcuts" and the split-link
> investigation notes). The split42 code config is still being narrowed there (the exact
> resting config + the remaining root-mechanism discrimination), so this file records the
> durable *hardware* facts and defers the evolving firmware detail to `CLAUDE.md`.

**Hardware source of truth:** `thpoll83/PolyKybd` (the KiCad repo), schematic
`poly_kybd/variations/poly_corne/poly_corne_split42_left.kicad_sch`. The MCU pins are
exposed as hierarchical labels GP0–GP29 from the `RpPico` sub-sheet
(`poly_kybd/rp_pico.kicad_sch`); the net each GPIO carries is defined on the parent
sheet above.

---

## 1. Definitive GPIO map (from the LEFT-half KiCad schematic)

| GPIO | Net (schematic) | Role | Firmware config | OK? |
|------|-----------------|------|-----------------|-----|
| GP0  | I2C_SDA | *intended* status-OLED SDA (I2C0) | shared `config.h` I2C0 | ⚠️ **not broken out to a pad on this rev** |
| GP1  | I2C_SCL | *intended* status-OLED SCL (I2C0) | shared `config.h` I2C0 | ⚠️ **not broken out on this rev** |
| GP2  | ENC_A | rotary encoder A | `keyboard.json` `pin_a` | ✅ |
| GP3  | ENC_B | rotary encoder B | `keyboard.json` `pin_b` | ✅ |
| GP4  | SERIAL_COM1 | **split UART RX** | shared `SERIAL_USART_RX_PIN` | ✅ |
| GP5  | SERIAL_COM2 | **split UART TX** | shared `SERIAL_USART_TX_PIN` | ✅ |
| GP6  | SPI SCK | keycap-display SPI clock | `SPI_SCK_PIN` | ✅ |
| GP7  | SPI MOSI | keycap-display SPI data | `SPI_MOSI_PIN` | ✅ |
| GP8  | D-C | keycap-display SPI D/C | `SPI_DC_PIN` | ✅ |
| GP9  | RESET | keycap-display HW reset | `HW_RST_PIN` | ✅ |
| GP10–15 | Col1–Col6 | key-matrix columns | `MATRIX_COL_PINS` | ✅ |
| GP16 | E2 | Exp0 header pad | (free) | — |
| GP17 | SPI CS | keycap-display SPI CS | `SPI_SS_PIN` | ✅ |
| GP18–21 | Row1–Row4 | key-matrix rows | `MATRIX_ROW_PINS` | ✅ |
| GP22 | E4 | Exp0 header pad → status-OLED SDA (current wiring) | see §4 | ⚠️ v2 workaround |
| GP23 | E3 | Exp0 header pad → status-OLED SCL (current wiring) | see §4 | ⚠️ open joint, §4 |
| GP25 | E0 | Exp0 header pad | (free) | — |
| GP26 | SR_DATA | shift-register data | `SR_DATA_PIN` | ✅ |
| GP27 | SR_CLOCK | shift-register clock | `SR_CLK_PIN` | ✅ |
| GP28 | SR_LATCH | shift-register latch | `SR_LATCH_PIN` | ✅ |
| GP29 | E1 | Exp0 header pad | (free) | — |

**Exp0 header (2×3, `Conn_02x03`):** E0=GP25, E1=GP29, E2=GP16, E3=GP23, E4=GP22.
Valid RP2040 hardware-I2C pairs on that header: **I2C1 = E4(GP22 SDA)+E3(GP23 SCL)**,
or I2C0 = E2(GP16 SDA)+E0(GP25 SCL)/E1(GP29 SCL).

**`SPI_MISO_PIN` is left at GP4** (mirrors split72). GP4 also carries `SERIAL_COM1`
(the split-serial RX) and there is no dedicated MISO net — the keycap SSD1306s are
**write-only**, so MISO is never read. This is deliberately unchanged from split72
(baseline-first). It is *not* the cause of the split-link bug in §9 — that was tracked
to a firmware dependency (`SPLIT_POINTING_ENABLE`), not SPI stealing the pin.

---

## 2. What the rebuild established (merged in #143)

split42 was reconstructed from split72 with one commit per step so every difference is
auditable (see `CLAUDE.md` § "Troubleshooting principle" for why this mechanical path
was the productive one). Merged facts:

- **Encoder** GP2/GP3 (ENC_A/ENC_B per schematic; GP25/GP29 were Exp-header pads).
- **Matrix** `MATRIX_COLS = 6`, rows GP18–21, cols GP10–15; `NUM_SHIFT_REGISTERS = 3`.
- **Status OLED** 128×32 (vs split72's 128×64); **PID `0x2008`**; layout `LAYOUT_lr_stacked42`.
- **`GET_ID` still reports `Split72`** — the per-variant name (`POLY_KB_NAME` /
  cmd-6 string) was **not** merged (`hid_com.c` hardcodes `"Split72"`), so the host
  currently shows the wrong board name for a split42. Cosmetic (PID `0x2008` already
  distinguishes the variant to the host); a follow-up can wire `POLY_KB_NAME`.
- **Keycap display map** — `split42.c key_display[]` is **6-wide** to match the 6-col
  matrix (split72's was 8-wide). Row 0 is hardware-verified; the thumb entries and the
  right-half fold are flagged TODO in `split42.c` (verify per §8).

---

## 3. Building & flashing

QMK pulls 5 deps as git submodules (`lib/chibios`, `lib/chibios-contrib`,
`lib/pico-sdk`, `lib/printf`, `lib/lufa`) — empty in a fresh clone. The ARM toolchain
builds this fork end-to-end (verified; see the top-level `CLAUDE.md` "Building &
flashing" for the container caveats, incl. the `codeload.github.com` tarball route when
the git proxy blocks the `qmk/*` submodules).

```bash
sudo apt-get install -y gcc-arm-none-eabi binutils-arm-none-eabi   # arm-none-eabi-gcc 13.2.x
make git-submodule                                                  # or per-SHA codeload (see CLAUDE.md)
qmk compile -kb polykybd/split42 -km default                        # or: make polykybd/split42:default
# raw image for HID flashing (the .bin is the deliverable, not the .uf2):
arm-none-eabi-objcopy -O binary .build/polykybd_split42_default.elf .build/polykybd_split42_default.bin
```

Flash **both halves** with the same image; the USB half becomes master (VBUS detection).

---

## 4. RESOLVED — status OLED dark on this rev: open SCL joint (deferred to next hardware rev)

**Root cause (2026-07-10): an open solder joint on the SCL line — the RP2040 GP23 pad
(U10.35) to the Exp0 E3 pad — so the status OLED never gets a clock and never ACKs.**
Not a firmware, config, module, or design problem. Chased end-to-end:

- **SDA (GP22 → E4) is fine.** A boot-time GPIO toggle (drive the pins as plain
  push-pull outputs) showed E4 swinging cleanly, but **E3 (SCL) stayed stuck at 3.3 V**
  even while GP23 was driving low at 1 Hz — a push-pull output beats any pull-up, so the
  drive was **not reaching E3**.
- **The 3.3 V came from the display, not the board:** unsoldering the display made it
  disappear → it was the module's own SCL pull-up holding an otherwise-floating pad high.
  Two different displays behaved identically; internal pull-ups + 100 kHz didn't help.
- **Not a short:** the netlist has E3 on exactly two pads — `U10` pad 35 (GP23) and
  `Exp0` pin 3 — with no connection to VDD/VBUS; removing the display killed the 3.3 V
  (a real short would persist).

**Decision: not fixing this hardware rev.** The next revision breaks out the board's
*intended* status-OLED bus — **I2C0 on GP0/GP1** — so the GP22/GP23 (I2C1) Exp0
workaround goes away. On the v2 board, drop the I2C1 override (split42 inherits the
shared `config.h` I2C0/GP0/GP1 defaults) and use `RP_I2C_USE_I2C0`. Until then the
status OLED is non-functional on this board; the **key displays and `qmk console` logs**
cover bring-up needs.

> The I2C1 status-OLED move (GP22/GP23) is **intentionally not in the shipped config** —
> it never worked on this rev (this open joint). It lived only on the bring-up branch.

---

## 5. Single-half quirk — "keystrokes only after a boot delay" (expected, not a fault)

At boot the master arms a **forced layer-resync** to push the default layer to the slave
(`g_force_layer_resync` in `keyboard_post_init_user`, spun in `sync_and_refresh_displays()`).
With no slave connected, each pass burns ~3× the bridge timeout until the budget is
exhausted, briefly stalling the USB/main loop → keystrokes appear after a delay, then
clear. Harmless once the slave half is present. (Optional hardening: gate the forced
push on `is_transport_connected()` — see the CLAUDE.md "HIL get current language" note.)

---

## 6. Stale hardcoded encoder pins in shared code (low priority)

`keyboard_post_init_user` (shared `poly_keymap.c`) still pokes
`gpio_set_pin_input_high(GP25/GP29)` under an `//encoder pins` comment — those are the
**split72** encoder pins. On split42 the encoder is GP2/GP3; GP25/GP29 are just Exp pads
there (harmless), but the poke is dead for split42. Since `poly_keymap.c` compiles for
both variants, a real fix needs a per-variant `POLY_ENC_*` macro rather than editing the
shared literal. The QMK encoder driver already uses the correct GP2/GP3 from
`keyboard.json`, so the encoder scans regardless — verify on hardware.

---

## 7. Keycap-display mapping — remaining hardware verification

`split42.c key_display[]` maps `LAYOUT_TO_INDEX(row,col)` → shift-register bitmask.
**Row 0 is verified** (matrix row 0 → `BITMASK1(0..5)` inverts the right displays).
Still to verify on the bench: rows 1–2, the thumb row (only 3 of 6 col slots are
wired), and the right-half fold in `invert_display()`. Fast procedure: press each key in
turn and note which display inverts; fill the row/col → BITMASK table. The KiCad
`poly_corne/shift_registers.kicad_sch` exposes 24 select nets `Out{1,2,3}_{1..8}`;
`BITMASK{n}(x)` drives `Out{n}_{x+1}` (byte order: `bitmask[2]`=first chip=`BITMASK1`,
`bitmask[0]`=last chip=`BITMASK3`; bit `x` → output `x`, MSB-first).

---

## 8. Verification checklist for a fresh split42 board

- [ ] `qmk compile -kb polykybd/split42 -km default` builds clean.
- [ ] `qmk compile -kb polykybd/split72 -km default` builds clean (shared code guard).
- [x] Key matrix scans; keycap-display shift-register chain drives the OLEDs.
- [x] Keycap-display **row 0** inverts on the correct key; **split link comes up** with
      the resting config (§9). Remaining: keycap rows 1–2 + thumb map (§7).
- [ ] Status OLED — non-functional this rev (§4); expected. Bitmap logos in
      `status_oled.c` are still placeholder zeros (cosmetic).

---

## 9. Split link — was dead, now identified (firmware dependency, not a board fault)

On the first physical split42 pair the inter-half UART was dead (master typed, slave
`S 0`, `transport_fail=100%`). It looked like a hardware problem — and multiple hardware
theories were floated — but the transparent split72→split42 rebuild + a per-subsystem
bisect showed it is a **split-link *establishment* failure in firmware**, fixed by
enabling **`SPLIT_POINTING_ENABLE`** (the split72 hardware always had the trackpad, which
hid the dependency). The trackpad hardware/I2C is *not* required — a no-op `custom`
pointing driver works too.

The full corrected record — every hypothesis that was ruled out, the localisation that
turned out to be a *wrong turn*, the bench toolkit, and the key learning that the
"physical build / bench-scope" conclusion was wrong — is in
[`SPLIT_LINK_DIAGNOSTICS.md`](SPLIT_LINK_DIAGNOSTICS.md). The authoritative, still-evolving
narrative (the exact resting config and the remaining `(b)` shmem-layout vs `(c)`
code-linkage discrimination) is in the top-level **`CLAUDE.md`**.
