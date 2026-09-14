---
paths:
  - "keyboards/polykybd/fonts/fonts.yaml"
  - "keyboards/polykybd/fonts/generate_fonts.py"
  - "keyboards/polykybd/fonts/noto-fonts.yaml"
---
# Regenerating fonts

Notes: `fonts/README.md` · `FONT_PACK.md`. The `reship-fontpack-bundle` skill handles a
reship without `fontconvert`.

- ⚠️ **Three artifacts are mirrored BYTE-IDENTICALLY into the host repo** —
  `noto-fonts.yaml`, `fontpack_render_settings.json`, `lang_flags.json`. `cmp` them, or
  the host editor pre-fills the wrong controls.
- **The list order IS the `ALL_FONTS[]` priority** (front-to-back, first match wins);
  categories only decide which header a font lands in.
- ⚠️ **Adding a whole new RESIDENT font shifts every pack font's gidx** and forces a
  full-pack reship. Extend the resident `IconsFont` for one or two glyphs instead.
- **Byte-reproducible output requires the pinned `fontconvert` build** (FreeType 2.13.3 /
  HarfBuzz 2.6.7).
- ⚠️ **Don't "simplify away" `parse_gfx_header()`'s bitmapOffset canonicalisation** — it
  is what stops a cosmetic header change reaching the `.plyf` bytes and forcing a reship.
- ⚠️ **`hinting: auto` on emoji is a measured NO-OP** (no autohinter script matches those
  codepoints); it rewrites only the provenance comment.
