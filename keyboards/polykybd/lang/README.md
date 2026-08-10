# `lang_lut.xlsx` — the code-generation workbook

`lang_lut.xlsx` is the source for everything cog generates under
`keyboards/polykybd/`. Regenerate with **`./run_cog.sh`** (from
`keyboards/polykybd/`); it needs `cogapp` + `openpyxl`. A clean run must leave the
tree **unchanged** — `run_cog.sh` is idempotent, so any diff after running it means
the workbook and the committed headers have drifted.

Sheets, and what reads them:

| Sheet | Consumer | Generates |
|---|---|---|
| `key_lut` | `lang/lang_lut.c`, `lang/lang_lut.h`, `keycode_helper.h`, `poly_keymap.c`, `hid_com.c`, `uni.h` | the per-language keycap tables, `LANG_*`/`KCL_*` enums, the packed language list |
| `named_glyphs` | `lang/named_glyphs.h` | one `#define NAME U"\xHH"` per row (columns **A** = name, **B** = hex; C–F are human-facing helper formulas cog never reads) |
| `latin_sup_ex` | `lang/lang_lut.c` | `latin_ex_map[52][10]` — the Intl-layer latin variations, upper-case rows then lower-case rows in pairs |

## ⚠️ NEVER save this workbook with openpyxl

Every cog block loads it as **`load_workbook(..., data_only=True)`**, i.e. it reads
the **cached** result of each formula. openpyxl does not evaluate formulas, and
saving through it **drops every cached value**. The sheets are full of formulas
(`latin_sup_ex`'s whole hex grid is `=DEC2HEX(UNICODE(..))`; `named_glyphs` columns
C–F likewise), so an openpyxl round-trip silently empties the generated tables on
the next `run_cog.sh` — no error, just `NULL`s.

Edit it in **Excel or LibreOffice**, which recalculate and re-cache on save. For a
scripted one-cell change, patch the sheet XML inside the `.xlsx` zip directly and
leave every other part byte-identical (that is how the 2026-08 `A27` `i`/`I` fix and
the `named_glyphs` sync were made). Reading with openpyxl is fine — it is only
*saving* that destroys the cache.

## The `named_glyphs` sheet and `named_glyphs.h` must agree

`named_glyphs.h` is generated, but it has been hand-edited before, and the sheet
then silently stops matching. That is not cosmetic: regenerating **deletes** any
macro the sheet lacks, and those macros are used in real legends, so
`run_cog.sh` breaks the build. It happened with `ICON_OS_WINDOWS`/`_LINUX`/
`_ANDROID`/`ICON_MAC_CONTROL` (0x94–0x97, added to the header only) versus
`ICON_VOL_UP`/`_VOL_DOWN`/`ICON_BONGO_CAT` (0x89/0x8A/0x93, left in the sheet after
their glyphs were dropped from `IconsFont`).

**Add the row to the sheet, not the macro to the header.** A name here is only
useful if `base/fonts/gfx_icons.h` actually has a glyph at that codepoint — that
file is hand-maintained and is the other half of the contract. Keep the rows in
ascending codepoint order; column F shows the gap to the previous row, which makes
a missing or duplicated codepoint visible at a glance.

## `latin_sup_ex` rows are `(UPPER, lower)` pairs and the label carries the case

Column A of each pair's first row is the upper-case letter, the next pair-row is the
same letter lower-cased, and the cog keys its table by that label. A mis-cased label
therefore makes one case overwrite the other and leaves the mislabelled letter's own
row empty — which shipped for ~2 years as "`Intl+i` does nothing, `Intl+Shift+I`
types the lower-case form" (`A27` said `I`). The cog now **fails the build** on a
duplicate label rather than emitting a quietly wrong table, so a repeat is loud.

### Adding a variation: `_add_latin_variation.py`

```bash
./_add_latin_variation.py --dry-run lang_lut.xlsx S 'Ș' s 'ș'   # report, change nothing
./_add_latin_variation.py          lang_lut.xlsx S 'Ș' s 'ș'
cd .. && ./run_cog.sh
```

It appends to the first free slot of each named row and writes **both** cells the
pair needs — the character, and the `=DEC2HEX(UNICODE(..))` cell below it *with the
hex the formula would have produced*, because that cached value is what cog reads.
Only `sheet3.xml` is rewritten; every other zip entry is copied byte-for-byte, so
the caches on the other two sheets survive (which an openpyxl save would not — see
above). Always pass **both** cases in one go: the table is case-symmetric and half a
pair is the bug the section above describes.

Two things it will not do for you. There are only **`LATIN_EX_VARIATIONS` (10)**
slots, because `KC_LAT0..KC_LAT9` are the picker keys — a full row (`A I O U`, both
cases) has no room, which is what currently keeps Vietnamese `Ơ ơ Ư ư` and pinyin
`ǖ ǘ ǚ ǜ` out. And the glyph must exist in a **resident** font or the keycap draws
nothing without a font pack; check with `PolyKybdHost/tools/gfx_font.py` against
`base/fonts` before adding (Latin-1 Sup / Ext-A / Ext-B are all covered by the
`latin` category, which is `resident: true`).
