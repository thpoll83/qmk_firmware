---
paths:
  - "keyboards/polykybd/base/disp_array.c"
  - "keyboards/polykybd/base/font_lookup.c"
  - "keyboards/polykybd/base/legend_plan.c"
---
# Drawing on the per-keycap OLEDs

Notes: `LEGEND_RENDERING.md` · `LEGEND_LAYOUT.md` · `DISPLAY_PIPELINE.md`. The
`keycap-layout-preview` skill measures a placement instead of flashing a build.

- ⚠️ **Adding a display-list op is TWO walkers — THREE counting the host.** Draw and
  measurement must clear the same flags and skip the same arguments, or the bbox
  describes a legend the draw does not produce. `oled_preview.py` is the third edit, and
  a refused op falls back to the keycode TEXT — which looks exactly like the op not
  working.
- ⚠️ **Nudge-run arithmetic is unverifiable by any test in this repo.** Render every
  legend you touched and require **0** pixels outside the 72×40 window.
- ⚠️ **Keep every glyph of one legend in ONE font** — the baseline align is
  `font->yAdvance - fonts[0]->yAdvance`, so two faces sit on two baselines.
- ⚠️ **The resident C1 icon band `0x80–0x9F` is FULL, and `0xA0+` is not an option.**
  `tools/check_icon_slots.py` is the only thing that can answer "is this slot free?".
- ⚠️ **The display grid is NOT a rectangle and two physical keys have no OLED** — gate on
  `key_has_display(r,c)`; a bounds check is not a substitute.
