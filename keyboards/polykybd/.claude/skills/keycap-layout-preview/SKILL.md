---
name: keycap-layout-preview
description: Place and verify anything drawn on a PER-KEYCAP 72x40 OLED — a corner badge, a modifier mark, a status dot, a second glyph beside the legend, a repositioned hint — by rendering a faithful Python model of the firmware draw path and MEASURING whether it collides with the legend, instead of flashing a build to look at it. Use when asked to "move the mark to the other corner", "show X on the keycap too", "does this overlap the letter", "make it smaller / put it in the corner", "give me a preview instead of another hardware round", or when a keycap render needs sign-off before a build. NOT for the STATUS OLED (that is status-oled-layout) and NOT for choosing a hint GLYPH (that is add-polykybd-shortcut-hint).
---

# Per-keycap layout changes

Keycap chrome is easy to get wrong in a way that survives every check you would
normally run: it compiles, it lints, the unit tests pass, and the defect only
exists in the pixels. The two that shipped in the 2026-08-18 session were both of
that shape — a badge drawn where the legend was, and later a badge that erased
nothing but *merged* with the shift preview.

So: **model the draw path, then measure ink against ink.** Do not eyeball the PNG,
and do not spend a hardware round on a placement question.

`keycap_preview.py` (beside this file) is a Python **re-implementation** of
`base/disp_array.c` and `render_key()`'s legend placement, including the parts a
naive renderer misses. Run everything from `keyboards/polykybd/`.

⚠️ **It is a model, so it can drift from the C** — that is not hypothetical, it is
the standing caveat on `oled_preview.py` one directory over (it models
`xOffset`/`yOffset` but *not* the baseline-align shift, so it cannot reproduce that
class of bug). Treat a result as evidence about **layout**, never as proof about the
compiled firmware, and re-read the C when you touch either side: the pixel plotters
and `KDISP_CY_DEFAULT` in `base/disp_array.c`, the legend/preview placement at the
end of `render_key()` in `poly_keymap.c`. If a preview and the hardware ever
disagree, the hardware is right and this file has a bug — fix it here rather than
working around it in a caller.

## The three facts that make this necessary

1. **A hint/overlay string is drawn OVER the legend at the same origin.**
   `update_displays()` draws the legend at `(BUFFER_X, 23)`, sets `text = NULL`,
   then draws `keycode_to_disp_overlay()`'s string at that *same* origin with
   `KDISP_CY_DEFAULT` — whose 3px courtyard clears the legend before the glyph
   lands. Fine for a held-modifier hint (the keycap *means* Ctrl+C then); fatal
   for a mark meant to coexist with the letter.
2. **The upper right is taken.** `render_key()` puts the shift preview there
   (baseline 23, x from `*_HOFFSET VAR_SHIFT`). en-US HIDEs it for letters, but
   **36 of 160 languages do not**, and *every* language shows one on the number
   and symbol rows. Corner chrome therefore anchors **bottom-right**.
3. **`kdisp_write_gfx_char` baseline-aligns to `fonts[0]`** (IconsFont, yAdvance
   40). `oled_preview.py` does **not** model that shift; `keycap_preview.full_ink`
   does.

## 1. Build the ink sets

```python
import sys; sys.path.insert(0, '.claude/skills/keycap-layout-preview')
import keycap_preview as K

legend = K.legend_ink('a')                    # base glyph + shift preview, real offsets
legend = K.legend_ink('1', shifted='!')       # non-letters: name the shifted glyph
legend = K.legend_ink('a', lang='ar-SA')      # another language's offsets

mark = K.thin_ink(0x2388, 80, 21)             # decimated (HINT_THIN)
mark = K.half_ink(0x2387, 79, 31)             # 2x2-OR (HINT_HALF)
mark = K.full_ink(0x90, 83, 40)               # normal draw, baseline-aligned
```

`thin_ink`/`half_ink` take the literal **ink top-left**; `full_ink` takes the
**cursor** and applies the baseline align — that asymmetry is the firmware's, not
the helper's.

## 2. Measure — this is the step that matters

```python
K.collision(legend, mark)        # px where the two overlap        -> want 0
K.erased(legend, mark)           # legend px a courtyard draw wipes -> want 0
K.offscreen(legend | mark)       # ink outside x28..99 / y0..39     -> want empty
K.extent(mark)                   # (xmin, xmax, ymin, ymax)
```

⚠️ **`collision()` is the metric — intersect, do not subtract.** A "how many
legend pixels survived" count reads **0 damage** for a real collision, because
lit-on-lit loses no pixels; it just reads as one merged blob. That metric hid a
21px digit/mark collision for a full round.

Sweep the cases that differ, not just the convenient one:

```python
for combo in COMBOS:                      # every state your chrome can be in
    for ch, kw in (('a', {}), ('1', {'shifted': '!'})):   # letter AND digit
        c = K.collision(K.legend_ink(ch, **kw), marks_for(combo))
        if c: print(combo, ch, c)
```

A letter and a digit are genuinely different tests: the digit carries a shift
preview in every language, the letter usually does not.

## 3. Render for sign-off

```python
K.sheet([(legend | mark, 'LCTL_T(KC_A)'), …], '/tmp/deliver/preview.png')
```

Send the PNG. One row of labelled keycaps beats a paragraph, and it is what the
user asked for when they said "give me previews instead of another hardware
round". The same renders are docs-quality — the mod-tap page's images are these.

## 4. Generate the firmware constants from the SAME source

If the placement becomes `#define`d positions (`HINT_MOVE` coords, an `MTB_*`
block), emit the C **and** the preview from one script, so they cannot disagree.
Assert the invariants there:

```python
if not (x and y):        # a raise, not an assert — `python -O` strips asserts,
    raise ValueError(    # and this one guards a silent-truncation hazard
        'zero coordinate (%d,%d) would NUL-terminate the hint string' % (x, y))
```

⚠️ **A `HINT_MOVE` coordinate is a codepoint in a `U"…"` literal, so a zero byte
is a NUL that terminates the display list.** A row placed at `y=0` truncated every
badge to two codepoints; nothing crashed, the marks just silently vanished.

## Pitfalls

- **Don't eyeball the sheet.** Every defect this skill exists for was invisible
  in the render and obvious in `collision()`.
- **Don't test only en-US letters.** That is the one case with no shift preview.
- **Bottom-right, not top-right**, for anything that must coexist with the legend.
- **`oled_preview.py` is still the right tool for a plain legend** (it renders the
  real OLED look for a whole key). This skill is for *added* chrome, where the
  baseline-align shift and the collision question both matter.
- **A pixel-count check on a transform∘inverse round-trip proves nothing** — the
  same rule as the IconsFont transpose. Compare against the other artefact
  (the legend), not against your own re-derivation.
- If the change alters something visible on a keycap, say **which pixel** tells
  two builds apart — that is a check the user can run with no tooling.

### Harness fidelity — four ways a preview validates the wrong thing

All four cost a round on 2026-08-26, and three of them report SUCCESS while wrong.

- ⚠️ **Model the COURTYARD.** A harness that skips a draw stage validates a render
  the firmware never produces. `kdisp_write_gfx_char` clears a ±3px band per glyph
  *before* plotting it, which at tight spacing removes the previous glyph's ink —
  a defect that shipped precisely because the preview drew glyphs and nothing else.
  Model it, and check **lit-pixel count against the same legend at `cy_radius` 0**;
  a visual read will not show a few missing pixels per letter.
- ⚠️ **Mirror C integer division.** Python's `//` FLOORS, C's `/` TRUNCATES toward
  zero, and they differ for a negative numerator — which `\v` produces the moment a
  `\f` lift precedes it (`(15-23)/15` is `0` in C, `-1` in Python). The first run
  of a two-line measurement reported all 16 legends drawing both lines on top of
  each other. Anything mirroring firmware arithmetic needs an explicit `cdiv()`.
- ⚠️ **Parse the legends AND the spacing back out of the SHIPPED SOURCE**, never
  retype them into the harness. Read the strings from `keycode_helper.c` /
  `poly_keymap.c` and the lift/push/nudge out of the macro in `named_glyphs.h`, so
  the harness cannot drift from what compiles.
- ⚠️ **Assert the parse found something, and unescape PER LITERAL.** A regex that
  matches zero legends reports "no failures" having checked nothing — the fail-open
  shape this repo documents elsewhere. And a `\xNN` escape ends at the closing quote
  in C, so joining adjacent literals before unescaping merges `\x06` + `E` into one
  codepoint and invents a defect the compiler cannot have.
