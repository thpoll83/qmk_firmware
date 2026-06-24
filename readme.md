# PolyKybd Firmware (QMK fork)

This repository is a fork of [QMK](https://github.com/qmk/qmk_firmware) that hosts
the firmware for **PolyKybd** — a split mechanical keyboard with a small OLED
display under every keycap. The PolyKybd-specific firmware lives at
[`keyboards/polykybd/`](/keyboards/polykybd/); the rest of the tree is upstream QMK,
kept so the customisations can ride on QMK's build system, HID stack and ChibiOS
RP2040 port.

Project progress and hardware info:

* Hardware & build log: [github.com/thpoll83/PolyKybd](https://github.com/thpoll83/PolyKybd)
* Support the project: [ko-fi.com/polykb](https://ko-fi.com/polykb)

## What is PolyKybd?

PolyKybd runs on a **Raspberry Pi RP2040** with 8 MB of external QSPI flash. It is a
split keyboard (left + right halves over UART) with up to **72 per-keycap OLED
displays** (72×40 px monochrome) plus a 128×64 status OLED. A companion host
application, [PolyKybdHost](https://github.com/thpoll83/PolyKybdHost), tracks the
active window and pushes per-app overlays, language layouts and icons to the keycaps
over a custom 64-byte HID report protocol.

### Variants

Two hardware variants share one firmware (the shared logic lives in
`keyboards/polykybd/poly_keymap.c`):

* **`split72`** — 72-key, RGB matrix, Cirque trackpad, 128×64 status OLED (actively developed).
* **`split42`** — 42-key CRKBD-footprint variant (formerly `corne42`, renamed 2026-06).

## Building

The full keyboard documentation is in [`keyboards/polykybd/readme.md`](/keyboards/polykybd/readme.md).
In short, replace `<variant>` with `split72` or `split42`:

```bash
qmk compile -kb polykybd/<variant> -km default
# or
make polykybd/<variant>:default
```

The `.uf2` is for manual bootloader-drive recovery; the deliverable for flashing over
HID via PolyKybdHost is the raw `.bin`
(`arm-none-eabi-objcopy -O binary .build/polykybd_split72_default.elf out.bin`).

## Related repositories

| Repo | Role |
|------|------|
| [thpoll83/PolyKybd](https://github.com/thpoll83/PolyKybd) | Hardware design (PCB, case, schematics) |
| [thpoll83/PolyKybdHost](https://github.com/thpoll83/PolyKybdHost) | Host application — overlay/window tracking, firmware updater |
| [thpoll83/polykybd-ctnd](https://github.com/thpoll83/polykybd-ctnd) | Hardware-in-the-loop test & deploy station / CI runner |
| [thpoll83/polykybd-docs](https://github.com/thpoll83/polykybd-docs) | Project documentation site |

## Upstream QMK

PolyKybd is built on QMK, the [Quantum Mechanical Keyboard Firmware](https://github.com/qmk/qmk_firmware),
which is based on the [tmk\_keyboard firmware](https://github.com/tmk/tmk_keyboard).
QMK's documentation lives at [docs.qmk.fm](https://docs.qmk.fm), and QMK is developed
and maintained by Jack Humbert of OLKB with contributions from the community and
[Hasu](https://github.com/tmk). All upstream QMK keyboards and features remain in this
tree; only `keyboards/polykybd/` and a handful of supporting build hooks are
PolyKybd-specific.
