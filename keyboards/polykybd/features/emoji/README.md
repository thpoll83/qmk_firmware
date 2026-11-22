# features/emoji/

The emoji layer (`_EMJ`): category tabs, pages of emoji slots, the recent-emoji keys and
the keyword search mode. It also has the Python tools that build the emoji search
dictionary, which the host flashes as the `emojidict` font-pack bundle.
`keys_com/poly_keymap.c` calls `emj_display_text()` for the legends and
`emj_process_keycode()` for the keys. `lib/split_link/split_sync.c` applies the master's
state on the slave, and `features/anim/demo_mode.c` uses the layer in its playlist.

## Files

- `emoji_layer.c` / `emoji_layer.h`: the layer. `emj_display_text()` returns the legend
  of a tab, page key or slot. `emj_process_keycode()` switches category and page and
  sends the emoji. `emj_draw_tab_indicator()` / `emj_draw_tab_bottom()` mark the active
  tab. The search mode (`KC_EMJ_MODE`, `emj_search_active()`) types a query with the
  letter keys of the active language (`build_slot_letters()`) and shows the results on
  the 12 tab keys (`EMJ_SEARCH_PAGE`). The search itself is `base/emoji_dict.c`.
- `emoji_data.h`: the picker's codepoints, one `static const uint32_t emj_catN[]` per
  category, and the table `EMJ_CATEGORIES[]` (12 categories, `EMJ_NUM_CATEGORIES`).
  `emoji_dict.py` reads this file to decide which codepoints to index.
- `emoji_dict.py`: builds the search dictionary. Commands: `extract` (CLDR XML to
  `dict_src/<id>.tsv`), `build` (one `.tsv` to a `.plyd` bundle), `build-all` (every
  dictionary in `dict_src/dictionaries.tsv` plus `manifest.json`), `dump`, `query` (the
  reference search that the firmware search copies) and `vectors` (reference results
  for the firmware unit test). The bundle layout is in the module docstring.
- `emoji_romanize.py`: writes keywords as the keys that type them, for `extract
  --input`: kana to romaji, and Chinese to pinyin, Zhuyin or Cangjie (from Unihan).
- `render_search_keys.py`: draws the split72 emoji layer in search mode for each base
  layout as an HTML page, for the docs site's emoji-search picture. It parses
  `split72/keymaps/default/keymap.c`.
- `dict_src/`: data, no code. One `<id>.tsv` per dictionary (codepoint, name, keywords,
  from Unicode CLDR release 48), `dictionaries.tsv` (the numbered list of dictionaries
  and their revisions) and `LICENSE` (Unicode License v3).

## Core concepts

- The master handles the keys. The slave gets `emj_category`, `emj_page`,
  `emj_query[16]` and `emj_search[4]` in `poly_sync_t`, and runs the same search on its
  own copy of the dictionary.
- Persisted: the search-mode choice (`poly_eeconf_t.emj_search_mode`, through
  `get/set_emj_search_mode()` in `lib/system/state.c`) and the recent emoji
  (`mru_emoji`, through `keys_com/mru.c`). The query is cleared when the layer is left.
- The dictionary is a data-only PlyF bundle (`FLAG_DATA`, no fonts) in font-pack slot 9,
  with a 128 KB limit (`SLOT_SIZE` in `emoji_dict.py`). Without it, the search mode key
  is blank and does nothing.
- Per variant: `EMJ_SLOTS_PER_PAGE` is set in the keymap's `config.h` (38 on split72, 18
  on split42). split72 also sets `EMJ_SEARCH_RESULT_OF_CAT` for its tab order.
- Tests: `make test:polykybd_emoji_dict` builds bundles with `emoji_dict.py` and
  compares `base/emoji_dict.c` with the Python reference search.
  `dev/tools/tests/emoji_dict_test.py` (Python `unittest`) checks the bundle layout,
  the 128 KB limit and some searches. `emoji_layer.c` has no unit test.

## Important

- ⚠️ `extract` writes `dict_src/<id>.tsv` again from CLDR and replaces every hand edit.
  Do not run it on an edited dictionary.
- ⚠️ After you edit a dictionary, increase its `revision` in `dict_src/dictionaries.tsv`
  (1 to 254; 255 is for `build --user`). The host flashes only a dictionary whose
  revision differs, so without this no keyboard gets the change.
- ⚠️ `dictionaries.tsv` is append only. A dictionary's number is its line number, and the
  bundle's `content_version` is `(number << 8) | revision`.
- ⚠️ `render_search_keys.py` copies the key rules of `build_slot_letters()`. Change it
  together with `emoji_layer.c`.
- ⚠️ `emj_display_text()` returns a pointer to one static buffer, valid until the next
  call.

## Related

- [`../../docs/features/emoji-search.md`](../../docs/features/emoji-search.md): the dictionary, the search and how to edit a dictionary
- [`../../docs/features/emoji-missing-glyphs.md`](../../docs/features/emoji-missing-glyphs.md)
- [`../../docs/display/font-pack.md`](../../docs/display/font-pack.md): the font-pack slots
