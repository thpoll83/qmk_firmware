# Emoji search by keyword

Status: the dictionary builder is done (`emoji_dict.py`). Firmware and host are not
started. This note records the agreed design and the remaining steps.

The user types a word on the emoji layer ("grin") and the result keys show the
matching emoji. The keyword index is a data-only font-pack bundle, `emojidict`. The
host flashes English by default. A localized dictionary is a manual host step.

## The dictionary bundle

`emoji_dict.py` builds it from Unicode CLDR annotations (release 48, Unicode License
v3, `dict_src/LICENSE`):

```bash
# CLDR XML -> dict_src/<lang>.tsv (committed; re-run only for a new CLDR release)
python3 emoji_dict.py extract en annotations/en.xml annotationsDerived/en.xml
python3 emoji_dict.py build en --revision 1 -o en.plyd
python3 emoji_dict.py dump en.plyd
python3 emoji_dict.py query en.plyd grin "red heart" rokcet
```

The XML files are in the CLDR repository under `common/annotations/` and
`common/annotationsDerived/`. Only the picker's codepoints (`emoji_data.h`, 2122
after duplicates are removed) are indexed. A localized dictionary uses the English
text for codepoints its CLDR file does not cover. German lacks 865 of them, mostly
dingbats, arrows and superscripts.

The binary layout is in the module docstring. In short: a PlyF header with
`flags = FLAG_DATA (0x0001)` and `font_count = 0`, then an `EmjD` header with the
language index (`iso_lang_country.LANG_CODES`), the codepoint table, sorted word
records (two u16 each), u16 postings and the folded word strings. Bit 15 of a
posting marks a word from the emoji's name, which ranks above a keyword.

Words are lowercase, with Latin, Greek and Cyrillic diacritics removed
(`fold()`). The firmware must fold the typed query the same way.

Measured sizes (128 KB slot):

| lang | bytes | slot |
|---|---|---|
| en | 65 KB | 51% |
| de | 81 KB | 62% |
| fr, fi, hu, pl | 69–80 KB | 53–62% |
| el | 105 KB | 82% |
| ru | 109 KB | 85% |

`build` refuses a bundle larger than the slot, and a word table or posting list
past the 16-bit record fields.

## Search

`Dict.search()` is the reference the firmware follows:

1. Split the query into words. Every word must match (AND).
2. A word matches every dictionary word it is a prefix of: binary search for the
   first word not below the query, then walk forward.
3. Only when a query word has no prefix match, compare it against every dictionary
   word with edit distance 1 (insert, delete, substitute, swap).
4. Rank: exact word in the name, exact keyword, prefix in the name, prefix
   keyword. Inside a rank, picker order. The firmware can produce this order in four
   passes over the postings with one 2122-bit "already shown" bitmap (266 B), so a
   result page needs no sorted result list in RAM.

## Remaining steps

Firmware:
- Slot table: split the 512 KB emoji slot into emoji `0x89000`, `0x60000`
  (384 KB, the bundle is 220 KB) and `emojidict` id 9 at `0xE9000`, `0x20000`. This
  changes `FONTPACK_LAYOUT_VERSION`. Check first that the table, `bundles.json` and
  the loader accept a slot whose id is out of offset order.
- Loader: accept a PlyF with `FLAG_DATA`. Check bounds and CRC, record the base
  pointer, append no fonts. The header-only wipe sentinel keeps its meaning.
- GET_ID: report the dictionary's language index and revision in a new block
  after `G`, never before `V`.
- Search mode on `_EMJ`: a search key, letters from the active layout appended to
  the query on the press edge and never sent to the host, results on the 12
  category-tab and 12 MRU keys, the query on the status OLED, Backspace, Enter
  (first result) and Esc. The result count and the query length are limits to
  agree with the maintainer first.
- Split: send the slave the result indices in `poly_sync_t`, not the query, so both
  halves show the same results when their bundles differ.

Host:
- Ship `en.plyd` as the default `emojidict` bundle and build it in CI from
  `dict_src/en.tsv`.
- Setting `emoji_search_lang` (default `en`). The connect flash replaces the
  dictionary only when the reported language differs from the setting or the
  revision is behind, so it never overwrites a language the user chose.
- Tray entry "Emoji search language…" and a `polyctl` subcommand that set the
  language and flash its dictionary. Gate both on the new protocol version.
