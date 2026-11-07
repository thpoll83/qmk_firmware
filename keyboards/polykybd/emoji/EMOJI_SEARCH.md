# Emoji search by keyword

Status: the dictionary builder is done (`emoji_dict.py`) and the search-mode layout
is agreed. Firmware and host are not started. This note records the agreed design and the remaining steps.

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
language index (`iso_lang_country.LANG_CODES`), the alphabet, the fold table, the
stop words, the codepoint table, sorted word records (two u16 each), u16 postings
and the word strings. Bit 15 of a posting marks a word from the emoji's name, which
ranks above a keyword.

Every letter folds to exactly one base letter (`fold_letter()`): ä, á, â → a,
ß → s, ı → i, Œ → o, й → и, ё → е, ά → α, ς → σ. A wrong extra match costs
nothing, because the user picks the emoji from the results. Letters of other
scripts are only lowercased, since the Indic vowel signs are separate letters.

Words are stored one byte per letter in every script. The alphabet table lists the
letters of the dictionary in codepoint order, and letter k is stored as byte k+1.
The fold table maps every letter a user can type, upper and lower case, to the
byte of its base letter: 352 entries (1.4 KB) for German. The firmware maps each
typed letter through it with a binary search and needs no Unicode data of its own.
A letter outside the table becomes 0xFF and matches only through the typo pass.
Russian uses 57 codes: 32 Cyrillic letters after folding plus the Latin letters
of the English fallback text. `build` refuses a dictionary with more than 254
letters, which rules out CJK.

Measured sizes (128 KB slot):

| lang | bytes | slot |
|---|---|---|
| en | 67 KB | 52% |
| de | 82 KB | 64% |
| fr, pl | 74–82 KB | 58–64% |
| el | 81 KB | 63% |
| ru | 84 KB | 65% |
| ta | 39 KB | 30% |

`build` refuses a bundle larger than the slot, and a word table or posting list
past the 16-bit record fields.

## Search

`Dict.search()` is the reference the firmware follows:

1. Split the query at spaces and map each letter through the fold table. Drop the
   stop words listed in the bundle ("with", "mit"), since the index has none.
   The last word stays, because the user is still typing it ("a" starts "apple").
2. A word matches every dictionary word it is a prefix of: binary search for the
   first word not below the query, then walk forward.
3. Only when a query word has no prefix match, compare it against every dictionary
   word with edit distance 1 (insert, delete, substitute, swap).
4. Every query word must match (AND). A word with no match at all, or one that
   would leave no result, is ignored.
5. Rank: exact word in the name, exact keyword, prefix in the name, prefix
   keyword. Inside a rank, picker order. The firmware can produce this order in four
   passes over the postings with one 2122-bit "already shown" bitmap (266 B), so a
   result page needs no sorted result list in RAM.

## Search mode on the emoji layer

A mode key switches `_EMJ` between tab mode (the category tabs and emoji slots of
today) and search mode. The keys keep their base-layer positions, so typing a query
feels like normal typing.

split72, search mode:

| position | tab mode | search mode |
|---|---|---|
| Tab (`KC_NO` today) | mode key | mode key |
| letter keys | emoji slots | letters of the active language layout; other keys there blank and inert |
| top row, 12 number keys | category tabs | results 1–12, best match first |
| Esc | previous page | previous result page |
| Backspace | next page | delete the last letter |
| `\` (`KC_NO` today) | – | next result page |
| Ret, right half (`KC_NO` today) | – | send the first result |
| both Space keys | recents | space between query words |
| Del | recent | clear the query |
| rest of the bottom row | recents | recents 1–9, most recent first |

The recents shrink from 12 to 9 in search mode, since Space, Space and Del take
three of their keys; the 9 remaining keys show the 9 most recent emoji in order.
Tab mode keeps all 12. The query shows on the status OLED. A result key sends the
emoji and pushes it to the recents, as an emoji slot does. Switching modes keeps
the query. The layer opens in the mode it was last in, kept in RAM until power-off.
The base key still leaves the layer.

split42 has no free Tab key (category tab 0 is there) and no room for a results
row while typing: 36 letter keys and 6 thumbs. The mode key goes on one of the two
free thumb keys. Search is two steps there: type the query, press Enter, and the
letter keys show the results as the emoji slots do today, with the page keys for
paging. The mode key or Backspace returns to typing.

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
- Search mode as above. Letters are appended to the query on the press edge and
  never sent to the host. Hidden keys are blank and inert. The mode key is hidden
  while no dictionary is loaded. The query length (16 letters proposed) is a limit
  to agree with the maintainer first.
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
