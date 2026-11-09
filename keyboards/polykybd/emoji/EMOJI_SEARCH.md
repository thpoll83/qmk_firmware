# Emoji search by keyword

Status: prototype on split72. The dictionary builder (`emoji_dict.py`), the
firmware loader and search (`base/emoji_dict.c`, suite `polykybd_emoji_dict`), font-pack
layout v4 with the `emojidict` slot, and the search mode on `_EMJ` are in place.
PolyKybdHost ships all 79 dictionaries and flashes the one its `emoji_search_dict`
setting names (tray: Selected Language → Emoji Search Language). Not done: split42's
two-step search. To flash a dictionary without the host:

```bash
python3 keyboards/polykybd/emoji/emoji_dict.py build en -o en.plyd
polyctl fontpack flash --file en.plyd --bundle-id 9
```

The user types a word on the emoji layer ("grin") and the result keys show the
matching emoji. The keyword index is a data-only font-pack bundle, `emojidict`. The
host flashes English by default; the user picks another language in the tray.

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

A localized dictionary also indexes English words, so a German speaker can type
"smile": every English name word, and every English keyword that names at least 3
emoji (`EN_KEYWORD_MIN_EMOJI`: "smile", "happy", "love", not one-off words). They
have no name bit, so the language's own name words rank above them. All English
keywords would not fit: Russian was 133.5 KB.

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

Words are stored as the keys that type them. Korean is split into the conjoining
jamo the ko-KR keys show (2-set layout): 한 → ᄒ ᅡ ᄂ, a compound vowel or final
as two keys, and a Shift double (ᄁ ᄈ ᅤ ᅨ) folded to its base key: 26 codes.

Japanese and Chinese words are written as the Latin or Zhuyin keys that type them
(`emoji_romanize.py`, `extract --input` through the dictionary id): ja-JP's letter
keys show Latin letters, so kana keywords become romaji (スマイル → sumairu; kanji
keywords have no reading and are dropped, 69% of the picker's emoji keep at least
one kana keyword). Chinese uses Unihan's kMandarin and kCangjie, first reading,
tones dropped. One language can have several dictionaries, told apart by country
and input method in the header's variant field: the id is `lang[-CC][@method]`.

| dictionary | letter keys | example |
|---|---|---|
| ja@romaji | Latin | スマイル → sumairu |
| zh-CN@pinyin | Latin | 笑脸 → xiaolian |
| zh-TW@zhuyin | Zhuyin | 笑脸 → ㄒㄧㄠㄌㄧㄢ |
| zh-HK@pinyin | Cangjie radicals, by key | 火 → huo |
| zh-HK@cangjie | Cangjie radicals | 笑 → ⽵⽵⼤ (hhk) |

```bash
python3 emoji_dict.py extract zh-HK@cangjie --unihan <Unihan dir> \
    annotations/zh_Hant_HK.xml annotations/zh_Hant.xml \
    annotationsDerived/zh_Hant_HK.xml annotationsDerived/zh_Hant.xml
python3 emoji_dict.py build zh-HK@cangjie -o zh-HK@cangjie.plyd
```

Words are stored as 6-bit letter codes (format 2), the same for every
dictionary: code k+1 is alphabet letter k, code 0 ends a word, and each word starts
on a byte (`pack_word()`), so the 16-bit word records and the binary search are
unchanged. A dictionary holds only words its language's keyboard can type in
search mode: `typeable_letters()` reads the unshifted legend of every key from
`lang/lang_lut.c` (the number row counts, since a letter there is moved to a free
key; Esc, Tab, Backspace, Enter and Space do not). English words are kept in
every dictionary: on a Latin layout (German) as written, on a layout without
Latin letters as the keys that type them by position, since those keyboards print
the Latin letters too or switch with Han/Eng (Russian "smile" is stored as
ыьшду, Korean "heart" as ᅩᄃᄆᄀᄉ). The most
letters any layout types is 41 (zh-TW), inside the 63 codes. A converted
dictionary maps each Latin letter to the legend on the key that types it
(`key_remap()`): zh-HK's keys show the Cangjie radicals, so Cangjie `a` is ⽇.

Measured sizes (format 2, 128 KB slot): all 79 dictionaries CLDR has data for
fit.

| dictionary | size | slot |
|---|---|---|
| en | 62 KB | 48% |
| de | 94 KB | 73% |
| ru | 97 KB | 75% |
| ko | 96 KB | 75% |
| cs (largest) | 121 KB | 94% |
| ja@romaji | 63 KB | 49% |
| zh-TW@zhuyin | 80 KB | 62% |
| zh-HK@cangjie | 110 KB | 86% |

All dictionaries are listed, numbered and named in `dict_src/dictionaries.tsv`
(append only). A bundle's content_version is `(number << 8) | revision`, which
GET_ID's version block reports, so the host can tell which dictionary a keyboard
holds without a command of its own. `emoji_dict.py build-all <dir>` writes every
bundle and the host's `manifest.json`.

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
| Tab (`KC_NO` before) | mode key: 🔍 with an upright toggle, round knob down (off) | 🥳 with the knob up (on) |
| Fn, below Tab (`KC_NO` before) | blank, inert | Clear: empty the query |
| letter keys | emoji slots | letters of the active language layout; other keys there blank and inert |
| top row, 12 number keys | category tabs | results 1–12, best match first |
| Esc (top left) | previous page | previous result page |
| top right | next page | next result page |
| right half, row 3, column 0 (slot 31) | emoji slot | Backspace (Space on Qwerty) |
| right half, row 3, column 1 (slot 32) | emoji slot | Space (blank on Qwerty) |
| `\`, below the top-right key (`KC_NO` before) | blank, inert | Backspace on Qwerty, blank otherwise |
| left half, row 3, inner end (slot 18, above the innermost thumb) | emoji slot | Space |
| bottom row | recents | recents, all 12 |

Backspace sits above the right innermost thumb key (Enter on Stag!), Space beside
it. When the base layout has Backspace on the top-right key (Qwerty), Backspace
moves one row down to the `\` position and Space takes the key above the thumb. None of these keys is a letter on any of the five base layouts in
English.

A letter of the active language whose key has a search function (Space,
Backspace, a result, a recent) moves to the free emoji slot nearest to where its
key sits on a standard keyboard (`[` one key right of P, `.` two right of M), on
the same half as its own key. When that neighbour is on the other half, the
distance is measured from the key itself; a letter never changes sides. Only a letter no other key can type moves: Russian х
and ю on Qwerty do, French é does not (e finds the same words, see `fold()`).
`build_slot_letters()` does this in three passes. With Russian, Qwerty moves х
under з and ю to the end of row 3, Neo moves ъ, Workman ъ and ё, and all 33
letters can be typed.

The letters are read from the base layout when search mode starts, from the
stored keymap, so a remapped layout is followed too. There is no Enter or Delete
(a result key sends its emoji), and the arrows keep their tab-mode meaning. The
query shows on the status OLED. Switching modes keeps the query, and leaving the
layer clears it (the mode stays).

split42 has no free Tab key (category tab 0 is there) and no room for a results
row while typing: 36 letter keys and 6 thumbs. The mode key goes on one of the two
free thumb keys. Search is two steps there: type the query, press Enter, and the
letter keys show the results as the emoji slots do today, with the page keys for
paging. The mode key or Backspace returns to typing.

## Firmware (prototype)

- Layout v4 (`fonts/fonts.yaml`): emoji is a 384 KB slot that still advances 512 KB
  (`reserve_kb`), and `emojidict` (id 9, `kind: emojidict`) is placed at `0xE9000`
  with `offset_kb`. No other slot moves; `compute_bundle_layout()` refuses an
  overlap. `tools/tests/fontpack_layout_test.py` pins it.
- `fontpack_load()` hands slot 9 to `emjdict_load_at()`, like the icon library: a
  PlyF with `FLAG_DATA` and no fonts, or the wipe sentinel. COMMIT's slot-present
  gate therefore works unchanged, and the slot's version is in the GET_ID `V` block
  (10 entries now).
- Search RAM: 864 B of scratch (candidate bitmap + 2-bit ranks) and about 300 B of
  search state. The DOOM monolith links with 3968 B of headroom locally.
- Search state is synced in `poly_sync_t` (`emj_query[16]`, `emj_search[4]`). The
  slave runs the same search on its own dictionary copy, and its sync handler only
  marks the letter map stale; the map is rebuilt from the main loop.
- The letter keys type the legend `translate_keycode()` gives the base layer's key
  at the same position, in the active language. A key types only when the
  dictionary's fold table lists that letter.

## Editing a dictionary

Every dictionary is a text file, `dict_src/<id>.tsv`, and any text editor can change
it. One line per emoji, three columns separated by a TAB:

```
1F600	grinning face	cheerful | cheery | face | grin | grinning | happy
```

- Column 1 is the codepoint in hex. Only codepoints of the emoji picker
  (`emoji_data.h`) are used; a line for any other codepoint is ignored. An emoji
  without a line uses the line from `en.tsv`.
- Column 2 is the name. Its words are searchable and rank above keywords.
- Column 3 is the keywords, separated by ` | `. A keyword may have several words.
- Lines starting with `#` are comments. Save the file as UTF-8.
- A spreadsheet program can open the file as tab-separated text. Save it back as
  tab-separated UTF-8 text without quotes. A quoted codepoint stops the build.
- `ja@romaji`, `zh-*@pinyin`, `@zhuyin` and `@cangjie` hold the keys a user types
  (romaji, pinyin, Zhuyin or Cangjie codes), not the native script.
- A word with a letter that no keyboard layout of that language can type is dropped
  at build time. Upper case and accents do not matter: the search folds both.
- Common English keywords are added to every other dictionary at build time, so a
  file does not need them.

⚠️ `extract` writes the file again from CLDR and replaces every hand edit. Do not run
it on an edited dictionary.

After an edit:

1. Increase the dictionary's `revision` in `dict_src/dictionaries.tsv` (1 to 255).
   The host flashes a keyboard whose dictionary revision differs from the shipped
   one, so without this step no keyboard gets the change.
2. Check it builds and fits the 128 KB slot, and try a few searches:
   ```bash
   python3 emoji_dict.py build de -o /tmp/de.plyd
   python3 emoji_dict.py query /tmp/de.plyd lachen herz
   ```
3. Rebuild all dictionaries and copy them into the host checkout:
   ```bash
   python3 emoji_dict.py build-all /tmp/emojidict
   cd ../../../../PolyKybdHost && python3 scripts/import_emojidict.py /tmp/emojidict
   ```
4. Commit `dict_src/` here and `polyhost/res/emojidict/` in PolyKybdHost.

### Your own dictionary, without a new host release

Build the edited file with `--user` and point the host at it:

```bash
python3 emoji_dict.py build de --user -o ~/my-de.plyd
polyctl emoji-dict file ~/my-de.plyd       # or tray: Emoji Search Language → Own dictionary file...
```

- `--user` sets the revision to 255, which no shipped dictionary uses, so the host
  always flashes the file in place of a shipped one, and the reverse.
- After the next edit, rebuild and choose the same file again. Choosing the file
  that is in use flashes it again; its version is the same for every user build.
- `polyctl emoji-dict set <id>` or a language in the tray menu returns to a shipped
  dictionary. `polyctl emoji-dict file --clear` returns to the selected one.
- A file the host cannot read or that is not a dictionary is not flashed, and the
  keyboard keeps what it has. `polyctl emoji-dict status` shows why.

A new dictionary is a new last line in `dictionaries.tsv` (the list is append only,
because a dictionary's line number is part of its version) plus its `dict_src/<id>.tsv`.

## Remaining steps

Firmware:
- split42: the two-step search and a mode key on a free thumb key.
- The query length (16 characters) is a limit to agree with the maintainer.
- The slave searches its own copy, so two halves with different dictionary versions
  show different results. Syncing the result indices instead costs 48 B more.
