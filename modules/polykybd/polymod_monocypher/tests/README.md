# polymod_monocypher/tests/

The unit-test suite `polymod_monocypher`. It runs known-answer tests against this
tree's build of the vendored Monocypher: the Ed25519 check that gates firmware signing
(FW-2) and the SHA-512 it uses. The aim is not to test Monocypher itself. The aim is
to catch a toolchain flag, a bad merge of the vendored files or an accidental edit on
the host, before a keyboard rejects (or accepts) every image.

## Files

- `monocypher_tests.cpp`: 7 googletest cases. `from_hex()` converts the published hex
  vectors.
  - `Ed25519KnownAnswerTest` (6 cases): RFC 8032 section 7.1 TEST 3 (2-byte message)
    and TEST 1 (empty message) verify with `crypto_ed25519_check()`. A flipped message
    bit, a flipped signature bit and the wrong public key return `-1`.
    `SignRoundTripsThroughCheck` signs with `crypto_ed25519_key_pair()` and
    `crypto_ed25519_sign()` and checks the result.
  - `Sha512KnownAnswerTest.Fips180AbcVector`: `crypto_sha512()` of `"abc"` against the
    FIPS 180-2 value.
- `testlist.mk`: adds `polymod_monocypher` to `TEST_LIST`. Included from
  `builddefs/testlist.mk`.
- `rules.mk`: links `monocypher.c`, `monocypher-ed25519.c` and the test. Included from
  `builddefs/build_test.mk`.

## Core concepts

- Pure C: no bus, no clock, no mocks.
- The vectors are published values from RFC 8032 and FIPS 180-2. They are not generated
  by this code.
- Run: `make test:polymod_monocypher`. CI runs it in `polykybd-unit-test.yml`.

## Important

- ⚠️ Do not replace a published vector with output from the code under test. Then the
  test can no longer find a broken build.
- ⚠️ Judge a run by the `[  PASSED  ] 7 tests.` line, not by the exit code.

## Related

- [Firmware signing](../../../../keyboards/polykybd/docs/fw-update/signing.md)
- [Unit tests](../../../../keyboards/polykybd/docs/build/unit-tests.md)
- [Module README](../README.md)
