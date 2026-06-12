---
name: add-polykybd-language
description: Add one or more keyboard-language layouts to the PolyKybd firmware (a new `xx-YY` entry in the per-keycap display LUT) end to end — pick fold/clone/new-mapping, build the key column from xkb, handle fonts & named glyphs, re-cog the generated files, rebuild the region tables + flags, wire the host (folds, frozen index table), build, and verify the keycaps with oled_preview.py. Use when asked to "add a language / layout" (e.g. Pashto, Cherokee, Basque), "support xx-YY", implement a FUTURE_LANGUAGES candidate, or extend the language picker.
---

# Add a PolyKybd language layout

PolyKybd **relabels keycaps** for the selected layout; the **OS generates the
characters**. So a layout = (1) a per-key codepoint map shown on the OLEDs, (2) a
font that can render those codepoints, (3) a host hint that switches the OS input
source. The firmware never composes characters and needs no IME.

Repos (siblings): `qmk_firmware/` (firmware + LUT), `PolyKybdHost/` (host +
`tools/oled_preview.py`), `polykybd-ctnd/` (HIL rig). All PolyKybd lang files live
under `qmk_firmware/keyboards/handwired/polykybd/` (called **PK/** below).
Authoritative deep-dive: `PK/lang/FUTURE_LANGUAGES.md` (read it first).

## 0. Toolchain (once per container)

```bash
python3 -m venv /tmp/pkvenv && /tmp/pkvenv/bin/pip -q install openpyxl cogapp pyyaml qmk fonttools pillow
sudo apt-get install -y gcc-arm-none-eabi binutils-arm-none-eabi
# submodules for a build: make git-submodule  (need lib/{chibios,chibios-contrib,pico-sdk,printf,lufa};
#   note lib/printf header is at lib/printf/src/printf/printf.h)
# pinned fontconvert (only if a NEW font/flag is involved): build Adafruit-GFX-Library/fontconvert
#   (FreeType 2.13.3 / HarfBuzz 2.6.7) -> /tmp/fontconvert_pinned. Byte-repro requires it.
```

## 1. Classify each language (this decides the work)

| Kind | When | Cost |
|---|---|---|
| **fold** | OS layout is plain US-QWERTY (Indonesian, phonetic-IME langs) | empty key column → en-US fallback; host fold `xx=us` |
| **clone** | distinct layout identical to one already present (Basque↔es-ES, latam variants) | `clone_col()` of the source column; inherits AltGr legends |
| **clone + override** | a base layout + a few extra AltGr letters (Welsh, Irish, Maltese) | clone + per-key overrides |
| **new mapping** | genuinely different keymap / new script (Sami, Pashto, Inuktitut) | transcribe from xkb; maybe a new font |

Rule of thumb: **identical keymap → fold; different keymap → its own column.**

## 2. Get the layout data (be authoritative)

- **xkb is ground truth**: `/usr/share/X11/xkb/symbols/<file>`, variant via
  `awk '/xkb_symbols "<variant>"/{f=1} f{print} /^};/{if(f)exit}' <file>`.
  Examples used: `af(ps)` Pashto, `ca(ike)` Inuktitut, `no(smi)` Sami,
  `ir(ku_ara)` Sorani, `mt`/`ie`/`gb`/`ch`/`latam`.
- **⚠️ Platform caveat — xkb = Linux, matches Windows, but NOT macOS at AltGr.**
  Verified 2026-06-11 (de/fr/es vs CLDR): xkb **level-3 ≡ Windows AltGr exactly**
  (0 diffs), so AltGr legends from xkb are right on Linux *and* Windows. **macOS
  Option diverges almost completely** (de 8/11, fr 18/20, es 7/7 keys differ — it
  yields typographic/math glyphs ∞ … ‹ ¶ ª, a different layer). Base/Shift are
  platform-consistent; only the AltGr **hint** is macOS-wrong, and the firmware
  carries one legend set (no per-platform switch) — so treat AltGr as
  **Linux/Windows-canonical**. To check a locale cross-platform, diff against CLDR
  `keyboards/{windows,osx}/<loc>-t-k0-{windows,osx}.xml` (release-42; the `altR`
  keyMap and the bare-`opt` keyMap).
- Resolve named keysyms with `/usr/include/X11/keysymdef.h` (`Arabic_dad` →
  `/* U+0636 */`). `0x100xxxx` literals = `U+xxxx`. `Uxxxx` keysyms = `U+xxxx`.
- **No xkb layout** (Cherokee, Cree): use the script's standard syllabary
  arrangement and say so — the user verifies on hardware.
- **Protocol codes are fixed 2+2 chars.** ISO-639-2/3 languages need a 2-char
  **pseudo-code** stored verbatim (Hawaiian `hw`, Nahuatl `nh`, Cherokee `ck`,
  Sorani `ku-IQ`). Country = ISO 3166-1 alpha-2.

## 3. Write the column generator

Add `PK/lang/_gen_<batch>_cols.py` modelled on the existing ones
(`_gen_euam_cols.py`, `_gen_world_cols.py`). Each language = `{row: [VAR_SMALL,
VAR_SHIFT, VAR_CAPS, VAR_ALTGR]}`; rows 2–55 = keys (`KC_A`=2 … `KC_Z`=27,
`KC_1..0`=28..37, plus the punctuation rows), rows 56–61 = settings
(letter/num/sym h/v offsets). Cell = `None | ["num",n] | ["str",token]`. Helpers:
`clone_col(sheet, langs, src)` copies a source column incl. settings; overlay a
dict of per-key overrides on top.

Output `/tmp/<batch>_cols.json` (the column spec) and `/tmp/<batch>_named.json`
(new named glyphs `[[NAME, HEXCP], …]`).

### Cell semantics (cog `make_key`)
A cell becomes a C token: starts with `u"` **or** is a name in the `named_glyphs`
sheet **or** has multiple space-separated tokens → emitted verbatim (`u"`→`U"`);
else wrapped `U"<cell>"`; empty → `NULL`.

### Two traps that cost real iterations
- **NULL-base fallback**: a key with a NULL `VAR_SMALL` falls back to **en-US
  entirely** for that key — its `VAR_SHIFT`/`VAR_ALTGR` is never shown. Any key
  that carries a shift/AltGr preview **must** also set a base.
- **AltGr letter voffset / descenders**: the `_SupAndExtA_`/`_LatinExtB_`/
  `_LatinExtAdd_` fonts have `yAdvance` 44 vs the base 40, so AltGr previews draw
  ~4 px low (oled_preview models this as `gy += yAdvance - base_yAdvance`). On the
  40 px panel: pure accent vowels (á à) clear it at **voffset 9**, but
  **descenders/ogoneks (ŷ ỹ ą ę į ǫ, q, y) bottom at y≈42–43**. **Always measure
  with oled_preview `--overshoot` (§9).** Never raise `VAR_SMALL` to make room —
  it clips the active glyph.
- **`\f` per-glyph nudge — the preferred clip fix.** A form-feed (`0x0C`) in a
  cell moves the cursor **up 2 px**, so `u"\f\f" MICRO_SIGN` lifts µ 4 px while
  leaving the rest of the category untouched — how de-DE (µ ¶) and he-IL (the
  Hebrew points) keep tall AltGr glyphs on-panel. Prefer this per-glyph fix over a
  category voffset drop when only one or two glyphs spill (µ ¶ ç, Arabic چ پ ژ,
  archaic Georgian, accented descenders). It composes with a named glyph (a
  multi-token cell). The **standard AltGr V-offset is 12** (13 lands the baseline
  of the 44 px-`yAdvance` fonts exactly on the 40 px edge → a 1 px clip); a few
  letter categories sit at 9. Reserve a category-wide drop for when *most* of the
  column clips.
  ⚠️ A fast analytical (bbox) clip-scan does **not** model `\f` cursor lifts, so it
  over-reports already-nudged cells — only a real pixel render (`--overshoot`) is truth.

## 4. Fonts & named glyphs

- **Glyph already renderable?** Check the `fonts.yaml` ranges + `named_glyphs.h`.
  Latin-1 + Latin Ext-A (`_SupAndExtA_` 0xA1–0x17E) and Ext-B (`_LatinExtB_`
  0x180–0x24F) are present → most European Latin needs **0 new font work**, just a
  named-glyph row if the name is missing.
- **Extending an existing script** (Pashto→Arabic, Sorani): add a small font
  *entry* to that category in `fonts.yaml` covering only the missing codepoints
  (model: `_Sorani_`, `_PerArab_`), then `generate_fonts.py --only arabic`.
- **New script** (Cherokee, Inuktitut/Cree): download the Noto source
  (`fonts/dl-fonts.sh` pattern, `google/fonts/main/ofl/...`), verify coverage with
  `fonttools` `getBestCmap()`, add a `sources:` entry + a new `categories:` block
  + a font entry. **A new category must appear in the ALL_FONTS index
  (`gfx_used_fonts.h`)** — `--only` skips the index, so either do a full
  `generate_fonts.py` run (needs all source fonts) or add the `#include` +
  `&Font` pointer to `gfx_used_fonts.h` by hand.
- **Byte-repro**: regenerate with `/tmp/fontconvert_pinned` and the canonical
  source font; the diff on an existing header should be **only** your new block.
  Many small glyph names → auto-generate them (`[[f"CHEROKEE_{cp:04X}",
  f"{cp:04X}"] for cp in …]`, like the Ethiopic set).

## 5. Apply to the spreadsheet (CRITICAL — never corrupt it)

`lang_lut.xlsx` has formula cells whose **cached values** cog reads. **Never save
it with openpyxl** (drops the caches → cog emits literal `LATIN_xxxx`). Patch
surgically and **always regenerate from a clean base**, not in place:

```bash
cd PK/lang
cp -f /tmp/clean_lang_lut.xlsx lang_lut.xlsx              # the committed pre-batch xlsx
/tmp/pkvenv/bin/python _gen_<batch>_cols.py               # -> /tmp/<batch>_{cols,named}.json
/tmp/pkvenv/bin/python _patch_named_glyphs.py lang_lut.xlsx /tmp/<batch>_named.json
/tmp/pkvenv/bin/python _patch_xlsx.py lang_lut.xlsx /tmp/<batch>_cols.json <comma,sep,order>
```
To *modify* already-appended columns, restore the clean base + re-append with the
fixed generator (don't try to edit existing cells). Verify integrity: reopen
read-only, confirm old langs still show their cached tokens (e.g. es-ES KC_SCLN =
`N_WITH_TILDE_SMALL`, not `LATIN_xxxx`).

## 6. Re-cog the 6 generated files

`lang_lut.xlsx` feeds **six** cog files — re-cog **all** or the language is
half-wired ("not selectable" = stale `_LL`/`KCL_`/HID list):

```bash
for f in lang/lang_lut.h lang/lang_lut.c lang/named_glyphs.h keycode_helper.h \
         hid_com.c split72/keymaps/default/keymap.c corne42/keymaps/default/keymap.c; do
  /tmp/pkvenv/bin/python -m cogapp -r "$f"; done
```
If only cell *values* changed (not the language list/order), `keycode_helper.h`,
`hid_com.c`, the keymaps and `lang_layer.c` won't change — a good scope check.

## 7. Region tables, flags, host, frozen table

- **Region tabs** (`lang_layer.c` `REGION_LANGS`/`REGION_OFFSET`): regenerate with
  `_gen_region_tables.py` (mirrors the host `lang_regions.py` country→continent
  map; sort country-asc, enum-tie). Geographic outliers (Hawaiian→Oceania) need a
  `LANG_REGION_OVERRIDE` entry.
- **Flags** (`base/fonts/flag_fonts.h`): `FONTCONVERT=/tmp/fontconvert_pinned
  PYTHON=/tmp/pkvenv/bin/python bash fonts/gen-lang-fonts.sh`. Existing flags stay
  byte-identical; new ones append. (`lang_label_font.h` shouldn't change — revert
  if only its path comment did.)
- **Host** `PolyKybdHost/polyhost/res/forced_country_match.txt`: add `xx=...`
  only when the country has no native xkb layout (e.g. `lu=ch`); most resolve via
  the country code or existing folds. `lang_regions.py` already maps every ISO
  country.
- **Frozen index table** `iso_lang_country.py` (cmd-27 packed list): only a
  **pseudo-code** (non-639-1 lang) needs appending to `PRIVATE_LANGS`. It is
  **byte-identical across 3 repos** — edit one, `cp` to
  `PolyKybdHost/polyhost/services/` and `polykybd-ctnd/station/`, `cmp` to verify.

## 8. Build

```bash
export QMK_HOME=$PWD/../../../..    # the qmk_firmware root
PATH=/tmp/pkvenv/bin:$PATH qmk compile -kb handwired/polykybd/split72 -km default
PATH=/tmp/pkvenv/bin:$PATH qmk compile -kb handwired/polykybd/corne42 -km default
```
Budget: firmware runs in a **2 MB flash partition** (`FW_STAGING_OFFSET`); a build
>2 MB fails to link (intended). Currently ~0.76 MB. A `.bin` for HID-flashing:
`arm-none-eabi-objcopy -O binary .build/<target>.elf out.bin`.

## 9. Verify the keycaps WITHOUT hardware (do this every time)

Use the existing tool — **do not write another renderer**:
```bash
cd PolyKybdHost/tools
/tmp/pkvenv/bin/python oled_preview.py --lang xx-YY                 # contact sheet of all keys
/tmp/pkvenv/bin/python oled_preview.py --lang xx-YY --key KC_Q --cell-scale 5   # one key, big
/tmp/pkvenv/bin/python oled_preview.py --lang xx-YY --key KC_Q --overshoot 8 --cell-scale 8  # measure clip
/tmp/pkvenv/bin/python oled_preview.py --lang xx-YY --channels     # overlap check
```
It reads `lang_lut.xlsx` + `named_glyphs.h` + the generated fonts and reproduces
the firmware draw exactly (incl. the `yAdvance` shift). **The render keeps up to
`--overshoot` px (default 2) outside the 72×40 viewport on a red margin with any
clipped pixels in yellow** — so **bottom clipping** (the descender trap, §3) is
visible instead of silently dropped; raise `--overshoot` to see how far a glyph
spills, fix with a `\f` nudge (§3), re-render. **`--channels`** colours base=green,
Shift=blue, AltGr=red, so any **overlap** between the three shows as a mixed colour
(cyan/yellow/magenta/white). Also catch **missing glyphs** (a blank preview = font
range doesn't cover the codepoint → widen it, §4) and **redundant AltGr** (a legend
that renders the same glyph as the key's inherited Shift/Base — drop the cell, it
just draws the character twice).

Also bump the host fixture: `PolyKybdHost/tests/device/poly_kybd_mock_test.py`
(`_ALL_FIRMWARE_LANGS` + count) — the new codes append after the last one, in
GET_LANG_LIST packets of 15; run the host test suite.

## 10. Commit (per-repo branch rules)

Commit firmware (qmk), host (PolyKybdHost), and rig (polykybd-ctnd) separately on
their branches. Document the batch in `PK/lang/FUTURE_LANGUAGES.md` (a STATUS
section) and bump the `NUM_LANG`/packet note in the top `CLAUDE.md`. Don't push
unless asked.

## Pitfalls (all hit this session)
- NULL `VAR_SMALL` → whole key falls back to en-US (§3).
- AltGr descenders clip → prefer a per-glyph `\f` nudge over a category voffset
  drop (standard V-offset is 12); measure with `oled_preview --overshoot` (§3/§9).
- Redundant AltGr (renders the same glyph as the inherited Shift/Base) draws the
  character twice — drop the cell. `--channels` spots overlaps (§9).
- A blank AltGr preview = the font's range doesn't include the codepoint, even if
  the named glyph exists — widen the `fonts.yaml` range and regen (§4).
- `--only` doesn't rewrite the ALL_FONTS index — new categories need the index
  updated (§4).
- Don't openpyxl-save the xlsx; regenerate from a clean base (§5).
- Re-cog **all 6** files (§6); the frozen table must stay identical in 3 repos (§7).
- Pseudo-code only for non-ISO-639-1 languages; standard codes touch nothing in
  the frozen table.
