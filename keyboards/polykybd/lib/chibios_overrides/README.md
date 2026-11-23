# lib/chibios_overrides/

A replacement for ChibiOS-Contrib's RP2040 platform makefile. It builds the RP2040 USB
device driver from a patched copy in [`USBDv1/`](USBDv1/) instead of from the
`lib/chibios-contrib` submodule. `keyboards/polykybd/rules.mk` selects it with
`PLATFORM_MK`. No C code in the keyboard calls into this folder; ChibiOS's USB HAL
uses the driver.

## Files

- `platform.mk`: a copy of Contrib's `os/hal/ports/RP/RP2040/platform.mk`. The only
  change is the USBDv1 line, which includes `$(POLY_CHIBIOS_OVERRIDES)/USBDv1/driver.mk`
  instead of Contrib's. The I2C, PWM and ADC drivers still come from Contrib.

## Subfolders

- [`USBDv1/`](USBDv1/): the vendored RP2040 USB driver, with the reset/SETUP
  order fix and opt-in diagnostics.

## Core concepts

- The submodule `lib/chibios-contrib` is pinned to `qmk/ChibiOS-Contrib` and cannot
  have a local change. So the driver is copied here and patched.
- `platforms/chibios/platform.mk` uses `PLATFORM_MK` when that file exists and falls
  back to Contrib's own `platform.mk` when it does not. `rules.mk` therefore stops the
  build with `$(error ...)` if `lib/chibios_overrides/platform.mk` is missing.
- The fix is in the USB interrupt on both halves of both variants: every PolyKybd
  build uses this driver.
- There is no unit test. The fix was verified on the HIL rig with the
  `dev/tools/hil_probes/usb_reset_race.py` probe (results in
  `docs/build/upstream-patches.md`).

## Important

- ⚠️ This is a copy and does not follow the submodule. After every `lib/chibios-contrib`
  update, diff it against the new Contrib files and carry any upstream change across:
  `diff -ru lib/chibios-contrib/os/hal/ports/RP/LLD/USBDv1 keyboards/polykybd/lib/chibios_overrides/USBDv1`
  and `diff lib/chibios-contrib/os/hal/ports/RP/RP2040/platform.mk keyboards/polykybd/lib/chibios_overrides/platform.mk`.
- ⚠️ When Contrib or QMK takes the fix (the `USB_INTS_BUS_RESET` branch comes before
  the SETUP branch in Contrib's `hal_usb_lld.c`), delete this folder and the
  `PLATFORM_MK` block in `rules.mk`.

## Related

- [`docs/build/upstream-patches.md`](../../docs/build/upstream-patches.md) →
  *ChibiOS-Contrib RP2040 USB driver*: why the copy exists, the rig A/B results and
  how to remove it.
- [`docs/debugging/crash-diagnostics.md`](../../docs/debugging/crash-diagnostics.md):
  boot hangs and the USB IRQ priority rule.
