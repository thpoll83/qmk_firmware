# polymod_cirque_gestures

A gesture layer for a Cirque Pinnacle trackpad in absolute mode, as a QMK community
module. It replaces the stock driver's `get_report` and decides tap, right click and
scroll itself, from the pad's real coordinates and with its own z threshold. The
stock driver still initialises the ASIC.

List the module in `keyboard.json` (or a keymap's `keymap.json`):

```json
{
    "modules": ["polykybd/polymod_cirque_gestures"]
}
```

It takes the pad over only when all of these hold:

- `POINTING_DEVICE_ENABLE`, with `cirque_pinnacle_i2c` or `cirque_pinnacle_spi` as
  the pointing device driver;
- `CIRQUE_PINNACLE_POSITION_MODE` is `CIRQUE_PINNACLE_ABSOLUTE_MODE` (the QMK
  default).

Then `polymod_cirque_gestures.h` defines `POLYMOD_CIRQUE_GESTURES_ACTIVE`. Otherwise the
adapter compiles to nothing and the stock driver keeps the pad. Code that calls
`poly_cirque_debug_snapshot()` tests the same macro. Do not
also enable the stock gesture options (`CIRQUE_PINNACLE_TAP_ENABLE`, circular
scroll): they would only compile code that nothing calls.

## Files

- `polymod_cirque_gesture_fsm.c` / `.h`: the decision. Pure: no `quantum.h`, no
  timer, no I2C. Samples and a millisecond clock go in; cursor deltas, wheel and
  buttons come out.
- `polymod_cirque_gestures.c` / `.h`: the adapter. It reads
  `cirque_pinnacle_read_data()`, feeds the decision, and installs its report
  function from the module's `pointing_device_init` hook. That hook runs after
  `driver->init()` and before `pointing_device_init_kb()`.
  `poly_cirque_debug_snapshot()` returns the live state for a status display.

## Fitting another pad

The geometry and the thresholds in `polymod_cirque_gesture_fsm.h` are `#ifndef`, so a
keyboard's `config.h` can override them. The defaults were measured on the PolyKybd
split72 pad (35 mm, mounted skewed). For another mounting, measure the four corners
in the sensor frame and re-fit the six affine terms `POLY_GEST_PX_A` …
`POLY_GEST_PY_C` (Q12 fixed point); define all six together.

## Tests

`make test:polymod_cirque_gestures` replays taps, drags and dials built from measured
pad numbers against the decision code (46 tests).

## Subfolders

- [`tests/`](tests/): the host unit tests of the gesture decision.
