# split42 hardware bring-up — handoff notes

split42 (42-key CRKBD footprint) was rebuilt from split72 one commit per step, every
pin re-derived from the KiCad schematic, and merged in **#143**; a confirmed working
config is in **#144**. The split-link investigation lives in the top-level **`CLAUDE.md`**
(§ "Troubleshooting principle" + the split-link notes) — this file is the hardware
reference and defers the firmware detail there.

**Hardware source of truth:** `thpoll83/PolyKybd` (KiCad), schematic
`poly_kybd/variations/poly_corne/poly_corne_split42_left.kicad_sch`.

---

## GPIO map (from the LEFT-half KiCad schematic)

| GPIO | Net | Role | Firmware config |
|------|-----|------|-----------------|
| GP0/GP1 | I2C_SDA/SCL | intended status-OLED bus (I2C0), not broken out to a pad on this rev | shared `config.h` I2C0 |
| GP2/GP3 | ENC_A/ENC_B | rotary encoder | `keyboard.json` `pin_a`/`pin_b` |
| GP4/GP5 | SERIAL_COM1/COM2 | split UART RX / TX | shared `SERIAL_USART_RX_PIN`/`_TX_PIN` |
| GP6/GP7 | SPI SCK / MOSI | keycap-display SPI | `SPI_SCK_PIN` / `SPI_MOSI_PIN` |
| GP8/GP9 | D-C / RESET | keycap-display SPI D/C, HW reset | `SPI_DC_PIN` / `HW_RST_PIN` |
| GP10–15 | Col1–Col6 | key-matrix columns | `MATRIX_COL_PINS` |
| GP17 | SPI CS | keycap-display SPI CS | `SPI_SS_PIN` |
| GP18–21 | Row1–Row4 | key-matrix rows | `MATRIX_ROW_PINS` |
| GP22/GP23 | E4/E3 (Exp0) | status-OLED SDA/SCL as wired on this rev (I2C1) | see status-OLED note |
| GP26/27/28 | SR_DATA/CLOCK/LATCH | shift-register control | `SR_DATA_PIN` / `SR_CLK_PIN` / `SR_LATCH_PIN` |
| GP16, GP25, GP29 | E2/E0/E1 (Exp0) | free header pads | — |

`SPI_MISO_PIN` is left at GP4 (mirrors split72); MISO is never read (the keycap
SSD1306s are write-only), so it shares GP4 with the split-serial RX.

---

## Merged config (from #143)

- Matrix `MATRIX_COLS = 6`, rows GP18–21, cols GP10–15; `NUM_SHIFT_REGISTERS = 3`.
- Encoder GP2/GP3; status OLED 128×32; PID `0x2008`; layout `LAYOUT_lr_stacked42`.
- No RGB matrix, no pointing hardware. The split link needs `SPLIT_POINTING_ENABLE`
  (see the split-link note below and `CLAUDE.md`).
- Keycap display map `split42.c key_display[]` is 6-wide (matches the 6-col matrix).

---

## Building & flashing

```bash
sudo apt-get install -y gcc-arm-none-eabi binutils-arm-none-eabi
make git-submodule                                    # or per-SHA codeload (see CLAUDE.md)
qmk compile -kb polykybd/split42 -km default
arm-none-eabi-objcopy -O binary .build/polykybd_split42_default.elf .build/polykybd_split42_default.bin
```

Flash **both halves** with the same image; the USB half becomes master.

---

## Status OLED

On this rev the status OLED is wired to the Exp0 header (GP22/GP23, I2C1); the intended
GP0/GP1 (I2C0) bus is not broken out to a pad. It does not display on this rev and is
not needed for bring-up — the keycap displays and `qmk console` cover it. Revisit on a
hardware rev that exposes GP0/GP1.

---

## Keycap display map — verification status

`split42.c key_display[]` maps `LAYOUT_TO_INDEX(row,col)` → shift-register bitmask.
Matrix **row 0 is verified** (inverts the correct displays). Rows 1–2, the thumb row
(only 3 of 6 col slots are wired), and the right-half fold in `invert_display()` are
not yet verified — press each key and note which display inverts to fill the table.

---

## Split link

The inter-half UART was initially dead on split42 (master typed; slave received nothing,
`transport_fail=100%`). The transparent rebuild + per-subsystem bisect (#143) established
that enabling **`SPLIT_POINTING_ENABLE`** brings the link up — the trackpad hardware is
not needed (a no-op `custom` driver suffices). The ongoing analysis of *why* is in
`CLAUDE.md`; the reusable bench toolkit is in
[`SPLIT_LINK_DIAGNOSTICS.md`](SPLIT_LINK_DIAGNOSTICS.md).
