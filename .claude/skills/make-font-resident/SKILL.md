---
name: make-font-resident
description: >
  Make a PolyKybd keycap glyph render WITHOUT a font pack flashed by moving it
  into the resident (compiled-in) font set. Use when asked to "make X render with
  no pack", "the GUI / emoji-layer / util-layer / modifier symbol is blank without
  the font pack", "add Y to the resident fonts", or "this icon disappears when the
  pack is wiped". Handles the single-glyph-in-a-big-pack-font case (tiny dedicated
  font), the byte-reproducible regenerate, and the verify+build. NOT for adding a
  whole new script/language (use add-polykybd-language) or generating overlays.
---

# Make a font glyph resident (renders with no pack)

PolyKybd fonts split into a small **resident** set compiled into the firmware and
a large **pack** in external flash (4–8 MB) flashed over HID. `g_all_fonts =
resident ++ pack`. A glyph that lives only in the pack shows as tofu when no pack
is present. This skill moves a glyph into the resident set.

See `CLAUDE.md` → "Font pack: resident fonts + external-flash pack" for the
architecture; this is the operational recipe.

## Steps

1. **Find the glyph's codepoint and font.** Grep the keycode→glyph mapping
   (`keycode_helper.c`, `lang/named_glyphs.h`) for the key, then find which
   `fonts.yaml` entry's `ranges` covers that codepoint and its generated symbol
   name (in `base/fonts/generated/*.h`, e.g. `NotoEmoji_Medium_Settings_20pt16b`).
   ```bash
   cd keyboards/polykybd
   grep -n "<KEY>" keycode_helper.c           # e.g. TO(_EMJ) -> PRIVATE_EMOJI_1F600
   grep -n "PRIVATE_<NAME>\|0x1f600" lang/named_glyphs.h
   grep -n "0x1f600" fonts/fonts.yaml         # which variant/range
   ```

2. **Decide: whole font, or a tiny dedicated one?** Measure the candidate font's
   on-flash size (bitmap bytes + glyph count). If it's a single glyph inside a big
   range (≫ a few hundred bytes — e.g. GUI ❖ U+2756 in the 12 KB `_SymBmp4_`),
   **add a tiny dedicated font entry** covering just that codepoint instead:
   ```yaml
   - {category: symbols, variant: _GuiKey_, ranges: [[0x2756, 0x2756]], note: '...'}
   ```
   It overlaps the big pack font, but **resident wins** (front-to-back precedence),
   so the big font stays in the pack. Generated name → `<Font>_<weight>_<variant>_<size>pt<bits>b`.

3. **Add the generated symbol name to `index.resident_fonts`** in `fonts.yaml`
   (this is what keeps it compiled-in / out of the pack).

4. **Ensure the toolchain is ready** (only if regen fails):
   - Pinned `fontconvert` built once (byte-reproducible): see `AdafruitGFX/CLAUDE.md`
     (CMake ExternalProject). Copy it to a stable path: `cp ~/.local/bin/fontconvert /tmp/fontconvert_pinned`.
   - Noto sources: `bash fonts/dl-fonts.sh` (some are variable fonts fetched
     separately — canadian-aboriginal, cherokee). `Font load error: 1` = missing source.

5. **Regenerate from the matching binary path** (byte-repro — the headers embed the
   fontconvert path in a provenance comment):
   ```bash
   cd fonts && FONTCONVERT=/tmp/fontconvert_pinned python3 generate_fonts.py && cd ..
   git diff --stat -- base/fonts/
   ```
   **Expect ONLY**: `gfx_used_fonts.h` (+resident line), `fontpack.manifest.json`
   (font_count drops), `all_fonts_order.json`, and — if you added a new entry — its
   one category header (`symbol_fonts.h`/`emoji_fonts.h`). **If any OTHER category
   header diffs, the toolchain/source drifted — stop** (you'll get spurious churn).

6. **Build both variants** and confirm the size bump is roughly the glyph size:
   ```bash
   export QMK_HOME=$PWD/../../../..   # repo root
   qmk compile -kb polykybd/split72 -km default
   qmk compile -kb polykybd/split42 -km default
   arm-none-eabi-size .build/polykybd_split72_default.elf
   ```

7. **(Optional) ship a matching slim pack + the .bin**:
   ```bash
   FONTCONVERT=/tmp/fontconvert_pinned python3 fonts/generate_fonts.py \
       --emit-pack /tmp/pack.plyf --content-version <N>
   arm-none-eabi-objcopy -O binary .build/polykybd_split72_default.elf out.bin
   ```

## Verify it renders with no pack

The pack lives in a flash region firmware flashing never touches, so it persists.
To test resident-only, **wipe**: `polyctl fontpack wipe` (flashes a 32-byte empty
pack) — then the glyph must still render. The old pack still works too (resident
wins on overlap), so re-flashing the pack is optional after a resident change.

## Pitfalls

- **Single-glyph icons get a baseline shift.** `kdisp_write_gfx_char`
  baseline-aligns to `fonts[0]` (`y += currentFont->yAdvance - fonts[0]->yAdvance`).
  If you also *draw* a standalone icon/flag from `g_all_fonts`, it shifts by the
  yAdvance difference vs IconsFont (40). Draw such a glyph through a **single-font
  array** `{ that_font }` (`kdisp_gfx_glyph_font` returns the glyph + its font in one
  scan). This bit the language flags (+14 px gap-at-top).
- **Byte-repro:** regenerate from the **same** fontconvert binary path the committed
  headers used, or every category header shows a 1-line provenance diff.
- **Don't make a big multi-glyph font resident for one glyph** — use a tiny
  dedicated overlapping font; resident precedence makes the pack copy dead weight.
- **Preview keycaps** with `PolyKybdHost/tools/oled_preview.py` (`gfx_font` +
  `oled_to_rgb`), never a hand-rolled bitmap reader (GFX bitmaps are continuous-bit,
  byte-padded per glyph — a per-scanline reader yields garbage).
