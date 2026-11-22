# features/hints/tests/

The host unit test for `../os_hints.c`. It runs in QMK's googletest harness with no
keyboard, no split state and no display. `builddefs/build_test.mk` includes
`test_rules.mk` from here, and the PolyKybd unit-test workflow runs every suite in
`testlist.mk`.

## Files

- `testlist.mk`: registers the suite `polykybd_os_hints` (`TEST_LIST +=`).
- `test_rules.mk`: the suite's sources (`os_hints.c`, `os_hints_reference.c`,
  `os_hints_tests.cpp`) and include paths (`features/hints`, `keyboards/polykybd`).
- `os_hints_tests.cpp`: the tests. `OsHintsExtraction.ShowsAHintForExactlyThePreExtractionChords`
  compares the live table with the reference over every (keycode, mods, OS) triple. The
  `OsHints.*` tests pin the rules: app shortcuts and F2/F5 have no hint, every shortcut
  hint is one icon glyph, `POLY_OS_AUTO_FLAG` changes nothing, left and right
  modifiers agree, an extra modifier never shows the narrower hint, no modifiers means
  no hint, an unknown OS behaves as the non-Apple default, and the mod-tap badges (15
  distinct combinations, both sides agree, GUI alone is not empty, each mark is
  positioned and complete).
- `os_hints_reference.c`: `os_hint_reference()`, the hint table as it was in
  `poly_keymap.c` before it moved to `features/hints/`. It was copied from
  `git show 4931d0d202^:keyboards/polykybd/poly_keymap.c`, with the two global-state
  reads turned into parameters.

## Core concepts

- Run it with `make test:polykybd_os_hints` (setup: `export QMK_HOME=$PWD` and
  `export PATH="/root/.qmk_venv/bin:$PATH"`, from the `qmk_firmware` root).
- The extraction test compares which chords have a hint, not the hint content. The icon
  restyle changed every hint's content on purpose. Which icon each chord shows is
  pinned separately by `kExpected`, keyed by the reference's old glyph.
- The reference's answer is masked by the app-shortcut rule (`is_app_chord`) and the
  F2/F5 rule (`is_app_key`) before the comparison, because those chords were moved to
  the host's app overlays on purpose.
- Hints are compared by content (`same_hint()`), never by pointer.

## Important

- ⚠️ `make test:<name>` with a name that is not in `TEST_LIST` exits 0 and prints
  nothing. The suite is `polykybd_os_hints`, not `os_hints`. Judge a run by the
  `[  PASSED  ] N tests.` line.
- ⚠️ Do not fix or reformat `os_hints_reference.c`, and do not move it toward the live
  table. It is only evidence while it is the old code. When a change alters a hint on
  purpose, update `kExpected`, or retire or regenerate the reference deliberately.
- ⚠️ The file must be called `test_rules.mk`, not `rules.mk`. `qmk
  ci-validate-keyboard-targets` treats every `keyboards/**/rules.mk` as a keyboard and
  fails the lint job.
- ⚠️ Use the 8-bit packed modifier bits (`MOD_BIT_RCTRL` = 0x10) for `mods_raw`, as
  `get_mods()` returns them. `MOD_RCTL` (0x11) is the 5-bit mod-tap form and never
  occurs in that byte.

## Related

- [`../../../docs/build/unit-tests.md`](../../../docs/build/unit-tests.md): the harness and the traps of a standalone suite
- [`../../../docs/display/hint-icons.md`](../../../docs/display/hint-icons.md)
