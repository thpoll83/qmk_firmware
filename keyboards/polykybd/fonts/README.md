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
instead of the pack. The firmware loader (`base/fontpack.c`, called from
`keyboard_pre_init_user`) validates the pack at boot (magic + `abi_version` +
CRC32 + bounds) and assembles `g_all_fonts = RESIDENT_FONTS ++ pack`; a missing,
erased, corrupt or ABI-mismatched pack falls back to resident-only fonts. The
full font priority order (resident + pack) is committed in
`generated/all_fonts_order.json` for the build tooling, since the firmware index
(`gfx_used_fonts.h`) now compiles only `RESIDENT_FONTS[]`.

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

## Font generation

Fonts for the per-keycap OLEDs are generated using the `fontconvert` tool from the [`AdafruitGFX/`](../../../AdafruitGFX/CLAUDE.md) repo. Generation is **config-driven** via `keyboards/polykybd/fonts/` — full docs in [`fonts/README.md`](README.md).

- **`fonts/fonts.yaml`** — single source of truth: an ordered list of font entries (font file, size, variant, codepoint ranges, weight, bits, …) grouped into categories with shared defaults. The list order **is** the `ALL_FONTS[]` priority (front-to-back lookup; first match wins on overlapping ranges) — categories only decide which header a font lands in.
- **`fonts/generate_fonts.py`** — reads the YAML, runs `fontconvert` per entry, writes one header per category to `base/fonts/generated/`, and composes `base/fonts/gfx_used_fonts.h` (the `ALL_FONTS[]` table, with `IconsFont` prepended). `--check` flags stale headers for CI. Needs PyYAML + `fontconvert` on PATH (or `$FONTCONVERT`). It also emits **`base/fonts/generated/fontpack_render_settings.json`** — a `global ALL_FONTS index → fonts.yaml render options` map (the `RENDER_SETTINGS` output, via `render_settings()`; sequence-mode entries also get `composite` + `seq_first` derived from their `-C`/`-F` extra_args, so the host editor can rebuild matra/combining-mark glyphs without guessing). This is mirrored byte-identically in the host at `PolyKybdHost/polyhost/res/fontpack/fontpack_render_settings.json`, where the font-pack **edit** dialog reads it to pre-fill the controls a glyph was generated with (the `.plyf` itself carries no render options). Keep both in sync (`cmp`); `--check` enforces it stays consistent with the headers.
- **`fonts/dl-fonts.sh`** — downloads the Noto source fonts first. The font list
  (url + dest) lives in **`fonts/noto-fonts.yaml`** (single source of truth); the
  script just parses it (PyYAML) and fetches each entry. ⚠️ `noto-fonts.yaml` is
  mirrored **byte-identically** in the host repo at
  `PolyKybdHost/polyhost/res/fonts/noto-fonts.yaml` (its "Download Noto…" button in
  the font-pack extend dialog reads the same catalog) — keep both in sync (`cmp`).
- `create_fonts.sh` is now a thin deprecated wrapper that forwards to `generate_fonts.py`.
- **`fonts/gen-lang-fonts.sh`** — generates `base/fonts/flag_fonts.h` for the language-selection layer (`_LL`): country flags from NotoColorEmoji, one per `LANG_*` at codepoint `0xE000 + enum index`, via fontconvert's `-F`; the country list is derived from `lang_lut.xlsx` automatically. (The `_Tiny_` lang-code label font moved to `fonts/gen-status-fonts.sh` — see "Standalone UI text fonts" below.) These are **not** in `fonts.yaml`/`ALL_FONTS` — like the status-OLED fonts they're used via dedicated single-font arrays. `render_lang_flag_key()` in `poly_keymap.c` draws the flag (top 28 px) + the `xx-YY` code (bottom 12 px) per key, with a frame on the selected language. Re-run only when the language list changes. It also emits **`base/fonts/generated/lang_flags.json`** — the flag font's render record (source NotoColorEmoji, the `-s20 -g -r54 -W72 -O1 -Dfs -e-0.10` options, `seq_first` 0xE000, and the per-flag regional-indicator `sequence`). The flag font isn't in `fonts.yaml`, so `generate_fonts.py` emits no render record for it; this sidecar lets the host font-pack **editor** rebuild a single flag (sequence mode). ⚠️ Mirrored **byte-identically** in `PolyKybdHost/polyhost/res/fontpack/lang_flags.json` — keep both in sync (`cmp`).
- **The keycap `latin` category is built grid-fitted (`hinting: auto` in
  `fonts.yaml` → `fontconvert -Hauto`).** Same reason as the status fonts: NotoSans
  ships no hinting bytecode, so without it the ASCII/Cyrillic/Greek keycap legends
  render ungridfitted. `latin` is `resident: true`, so this changes only the
  compiled-in font — **every font-pack bundle stays byte-identical, so there is NO
  `.plyf` reship and no `content_version` bump**. The gain is real but modest at
  27 px (the `_Base_` size): measured mirror-asymmetry improves 12.4% → 7.1%,
  versus 14.9% → 4.4% at the 15 px status size.
- **Everything is grid-fitted EXCEPT emoji.** `hinting: auto` is set on every
  category — `latin`, `latinbig`, `hebrew`, `jp`, `kr`, `arabic`, `devanagari`,
  `bengali`, `telugu`, `tamil`, `thai`, `georgian`, `armenian`, `bopomofo`,
  `vietnamese`, `ethiopic`, `canadian`, `cherokee`, `tengwar`, `gscript` and
  `symbols`. Only **`emoji` / `emoji_fig`** (and the `flags` `pack_extra`) stay
  on `native`.
  - **It is NOT decorative at the bigger keycap sizes — measured, not assumed.**
    The intuition that grid-fitting only matters for small text is wrong here:
    counting stems that come out unequal WITHIN one glyph (the tell that a stem's
    two edges rounded independently), `latinbig` improves 26.1% → 21.4% at the
    33 px tier and **59.2% → 19.0%** at 39 px. The large tier is the bigger win,
    not the smaller one. Also note `latinbig` is sized with **`-p` (pixels)**, not
    `-s`: points-at-141-DPI can only land on even ppem, so 33/35/37/39 are simply
    not expressible in points — and those odd sizes are exactly what the 40 px
    ink ceiling forces.
  - ⚠️ **Do not "finish the job" by setting it on emoji — it is a measured no-op.**
    The autohinter assigns each glyph to a script by codepoint range and applies
    that script's blue zones (stems, x-/cap-height, baselines). Emoji codepoints
    match none of its ranges, so they get the no-script style with no zones:
    **0 of 1156 emoji glyphs and 0 of 51 emoji_fig glyphs change**. Setting it
    there only rewrites the provenance comment in the header (the flag is echoed
    into it) — the bitmaps are byte-identical. `symbols` by contrast is line art
    read as glyphs (arrows, modifier symbols, util icons) and 134 of 1060 do change.
  - **Reship cost so far**: `mideast` 1→2, `syllabic` 1→2, `asia` 1→2, `fantasy`
    3→4 (the text scripts), then `symbol` 6→7 (119 glyphs). `flags` and `emoji`
    stayed byte-identical throughout. Use the `reship-fontpack-bundle` skill — its
    `--check` is what tells you which bundles actually moved.
- ⚠️ **Two committed generated artifacts were already STALE before this work and
  `--check` flags them**: `fontpack.manifest.json`'s `total_size` (the committed
  480140 is the *post-dedupe* size, but the script builds that manifest **before**
  `prune_shadowed_glyphs` runs, so it emits the unpruned 492328), and
  `fontpack_render_settings.json` was missing all 12 `latin` records. Regenerating
  corrects both. If `--check` is ever wired into CI, fix the manifest/prune ordering
  rather than hand-editing the committed value.
- **Byte-reproducible output requires the pinned `fontconvert` build (FreeType 2.13.3 / HarfBuzz 2.6.7, the CMake ExternalProject)** — the distro fast-path build renders ~1px differently on some glyphs. The committed headers are built with the pinned toolchain.
- ✅ **`generate_fonts.py --check` PASSES on a clean checkout (2026-08-11) — if it
  drifts, something is genuinely wrong.** It had failed on every header for a long
  time, which was a **formatting** drift, not a rendering one: the committed headers
  were emitted during the column-native (PolyColGfx) work by a fontconvert built from
  a work-in-progress tree, so the current emitter wrote the same bytes differently
  (`Bitmaps[]PROGMEM` vs `[] PROGMEM`, 16 vs 12 bytes per line, glyph column widths,
  and `0` vs the running length in a **gap** record's dead `bitmapOffset`). The tree
  has now been regenerated with the pinned toolchain, so the committed headers are
  the emitter's native output and the interim `normalize_header_format.py` is gone.
  Verified across **156 fonts / 6714 glyphs** that the reformat changed no data.
  - **Two real bugs were hiding behind the permanent drift**, both fixed:
    `manifest_from_texts()` built `fontpack.manifest.json` **without** the dedupe the
    bundle path applies, so its `total_size` could never match the committed
    (post-dedupe) value; and `--only <cat> --check` reported every *other* committed
    header as `STALE`, because only the write path skipped the stale sweep under
    `--only`.
  - ⚠️ **`parse_gfx_header()` CANONICALISES every glyph's `bitmapOffset` to the
    running cumulative length**, so a purely cosmetic header change cannot reach the
    `.plyf` bytes. Without it, reformatting the tree changed **4 shipped bundles**
    (same size, ~535 single-byte diffs, all in dead gap offsets) and would have forced
    a reship + `content_version` bump for zero visual change. With it, all 7 bundles
    stay byte-identical to the shipped host copies. Don't "simplify" it away.
  - Regenerating needs **all** source fonts (`fonts/dl-fonts.sh`, ~75 MB, 21 entries)
    plus the pinned fontconvert at `/tmp/fontconvert_pinned` (the path is echoed into
    each header's provenance comment).
- **Adding codepoints to an existing `latin` font is the cheap case, and the cheapest
  sub-case is filling a GAP.** `latin` is `resident: true` and in no bundle, so the
  change is confined to the firmware image: **no `.plyf` reship, no `content_version`
  bump** — provided no *pack* font covers the new codepoints (check before assuming;
  a new resident glyph that a pack font draws identically would be pruned by
  `prune_shadowed_glyphs` on the next regen and change that bundle). A font emits one
  **contiguous** `first..last` table with gap records for unassigned slots, so a
  codepoint **inside** the existing span costs only its bitmap — the record already
  exists. `_LatinExtAdd_` spans `0x1E62..0x1EF9` with 144 gaps, which is why the Welsh
  `Ẁẁ Ẃẃ Ẅẅ Ỳỳ` + `Ẽ Ỹ` addition (2026-08-11) grew the table by **zero** entries.

See [`AdafruitGFX/CLAUDE.md`](../../../AdafruitGFX/CLAUDE.md) for `fontconvert` build and usage details.

- **The font pack — the resident set, the eight `PlyF` bundles, the slot layout,
  the HID flash transport and the reship procedure — is
  [`keyboards/polykybd/FONT_PACK.md`](../FONT_PACK.md).**
  `fontpack_assemble()` builds `g_all_fonts = resident ++ pack` at boot; with no
  pack, only the resident set is present, so the keyboard always renders ASCII.
  Five rules that reach outside that file:
  - ⚠️ **`g_all_fonts` is scanned FRONT TO BACK and resident is always in front.**
    A resident font therefore WINS over an overlapping pack copy — which is how a
    single UI glyph is made resident (a tiny dedicated font covering just that
    codepoint), and why a second face at native codepoints can never be reached, so
    the bigger legend tiers are RELOCATED into private PUA instead.
  - ⚠️ **Adding a whole new resident FONT shifts every pack font's gidx and forces a
    full-pack reship.** For one or two glyphs, extend the resident `IconsFont`
    instead (`base/fonts/gfx_icons.h`) — that shifts nothing and ships with the
    firmware. Better still, use a pack glyph or a base-font character.
  - ⚠️ **Never do heavy work in a split-transaction handler.** The slave's font-pack
    COMMIT runs inside a ~20 ms RPC callback; re-CRCing the whole ~459 KB pack there
    (~50 ms) made the master time out and report a perfect flash as a CRC failure.
    `fw_staging_finalize_defer_reload()` ACKs on the O(1) transport CRC and defers
    the reload to housekeeping.
  - ⚠️ **Because FONTPACK writes IN PLACE, a slot is a valid current bundle as soon
    as the last chunk lands — COMMIT is not what makes it so.** A complete stream
    whose COMMIT ack was lost reads back at the shipped `content_version`, so the
    version comparison alone must never decide a re-flash. The host consequences are
    in `PolyKybdHost/CLAUDE.md`.
  - **Reshipping needs no `fontconvert`** — bundles derive deterministically from the
    committed category headers. The `reship-fontpack-bundle` skill wraps it; its
    `--check` reports which bundles actually moved.

