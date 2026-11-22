# fonts/generated/

Generated font headers and the JSON records that go with them. Every file here is
written by a generator. Do not edit any of them by hand: regenerate them. The
firmware includes the font headers through `fonts/gfx_used_fonts.h`.
`fontpack_layout.h` is included by `config.h`, `base/fontpack.h` and others, and
`lang_flag_glyph.h` by `keys_com/poly_keymap.c`. `fonts/fontpack.py`, the tools in
`dev/tools/` and the host app read the JSON files.

## Files

- `<category>_fonts.h`: one header per `categories:` entry in `fonts/fonts.yaml`,
  with that category's `GFXfont` definitions. Written by `fonts/generate_fonts.py`,
  which runs `fontconvert` once per font. The first comment line of each font block
  is the exact `fontconvert` command. The files: `latin_fonts.h`,
  `latin_big_fonts.h` (category `latinbig`), `hebrew_fonts.h`, `jp_fonts.h`,
  `kr_fonts.h`, `arabic_fonts.h`, `devanagari_fonts.h`, `bengali_fonts.h`,
  `telugu_fonts.h`, `tamil_fonts.h`, `thai_fonts.h`, `georgian_fonts.h`,
  `armenian_fonts.h`, `bopomofo_fonts.h`, `vietnamese_fonts.h`,
  `ethiopic_fonts.h`, `canadian_fonts.h`, `cherokee_fonts.h`, `tengwar_fonts.h`,
  `gscript_fonts.h`, `symbol_fonts.h` (category `symbols`), `emoji_fonts.h`,
  `emoji_fig_fonts.h`.
- `all_fonts_order.json`: the full font priority order, resident and pack. Written
  by `generate_fonts.py` (full run only). Read by `fonts/fontpack.py`,
  `dev/tools/check_glyph_coverage.py` and the host's `oled_preview.py`.
- `fontpack_render_settings.json`: the `fonts.yaml` render options of each font,
  keyed by global `ALL_FONTS` index. Written by `generate_fonts.py` (full run only).
  The host's font-pack edit dialog reads its mirrored copy.
- `fontpack.manifest.json`: the structural manifest of the font pack (ABI version,
  font count, total size, each font's index and range). Written by
  `generate_fonts.py`, or by `fontpack.py build --manifest <path>` from the committed
  headers.
- `fontpack_bundles.manifest.json`: the split-pack bundles: slot, size and version
  of each bundle in `fonts.yaml`'s `bundles.list`. Written by `generate_fonts.py`,
  and rebuilt by `.claude/skills/reship-fontpack-bundle/reship_bundles.py --apply`
  in a reship.
- `fontpack_layout.h`: the bundle flash layout (`FONTPACK_LAYOUT_VERSION`,
  `FONTPACK_REGION_SIZE`, the directory and the slot of each bundle). Written by
  `generate_fonts.py`.
- `lang_flag_glyph.h`: `LANG_FLAG_GLYPH[]`, the flag glyph index of each `LANG_*`
  value in `fonts/flag_fonts.h` (codepoint `0xE000` + index). Written by
  `fonts/gen-lang-fonts.sh`.
- `lang_flags.json`: the render record of the flag font (source, `fontconvert`
  options, `seq_first`, each flag's regional-indicator sequence). Written by
  `fonts/gen-lang-fonts.sh`. The host's font-pack editor reads its mirrored copy.

## Core concepts

- Regenerate everything from `fonts/`: `python3 generate_fonts.py`. Check without
  writing: `python3 generate_fonts.py --check`. One header only:
  `python3 generate_fonts.py --only <category>`. Flags: `./gen-lang-fonts.sh`.
- The `fonts:` list order in `fonts.yaml` is the `ALL_FONTS[]` priority. The first
  font whose range holds a codepoint draws it. A category only decides which header
  a font is written to.
- Every font is a header, but only `RESIDENT_FONTS[]` reaches the firmware image.
  The other fonts ship in the font-pack bundles (`.plyf`) and are dropped by the
  linker. Category `latin` is `resident: true`, and `fonts.yaml` lists a few more
  fonts in `index.resident_fonts`.
- `--only <category>` does not rewrite `all_fonts_order.json`,
  `fontpack_render_settings.json` or the manifests. A new pack font needs the full
  run.
- A change to a pack font changes its bundle's bytes. Reship the bundle with the
  `reship-fontpack-bundle` skill.

## Important

- ⚠️ Byte-reproducible headers need the pinned `fontconvert` build (FreeType
  2.13.3, HarfBuzz 2.6.7). Other builds draw some glyphs about 1 px differently.
- ⚠️ `generate_fonts.py --check` does not pass on a clean checkout (see
  `fonts/README.md`). Run it on the unmodified tree first, and after a full run
  restore every file you did not mean to change. A full run also deletes
  `lang_flag_glyph.h`: it removes every `*.h` here that it did not write itself.
  Restore the file or re-run `gen-lang-fonts.sh`.
- ⚠️ `fontpack_render_settings.json` and `lang_flags.json` are mirrored
  byte-identically in `PolyKybdHost/polyhost/res/fontpack/`. Copy them after a
  change and `cmp` them.
- ⚠️ Adding a resident font shifts the index of every pack font and forces a reship
  of every bundle. Extend `IconsFont` (`fonts/gfx_icons.h`) for a few glyphs, or
  append a pack font at the end of `fonts.yaml`.
- ⚠️ The qmk venv (`/root/.qmk_venv`) has no PyYAML. Run the generator with
  `/usr/bin/python3` or the host venv.

## Related

- [`../README.md`](../README.md)
- [`../../docs/display/font-pack.md`](../../docs/display/font-pack.md)
- [`../../docs/display/legend-rendering.md`](../../docs/display/legend-rendering.md)
- [`../../docs/display/glyph-script.md`](../../docs/display/glyph-script.md)
