# polymod_crc32/tests/

The unit-test suite `polymod_crc32`. It checks that `crc32_large()` gives the same
result as a one-shot CRC-32 at the lengths where a chunking mistake would show. Run it
after any change to `../polymod_crc32.c`.

## Files

- `crc32_tests.cpp`: 4 googletest cases. `ref_crc32()` is an independent bitwise CRC-32
  (no table) used as the reference. `pattern()` fills a buffer with a fixed
  linear-congruential sequence.
  - `CheckValue`: `"123456789"` gives `0xCBF43926` from both `crc32_1byte()` and
    `crc32_large()`.
  - `EmptyIsSeed`: a zero length returns the seed unchanged.
  - `MatchesReferenceAcrossChunkBoundaries`: lengths 1, `0x7FFF`, `0x8000`, `0x8001`,
    `0xFFFF`, `0x10000`, `0x10001`, `0x18000`, 230000 and 300000 match the reference.
  - `ChainsLikeOneShot`: 70000 bytes, then 130000 bytes seeded with the first result,
    equal the CRC of all 200000 bytes.
- `testlist.mk`: adds `polymod_crc32` to `TEST_LIST`. Included from
  `builddefs/testlist.mk`.
- `rules.mk`: `polymod_crc32_SRC` (the module source and the test) and
  `polymod_crc32_INC`. Included from `builddefs/build_test.mk`.

## Core concepts

- Standalone suite: it links only `polymod_crc32.c` and the test. No mocks, no QMK
  headers.
- Run: `make test:polymod_crc32` from the repository root (see the unit-tests doc for
  the setup). CI runs it in `polykybd-unit-test.yml`, which reads the suite name from
  `testlist.mk`.

## Important

- ⚠️ Judge a run by the `[  PASSED  ] 4 tests.` line. `make test:<name>` with a name
  that is not in `TEST_LIST` exits 0 and prints nothing.
- ⚠️ The reference must stay independent of the code under test. Do not replace
  `ref_crc32()` with a call to `crc32_1byte()`.

## Related

- [Unit tests](../../../../keyboards/polykybd/docs/build/unit-tests.md)
- [Module README](../README.md)
