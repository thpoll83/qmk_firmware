# PolyKybd v1.11.0 Emoji search

The protocol moves from 27 to 28. Update PolyKybdHost to 1.21.0 first, then this firmware. An older app still connects, but it cannot choose a search dictionary and does not wait for Eden before writing font packs.

## 1.11.0: Emoji search by keyword 🔍
Type a word on the emoji layer and the number row shows the matching emoji.

- **The mode key at Tab switches the emoji layer to search.** Letters stay at their normal positions, results fill the number row best match first, and ◀ ▶ page through them.
- **79 languages.** Words match by their start, several words narrow the result, and one typo is allowed. Japanese is typed as romaji, Korean as jamo, and Chinese as Pinyin, Zhuyin or Cangjie keys.
- **The dictionary has its own 128 KB font-pack slot.** With Auto Flash on, the app writes the English dictionary once after this update.
- ⚠️ split72 only for now; split42 does not have search yet.

## 1.10.3: The keyboard reports Eden 🌿
- **GET_ID now says when Eden owns the keycaps** (protocol 28), so app 1.21.0 waits before writing font packs. After a firmware update, one half could stay in Eden.
