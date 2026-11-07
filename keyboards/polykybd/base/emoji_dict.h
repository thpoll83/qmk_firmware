// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Emoji search dictionary (font-pack bundle `emojidict`): the loader and the
// keyword search of the emoji layer's search mode.
//
// The bundle is built by emoji/emoji_dict.py, whose module docstring has the
// byte layout and whose Dict.search() is the reference this search follows; the
// polykybd_emoji_dict suite checks the two agree on a built bundle. Design and
// sizes: emoji/EMOJI_SEARCH.md.
//
// Pure: no QMK, no flash access of its own. The caller passes the slot's XIP
// pointer to emjdict_load_at().

#include <stdbool.h>
#include <stdint.h>

#define EMJDICT_FLAG_DATA    0x0001u  // PlyF flags: data-only bundle, no fonts
#define EMJDICT_CODE_UNKNOWN 0xFFu    // a typed letter the fold table does not list
#define EMJDICT_MAX_CPS      2304u    // picker codepoints a dictionary may index (2122 today)
#define EMJDICT_QUERY_MAX    16u      // typed characters, spaces included
#define EMJDICT_MAX_WORDS    4u       // query words searched; later ones are ignored

// Validate the bundle at `base` (bounded by `cap`, the slot size). True for a
// valid dictionary and for the header-only wipe sentinel; *out_ver gets the
// bundle's content_version. False leaves no dictionary loaded.
bool emjdict_load_at(const uint8_t *base, uint32_t cap, uint16_t *out_ver);
void emjdict_unload(void);

// A dictionary with words is loaded (false for none and for the wipe sentinel).
bool     emjdict_present(void);
uint16_t emjdict_lang(void);       // iso_lang_country LANG_CODES index
uint16_t emjdict_cp_count(void);

// The dictionary has a letter for this typed codepoint (its fold table lists it),
// so a key with this legend can type into the query.
bool emjdict_maps_letter(uint32_t cp);

// Search for `query` (n typed codepoints; U+0020 separates words). Writes up to
// out_max codepoints of the results, starting at result number `skip`, to out[],
// and returns the total number of results. *typo (may be NULL) is set when a
// query word matched only through the edit-distance-1 pass.
uint16_t emjdict_search(const uint32_t *query, uint8_t n, uint16_t skip,
                        uint32_t *out, uint8_t out_max, bool *typo);
