# Future keyboard-layout languages — research notes

How layout support works here (important context): PolyKybd **relabels keycaps**
for the selected layout; the **OS does the actual character generation**. The
host's "Change System Input Language" switches the OS input source, and
`translate_keycode()` in `split72/keymaps/default/keymap.c` is **display-only**
(its result only ever goes to `kdisp_write_gfx_text()`, never sent as a keycode
or unicode). Consequence: for **every** script below, all shaping / conjunct
formation / matra reordering / transliteration is the **OS's** job — the
firmware never composes characters and needs **no IME**.

Adding a language therefore = three things:
1. a keycap **font** for the script,
2. a per-key **codepoint mapping** in `lang_lut.xlsx`,
3. a **host** locale → OS-input-source mapping (+ flag, which is automatic).

---

## Deferred batch (revisit later): Bengali, Thai, Telugu, Tamil

| Language | Locale | Speakers (~) | Script / block | Std layout | Noto font | New font? |
|----------|--------|-------------|----------------|------------|-----------|-----------|
| Bengali  | bn-IN / bn-BD | 270M | Bengali, U+0980–09FF | InScript (IN); Jatiyo/Bijoy (BD) | NotoSansBengali | yes |
| Thai     | th-TH  | 60–70M | Thai, U+0E00–0E7F   | Kedmanee   | NotoSansThai   | yes |
| Telugu   | te-IN  | 95M | Telugu, U+0C00–0C7F   | InScript   | NotoSansTelugu | yes |
| Tamil    | ta-IN  | 85M | Tamil, U+0B80–0BFF    | Tamil99 / InScript | NotoSansTamil | yes |

### Key efficiency: InScript is ONE unified layout across Indic scripts
The Indian-government InScript layout puts the **same phonetic letter on the
same physical key across every Indic script** (the key that is क/ka in
Devanagari is the ka of Bengali, Telugu, Tamil, …). So once **Hindi InScript**
exists in `lang_lut.xlsx`, Bengali/Telugu/Tamil InScript are the *same key map
with the per-key codepoint shifted into the target Unicode block* — scriptable.
After Hindi, the real remaining cost of each Indic language is basically just
the **font**.

### Per-language notes

**Bengali (~270M, U+0980–09FF)** — Bangladesh (~170M) + India West
Bengal/Tripura/Assam (~100M); 6th–7th most spoken worldwide. India uses
**InScript**. Bangladesh overwhelmingly uses **Avro phonetic** (a
transliteration IME on Latin keys — needs no special hardware layout), with
Jatiyo/Bijoy as the keymap-style options. Heavy conjuncts + i-kar (ি)
reordering — all OS-side. The InScript entry mainly serves the Indian market.

**Thai (~60–70M, U+0E00–0E7F)** — Layout **Kedmanee** (standard; heavy Shift
use for 44 consonants + vowels + 4 tone marks). Pattachote is a rare ergonomic
alternative. Notably **no stacked conjuncts and no Indic-style reordering** —
even the "leading" vowels เ แ โ ใ ไ are typed in visual order — so Thai input is
more linear/simpler than Indic (still OS-driven either way). New font; large
key map (base + shift both full).

**Telugu (~95M, U+0C00–0C7F)** — Layout **InScript** = Hindi InScript key map
with Telugu codepoints. Subjoined-consonant conjuncts + matras (OS shaper).
New font + mechanical InScript clone → cheap once Hindi lands.

**Tamil (~85M, U+0B80–0BFF)** — **Tamil99** is the most popular layout in
Tamil Nadu (syllabic, Tamil-specific) and worth offering *in addition to*
InScript (Tamil). Simpler abugida than Devanagari (fewer consonants, far fewer
ligatures). New font; InScript map is a clone, Tamil99 is a separate map if we
want the popular option.

---

## Shared implementation checklist
(applies to the batch above *and* the current Hindi / Persian / Urdu / Tier-C work)

1. **Font** — add `NotoSans<Script>` to `fonts/fonts.yaml` (new category),
   fetch via `fonts/dl-fonts.sh`, regenerate headers with the **pinned**
   fontconvert, then `generate_fonts.py --check`. For Persian/Urdu and
   Serbian-Cyrillic/Macedonian there is **no new font** — just widen the
   existing Arabic / Cyrillic ranges (NotoSansArabic / NotoSans are already in
   the tree).
2. **Mapping** — add 4 columns per language to `lang_lut.xlsx` `key_lut`
   (header in row 1); add any new named glyphs to the `named_glyphs` sheet (or
   inline `U"\xXXXX"`). Re-run `cog -r lang_lut.h lang_lut.c` (and `lang_lut.h`).
3. **Host** — `polyhost/lang/lang_compat.py` locale → OS-input-source map; the
   flag emoji is automatic via `langcode_to_flag()` (regional-indicator letters
   from the country code).
4. **Display nuance** — combining matras / tone marks render on a dotted circle
   ◌ on keycaps. This is expected and matches physical Indic/Thai keycaps.
5. **No firmware IME** — the OS handles all shaping / conjuncts / reordering /
   transliteration. Same architecture already used for ru/el/ar/he.
6. **Flag + language label** (language-selection layer) — every new language
   needs a country flag. Add its ISO-3166-1 alpha-2 code to `COUNTRIES=()` in
   `fonts/gen-lang-fonts.sh` **in the exact same order as `enum lang_layer`**
   (flag codepoint = `FLAG_CP_BASE 0xE000 + LANG_*` index — order MUST match or
   flags misalign), then regenerate `base/fonts/flag_fonts.h` with the pinned
   fontconvert + NotoColorEmoji (`-F` sequence option). Caveat: languages that
   share a country (e.g. Serbian Cyrillic & Serbian Latin both = RS → 🇷🇸) get
   the **same** flag and "sr" label, so they are indistinguishable on the
   selection layer — prefer a single entry per country unless the labels are
   disambiguated.

## Glyph-coverage facts (verified 2026-06-06 against `named_glyphs.h`)
- Latin-1 Supplement + Latin Extended-A fully present → **all European Latin
  layouts need zero new glyphs** (pt-BR, hr, sr-Latn, bs, sk, sl, lt, lv, et).
- Cyrillic: 96 glyphs; Mongolian Өө/Үү **present**; Serbian (ђћљњџ ј) and
  Macedonian (ѓќѕ + ljnjdžj) extras **missing** (~16 shared, easy to add).
- Perso-Arabic core (پچژگکی) and Urdu extras (ٹڈڑںہھے) **missing** — add to the
  existing Arabic font range.
- Devanagari (U+0900–097F): **0 glyphs** → genuine new font (NotoSansDevanagari).

## Regional variants to add as SEPARATE entries (NOT compat folds)

These share a *language* with an existing entry but use a **genuinely different
physical layout**, so — exactly like `pt-BR` vs `pt-PT` — they need their own
mapping column + flag, NOT a `forced_country_match.txt` fold:

| Layout | Locale / xkb | Differs from | Why separate |
|--------|--------------|--------------|--------------|
| Latin-American Spanish | es-419 / `latam` | es-ES | different symbol & dead-key positions; ~400M speakers |
| Swiss (German/French) | de-CH / fr-CH / `ch` | de-DE & fr-FR | own QWERTZ; ä ö ü reached via different keys |
| Belgian | fr-BE / nl-BE / `be` | fr-FR (AZERTY) | Belgian AZERTY differs from French AZERTY |
| British | en-GB / `gb` | en-US | £ " @ # \ ¬ positions differ |
| Canadian French | fr-CA / `ca` | fr-FR | CSA / Multilingual-Standard layout, distinct |

All are Latin-script → **no new font/glyphs**, just a new mapping column + flag.
For contrast, the *folds* that DID happen (identical keymaps, handled host-side in
`PolyKybdHost/polyhost/res/forced_country_match.txt`): Austrian→`de`,
Bosnian/Montenegrin/Slovenian→`hr`, and every Arab state→`ara` via the xkb `ara`
code. Rule of thumb: **identical keymap → fold; different keymap → own entry.**

---

# Implementation playbook — hard-won mechanics (2026-06-06)

Everything below is the "how" that cost real iterations. Read it before touching
layouts again; it will save a session.

## A. Re-cog ALL generated files (not just lang_lut)
`lang_lut.xlsx` feeds **five** cog-generated files. Adding/changing a language
means re-running cog on every one or the language is half-wired:
- `lang/lang_lut.c` + `lang/lang_lut.h` — the LUT, `poly_settings`, the
  `enum lang_layer` + `NUM_LANG`.
- `lang/named_glyphs.h` — the `#define <NAME> U"\xXXXX"` table.
- `keycode_helper.h` — the `KCL_*` keycodes (`KCL_x - KCL_ENUS == LANG_x`).
- `hid_com.c` — the `GET_LANG_LIST` response.
- `split72/keymaps/default/keymap.c` — the `_LL` language-selection layer cog
  block (the grid of `KCL_*` keys).

Command: `python3 -m cogapp -r <file>` (`pip install cogapp`).
**Symptom of forgetting:** "new languages not selectable" — only `lang_lut` was
re-cogged, so the `_LL` layer / `KCL_*` enum / HID list were stale.

## B. Editing `lang_lut.xlsx` without corrupting it (CRITICAL)
Some cells are **formulas** (e.g. `LATIN_xxxx = =TEXTJOIN(...)`) and cog reads
their **cached** values via openpyxl `data_only=True`.
- **NEVER save the workbook with openpyxl** — it discards formula caches, so cog
  then emits literal `"LATIN_0161"` tokens (build breaks) or `NULL`s. This was a
  major earlier bug.
- Edit **surgically inside the .zip**: rewrite only `xl/worksheets/sheetN.xml`,
  leaving every other byte/cache intact. **sheet1 = named_glyphs, sheet2 =
  key_lut, sheet3 = latin_sup_ex** (confirm via `xl/workbook.xml` + rels).
- Column model (both key rows and `poly_settings` rows): language columns start
  at **col B (index 2)**, **4 columns per language** = `[VAR_SMALL, VAR_SHIFT,
  VAR_CAPS, VAR_ALTGR]`. Lang index `i` → base col `2 + i*4`. Row 1 = the
  language-code header (`hi-IN`, …); a blank header cell ends the list.
- Empty cells are placeholders `<c r="A1085" s="7"/>`. Replace with a string
  cell `<c r=".." t="inlineStr"><is><t xml:space="preserve">VALUE</t></is></c>`
  or a numeric/settings cell `<c r=".."><v>-26</v></c>`. To insert mid-row,
  rebuild the whole `<row>` in column order. `named_glyphs` already has blank
  row elements out to row 1125 (dimension `A1:I1125`) — just fill them.
- **Verify after every edit:** reopen with `openpyxl(data_only=True,
  read_only=True)` and spot-check, then `git diff lang_lut.c` and confirm ONLY
  the intended cells changed — if some *other* language's accented letters turned
  into literal `LATIN_xxxx` tokens, you wiped the cache: discard and redo.

## C. cog `make_key` cell semantics (key_lut → C)
A cell becomes a C token thus: if it starts with `u"` **or** is a name present
in the `named_glyphs` sheet **or** contains multiple space-separated tokens →
emitted **verbatim** (with `u"`→`U"`); otherwise wrapped as `U"<cell>"`; empty →
`NULL`. So a bare reference like `DEVA_DC_0902` **must exist in named_glyphs** to
be emitted unwrapped. (Gotcha: a literal `"` in a cell makes `U"""` — use the
`QUOTE` named glyph instead.)

## D. Keycap display geometry — the thing that bit us 3× on ur-PK
- The per-keycap OLED is **72×40 px**, but the render buffer is **128 px wide**
  and the panel shows the **centre 72 px**: visible window = buffer
  `x ∈ [BUFFER_X, BUFFER_X+SCREEN_WIDTH) = [28, 100)` (`base/disp_array.h`:
  `SCREEN_WIDTH 72`, `BUFFER_X 28`).
- Every draw uses origin `x = 28 + h_off`, baseline `y = 23 + v_off`. So
  **`h_off = 0` is the LEFT edge of the visible area, not the centre.** Usable
  `h_off` ≈ `[0, 72]`. A glyph is fully visible iff
  `28 + h_off + xOffset ≥ 28` and `… + width ≤ 99`.
- A glyph drawn at `x < 28` is in the buffer but **off-screen** → it
  "disappears". (First ur-PK regression: a `-26` h_off put the base at x≈2.)

## E. `poly_settings` offsets + the VAR_SMALL trap
- Setting rows live in the **key_lut sheet after the key rows**, named in col A:
  `{letter.hoffset} {letter.voffset} {num.hoffset} {num.voffset} {sym.hoffset}
  {sym.voffset}` (the first col-A value starting with `{` ends the key loop and
  begins the settings loop).
- Each = a 4-tuple per language `[SMALL, SHIFT, CAPS, ALTGR]`; empty → `0`;
  literal `HIDE` → `-128` (`HIDE_KEY`, suppresses that preview).
- Routing: `KC_A..KC_Z` → letter offsets; `KC_1..KC_0` → num; else → sym.
- **VAR_SMALL is the offset of the ACTIVE glyph** — the base when unshifted AND
  the shift letter when shift is held. **Never raise VAR_SMALL to make room for
  a preview**: it clips tall letters / high marks (e.g. Arabic SHADDA vanished)
  when shift is held. Only VAR_SHIFT / VAR_ALTGR are preview-only and free.

## F. Dual base+preview rendering & generic auto-placement (2026-06-06)
`keycode_to_disp_text()` draws, for the active layer letter: the active glyph at
VAR_SMALL; an unshifted **shift preview** at VAR_SHIFT (skipped on shift/caps or
HIDE); an **altgr preview** at VAR_ALTGR. Latin langs usually `HIDE` the shift
preview; Arabic-script langs (ar/fa/ur) show base+shift (both real letters).
- Added `kdisp_gfx_text_bounds()` (`base/disp_array.c/.h`) — measures a string's
  pixel min/max x without drawing (mirrors the cursor advance).
- The shift preview is now placed **generically, glyph-width driven** (no
  per-language code): start at the VAR_SHIFT x, push right to clear the base's
  measured right edge, clamp to the window's right edge. Only a preview that
  would actually overlap/overflow moves, so other languages are untouched.
- For two glyphs too wide to ever separate in 72 px (only ur-PK `ص`/`ض`, both
  39 px), when overlap is unavoidable the **flat base is lifted and the preview
  dropped** so they read diagonally. This is in the unshifted preview path only,
  so it never moves the shift-held active glyph (see E).

## G. Combining marks → dotted circle (matras / harakat / Thai tone marks)
Isolated combining marks render invisibly or as a stray dot (nothing to attach
to). Composite each onto a **dotted circle U+25CC**, as the Unicode charts and
physical InScript keycaps do.
- Mechanism: **fontconvert `-C` composite mode** (added 2026-06-06 in
  AdafruitGFX). In `-S` sequence mode it shapes each comma-group with HarfBuzz
  and composites ALL glyphs of the group into ONE 1-bit bitmap using the GPOS
  x/y offsets (mono path; opt-in, default behaviour unchanged). Example:
  `-S "25CC 0901, 25CC 0902, …" -F0xE100 -C` → one glyph per mark at PUA
  `0xE100..`, mark correctly attached to the circle.
- Wire-up: a `fonts.yaml` entry with `sequence:` + `extra_args: ['-F0xE100','-C']`
  (devanagari category); `DEVA_DC_xxxx` named glyphs → those PUA codepoints in
  the named_glyphs sheet; remap the matra cells (VAR_SMALL/CAPS and any combining
  VAR_SHIFT) from bare `DEVANAGARI_xxxx` → `DEVA_DC_xxxx`. **Keep the fonts.yaml
  sequence order identical to the DEVA_DC_* codepoint order.** Drop any old `\v`
  control-char nudges — the composite is self-positioned.
- **PUA `0xE100+` is free in `ALL_FONTS`** (the lang-layer flags use a SEPARATE
  array at `0xE000+`, not in ALL_FONTS; IconsFont sits at low codepoints).

## H. Verify display WITHOUT hardware (the biggest time-saver)
`kdisp_write_gfx_char` is trivial to mirror: draw the 1-bit glyph bitmap at
`(cursor + xOffset, baseline + yOffset)`, advance by `xAdvance`; bitmaps are
row-major, continuous, byte-padded per glyph; `first`/`last`/`height` in the
struct. So a ~30-line Python parser of a fontconvert `.h` + the same draw loop on
a 128-wide canvas, **cropped to [28,100)** and saved as PNG (`pypng`), reproduces
**exactly** what a keycap shows. Render candidates before building. For composited
matras, prototype the GPOS placement with `uharfbuzz` + `freetype-py`, confirm
visually, then port to `-C` and re-verify by simulating the generated header.
Libs: `pip install pypng fonttools uharfbuzz freetype-py`.

## I. Toolchain in the container
- ARM: `sudo apt-get install -y gcc-arm-none-eabi binutils-arm-none-eabi`
  (skip `apt-get update` — broken distro PPAs make the chained install fail).
- qmk: venv (`pip install qmk cogapp pyyaml`), `export QMK_HOME=<repo>`, build
  `qmk compile -kb handwired/polykybd/split72 -km default` (~0.5 MB `.uf2`).
- fontconvert pinned build: `fontconvert/cmake-build-debug/` (FreeType 2.13.3 /
  HarfBuzz 2.6.7); rebuild with `cmake --build .`; committed font headers must be
  produced with it (provenance comment references `/tmp/fontconvert_pinned`), via
  `generate_fonts.py --fontconvert /tmp/fontconvert_pinned` then `--check`.
- Produce a flashable artifact: `qmk compile` writes `.uf2`; for a raw `.bin`,
  `arm-none-eabi-objcopy -O binary .build/…default.elf …default.bin`.
