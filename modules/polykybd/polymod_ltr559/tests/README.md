# polymod_ltr559/tests/

The unit-test suite `polymod_ltr559`. It runs the real driver (`../polymod_ltr559.c`)
against a mock LTR-559 on a mock QMK I2C bus and the QMK test-platform timer. It covers
the parts that broke in the field or that a refactor could change.

## Files

- `ltr559_mock_i2c.cpp` / `ltr559_mock_i2c.hpp`: `LtrMockI2C`, a singleton that models
  the part as a 256-byte auto-incrementing register file at address `0x23`. It implements
  the `i2c_master.h` functions the driver calls. Test controls: `reset()` (healthy part,
  `PART_ID` `0x92`, `MANUFAC_ID` `0x05`), `set_present()`, `set_reads_fail()`,
  `set_failing_write_reg()`, `set_ids()`, `set_als()` and `set_status()`. `set_als()`
  lays out `0x88..0x8B` as the part does: CH1 first, each channel little-endian.
- `ltr559_tests.cpp`: 19 googletest cases in `Ltr559Test`. `SetUp()` resets the mock,
  calls `ltr559_reset_for_test()` and sets the clock to 0. `FeedAls()` delivers one ALS
  sample through a full poll cycle. The cases cover the probe (part and manufacturer ID,
  configuration writes), the bounded and throttled retry, the poll rate, the ALS byte
  order, the piecewise lux fit, the growing-then-rolling average, the invalid-sample
  flag, the 11-bit proximity value and its saturation flag, and a transient bus error.
- `testlist.mk`: adds `polymod_ltr559` to `TEST_LIST`. Included from
  `builddefs/testlist.mk`.
- `rules.mk`: sets `-DLTR559_UNIT_TEST` and links the driver, the mock, the test,
  `$(PLATFORM_PATH)/timer.c` and `$(PLATFORM_PATH)/test/timer.c`. Included from
  `builddefs/build_test.mk`.

## Core concepts

- `LTR559_UNIT_TEST` replaces the community-module hooks (which need the generated
  `community_modules.h`) with `ltr559_reset_for_test()`. The tests call `ltr559_init()`
  and `ltr559_task()` directly.
- The clock is driven by `set_time()` and `advance_time()` from
  `platforms/test/timer.c`. They have no header, so the test file declares them.
- The constants at the top of `ltr559_tests.cpp` (`kPollMs` 100, `kRetryMs` 1000,
  `kMaxRetries` 30, `kAvgSamples` 50, register addresses) are copies of the driver's
  values on purpose. A change in the driver must make a test fail.
- Run: `make test:polymod_ltr559` (about 1 s). CI runs it in `polykybd-unit-test.yml`.

## Important

- ⚠️ Keep the mock a register file with auto-increment. The driver reads 4 bytes from
  `0x88` and 2 from `0x8D` in one transfer, so a per-register mock would not catch an
  endianness or offset mistake.
- ⚠️ Judge a run by the `[  PASSED  ] 19 tests.` line, not by the exit code.

## Related

- [Community modules and the LTR-559 sensor](../../../../keyboards/polykybd/docs/build/community-modules.md)
- [Unit tests](../../../../keyboards/polykybd/docs/build/unit-tests.md)
- [Module README](../README.md)
