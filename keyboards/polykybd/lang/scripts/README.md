# lang/scripts/

Scripts that edit `lang/lang_lut.xlsx`, generate data for it, or download layout
sources. The workbook is the input of every cog block (`dev/scripts/run_cog.sh`).
Developers run these by hand, mostly while adding languages (the
`add-polykybd-language` skill). No build and no CI job calls them.

## Files

### Workbook editors

Each one rewrites one sheet XML inside the `.xlsx` zip and copies every other zip
entry byte for byte, so the cached formula results that cog reads stay intact.

- `_add_latin_variation.py`: appends a variation to a letter's row pair in the
  `latin_sup_ex` sheet (`sheet3.xml`). Writes both the character and the hex that
  its `=DEC2HEX(UNICODE(..))` cell would produce. `--dry-run` changes nothing;
  `--allow-asymmetric` accepts one case only.
- `_insert_settings_row.py`: inserts a `{category.name}` settings row into the
  `key_lut` sheet (`sheet2.xml`) and renumbers every row below, including the
  `<mergeCell>` and `<hyperlink>` refs after `<sheetData>`. Usage:
  `_insert_settings_row.py <xlsx> <{key.name}> <variation 0-3> <lang,lang,...> [value]`.
- `_set_cell.py`: sets one cell in `key_lut` (`sheet2.xml`). Usage:
  `_set_cell.py <xlsx> <row> <col1based> <value> [num|str]`.
- `_patch_xlsx.py`: appends one 4-column group per language after the last language
  in `key_lut`. Usage: `_patch_xlsx.py <xlsx> <spec.json> <lang,lang,...>`. The spec
  is `{lang: {row: [c0, c1, c2, c3]}}`, as the column generators write it.
- `_patch_named_glyphs.py`: appends `[name, hexcode]` rows after the last filled row
  of the `named_glyphs` sheet (`sheet1.xml`). Usage:
  `_patch_named_glyphs.py <xlsx> <named_glyphs_add.json>`.

### Generators

- `_gen_euam_cols.py`: the 2026-06 "Europe + Americas" batch (eu-ES, gl-ES, rm-CH,
  cy-GB, ga-IE, mt-MT, lb-LU, se-NO, gn-PY, qu-PE, ay-BO, nv-US, nh-MX). Reads
  `lang_lut.xlsx` from the current directory and writes `/tmp/euam_cols.json` and
  `/tmp/euam_named.json` for the two patch scripts.
- `_gen_world_cols.py`: the 2026-06 "world" batch (en-AU, mi-NZ, af-ZA, am-ET, ku-IQ
  and others). Same flow; writes `/tmp/world_cols.json` and `/tmp/world_named.json`.
- `_gen_region_tables.py`: prints the `REGION_OFFSET` / `REGION_LANGS` C block for
  `keys_com/lang_layer.c`, from the `key_lut` language order and the host's
  `lang_regions.py`. Paste the output into `lang_layer.c`.

### Downloads

- `dl-layouts.sh`: downloads CLDR release-42 keyboard XML files into `lang/layouts/`
  (today `chr-windows`, `chr-osx`, `iu-osx`). Add a layout as a
  `<locale>:<platform>` entry in `LAYOUTS`.

## Core concepts

- Flow for a batch of languages: a generator writes the JSON specs, `_patch_xlsx.py`
  and `_patch_named_glyphs.py` write them into the workbook, then
  `dev/scripts/run_cog.sh` regenerates the C sources.
- The edits are string operations on the sheet XML. `_add_latin_variation.py`
  parses its result before it replaces the workbook, so a bad edit fails.
- The scripts read the workbook with openpyxl (`data_only=True`) but never save it
  with openpyxl.
- Language codes are limited to 2+2 characters by the protocol. A code with no
  ISO-639-1 entry goes into `PRIVATE_LANGS` in `lang/iso_lang_country.py`, which is
  mirrored in the host and the rig.

## Important

- ⚠️ Never save `lang_lut.xlsx` with openpyxl. It drops every cached formula
  value, and the next `run_cog.sh` writes empty tables with no error.
- ⚠️ `_add_latin_variation.py` allows `MAX_SLOTS` (64) variations per row, the
  `LATIN_PICK_MAX` of the 6-bit pick field in `lib/system/state.h`. Keep both
  together. `LATIN_EX_VARIATIONS` in `lang/lang_lut_ext.h` (now 36) must match the
  widest row; the cog emits a `_Static_assert` for it.
- ⚠️ The `named_glyphs` rows must stay contiguous. The cog loop stops at the first
  blank row, so a row after a gap is not read.
- ⚠️ `_gen_region_tables.py` imports the host module from the fixed path
  `/home/user/PolyKybdHost/polyhost/services`. It needs a host checkout there.
- ⚠️ `iso_lang_country.py` is frozen and append-only, with byte-identical copies in
  the host and the rig. Change all three.

## Related

- [`../README.md`](../README.md)
- [`../../docs/lang/future-languages.md`](../../docs/lang/future-languages.md)
- [`../../docs/features/intl-layer.md`](../../docs/features/intl-layer.md)
- [`../../docs/display/legend-layout.md`](../../docs/display/legend-layout.md)
