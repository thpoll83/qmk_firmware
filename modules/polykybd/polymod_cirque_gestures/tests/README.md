# polymod_cirque_gestures/tests/

The unit-test suite `polymod_cirque_gestures`. It feeds sample sequences into the
gesture decision (`poly_gest_feed()` in `../polymod_cirque_gesture_fsm.c`) and checks
the clicks, scroll and cursor motion that come out. No trackpad and no keyboard are
needed.

## Files

- `cirque_gesture_tests.cpp`: 46 googletest cases in `CirqueGesture`. The `Runner`
  struct holds a `poly_gest_t`, a clock, and totals of buttons, wheel and cursor motion.
  The input numbers are measured on the real pad: the four corners in sensor frame
  (`RT_X`/`RT_Y`, `RB_X`/`RB_Y`, `LB_X`/`LB_Y`, `LT_X`/`LT_Y`) and the z profile at 4X
  gain. The cases cover:
  - taps: a light tap clicks, held touches and drags do not, the z bar rejects contact
    that is too light, and taps survive position scatter and a slow poll rate;
  - the corner right-click and the wedge that arms the scroll dial;
  - the dial: continuous scroll in each direction, the seam crossing, radial wobble, and
    a radial drag that aborts it;
  - cursor motion: speed scaling, acceleration and its cap, tremor, impossible samples
    dropped or capped, lift-off wander;
  - helpers: `Median3ReturnsTheMiddle`, `IsqrtIsAnActualSquareRoot`,
    `PadCornersMapToPadCorners` (`poly_gest_to_pad()`).
- `testlist.mk`: adds `polymod_cirque_gestures` to `TEST_LIST`. Included from
  `builddefs/testlist.mk`.
- `rules.mk`: links only `polymod_cirque_gesture_fsm.c` and the test. Included from
  `builddefs/build_test.mk`.

## Core concepts

- The FSM is pure: no `quantum.h`, no timer, no I2C. The caller passes each sample and
  its time. That is why the suite needs no mocks.
- `polymod_cirque_gestures.c` (the sensor side) is not linked here.
- Run: `make test:polymod_cirque_gestures`. CI runs it in `polykybd-unit-test.yml`.

## Important

- ⚠️ Keep the input values measured, not invented. Three "tapping does not work" reports
  had three different causes (a touchdown magic coordinate, path length used as
  displacement, a z bar above the tap distribution). Each one cost a hardware round.
- ⚠️ Judge a run by the `[  PASSED  ] 46 tests.` line, not by the exit code.

## Related

- [Trackpad](../../../../keyboards/polykybd/docs/features/trackpad.md)
- [Unit tests](../../../../keyboards/polykybd/docs/build/unit-tests.md)
- [Module README](../README.md)
