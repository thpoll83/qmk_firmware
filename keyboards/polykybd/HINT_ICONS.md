# OS shortcut-hint icons: the design and the decisions behind it

A keycap shows a **hint** while a modifier is held: a preview of what the chord is
about to do (Win+L lock, Win+V clipboard history, Ctrl+Left word left). Since
2026-10 every hint is one 34×34 icon from one drawn family. This file keeps the
design rules and the reasons for each icon's shape, so the next change starts from
them instead of from the pixels. The mechanics (font, slots, pack) are in
[`FONT_PACK.md`](FONT_PACK.md); the procedure for adding or retouching a hint is the
`add-polykybd-shortcut-hint` skill.

## Where things live

| What | Where |
|---|---|
| The drawings (style D, the one that ships) | `tools/hint_icons_stage.py` |
| Slot order, retired set, header writer | `tools/hint_icons.py` (`ICONS`, `RETIRED`) |
| Four other styles, same slots | `tools/hint_icons.py` (a), `_solid` (b), `_bold` (c), `_retro` (e) |
| The generated font `PolyHintIcons` | `base/fonts/hint_icons.h`, U+100100.. |
| The `ICON_HINT_*` names | `lang/named_glyphs.h`, pasted from `hint_icons.py --macros` |
| Which chord shows which icon | `hints/os_hints.c` |
| The pin on that mapping | `hints/tests/os_hints_tests.cpp` (`kExpected`) |

The font ships in the `symbol` font-pack bundle, not in the image. A board without
the pack shows no hint at all, and the key keeps its legend.

## The grid

- **34×34, one bit per pixel.** The glyph's own metrics place it on the 72×40
  keycap: xOffset 36 (panel columns 36..69) and top row 3 (rows 3..36), with the
  cursor at y 23 and yAdvance 40 like `IconsFont`. So a hint string is the bare
  glyph, with no leading spaces to tune.
- The right half of the keycap is the hint's; the legend keeps the left.

## Style D, "stage and actor"

A hint is a short sentence: *this thing* (the stage) *has this happen to it* (the
actor).

1. **Stage light, actor heavy.** The stage is what is affected (a window, a screen, a
   page, a speech bubble). It is a **3 px** outline. The actor is what happens. It is
   **solid**, or 4 px. One actor per icon. Where the object itself is the point
   (settings, lock, folder) it is the actor and there is no stage.
2. **Only 0°, 45° and 90°.** Those are the only angles a 1-bit grid draws cleanly, so
   every curve becomes a 45° chamfer and round things are octagons. That is the
   family's signature.
3. **One wedge** for every direction: the same solid 45° wedge.
4. **The corner.** A modifier (plus, minus, clock, record...) is a solid octagon in
   the bottom-right corner with its symbol cut out (`I.corner()`).
5. **Content lines are 2 px.** Lines that are the stage's content, not its outline
   (the clipboard's text lines), stay 2 px so the 3 px board still reads as the frame.

## Decisions from the hardware rounds

Each of these came from the maintainer looking at the icon on a real keycap. The
preview and the panel disagree often enough that a rendered sheet alone did not
settle any of them.

| Icon | What it is | Why |
|---|---|---|
| All outlines | 3 px | 2 px read faint on the panel next to a solid actor. Every screen, bubble and board went to 3 px, then network, volume mixer and Click to Do did too. |
| Narrator (Win+Ctrl+N) | Speech bubble with a speaker and two sound arcs inside | A bare speaker read as volume control. The bubble says the PC is speaking. |
| Speech recognition (Win+Ctrl+S) | The same bubble with four level bars | The bars sit 3 px clear of the 3 px frame on every side; an earlier version touched it. |
| Display (Win/Super+P) | An outlined screen handing over to a second, solid screen in front of it, with a stand | A single screen with a symbol was not identifiable. This was round 2's P7. |
| Network (Win+Ctrl+F) | Three solid nodes joined in a triangle | A tree of one node and two leaves did not read. This was the simplest of a round of simple shapes (S5). |
| Clipboard history (Win+V) | 3 px board, clip, three 2 px text lines, clock corner | The lines are content, so 2 px. They sit high: 2 px below the clip, 4 px above the bottom edge. Lowering them (the first reading of "move the lines") was wrong. |
| Emoji (Win+. and Win+;) | The face fills the whole grid (`ring(0, 0, 33, 33, ch=9, w=3)`) | The mouth is computed, not drawn: every empty pixel below row 19 at least 4 px from the outline. So its gap to the border is the same at the sides, the diagonals and the bottom. |
| Folder (Ctrl+O) | Retired | It was cut off on the right, and Ctrl+O is an app shortcut now anyway. |

## Retired: the app shortcuts

Copy, cut, paste, undo, redo, select all, find, save, open, print, delete, rename
(F2), refresh (F5) and quit stay drawn in every style but are not written to the
font (`RETIRED`). Their chords mean different things in different programs, so the
built-in hints promised Ctrl+S in programs without save. The host's app overlays
draw them now, per program, where the program documents them. OS chords on the same
letters keep their hints (Win+L lock, Ctrl+Cmd+Q lock).

## Styles not chosen

`--style a|b|c|d|e` switches the whole font; the slots and names stay the same. All
five were shown side by side on contact sheets and D was picked, then tuned on
hardware (the table above). The others are kept so a future comparison starts from
real drawings:

- **a**, outline (`hint_icons.py`): authored on a 28 grid and scaled to 34, 2 px strokes.
- **b**, solid (`hint_icons_solid.py`) and **c**, bold (`hint_icons_bold.py`): heavier
  variants of the same shapes.
- **e**, retro (`hint_icons_retro.py`): 80s home-computer pixel art.

A style other than D has not had D's hardware rounds, so switching back means
redoing them.

## Comparing a change

- **Look at a before/after sheet, at 12× with row numbers, before committing.** A
  1 px move is invisible at 4–5×, and "the lines are on the same y in all four" was
  the right reaction to a 5× sheet. `compare_sheet.py` in the
  `add-polykybd-shortcut-hint` skill draws one against any git revision.
- ⚠️ **Never render variants by rewriting one module and `importlib.reload()`-ing
  it.** Python's bytecode cache is keyed by (mtime, size). A variant that changes
  `14` to `12` in the same second keeps the size, so the reload runs the OLD bytecode
  and every variant comes out identical (2026-10-09, the clipboard variants). Give
  each variant its own module name and run with `python3 -B`.
- **A re-applied unshipped bundle keeps its `content_version`, so a tester's board
  does not pick it up on connect.** Hand over the `.plyf` with
  `polyctl fontpack flash --file symbol.plyf --bundle-id 0`.
