---
name: add-polykybd-shortcut-hint
description: >
  Add, retouch or retire an OS shortcut-hint icon on the PolyKybd keycaps: the
  34x34 icon shown on a key while a modifier is held (Win+V clipboard history,
  Win+L lock, Cmd+Tab app switch, Ctrl+Left word left). Use when asked to "add a
  hint for Win/Cmd/Super+X", "change the <name> icon", "make the lines thicker /
  move them up", "try variations of the network icon", or "remove a hint". Covers
  drawing in the style-D grammar, before/after sheets, regenerating
  PolyHintIcons and the named_glyphs.h macros, wiring hints/os_hints.c and its
  tests, reshipping the symbol font-pack bundle, and handing the tester a .bin,
  the .plyf and the flash command. NOT for app shortcuts like Ctrl+C or F2 (those
  belong to the host app overlays: generate-app-overlays), full keyboard
  languages (add-polykybd-language), or legend glyphs (LEGEND_RENDERING.md).
---

# Add or retouch an OS shortcut-hint icon

Read [`keyboards/polykybd/HINT_ICONS.md`](../../../keyboards/polykybd/HINT_ICONS.md)
first. It holds the style-D rules and the reason behind every icon's current shape,
and most change requests were already decided there once.

Every hint is ONE glyph of the font `PolyHintIcons` (U+100100..), drawn by
`tools/hint_icons_stage.py`, written by `tools/hint_icons.py` to
`base/fonts/hint_icons.h`, and shipped in the `symbol` font-pack bundle, NOT in the
firmware image. `hints/os_hints.c` returns the `ICON_HINT_*` macro for a chord.

All paths below are relative to `keyboards/polykybd/` unless they start with a repo
name.

## 0. Is it a hint at all?

- **App shortcuts are NOT hints.** Ctrl/Alt + letter or digit (Shift allowed), Cmd +
  letter or digit on macOS, F2 and F5 mean different things per program, so the
  host app overlays draw them. `os_hints.c` returns nothing for them and the
  `AppShortcutsHaveNoBuiltInHint` test enforces it.
- **The chord must be real for the OS you gate it on.** Dictation is Win+H on
  Windows; macOS has no held-chord equivalent. Gate with the predicates in
  `os_hints.c` (`apple`, `win_or_unknown`, `gnome`, `linux_any`).

## 1. Draw (or retouch) the icon

In `tools/hint_icons_stage.py`, an icon is a function on the 34×34 grid:

```python
@icon("clip_history")
def _(I):
    I.thin(3, 4, 30, 33)                          # stage: 3 px chamfered outline
    I.F(10, 1, 23, 8, 0); I.thin(11, 1, 22, 7, ch=1)
    I.F(8, 11, 25, 12); I.F(8, 18, 25, 19); I.F(8, 25, 18, 26)   # 2 px content lines
    I.corner("clock")                             # modifier badge, bottom right
```

The primitives: `F` (filled rect, inclusive corners), `thin`/`frame` (3 px stage
outline), `solid`/`heavy` (actor), `ring` (outline of any width), `oct` (octagon),
`wedge` (the one direction mark), `dash_h`/`dash_box`, `screen`, `window`, `sheet`,
and `corner(sym)`. Keep the grammar: stage 3 px, actor solid, 0/45/90° only.

- **A NEW icon also needs a drawing in the other four style modules**
  (`hint_icons.py` itself, `hint_icons_solid.py`, `hint_icons_bold.py`,
  `hint_icons_retro.py`). `style_render()` asserts every style covers the same
  names. A placeholder there is fine; the shipped style is D.
- **Append a new name to `ICONS` in `hint_icons.py` at the END.** The order there
  is the slot order, so inserting in the middle renumbers every later slot.
- **Retiring** is adding the name to `RETIRED` (it stays drawn, is not written).
  That renumbers every later slot, which is harmless because only the firmware
  uses these codepoints, but step 3 must re-paste the macros.

## 2. Show it before writing anything else

```bash
SK=.claude/skills/add-polykybd-shortcut-hint
python3 -B $SK/compare_sheet.py /tmp/cmp.png                  # HEAD vs working tree
python3 -B $SK/compare_sheet.py /tmp/cmp.png --base <rev> clip_history emoji
```

12× with row numbers, before and after side by side. Send it to the maintainer.
For "give me variations", draw each variant as its own `@icon` name in a scratch
copy and render them on one sheet; never rewrite-and-reload one module (see
Pitfalls). A whole-set overview at 4×: `python3 tools/hint_icons.py --sheet /tmp/all.png --style d`.

## 3. Regenerate and check

```bash
cd keyboards/polykybd
python3 tools/hint_icons.py                # rewrite base/fonts/hint_icons.h (keeps style d)
python3 tools/hint_icons.py --macros       # only when names or slots changed:
                                           #   paste into lang/named_glyphs.h's ICON_HINT block
python3 tools/hint_icons.py --check        # header + macro block both current
python3 tools/check_icon_slots.py          # no macro between the two icon fonts
```

## 4. Wire a new chord (skip for a retouch)

- Return the macro from the right OS branch of `hints/os_hints.c`, with a trailing
  comment naming the chord, like its neighbours.
- **The extraction test will refuse a NEW chord.**
  `OsHintsExtraction.ShowsAHintForExactlyThePreExtractionChords` compares every
  (keycode × mods × OS) against a frozen copy of the pre-extraction table, and a
  chord that table lacked fails as "shows a hint the reference did not". Keep the
  reference file verbatim (its value is being unedited) and admit the new chord the
  same way the app-shortcut rule masks its chords: a small `is_added_chord()`
  predicate written independently of `os_hints.c`, plus a positive test that names
  the chord and the icon.
- A new icon used by a pre-extraction chord goes in `kExpected` instead.
- `EveryShortcutHintIsOneIconGlyph` bounds the block by its first and last macro
  (`ICON_HINT_SEARCH`, `ICON_HINT_QUICK_ASSIST`). An icon appended at the end is
  the new last: update that line.

```bash
export QMK_HOME=$PWD PATH="/root/.qmk_venv/bin:$PATH"     # from the qmk_firmware root
make test:polykybd_os_hints
```

## 5. Reship the symbol bundle

Use the `reship-fontpack-bundle` skill: `--check`, then
`--apply symbol=<N>`. Keep N while the bundle is unshipped; bump it only once a
release has carried it. Then run the host font-pack tests it lists.

## 6. Build and hand it over

```bash
qmk compile -kb polykybd/split72 -km default -e POLYKYBD_DOOM_PACK=yes   # also split42
```

Use the `deliver-test-firmware` skill (sha in the `.bin` name). For an icon-only
change the `.bin` does not change at all; the new pixels are in `symbol.plyf`. Hand
the tester the `.plyf` too, with the flash command, because a bundle at an
unchanged version is NOT re-flashed on connect:

```bash
python -m polyhost.cli.polyctl fontpack flash --file symbol_v<N>.plyf --bundle-id 0
```

Commit firmware (`hint_icons_stage.py`, `hint_icons.h`, and when wired
`named_glyphs.h`, `os_hints.c`, the tests) and host (`res/fontpack/symbol.plyf`,
`bundles.json`) separately. Release order: host first.

## Output

Per icon: name, chord(s) and OS, what changed (with the row numbers), the
before/after sheet, test count, and the files handed over (`.bin` with sha,
`.plyf` with version, the flash command).

## Pitfalls

- ⚠️ **Rewriting one module and `importlib.reload()`-ing it renders STALE icons.**
  The bytecode cache is keyed by (mtime, size); a same-size edit in the same second
  (`14` → `12`) re-runs the old code and every variant looks identical. One module
  name per variant, `python3 -B`. `compare_sheet.py` does this.
- ⚠️ **"Looks identical on the keyboard" after a retouch usually means the board
  never got the new bundle**, not that the change was too small: same
  `content_version`, so no re-flash on connect. Check the flash command was run.
- **A 1 px change is invisible at 4–5×.** Show 12× with row numbers before asking
  for a verdict.
- **Read a direction request twice.** "Remove the px line from the top instead of
  the bottom" and "move the lines up" were the same request; the first attempt moved
  the lines down. Restate the target rows ("first line at rows 11–12") before
  drawing.
- **Do not hand-edit `hint_icons.h` or the `ICON_HINT_*` block**; both are generated,
  and `--check` fails on a stale one.
- **Hardware decides.** Every accepted shape in `HINT_ICONS.md` came from a look at
  a real keycap; record the new decision there when the maintainer settles one.
