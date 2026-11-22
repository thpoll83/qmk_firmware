# polymod_oled_i2c_diag/tests/

The unit-test suite `polymod_oled_i2c_diag`. It tests the decision part of the OLED I2C
write diagnostics (`../polymod_oled_i2c_diag_log.c`): which console line a failed write
prints, the rate limit, the stuck and recovered messages, and whether to retry.

## Files

- `oled_i2c_diag_tests.cpp`: 15 googletest cases in `OledI2cDiag`. `SetUp()` calls
  `oled_i2c_diag_init()`. `Ok()` reports a good write through `oled_i2c_diag_ok()`.
  `Fail()` builds an `oled_i2c_failure_t`, asks `oled_i2c_diag_should_retry()`, and
  reports it through `oled_i2c_diag_fail()`. Both return the printed text, which the
  cases compare (for example
  `oled_i2c: cmd write failed #1 (nack, ... 66 ms after the last good write) - retry ok`).
  The cases cover:
  - good writes print nothing, and a transient failure prints one detail line;
  - a failure before any good write says `no good write since boot`;
  - three lost writes in a row mark the display stuck once and stop the retries; a later
    good write announces the recovery and retries resume;
  - good command writes do not reset a data-write streak, and data writes are labelled;
  - detail lines are rate-limited and summarised; the stuck line is not rate-limited;
  - a small output buffer is truncated, not overrun; an uptime wrap keeps the gap.
- `testlist.mk`: adds `polymod_oled_i2c_diag` to `TEST_LIST`. Included from
  `builddefs/testlist.mk`.
- `rules.mk`: links `polymod_oled_i2c_diag_log.c` and the test. Included from
  `builddefs/build_test.mk`.

## Core concepts

- The log logic is pure. The caller passes the clock (`now_ms`) and the classified
  failure (`oled_i2c_class`, `oled_i2c_kind`).
- The firmware binding `polymod_oled_i2c_diag_hw.c` is not linked here.
- Run: `make test:polymod_oled_i2c_diag`. CI runs it in `polykybd-unit-test.yml`.

## Important

- ⚠️ Several cases compare the exact console text. A change to a message format needs
  the matching change here, and in any tool that reads these lines from a log.
- ⚠️ Judge a run by the `[  PASSED  ] 15 tests.` line, not by the exit code.

## Related

- [Status OLED](../../../../keyboards/polykybd/docs/display/status-oled.md)
- [Unit tests](../../../../keyboards/polykybd/docs/build/unit-tests.md)
- [Module README](../README.md)
