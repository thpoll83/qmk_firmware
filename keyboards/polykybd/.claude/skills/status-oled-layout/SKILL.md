---
name: status-oled-layout
description: Adjust the PolyKybd STATUS OLED layout (the 128x64 split72 / 32x128 portrait split42 panel) — move, add, resize or re-space rows and indicator columns, and verify the result before flashing. Use when asked to "distribute the lines better", "give X more space", "swap the brightness and WPM rows", "move the layer number", "bottom out the last row", "add a field to the status display", or when a status-OLED render looks cramped/lopsided. Measures real pixel bands rather than eyeballing the preview, so row collisions and wasted slack are caught numerically. NOT for the per-KEYCAP 72x40 displays (that is oled_preview.py / render_key) and NOT for the boot-splash or flash-progress screens.
---

# Status-OLED layout changes

Row placement on these panels is fiddly for one reason: **`--diag` cannot see the
problems that actually matter.** It reports pixels drawn off the *panel*, so it
stays happily at `0 clipped` while two rows overlap, while a gap is 0px, or while
4 unused rows sit under the bottom row. Every layout bug found in the 2026-07-29
session was invisible to `--diag` and to the eye, and obvious the moment the
bands were measured.

So: **measure, place, render, build.** Never tune by looking at the PNG alone.

Run everything from `keyboards/polykybd/`.

## 1. Measure the current layout

```bash
python3 .claude/skills/status-oled-layout/measure_bands.py 72          # both RGB modes
python3 .claude/skills/status-oled-layout/measure_bands.py 72 --calls  # per-element extents
python3 .claude/skills/status-oled-layout/measure_bands.py 42
```

It imports the preview module, wraps every draw helper so each lit pixel is
attributed to the call that made it, and prints contiguous lit-row **bands** plus
the **gaps** between them and the **bottom slack**. A negative gap is an overlap;
`0` means touching. It flags both.

It slices each panel into its **text area** and its **indicator column**
separately — they are independent stacks (lock icons / speed gauge live in the
column), and measuring them together merges the two into one meaningless band.

## 2. Derive each row's extent from its content

You need these to place rows without re-measuring every time:

| element | extent relative to its baseline |
|---|---|
| 8pt text, no descender | `base-10 .. base` |
| 8pt text with descenders | `base-10 .. base+4` |
| 11pt header text | `base-11 .. base` |
| 13px globe bitmap | `base-12 .. base` |
| brightness gauge (10 bars) | `base-12 .. base`, and **98px wide** |
| WPM dial + digits | `base-10 .. base` |

**Worst-case content to place against** — not the default fixture:

- longest layout name **`Qwerty Stag!`** — 95px wide, descends to `base+4` across
  `x7..90`. The tail is *wide*, so the row below cannot dodge it horizontally.
- a **fully lit** brightness gauge (all 10 bars) — tallest bar is 12px.
- a 3-digit WPM, and a wide language code (`mn-MN`, `TW` is 18px at 6pt).

## 3. Place the rows

Rules that fell out of doing this by hand:

- **Bottom out the last row** so its final pixel lands on the last screen row (63
  on split72, 127 in split42's logical portrait space). Give the physical-side
  marker that same baseline — it then sits level with the last content row.
- **Space each panel independently.** The two split72 panels have opposite shapes
  (layout panel's descenders on row B, RGB panel's on row C). A brute force over
  all shared `(rowB, rowC)` pairs maxes out at a **1px** minimum gap with a
  lopsided 7px elsewhere; splitting them gives **3/2/2** and **3/3/3**. The halves
  are ~20cm apart, so the 1–3px offset between them is not visible as
  misalignment.
- **The 98px brightness meter can only ever hold a row alone** — anything sharing
  its row collides. This is why the language slot rides with the speed group.
- Solve it as arithmetic, not by nudging: `free = region_height - sum(content
  heights)`, split `free` over the gaps as evenly as it divides. If you need the
  optimum under a constraint, brute-force the baselines (a dozen lines of Python)
  rather than guessing.

Name every baseline you introduce (`LOCK_ROW_C`, `SIDE_MARKER_BASE`,
`CAPS_LOCK_BASE`) and note any **coupling** in a comment — e.g. `SPEED_BOX_H` is
defined as "Num Lock top .. Caps Lock bottom", so moving Caps Lock silently
resizes the gauge on the *other* panel.

## 4. Mirror into the preview, then verify

The preview is a separate implementation, so **every C edit needs the matching
Python edit** or the renders stop being evidence. Then:

```bash
python3 tools/status_oled_preview.py -b 35 --lang en-US --wpm 62 -o /tmp/on.png --diag
python3 tools/status_oled_preview.py --rgb-off -b 35 --lang en-US --wpm 62 -o /tmp/off.png --diag
python3 tools/status_oled42_preview.py -o /tmp/42.png --diag
python3 .claude/skills/status-oled-layout/measure_bands.py 72   # re-measure: gaps as intended?

export QMK_HOME=$(git rev-parse --show-toplevel)
for kb in split72 split42; do qmk compile -kb polykybd/$kb -km default >/tmp/b_$kb.log 2>&1; echo "$kb exit=$?"; done
```

Both variants must build even for a split72-only change — `oled_helper.c/.h` and
`text_helper.c/.h` are shared. For a pure refactor, `cmp` the rendered PNGs
against the pre-change ones to prove the layout did not move.

Deliverable for hardware testing is the `.bin`, not the `.uf2`:

```bash
arm-none-eabi-objcopy -O binary .build/polykybd_split72_default.elf /tmp/split72.bin
```

## Output format

Report the before/after gap table, not prose:

```
                       before          after
layout panel   gaps  3 / -2 / 2     3 / 2 / 2     (-2 was a real overlap)
RGB panel      gaps  2 / 4 / 0      3 / 3 / 3
bottom slack   4px on both          0
```

State any judgement call explicitly (per-panel spacing loses cross-panel row
alignment; a group had to move panels), so it can be vetoed cheaply.

## Pitfalls

- **`--diag` 0 clipped ≠ good layout.** It only checks the panel edge. split42
  legitimately reports **33 clipped** — the Link icon's deliberate left overhang.
  Don't chase it.
- **Draw helpers nest** (`draw_text` → `draw_glyph`, `draw_brightness_row` →
  `draw_bitmap` + `draw`). Attributing pixels without tracking nesting depth
  double-counts and makes the derived gaps nonsense; `measure_bands.py` records
  only depth-0 calls.
- **Check a value exists on the panel you move it to.** `get_current_wpm()` reads
  correctly on both halves only because `config.h` sets `SPLIT_WPM_ENABLE`; a
  value that is master-only renders 0 on the other half.
- **Fonts differ per variant.** split72's status font is ASCII `0x20..0x7e` only —
  no `°`, no `²` (both are hand-drawn bitmaps). split42 has ~32px of width, so it
  uses `layout_name_short()`.
- **Some glyphs cannot be measured from the preview** — the scroll-lock arrow
  (`ARROWS_DOWNSTOP`, U+2B73) lives in a pack font the preview does not load. Do
  not reposition an element whose extent you cannot measure; leave it and say so.
- **`rm -rf tools/__pycache__`** before running `qmk lint` locally, or the stale
  `.pyc` shows up as a tracked-but-ignored false positive.
- Mirror C ↔ Python in the *same* commit. A preview that lags the firmware is
  worse than no preview.
