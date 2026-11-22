# lang/tests/

Host-side tests for two pieces of the language code: the Shift-preview rule in
`lang/shift_preview.py` and the 6-bit Latin pick packing of `lib/system/state.h`.
Neither test is in a `testlist.mk` or in CI. Run them by hand.

```bash
python3 -m unittest discover -s keyboards/polykybd/lang/tests -p "*_test.py"
gcc -o /tmp/latin_pick_test keyboards/polykybd/lang/tests/latin_pick_test.c && /tmp/latin_pick_test
```

## Files

- `shift_preview_test.py`: tests `lang/shift_preview.py`, which decides per language
  and key whether the small Shift preview on the unshifted keycap is redundant. The
  cog block in `lang/lang_lut.c` uses it to emit the `shift_preview_redundant` bitmap.
  `ResolveCellTest` pins how a cell resolves (named glyphs, literals, hex escapes,
  unknown names). `RedundancyTest` pins the rule: a case pair or identical glyphs
  are redundant; a different character, a caseless script, a multi-glyph legend, a
  missing cell or an unresolvable cell are not. `CodepointRuleTest` checks that the
  codepoint form agrees with the cell form.
- `latin_pick_test.c`: a standalone C program. It copies `latin_pick_get()` and
  `latin_pick_set()` from `lib/system/state.h` and checks every field with every
  value, and that a write to one field leaves the others unchanged. It prints
  `all packing checks passed` and returns 0 on success.

## Core concepts

- `shift_preview.py` runs at build time (cog), where Python's Unicode case tables
  are available. The firmware only reads the generated bitmap.
- The Shift cell keeps the uppercase legend. The rule suppresses only the preview.
- The pick fields are `LATIN_PICK_BITS` (6) wide, so a field can cross a byte
  boundary. That boundary is where `latin_pick_test.c` looks for off-by-one errors.

## Important

- ⚠️ Do not empty a Shift cell to hide a preview. The cell is also the legend drawn
  while Shift is held, and `translate_keycode()` then falls back to the lower case.
- ⚠️ The rule fails safe: a cell it cannot resolve keeps its preview.
- ⚠️ `latin_pick_test.c` has its own copy of the accessors and constants. It still
  uses `LATIN_PICK_FIELDS = 26 * 2` (39 bytes), while `state.h` now has
  `LATIN_TARGETS * 2` = 76 fields (57 bytes). The test does not cover the 24
  punctuation fields. Update the copy when `state.h` changes.

## Related

- [`../README.md`](../README.md)
- [`../../docs/display/legend-layout.md`](../../docs/display/legend-layout.md)
- [`../../docs/features/intl-layer.md`](../../docs/features/intl-layer.md)
