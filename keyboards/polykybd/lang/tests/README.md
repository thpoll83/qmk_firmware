# lang/tests/

Host-side tests for the Shift-preview rule in `lang/shift_preview.py`. The test is not
in a `testlist.mk` or in CI. Run it by hand.

```bash
python3 -m unittest discover -s keyboards/polykybd/lang/tests -p "*_test.py"
```

The 6-bit Latin pick packing has its own unit test suite in CI:
`make test:polykybd_latin_bits` (`../../base/tests/latin_bits_tests.cpp`, testing
`../../base/latin_bits.h`).

## Files

- `shift_preview_test.py`: tests `lang/shift_preview.py`, which decides per language
  and key whether the small Shift preview on the unshifted keycap is redundant. The
  cog block in `lang/lang_lut.c` uses it to emit the `shift_preview_redundant` bitmap.
  `ResolveCellTest` pins how a cell resolves (named glyphs, literals, hex escapes,
  unknown names). `RedundancyTest` pins the rule: a case pair or identical glyphs
  are redundant; a different character, a caseless script, a multi-glyph legend, a
  missing cell or an unresolvable cell are not. `CodepointRuleTest` checks that the
  codepoint form agrees with the cell form.

## Core concepts

- `shift_preview.py` runs at build time (cog), where Python's Unicode case tables
  are available. The firmware only reads the generated bitmap.
- The Shift cell keeps the uppercase legend. The rule suppresses only the preview.

## Important

- ⚠️ Do not empty a Shift cell to hide a preview. The cell is also the legend drawn
  while Shift is held, and `translate_keycode()` then falls back to the lower case.
- ⚠️ The rule fails safe: a cell it cannot resolve keeps its preview.

## Related

- [`../README.md`](../README.md)
- [`../../docs/display/legend-layout.md`](../../docs/display/legend-layout.md)
- [`../../docs/features/intl-layer.md`](../../docs/features/intl-layer.md)
