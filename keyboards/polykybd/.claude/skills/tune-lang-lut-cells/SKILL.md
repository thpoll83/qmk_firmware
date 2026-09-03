---
name: tune-lang-lut-cells
description: Change the per-language keycap LUT across MANY layouts at once — a category offset for every layout, a glyph nudged the same way everywhere, a cell body rewritten wherever it appears — and prove the result. Use when asked to "set X for all languages", "populate the default offsets", "this glyph sits too low / too high on every layout", "make the nudges uniform", "apply these tuner values", or after a render change leaves the hand-tuned cells inconsistent. Drives lang_lut.xlsx through apply_tuner.py's surgical sheet2.xml path, re-cogs, confines the generated diff, and MEASURES the realised pixel effect. NOT for placing a new element on a keycap (that is keycap-layout-preview), NOT for adding a language (add-polykybd-language), and NOT for a single cell you can edit by hand.
---

# Bulk-tuning the language LUT

`lang_lut.xlsx` holds ~160 layouts × 54 key rows × 4 variations plus a block of
per-category settings rows. A tuning pass touches tens to hundreds of cells at
once, and every step below has a way of failing that leaves the tree looking
correct — so this is a measure-it loop, not an edit.

⚠️ **A data-only commit here is reviewed by NOBODY.** CodeRabbit skips it
(*"selected files did not have any reviewable changes"* — the only reviewable file
is the cog-generated `lang_lut.c`, and `.xlsx` is excluded by its path filter), and
cppcheck sees no C change. §6 is therefore the entire review.

Run the Python from `PolyKybdHost` (`.venv/bin/python`); the workbook lives in
`qmk_firmware/keyboards/polykybd/lang/`.

## 1. Survey first — never edit what you have not counted

The point of the survey is that "some layouts already have it" is almost always an
understatement. Resolve every candidate cell and histogram what is actually there.

```python
import os, sys, re, subprocess; sys.path.insert(0, 'tools')
import oled_preview as op
# derive the firmware root rather than hard-coding it - this skill lives inside it,
# and a sibling checkout under another path is the normal case for anyone else.
qmk  = subprocess.run(['git', 'rev-parse', '--show-toplevel'], cwd=os.environ.get(
       'QMK_HOME', '.'), capture_output=True, text=True).stdout.strip()
pk   = os.path.join(qmk, 'keyboards', 'polykybd')
named = op.load_named_glyphs(f'{pk}/lang/named_glyphs.h')
L     = op.Lang(f'{pk}/lang/lang_lut.xlsx', named)
R     = op.load_renderer(f'{pk}/base/fonts')          # NOT Renderer(load_all_fonts(...)) - see §5
# key rows are 2..55; variation = (col-2)%4  ->  0 base / 1 shift / 2 caps / 3 altgr
```

For a **glyph** pass, match the cell shape before trusting a count — assert every
hit is `[U"<ops>" ]TOKEN` and bail on anything else, so a multi-glyph legend can
never be rewritten by a rule written for a lone symbol.

For a **depth** question ("which glyphs sit under the baseline"), rank distinct
codepoints by `R.bbox([cp])[3]` and group by the owning font's
`f.yAdvance - R.base_yadv`. That separates a font-shifted symbol from a real
descender; a per-cell scan cannot.

## 2. Write through apply_tuner.py

`PolyKybdHost/tools/apply_tuner.py` edits `xl/worksheets/sheet2.xml` **in place**,
so the other sheets' formula caches survive. Never round-trip the workbook through
`openpyxl.save()`.

- **Offsets / settings rows** → generate its export grammar and run the CLI:
  `[offset] <letter|num|sym> <small|shift|altgr|held> <H|V> = <n>`, under a
  `=== <lang> ===` header. `--dry-run` first, always.
- **Key cells in the `caps` column** → ⚠️ the grammar has no `caps` (its regex is
  `base|shift|altgr`, matching what the tuner emits). **Import the module** and call
  `A.set_cell(xml, r, c, A.str_cell(ref, new))` yourself rather than widening a
  grammar nothing exercises.

## 3. Verify the workbook before re-cogging

⚠️ **openpyxl's two readers disagree when a renumbering pass has gone wrong**, and
the streaming one says everything is fine. Compare them over the edited rows and
require that the **eager** reader holds no value the streaming one lacks (the
reverse is a pre-existing shared-string quirk on the legend row and must be
tolerated, or the guard can never be green).

## 4. Re-cog, then confine the diff

⚠️ **Run `run_cog.sh`, not `cog -r lang_lut.c`.** The workbook feeds **seven**
generated files — `lang/lang_lut.c`, `lang/lang_lut.h`, `lang/named_glyphs.h`,
`poly_keymap.c`, `keycode_helper.h`, `uni.h` and `hid_com.c` — and which of them a
given edit reaches depends on what you touched: an offset row reaches only
`lang_lut.c`, a named glyph or a language-list change reaches several. Regenerating
one file leaves the rest stale with nothing to say so. `run_cog.sh` is **idempotent**
(verified: a full run on a clean tree changes nothing), so running all seven costs
only seconds and removes the judgement call.

```bash
# subshell: §6 does `export QMK_HOME=$PWD`, so this must NOT leave you in polykybd/
(cd keyboards/polykybd && PATH="/root/.qmk_venv/bin:$PATH" ./run_cog.sh)
```

Then **assert the generated diff contains only what you meant** — this is the check
that replaces a reviewer:

```bash
# EVERY generated file run_cog.sh touches, and BOTH directions: a line that
# disappeared is as much a surprise as one that appeared.
git diff -U0 -- keyboards/polykybd/lang/ keyboards/polykybd/*.c keyboards/polykybd/*.h \
  | grep -E '^[+-]' | grep -Ev '^(\+\+\+|---)' \
  | grep -cvE '<the token or pattern you changed>'    # must be 0
```

⚠️ **Do NOT expect one contiguous hunk** — the count depends entirely on how the
changed cells are distributed through the emitted tables, and it is not a signal.
The two passes on 2026-09-03 measured **1** hunk (six settings rows, adjacent in the
table) and **53** (117 key cells scattered across 19 rows). Judge the *content* of
the changed lines, not their grouping.

⚠️ Line counts will not match cell counts either — the emitter writes one line per
(language, row) carrying all four variations.

## 5. Measure the REALISED effect, not the requested one

The number in the cell is not the number on the keycap.

- **The panel clamp eats part of a nudge** when the glyph is already jammed against
  an edge: a −6 px lift came out 1–4 px on 13 of 92 elements, all previously pinned
  at the south edge. Render before and after (`op.render_key(..., report=rep)`,
  `rep['box']`) and report the distribution, not the intent.
- **Check off-panel ink both sides** (`rep['oob']`) — it must not grow.
- Fetch the "before" workbook from git rather than reconstructing it:
  `git show HEAD:keyboards/polykybd/lang/lang_lut.xlsx` into a temp file, then build
  a second `op.Lang` on it.

⚠️ Two renderer traps:
- `op.load_renderer(base/fonts)` — plain `Renderer(load_all_fonts(d))` omits the
  standalone mid face, so `HINT_MID` silently renders full size.
- `R.draw(setpix, cps, x, y)` takes **buffer** coordinates and `plot` subtracts
  `BUFFER_X` (28). Drawing at `x=2` puts everything off the viewport and you get a
  blank sheet that looks like a broken font.

## 6. Gates

From the **firmware root** (`QMK_HOME` is taken from `$PWD`):

```bash
export QMK_HOME=$PWD && export PATH="/root/.qmk_venv/bin:$PATH"
qmk compile -kb polykybd/split72 -km default -e POLYKYBD_DOOM_PACK=yes
qmk compile -kb polykybd/split72 -km default -e POLYKYBD_DOOM=yes   # RAM-tight; CI never builds it
qmk compile -kb polykybd/split42 -km default
qmk lint --strict --keyboard polykybd/split72 && qmk lint --strict --keyboard polykybd/split42
qmk ci-validate-keyboard-targets && qmk ci-validate-aliases
```

⚠️ **Run them SERIALLY.** Every flavour shares one `.build/` tree, so a
backgrounded build beside another one corrupts the link — and it presents as a
missing symbol (`undefined reference to doom_shim_menu_key_tile`), not as a
collision.

## Pitfalls

- **A settings row must be INSERTED, not appended** (`lang/_insert_settings_row.py`),
  and the row number lives in four places in `sheet2.xml` — two outside `<sheetData>`.
- **`HIDE_KEY` (−128) is a position sentinel, not a delta.** A delta row (the held
  offsets) takes real negatives, and the firmware maps `HIDE_KEY` to 0 there.
- **Say what did NOT move.** A pass that reports only the changed cells hides the
  layouts the clamp pinned, which are exactly the ones a human should look at.
- **Hand over a rendered sheet**, not just numbers, when the question was visual.
