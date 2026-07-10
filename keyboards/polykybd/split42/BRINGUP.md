# split42 hardware bring-up — handoff notes

**Status: in progress.** First physical split42 board (LEFT half only) being brought
up. Firmware config is being corrected against the actual KiCad schematic.

**Hardware-verified so far (user, on the branch below):**
- ✅ **Key matrix** scans correctly.
- ✅ **Shift-register chain** (GP26 data / GP27 clock / GP28 latch) drives the keycap
  OLEDs.
- ✅ **Keycap-display FIRST row** inverts on the correct key press — i.e. matrix
  row 0 → `BITMASK1(0..5)` in `split42.c key_display[]` is correct. Rows 1–2 and the
  thumb row are **not yet verified** (only the first row was wired/tested).

Still open: the **status OLED** (128×32 I2C1) — see §4. ("Keystrokes only after a
boot delay" is expected single-half behaviour, §5.)

**Branch (qmk_firmware):** `claude/split42-oled-status-display-8uok79`.
⚠️ The branch `claude/split42-oled-status-display-ndjc6q` points at the **same
commit** (identical content) — either name is fine; `8uok79` is canonical here.
(The other three repos — PolyKybdHost / Adafruit-GFX-Library / polykybd-ctnd — are
**untouched** so far.) Continue on this same branch.

**Hardware source of truth:** `thpoll83/PolyKybd` (the KiCad repo), schematic
`poly_kybd/variations/poly_corne/poly_corne_split42_left.kicad_sch`. The MCU pins are
exposed as hierarchical labels GP0–GP29 from the `RpPico` sub-sheet
(`poly_kybd/rp_pico.kicad_sch`); the net each GPIO carries is defined on the parent
sheet above.

---

## 1. Definitive GPIO map (from the LEFT-half KiCad schematic)

| GPIO | Net (schematic) | Role | Firmware config | OK? |
|------|-----------------|------|-----------------|-----|
| GP0  | I2C_SDA | *intended* status-OLED SDA (I2C0) | shared `config.h` I2C1_SDA_PIN | ⚠️ **not broken out to a pad on this rev** |
| GP1  | I2C_SCL | *intended* status-OLED SCL (I2C0) | shared `config.h` I2C1_SCL_PIN | ⚠️ **not broken out on this rev** |
| GP2  | ENC_A | rotary encoder A | `keyboard.json` pin_a | ✅ fixed |
| GP3  | ENC_B | rotary encoder B | `keyboard.json` pin_b | ✅ fixed |
| GP4  | SERIAL_COM1 | split UART RX | shared `SERIAL_USART_RX_PIN` | ✅ |
| GP5  | SERIAL_COM2 | split UART TX | shared `SERIAL_USART_TX_PIN` | ✅ |
| GP6  | SPI SCK | keycap-display SPI clock | `SPI_SCK_PIN` | ✅ |
| GP7  | SPI MOSI | keycap-display SPI data | `SPI_MOSI_PIN` | ✅ |
| GP8  | D-C | keycap-display SPI D/C | `SPI_DC_PIN` | ✅ |
| GP9  | RESET | keycap-display HW reset | `HW_RST_PIN` | ✅ |
| GP10–15 | Col1–Col6 | key-matrix columns | `MATRIX_COL_PINS` | ✅ **already correct** |
| GP16 | E2 | Exp0 header pad | (free) | — |
| GP17 | SPI CS | keycap-display SPI CS | `SPI_SS_PIN` | ✅ |
| GP18–21 | Row1–Row4 | key-matrix rows | `MATRIX_ROW_PINS` | ✅ **already correct** |
| GP22 | E4 | Exp0 header pad → **status-OLED SDA (current wiring)** | split42 `post_config.h` I2C1_SDA_PIN | ✅ |
| GP23 | E3 | Exp0 header pad → **status-OLED SCL (current wiring)** | split42 `post_config.h` I2C1_SCL_PIN | ✅ |
| GP25 | E0 | Exp0 header pad | (free) | — |
| GP26 | SR_DATA | shift-register data | `SR_DATA_PIN` | ✅ |
| GP27 | SR_CLOCK | shift-register clock | `SR_CLK_PIN` | ✅ |
| GP28 | SR_LATCH | shift-register latch | `SR_LATCH_PIN` | ✅ |
| GP29 | E1 | Exp0 header pad | (free) | — |

**Exp0 header (2×3, `Conn_02x03`):** E0=GP25, E1=GP29, E2=GP16, E3=GP23, E4=GP22.
Valid RP2040 hardware-I2C pairs on that header: **I2C1 = E4(GP22 SDA)+E3(GP23 SCL)**
(the chosen one), or I2C0 = E2(GP16 SDA)+E0(GP25 SCL)/E1(GP29 SCL).

**Confirmed by the user with a multimeter:** OLED SDA→E4 (GP22), SCL→E3 (GP23), plus
VCC and GND. The display is a **generic 0.91" 128×32 SSD1306** (not the PolyKybd part).

---

## 2. Changes already made on this branch (all pushed)

| Commit | File(s) | What / why |
|--------|---------|-----------|
| `944ff49` | **`split42/post_config.h`** (new), `split42/mcuconf.h` | Status OLED moved to **I2C1, SDA=GP22, SCL=GP23**. Must live in `post_config.h` (not `config.h`) because QMK `-include`s `split42/config.h` **before** the shared `polykybd/config.h`, so a plain `#define` there is clobbered by the parent; post_config is processed last. `mcuconf.h` now `RP_I2C_USE_I2C1 TRUE` / `RP_I2C_USE_I2C0 FALSE`. Verified with the preprocessor: `I2C_DRIVER=I2CD1`, SCL=23, SDA=22. (`I2CD1↔RP_I2C_USE_I2C1↔hw i2c1` confirmed against QMK's `QMK_PM2040`/`GENERIC_PROMICRO_RP2040` board configs.) |
| `b788134` | `split42/keyboard.json` | Encoder pins corrected **GP25/GP29 → GP2/GP3** (ENC_A/ENC_B per schematic). GP25/GP29 were actually Exp-header pads. |
| `bb87e57` | `split42/post_config.h`, `split42/mcuconf.h` | `TODO(v2 hardware)` notes: when a v2 board breaks out GP0/GP1, delete `post_config.h` and revert mcuconf to `RP_I2C_USE_I2C0`. |
| `7a538f4` | `split42/config.h`, **`split72/config.h`** | `SPI_MISO_PIN` GP4 → **`NO_PIN`** on both variants (GP4 is the split-serial RX; keycap SSD1306s are write-only). `NO_PIN` not deletion — QMK defaults an undefined `SPI_MISO_PIN` to `B14` (bogus on RP2040). ⚠️ **split72 is shipping — this hunk needs a build + split-link sanity check before it goes in a split72 release**, or split it to its own branch. |

---

## 3. Building (session-dependent — a broader-rights session CAN build)

QMK pulls 5 deps as **git submodules** (`lib/chibios`, `lib/chibios-contrib`,
`lib/pico-sdk`, `lib/printf`, `lib/lufa`) — empty in a fresh clone.

- **Broader-rights session (verified 2026-07-08):** direct `git` access to
  `github.com/qmk/*` is permitted (the submodule dirs were also pre-populated by the
  setup script), the ARM toolchain (`arm-none-eabi-gcc` 13.2.1) and the `qmk` CLI
  (`/root/.qmk_venv/bin/qmk`) are installed, and **`qmk compile -kb polykybd/split42
  -km default` builds end-to-end** (`.uf2` + `.elf`, exit 0) — including with
  `-DOLED_I2C_SCAN`. So in such a session I build and produce the flashable `.bin`
  directly.
- **Locked-down web/cloud session:** the git proxy only serves the session's
  authorized repos (`thpoll83/*`), so every route to the `qmk/*` submodules can fail
  (`make git-submodule` → 403, `codeload.github.com` tarball → 403, `add_repo qmk/*`
  → refused). In that case the firmware can't be compiled there; build locally.

Build steps (either way):

```bash
sudo apt-get install -y gcc-arm-none-eabi binutils-arm-none-eabi   # arm-none-eabi-gcc 13.2.x
# submodules: either `make git-submodule`, or if only codeload is open, per pinned SHA:
#   git submodule status lib/chibios lib/chibios-contrib lib/pico-sdk lib/printf lib/lufa
#   curl -sSL https://codeload.github.com/qmk/<Repo>/tar.gz/<sha> | tar xz -C lib/<name> --strip-components=1
qmk compile -kb polykybd/split42 -km default        # or: make polykybd/split42:default
# raw image for HID flashing (the .bin is the deliverable, not the .uf2):
arm-none-eabi-objcopy -O binary .build/polykybd_split42_default.elf .build/polykybd_split42_default.bin
```

---

## 4. RESOLVED — status OLED dark on this rev: open SCL joint (deferred to next hardware rev)

**Root cause (2026-07-10): an open solder joint on the SCL line — the RP2040 GP23 pad
(U10.35) to the Exp0 E3 pad — so the status OLED never gets a clock and never ACKs.**
Not a firmware, config, module, or design problem. Chased end-to-end:

- **SDA (GP22 → E4) is fine.** A boot-time GPIO toggle test (drive the pins as plain
  push-pull outputs) showed E4 swinging cleanly, but **E3 (SCL) stayed stuck at 3.3 V**
  even while GP23 was driving low at 1 Hz — a push-pull output beats any pull-up, so the
  drive was **not reaching E3**.
- **The 3.3 V came from the display, not the board:** unsoldering the display made it
  disappear → it was the module's own SCL pull-up holding an otherwise-floating pad high.
  Two different displays behaved identically; internal pull-ups + 100 kHz didn't help.
- **Not a short:** the PCB netlist has E3 on exactly two pads — `U10` pad 35 (GP23) and
  `Exp0` pin 3 — with no connection to VDD/VBUS; and removing the display killed the 3.3 V
  (a real short would persist). On the QFN, GP23 (pad 35) sits 0.40 mm from GP24/VBUS and
  0.80 mm from VDD, so a bridge *would* live there — but the evidence points to an **open**,
  not a bridge.

**Decision: not fixing this hardware rev.** The next hardware revision will break out the
board's *intended* status-OLED bus — **I2C0 on GP0/GP1** — so the GP22/GP23 (I2C1) Exp0
workaround goes away entirely. Per the TODO in `split42/post_config.h`: on the v2 board,
**delete `post_config.h`** (split42 then inherits the shared `config.h` I2C0/GP0/GP1
defaults) and **revert `split42/mcuconf.h`** to `RP_I2C_USE_I2C0`. Until then the status
OLED is non-functional on this board; the **key displays and `qmk console` logs** cover
bring-up needs.

> The opt-in I2C bring-up diagnostics (`-DOLED_I2C_SCAN`, `-DOLED_I2C_PULLUP`,
> `-DOLED_I2C_GPIO_TEST`, `-DOLED_I2C_GPIO_TEST_SCL`) that were used to isolate this have
> been **removed** from the tree now that the cause is known. If needed again, the GPIO
> toggle (drive GP22/GP23 as push-pull outputs in `keyboard_post_init_user` and watch
> E4/E3 on a multimeter) is the quickest way to prove an open on either line.

---

## 5. OPEN ISSUE #2 — "keystrokes only after a while" (single half, expected)

Not a fault. At boot the master arms a **forced layer-resync** to push the default layer
to the slave (`g_force_layer_resync` / `g_force_resync_tries = FORCE_LAYER_RESYNC_TRIES`
in `keyboard_post_init_user`, spun in `sync_and_refresh_displays()` from housekeeping).
With no slave, each pass burns ~3× the bridge timeout (`PERIODIC_SYNC_RETRIES`) until the
budget is exhausted, briefly stalling USB/main-loop → keystrokes appear after a delay,
then clears. **Optional hardening for single-half bring-up:** gate the forced push on the
split transport actually being connected (`is_transport_connected()`) and clear the flag
regardless of ACK — see the CLAUDE.md "HIL get current language" note, same mechanism.
Leave alone once the slave half exists.

---

## 6. OPEN ISSUE #3 — stale hardcoded encoder pins in shared code

`keyboard_post_init_user` (shared `poly_keymap.c`) has hardcoded
`gpio_set_pin_input_high(GP25); gpio_set_pin_input_high(GP29);` under a `//encoder pins`
comment. Those are the **split72** encoder pins; on split42 the encoder is **GP2/GP3**
(this poke is harmless to the OLED — GP25/GP29 are just Exp pads on split42 — but it's
wrong/dead for split42's encoder). Since `poly_keymap.c` is compiled for **both** variants,
fixing it needs a per-variant guard (e.g. a `POLY_ENC_A_PIN`/`POLY_ENC_B_PIN` macro in each
variant header) rather than editing the shared literal. Low priority; the QMK encoder
driver already uses the correct GP2/GP3 from `keyboard.json`, so the encoder should scan
regardless — verify on hardware.

---

## 7. OPEN ISSUE #4 — split72 MISO change unbuilt

The `SPI_MISO_PIN NO_PIN` change (commit `7a538f4`) also touched **split72/config.h**
(shipping board). It's behavior-neutral (MISO never read; serial RX already works), but it
is **unbuilt/untested here**. Before any split72 release: build `polykybd/split72:default`
and do a quick split-link sanity check, or move that hunk to a dedicated branch/PR.

---

## 8. Verification checklist for the next session

- [ ] Build `polykybd/split42:default` cleanly (confirms the I2C1/GP22-GP23 + encoder +
      MISO config all compile).
- [ ] Build `polykybd/split72:default` cleanly (guards the shared MISO change).
- [ ] Flash split42 with the `-DOLED_I2C_SCAN` probe; read `qmk console` → determine
      0x3C / 0x3D / nothing.
- [ ] Apply the address fix if 0x3D; otherwise chase per §4.
- [ ] Confirm status OLED shows the layer/side/brightness/WPM/lang screen
      (`split42/status_oled.c` `oled_update_buffer`). Note the boot-splash logo bitmaps in
      that file are still placeholder zeros — cosmetic, separate task.
- [ ] (optional) single-half resync hardening (§5).
- [x] Keycap-display shift-register mapping, **row 0 verified** (`split42/split42.c`
      `key_display[]`): matrix row 0 → `BITMASK1(0..5)` inverts the right displays.
      Remaining: verify rows 1–2 and the thumb row (only 3 of 6 col slots wired), and
      the right-half `c--` question in `invert_display()`. Fast bench procedure: press
      each key in turn and note which display inverts; fill the row/col → BITMASK table.
      (The KiCad `poly_corne/shift_registers.kicad_sch` exposes 24 select nets
      `Out{1,2,3}_{1..8}`; `BITMASK{n}(x)` drives `Out{n}_{x+1}` — data byte order:
      `bitmask[2]`=first chip=`BITMASK1`, `bitmask[0]`=last chip=`BITMASK3`; bit `x` →
      output `x`, MSB-first. Row 0 matching `BITMASK1(0..5)` is consistent with that.)
