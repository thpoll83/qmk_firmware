# polymod_os_actions/tests/

The unit-test suite `polymod_os_actions`. It runs the real emitter
(`emit_os_action()` in `../polymod_os_actions.c`) against a recording mock of the QMK
modifier and tap functions. It also tests the GNOME/KDE to Linux column fold in
`keyboards/polykybd/keys_com/poly_os.h`.

## Files

- `os_actions_test_shim.h`: replaces `quantum.h` when `OS_ACTIONS_UNIT_TEST` is
  defined. It declares `get_mods()`, `set_mods()`, `clear_mods()`, `register_mods()`,
  `unregister_mods()` and `tap_code()`, and defines the `MOD_L*` masks and the `KC_*`
  values the chord table uses. The values are QMK's real ones (HID usage IDs, `MOD_BIT`
  masks).
- `os_actions_tests.cpp`: 13 googletest cases in `OsActionsTest`. The mock appends each
  call to a log of `Event` values (`GET`, `SET`, `CLEAR`, `REG`, `UNREG`, `TAP`), and
  `emitted()` reads the chord back. The cases cover:
  - per-OS routing: `OSA_COPY` on macOS is Cmd+C, Unknown falls back to Ctrl; Redo on
    macOS is a multi-modifier chord; Search on Windows taps bare GUI; line-home on macOS
    is Cmd+Left;
  - an unbound cell, an out-of-range action and an unknown OS value;
  - `TheCountMatchesTheEnum`;
  - held user modifiers are cleared for the chord and restored after;
  - `poly_os_action_column()` folds GNOME and KDE to the Linux column and passes every
    other value through.
- `testlist.mk`: adds `polymod_os_actions` to `TEST_LIST`. Included from
  `builddefs/testlist.mk`.
- `rules.mk`: sets `-DOS_ACTIONS_UNIT_TEST` and puts `keyboards/polykybd` on the include
  path, for `poly_os.h` only. Included from `builddefs/build_test.mk`.

## Core concepts

- The suite pins the behaviour that broke before, not every table cell: column routing,
  the Unknown fallback, the no-op cells, both bounds and the modifier save and restore.
- `poly_os.h` is `stdint.h`-only by design, so the standalone build can include it.
- Run: `make test:polymod_os_actions`. CI runs it in `polykybd-unit-test.yml`.

## Important

- ⚠️ A chord that leaks the user's held Shift into Ctrl+C is the typical bug here.
  Keep `HeldUserModsAreClearedForTheChordAndRestoredAfter` passing.
- ⚠️ Without the GNOME/KDE fold, the action keys did nothing on a Linux desktop that the
  host had refined over cmd 29.
- ⚠️ A new `KC_*` in the chord table must be added to `os_actions_test_shim.h`, or the
  suite does not compile.
- ⚠️ Judge a run by the `[  PASSED  ] 13 tests.` line, not by the exit code.

## Related

- [Community modules](../../../../keyboards/polykybd/docs/build/community-modules.md)
- [Unit tests](../../../../keyboards/polykybd/docs/build/unit-tests.md)
- [Module README](../README.md)
