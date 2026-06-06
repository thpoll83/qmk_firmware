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

## Glyph-coverage facts (verified 2026-06-06 against `named_glyphs.h`)
- Latin-1 Supplement + Latin Extended-A fully present → **all European Latin
  layouts need zero new glyphs** (pt-BR, hr, sr-Latn, bs, sk, sl, lt, lv, et).
- Cyrillic: 96 glyphs; Mongolian Өө/Үү **present**; Serbian (ђћљњџ ј) and
  Macedonian (ѓќѕ + ljnjdžj) extras **missing** (~16 shared, easy to add).
- Perso-Arabic core (پچژگکی) and Urdu extras (ٹڈڑںہھے) **missing** — add to the
  existing Arabic font range.
- Devanagari (U+0900–097F): **0 glyphs** → genuine new font (NotoSansDevanagari).
