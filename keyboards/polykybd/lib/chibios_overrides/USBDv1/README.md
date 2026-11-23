# lib/chibios_overrides/USBDv1/

A copy of ChibiOS-Contrib's RP2040 USB device driver (`os/hal/ports/RP/LLD/USBDv1`, taken
at Contrib commit `5a9ad82b`), with one fix and opt-in diagnostics.
[`../platform.mk`](../platform.mk) includes `driver.mk` from here, so every PolyKybd build
compiles this driver instead of Contrib's. ChibiOS's USB HAL (`hal_usb.c`) and QMK's USB
stack call it through the `usb_lld_*` functions. The diagnostics are read by
`dev/diag/usb_stress.c`.

## Files

- `hal_usb_lld.c`: the driver. The USB interrupt (`OSAL_IRQ_HANDLER(RP_USBCTRL_IRQ_HANDLER)`)
  and `usb_lld_init()`, `usb_lld_start()`, `usb_lld_reset()`, `usb_lld_start_in()` /
  `usb_lld_start_out()`, the stall and clear functions. The PolyKybd change: the
  interrupt handles `USB_INTS_BUS_RESET` before `USB_INTS_SETUP_REQ` (ChibiOS trunk
  r17878). The `POLYKYBD_USB_RACE_DIAG` blocks add the counters and the event log.
- `hal_usb_lld.h`: the driver's types and configuration macros. Must be identical to
  Contrib's file.
- `rp2040_usb.h`: the RP2040 USB register layout. Must be identical to Contrib's file.
- `driver.mk`: adds `hal_usb_lld.c` to `PLATFORMSRC` and this folder to `PLATFORMINC`.
  It differs from Contrib's only in the `$(POLY_CHIBIOS_OVERRIDES)` paths.
- `poly_usb_diag.h`: PolyKybd only. `poly_usb_diag_t` counts bus resets, SETUPs, passes
  that saw both, and EP0 stalls. `poly_usb_ev[]` keeps the last `POLY_USB_EV_MAX` (48) ISR
  passes that touched EP0, and stops `POLY_USB_EV_AFTER_RACE` (16) passes after the first
  pass with a reset and a SETUP together. `poly_usb_diag_reset_first` says which order
  the image uses.

## Core concepts

- The bug: when a bus reset and the SETUP after it are both pending in one interrupt
  pass, Contrib's order arms the SETUP reply first. `_usb_reset()` then rewinds the EP0
  state machine, and EP0 is STALLed. Both flags are pending together only when the
  interrupt was masked for 10–20 ms or more, which a flash erase does.
- `-e POLYKYBD_USB_STRESS=yes` sets `-DPOLYKYBD_USB_RACE_DIAG` and compiles the
  diagnostics. A normal build contains none of them.
- `-e POLYKYBD_USB_LEGACY_RESET_ORDER=yes` (only together with `POLYKYBD_USB_STRESS`)
  restores Contrib's original order, for a rig A/B test. Never ship it.
- The ISR is the only writer of `poly_usb_diag` and `poly_usb_ev[]`. `usb_stress.c`
  reads them and prints the `usbdiag:` lines.
- There is no unit test. The rig probe `dev/tools/hil_probes/usb_reset_race.py` checks
  the fix with a stress build.

## Important

- ⚠️ `hal_usb_lld.h` and `rp2040_usb.h` must stay identical to Contrib's files. The
  include path resolves to this folder, so a stale header compiles against newer Contrib
  code with no error.
- ⚠️ After every `lib/chibios-contrib` update, run
  `diff -ru lib/chibios-contrib/os/hal/ports/RP/LLD/USBDv1 keyboards/polykybd/lib/chibios_overrides/USBDv1`.
  The only expected differences are the reordered reset/SETUP block, the
  `POLYKYBD_USB_RACE_DIAG` hunks, the `driver.mk` paths and `poly_usb_diag.h`.
- ⚠️ The Linux kernel retries a STALLed descriptor request without a log line. Count the
  device-side EP0 stalls (`ep0_stalls`) instead of reading `dmesg`. 6 per boot is normal
  (three refused DEVICE_QUALIFIER requests).

## Related

- [`docs/build/upstream-patches.md`](../../../docs/build/upstream-patches.md) →
  *ChibiOS-Contrib RP2040 USB driver*: the fix, the verification runs and how to drop
  the copy.
- [`docs/debugging/crash-diagnostics.md`](../../../docs/debugging/crash-diagnostics.md):
  the boot hang caused by an IRQ that preempted USB.
