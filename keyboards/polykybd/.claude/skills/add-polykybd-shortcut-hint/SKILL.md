---
name: add-polykybd-shortcut-hint
description: >
  Add an OS-aware keycap shortcut-hint glyph to the PolyKybd firmware — a
  display-only preview icon shown on a key while a modifier is held (e.g. Win+V
  clipboard, Cmd+Tab app-switch, Ctrl+Left word-nav). Use when asked to "add a
  hint for Win/Cmd/Super+X", "show an icon when <modifier>+<key> is held", "wire
  a shortcut preview", or to extend the wave-A/B/C OS-aware hint set in
  keycode_to_disp_overlay(). Handles glyph selection + legibility check, the
  per-glyph leading-space tuning, adding a NEW glyph (symbol font-pack bundle or
  resident IconsFont), wiring the correct OS branch, the byte-repro regenerate +
  host reship, build, and real-keycap preview. NOT for full keyboard languages
  (use add-polykybd-language) or app overlay PNGs (use generate-app-overlays).
---

# Add an OS-aware keycap shortcut-hint glyph

Hints are returned by `keycode_to_disp_overlay(keycode, state)` in
`keyboards/polykybd/poly_keymap.c` as a `U"..."` string = **N leading spaces +
the glyph codepoint(s)**. They render through the normal text path (full
`g_all_fonts` lookup, baseline-aligned), so a hint glyph must resolve in either
the **resident** set or a **shipped pack bundle**.

## 0. Decide the glyph + the OS branch

For each shortcut, pick a codepoint and which OS the chord belongs to. The branch
structure in `keycode_to_disp_overlay()`:

- `apple` (macOS): editing on **Cmd (GUI)**, word-nav on **Option+arrows**.
- non-mac (`else`): editing on **Ctrl**; window-mgmt on **GUI/Super** via the
  `wm_held` switch. Gate per-OS inside it:
  - `win_or_unknown` — Windows-only chords (Win+H/I/M/R/T/K/V/X/`,`/`.`, etc.).
  - `gnome` / `linux_any` — desktop-specific (GNOME vs KDE differ; see wave-B/C).

⚠️ Confirm the chord is **real for that OS**. e.g. dictation is **Windows-only**
(Win+H): macOS triggers it with a double-tap Fn/Ctrl — not a held GUI+letter
chord the hint engine can preview — and Linux/Android bind nothing standard.

## 1. Check the glyph renders (reject notdef)

From the **PolyKybdHost repo root** (so `tools/gfx_font.py` imports):

```bash
cd PolyKybdHost
HP=../qmk_firmware/keyboards/polykybd/.claude/skills/add-polykybd-shortcut-hint/hint_preview.py
python3 $HP --check 1F5E3 1F4DC 1F4D1 2699   # owning font + lit-pixel count per cp
```

`lit px == 0` ⇒ the glyph is **notdef** in the font that claims its range — pick
another glyph or another source (this is how 🗃 1F5C3 / 🗂 1F5C2 were rejected:
notdef in `_Util_`). Many `1F5xx` "symbol" emoji are **notdef in NotoSansSymbols2
but present in NotoEmoji-Medium** (`noto-emoji`) — source those from the emoji
font.

## 2. Tune the leading-space count (centering)

```bash
python3 $HP --sweep 1F4DC        # prints x-range per space count + a recommendation
python3 $HP --string '   ' 1F4DC --out /tmp/v.png   # render the exact firmware string
```

Hints sit **right-of-center (~x46-50** in the 72px window), NOT mathematically
centered — match the existing hints. The sweep picks the **largest space count
with `max_x <= 69`** (no clip). **Wide emoji need fewer spaces than narrow
symbol/math glyphs** (4 clipped every wide emoji at x71 this session; 3 was
right; a narrow glyph like 📑 took 4).

## 3. Named-glyph defines

Add to the **hand-defined wave section** at the BOTTOM of
`keyboards/polykybd/lang/named_glyphs.h` (after `ICON_CLOSE`), NOT the cog table
at the top (that round-trips through `lang_lut.xlsx` and strips cached formula
values headlessly):

```c
#define ICON_CLIP_HISTORY  U"\x1F4DC"   // 📜 scroll — Win+V clipboard history
```

Reference them in `poly_keymap.c` with the tuned spaces, e.g.
`case KC_V: if (win_or_unknown) return U"   " ICON_CLIP_HISTORY;`.

## 4. A NEW glyph (not already shipped) — pick the cheapest home

- **Reuse a shipped glyph** (resident or already in a pack bundle): nothing to
  generate — just steps 3 + 5. (Most hints land here.)
- **Symbol-bundle singleton** (a new mono glyph, matching the crisp hint style):
  append to `fonts/fonts.yaml` symbols **last** (highest index), e.g.
  `- {category: symbols, variant: _Dictation_, source: noto-emoji, bits: 32, ranges: [[0x1f5e3, 0x1f5e3]]}`.
  Regenerate (step 6), bump the `symbol` `content_version`, reship `symbol.plyf`.
  Thanks to the **pack_extra gidx pin** this changes **only** `symbol.plyf` now
  (flags no longer shifts).
- **A single bigger/custom glyph** (e.g. the Win+R `>_` at 16 pt): inject into the
  **resident IconsFont** `base/fonts/gfx_icons.h` at a free PUA slot (`0x9A`…) —
  it is `g_all_fonts[0]`, so adding a glyph shifts no pack index and needs **no
  reship**. Generate the glyph with the pinned `fontconvert`, append its bitmap
  bytes + a `GFXglyph` record, bump the `GFXfont` `last`. Do NOT add a whole new
  *resident font* (that prepends ahead of the pack and shifts every pack gidx).

## 5. Wire the case(s)

Add `case KC_<x>:` returns in the correct branch of `keycode_to_disp_overlay()`,
each gated on its OS predicate, with the step-2 leading spaces.

## 6. Regenerate (byte-repro) + reship

```bash
cd keyboards/polykybd/fonts
FONTCONVERT=/tmp/fontconvert_pinned python3 generate_fonts.py --check   # should be clean before
FONTCONVERT=/tmp/fontconvert_pinned python3 generate_fonts.py \
  --emit-bundles /tmp/b \
  --bundle-version symbol=N --bundle-version mideast=1 --bundle-version syllabic=1 \
  --bundle-version asia=1 --bundle-version flags=3 --bundle-version emoji=1
# cmp /tmp/b/<id>.plyf vs PolyKybdHost/polyhost/res/fontpack/<id>.plyf -> copy + bump only changed
```

⚠️ Pass **every** `--bundle-version` id or unspecified ones reset to 0. Rebuild
`bundles.json` from the firmware manifest + each `.plyf` (`size=len`,
`sha256=hexdigest()[:16]`). Use `/tmp/fontconvert_pinned` (FreeType 2.13.3) or
category headers show 1-line provenance diffs. (Full reship recipe: qmk
`CLAUDE.md` "Font pack".)

## 7. Build + preview the real keycaps

```bash
QMK_HOME=$PWD/../../.. /tmp/qmkvenv/bin/qmk compile -kb polykybd/split72 -km default
python3 $HP --string '   ' <cp> --out /tmp/key.png   # send to the user before committing
```

Build `split42` too (shared keymap). Commit firmware (poly_keymap.c +
named_glyphs.h + fonts.yaml + generated/) and, if a bundle changed, the host
(`res/fontpack/*.plyf` + `bundles.json`) — separate repos, separate commits.

## Output

Per shortcut: the chosen glyph + codepoint, owning font, lit-pixel count, tuned
space count, OS branch, and whether it needed a new glyph (and where it landed).
Always **send the user a real-keycap render** (step 7) before committing.

## Pitfalls

- **notdef renders blank.** Always `--check` lit-pixel count > 0 before choosing a
  glyph; `1F5xx` "symbols" are often notdef in NotoSansSymbols2 → use `noto-emoji`.
- **4 leading spaces clips wide emoji.** Sweep per glyph; hints are right-of-center
  (~46-50), not centered. Don't hand-pick a fixed count.
- **Don't touch the cog table.** Add named defines to the hand-defined wave section
  of `named_glyphs.h`, not `lang_lut.xlsx`.
- **`--bundle-version` resets unlisted bundles to 0.** Pass all ids; `cmp` to find
  the changed `.plyf`; only the edited bundle changes now (flags gidx is pinned).
- **Run the preview from PolyKybdHost** (so `gfx_font`/`tools` resolve) and use the
  pinned `fontconvert` for byte-repro.
- **A new resident *font* shifts every pack gidx** → full reship. For one/two glyphs
  inject into the resident IconsFont (`gfx_icons.h`, `g_all_fonts[0]`) instead.
- **Verify the chord is real for the gated OS** (e.g. mac dictation is Fn-Fn, not a
  Cmd chord — so it's Windows-only). Don't show a hint for a chord the OS doesn't bind.
