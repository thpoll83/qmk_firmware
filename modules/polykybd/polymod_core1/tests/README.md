# polymod_core1/tests/

The unit-test suite `polymod_core1_owner`. It tests the core1 owner
(`../polymod_core1_owner.c`) against a fake of the hardware seam, so each case can
check exactly what the chip would have seen.

## Files

- `core1_owner_tests.cpp`: 20 googletest cases in `Core1OwnerTest`. `FakeCore1`
  implements the seam functions declared in `polymod_core1_owner.h`:
  `core1_hw_lock()`, `core1_hw_unlock()`, `core1_hw_force_off()`, `core1_hw_release()`,
  `core1_hw_launch_service()`, `core1_hw_launch()` and `core1_hw_report()`. Hooks in the
  fake (`during_launch`, `after_launch`, and one after the next unlock) let a test make a
  hold or a release happen in the middle of a launch, to model another thread. The cases
  cover:
  - `core1_hold()` / `core1_release()` nesting: only the last release relaunches the
    service, and a release without a hold does nothing;
  - `core1_run()` refused while a hold is outstanding, and failed when a hold arrives
    during or after the launch;
  - `core1_restore_service()`: relaunch when nothing holds core1, no launch while held,
    stop retrying once a hold arrives, give up after three timeouts;
  - races: a release during a launch does not start a second handshake, and the last
    launch is the one left running.
- `testlist.mk`: adds `polymod_core1_owner` to `TEST_LIST`. Included from
  `builddefs/testlist.mk`.
- `rules.mk`: defines `polymod_core1_owner_SRC` (`polymod_core1_owner.c` and the test).
  Included from `builddefs/build_test.mk`.

## Core concepts

- The rule under test: core1 leaves PSM reset only when the last holder releases it,
  and nothing launches core1 while a hold is outstanding.
- `polymod_core1_owner.c` is pure. The RP2040 and ChibiOS calls are in
  `polymod_core1_owner_hw.c`, which is not linked here.
- Run: `make test:polymod_core1_owner`. CI runs it in `polykybd-unit-test.yml`.

## Important

- ⚠️ The suite name is `polymod_core1_owner`, not `polymod_core1`.
  `make test:polymod_core1` exits 0 and prints nothing. Judge a run by the
  `[  PASSED  ] 20 tests.` line.
- ⚠️ A new call from the owner into the hardware needs a `core1_hw_*` seam function and
  a fake for it here, or the suite does not link.

## Related

- [Firmware staging](../../../../keyboards/polykybd/docs/fw-update/fw-staging.md)
- [Unit tests](../../../../keyboards/polykybd/docs/build/unit-tests.md)
- [Module README](../README.md)
