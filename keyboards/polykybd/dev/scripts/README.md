# dev/scripts/

Shell wrappers for the two code generators of the firmware. Both scripts change to
`keyboards/polykybd/` first, so they work from any directory. Developers run them by
hand after editing `lang/lang_lut.xlsx` or `fonts/fonts.yaml`. No CI workflow calls
them.

## Files

- `run_cog.sh`: runs `cog -r` on every file with a cog block: `lang/lang_lut.c`,
  `lang/lang_lut.h`, `lang/named_glyphs.h`, `keys_com/poly_keymap.c`,
  `keys_com/keycode_helper.h`, `keys_com/uni.h` and `lib/hid/hid_com.c`. The cog
  blocks read `lang/lang_lut.xlsx`. Needs `cogapp` and `openpyxl`.
- `create_fonts.sh`: deprecated. It prints a note and runs
  `fonts/generate_fonts.py` with the same arguments (for example `--check` or
  `--fontconvert PATH`).

## Core concepts

- `run_cog.sh` is idempotent. A clean run on an unchanged workbook leaves the tree
  unchanged, so any diff after a run means the workbook and the committed sources
  have drifted.
- The cog blocks read the workbook with `load_workbook(..., data_only=True)`, which
  returns the cached formula results stored in the `.xlsx`.
- Run `run_cog.sh` (or at least `cog -r lib/hid/hid_com.c`) after a change to the
  language list or to `lang/iso_lang_country.py`.
- Font generation is driven by `fonts/fonts.yaml`. `generate_fonts.py` needs PyYAML
  and a `fontconvert` binary on `PATH` (or in `$FONTCONVERT`).

## Important

- ⚠️ Never save `lang_lut.xlsx` with openpyxl. It does not evaluate formulas, so the
  save drops every cached value, and the next `run_cog.sh` writes empty tables with
  no error. Use Excel, LibreOffice or the scripts in `lang/scripts/`.
- ⚠️ `run_cog.sh` deletes any macro in `lang/named_glyphs.h` that the `named_glyphs`
  sheet does not have. Add the row to the sheet, not the macro to the header.
- ⚠️ Byte-reproducible font headers need the pinned `fontconvert` build (FreeType
  2.13.3, HarfBuzz 2.6.7).
- ⚠️ The qmk venv (`/root/.qmk_venv`) has no PyYAML. Run `create_fonts.sh` or
  `generate_fonts.py` with `/usr/bin/python3` or the host venv.

## Related

- [`../../lang/README.md`](../../lang/README.md)
- [`../../fonts/README.md`](../../fonts/README.md)
- [`../../docs/display/font-pack.md`](../../docs/display/font-pack.md)
