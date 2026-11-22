# dev/tools/tests/

Offline Python `unittest` suites for the keyboard's Python tools and generators.
They need no keyboard and no firmware build. CI runs them in the "Run PolyKybd tool
tests (Python)" step of `.github/workflows/polykybd-unit-test.yml`.

```bash
python3 -m unittest discover -v -s keyboards/polykybd/dev/tools/tests -p '*_test.py'
```

## Files

- `check_glyph_coverage_test.py`: tests `../check_glyph_coverage.py`. Pins escape
  decoding, named-glyph expansion, the display-list op walk (op arguments are not
  glyphs, glyph arguments are resolved), a comma inside a legend, and that a 0x0 gap
  record is not coverage. `RealTreeTest` then runs the check on the committed
  `lang/lang_lut.c` and fonts.
- `emoji_dict_test.py`: tests `features/emoji/emoji_dict.py`. Builds the committed
  `en` and `de` sources and checks the bundle layout, the 128 KB slot limit,
  `MAX_CPS` against `EMJDICT_MAX_CPS` in `base/emoji_dict.h`, the fold and
  romanisation rules, and search results for typical queries.
- `fontpack_layout_test.py`: tests `compute_bundle_layout()` in `fonts/fontpack.py`.
  Pins the layout v3 slots, the `emojidict` slot inside the `emoji` reserve, and
  that an overlap or a reserve smaller than its slot is refused. Skipped when PyYAML
  is not installed.
- `make_hand_uf2_test.py`: tests `../make_hand_uf2.py` against the RP2040 bootrom's
  acceptance rules, written out again here (`bootrom_accepts()`). Pins
  `payloadSize` 256, the stamp record the firmware reads, and the target sector.
- `prc_tool_test.py`: tests `modules/polykybd/polymod_prc/tools/prc_tool.py`.
  Encodes the images of the golden vectors in the module's `tests/prc_vectors.h`
  and requires the same payload bytes that the host's encoder produced.

## Core concepts

- The suites import the tool under test by path, so they run from the repository
  root without installing anything except the tool's own dependencies.
- CI fails the step when the run reports zero tests or no `OK` line.
- These suites test Python only. The googletest suites for the C code run as
  `make test:<name>`, listed in the `testlist.mk` files (for example
  `base/tests/testlist.mk`).

## Important

- ⚠️ The CI discover is scoped to this folder, not the whole repository. A suite
  added elsewhere (for example in `lang/tests/`) does not run in CI.
- ⚠️ A round-trip check against the tool's own reader proves only that the tool is
  consistent with itself. Check a generated artifact against the rules of the
  component that consumes it, as `make_hand_uf2_test.py` does.

## Related

- [`../../../docs/build/ci-checks.md`](../../../docs/build/ci-checks.md)
- [`../../../docs/build/unit-tests.md`](../../../docs/build/unit-tests.md)
- [`../../../docs/display/font-pack.md`](../../../docs/display/font-pack.md)
- [`../../../docs/features/emoji-search.md`](../../../docs/features/emoji-search.md)
