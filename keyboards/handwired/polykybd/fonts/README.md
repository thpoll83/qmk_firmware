# Pixel-font generation

The per-keycap OLED fonts are generated from **`fonts.yaml`** — the single
source of truth — by **`generate_fonts.py`**, which drives the `fontconvert`
tool (from the AdafruitGFX repo) and writes the headers the firmware compiles.

This replaces the old `create_fonts.sh` (now a thin wrapper that just calls the
generator).

```
fonts.yaml ──► generate_fonts.py ──► base/fonts/generated/<category>_fonts.h
                    │ (runs fontconvert N times)        base/fonts/gfx_used_fonts.h
                    ▼
              fontconvert
```

## One-time setup

1. **Build `fontconvert`** — see `../../../../AdafruitGFX/fontconvert/README.md`.
   For byte-reproducible output use the **pinned** CMake build (FreeType 2.13.3 /
   HarfBuzz 2.6.7). The distro fast-path build renders a handful of glyphs ~1px
   differently; the committed headers are built with the pinned toolchain.
2. `pip install pyyaml`
3. Download the Noto source fonts: `./dl-fonts.sh`

## Generate

```bash
# fontconvert on PATH:
python3 generate_fonts.py
# or point at a specific binary:
FONTCONVERT=/path/to/fontconvert python3 generate_fonts.py
# CI / sanity: regenerate in memory and fail if the committed headers are stale:
python3 generate_fonts.py --check
```

Outputs (overwritten in place):

| File | Contents |
|------|----------|
| `base/fonts/generated/<cat>_fonts.h` | one header per category, holding that category's `GFXfont` definitions |
| `base/fonts/gfx_used_fonts.h` | `#include`s the category headers + builds the `ALL_FONTS[]` lookup table |
| `base/fonts/generated/fontpack.manifest.json` | structural manifest of the external-flash font pack (committed; the pack ABI contract — see below) |

The generator deletes any stale `generated/*.h` that the config no longer
produces, so the directory always reflects `fonts.yaml`.

## Font pack (external-flash, position-independent `PlyF`)

Most of the glyph data — emoji, symbols, CJK, Indic, Arabic, … (~400 KB, the
bulk of the firmware image) — does **not** need to live in the firmware. It can
be split into a **font pack** stored in the external-flash resource region and
updated independently over HID, so the firmware itself shrinks (~812 KB → ~352 KB
measured) and most updates ship a much smaller image. A "functional minimum" set
stays compiled in so the keyboard works with **no pack present**: `IconsFont`
(`index.prepend_fonts`) plus every category tagged `resident: true` (currently
`latin` — ASCII + Latin-extended + Cyrillic + Greek) plus the status-OLED fonts,
plus any UI-chrome fonts listed in `index.resident_fonts` (the NotoSansSymbols2
Arrows font — Tab / Enter / Undo / Redo / nav arrow-stops — even though its
`symbols` category is otherwise packed).

The pack is **position-independent**: every internal reference is a byte offset
from the pack base, so the same binary works from either A/B flash slot. The
on-flash format is defined once in [`base/fontpack.h`](../base/fontpack.h) (the C
contract) and produced/validated by [`fontpack.py`](fontpack.py) (the build-side
tool). It is built straight from the committed headers — no `fontconvert` or TTFs
needed.

```bash
# Build the binary pack + (re)write the committed manifest:
python3 generate_fonts.py --emit-pack /tmp/fontpack.bin --content-version 3
# Or, standalone (no fontconvert), straight from the committed headers:
python3 fontpack.py build --out /tmp/fontpack.bin --content-version 3

# Offline validator (magic / ABI / CRC / bounds, + manifest cross-check):
python3 fontpack.py validate /tmp/fontpack.bin \
    --manifest ../base/fonts/generated/fontpack.manifest.json
python3 fontpack.py dump /tmp/fontpack.bin      # list fonts + ranges
python3 fontpack.py selftest                    # round-trip self test (no inputs)
```

`generate_fonts.py --check` also verifies the committed manifest is consistent
with the headers (it is regenerated from the same in-memory font data). Mark a
category `resident: true` in `fonts.yaml` to keep it compiled into the firmware
instead of the pack. The firmware loader (a later step) refuses a pack whose
`abi_version` ≠ `FONTPACK_ABI_VERSION` and falls back to resident-only fonts.

## Editing `fonts.yaml`

- **`sources:`** — named font files (paths relative to this keyboard dir),
  referenced by each entry's `source:`.
- **`categories:`** — each maps to one generated header and carries `defaults:`
  (e.g. `source`, `size`, `weight`, `render_height`) inherited by its fonts.
- **`index:`** — the composed header: `extra_includes` (headers pulled in besides
  the category ones) and `prepend_fonts` (added to the front of `ALL_FONTS[]`,
  e.g. `IconsFont`).
- **`fonts:`** — the **ordered** list of fonts. Each entry picks a `category`
  (which supplies defaults), a `variant` (the `_Xxx_` token embedded in the C
  symbol), and either `ranges` (list of `[first, last]` codepoint pairs) or a
  HarfBuzz `sequence`. Any category default can be overridden per entry.
  `bits: 32` marks SMP ranges (codepoints > U+FFFF) so the real codepoint is
  written into the struct.

### ⚠️ Order is load-bearing

The `fonts:` list order **is** the `ALL_FONTS[]` priority. Glyph lookup scans
`ALL_FONTS` front-to-back, so when two entries' codepoint ranges overlap the
**first one wins**. Several overlapping ranges are split deliberately so a more
specific glyph shadows a broader one (see the `note:` fields). Categories only
decide which header a font's data lands in — they do **not** affect priority.
When adding or moving an entry, place it at the right position in the list, not
just under a convenient category.

### Adding a font

1. If it uses a new font file, add it under `sources:`.
2. Add an entry to `fonts:` at the correct priority position, with `category`,
   `variant`, `ranges`/`sequence`, and any overrides.
3. `python3 generate_fonts.py` and rebuild the firmware.
