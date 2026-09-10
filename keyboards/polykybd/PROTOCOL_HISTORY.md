# HID protocol history (PolyKybd)

The per-version narrative moved out of `CLAUDE.md` on 2026-09-10. `CLAUDE.md` keeps
the version TABLE and the rules that still bind; this file is why each version did
what it did.

⚠️ **This is history. The wire facts here are true of the version they describe, not
necessarily of today** — read `CLAUDE.md` and the code first, and treat a rationale
here as the reasoning of the day it was written. Several entries explain a decision
by contrast with a neighbouring command, and those contrasts are the part worth
reading before you change either one.

---

- **`PROTOCOL_VERSION`** (`config.h`, reported in the GET_ID string) gates host
  features. **v2** added `GET_LANG_LIST_PACKED` (cmd `27` / `0x1b`): the language
  list as a count byte + one `(ISO 639-1 idx, ISO 3166-1 alpha-2 idx)` **2-byte
  pair per language** instead of the 4 ASCII chars of cmd `0x08` — it halves the
  emitted bytes/lang and the report count. As of the **P2-only cleanup**, cmd `27`
  is the **only** language-list command: the legacy ASCII cmd `0x08` has been
  **retired and now NACKs** (`P\x08!`), dropping its ~570 B `.rodata` table. The
  host (protocol ≥ 2) uses cmd `27` exclusively with **no ASCII fallback**, and
  firmware older than v2 is unsupported; the rig asserts cmd `0x08` NACKs. The
  index↔code tables are the **frozen, append-only** `lang/iso_lang_country.py`
  (see "Language list encoding" below). **v3** made `SEND_OVERLAY_MAPPING`
  (cmd `21`) **silent** — no per-chunk ACK, matching the other bulk overlay
  commands (`0x0A`, `0x10`/`0x11`, `0x12`/`0x13`). The old ACK was informationless
  (always `.`), discarded unread by the host, and arrived only after the blocking
  UART bridge to the slave — escaped ACKs were the main source of stale replies
  the host had to drain. The host (protocol 3) no longer drains after mapping
  sends; ordering for `enable_overlays` (case 11) is preserved because HID
  reports dispatch sequentially and the bridge completes before case 21 returns.
  **v4** added `GET/SET_IDLE_STYLE` (cmd `28` / `0x1c`): selects the idle
  (anti-burn-in) display style — payload `0xFF` queries (reply byte = current
  style), else sets it (`0` = legacy pulse, `1` = jitter); out-of-range NACKs.
  Persisted in `poly_eeconf_t.idle_style` (flushed at the next suspend/store) so
  it survives reboots. The host (PolyKybdHost) toggles it over this command; the
  rig has a v4-gated round-trip HIL test. See "Idle anti-burn-in styles" below.
  **v5** added the brightness flags (`SET_BRIGHTNESS` cmd 13 payload byte: volatile /
  host-auto). **v6** appends a **per-bundle font-pack version block** to the `GET_ID`
  (cmd 6) reply — AFTER the NUL-terminated id string: `['V'][count][u16 little-endian
  content_version × count]` in bundle-slot order. The host reads it to flash only the
  font-pack bundles the keyboard is missing/behind on (no extra query); older hosts
  stop at the NUL and ignore it. See "Font pack" below. **v9** added
  `GET/SET_GLYPH_SCRIPT` (cmd `30` / `0x1e`): a glyph-script **override** that swaps
  the language-layer letter/digit legends for an alternative script (`0` = standard/off,
  `1` = Tengwar), leaving overlays and OS-hints untouched. `0xFF` queries (reply byte =
  current script), else sets it; out-of-range NACKs. Persisted in
  `poly_eeconf_t.glyph_script`, synced via `poly_sync_t.glyph_script`. The Tengwar
  glyphs ship in a new **`fantasy`** font-pack bundle (the host flashes it on connect);
  with no bundle the override falls back to Latin. See "Glyph-script override" below.
  **v10** makes the glyph script an **open-ended index** and ships 9 more scripts,
  values `2..10`: Elder Futhark runes, Aurebesh, Standard Galactic Alphabet,
  Cirth/Angerthas, IBM VGA/CP437, Commodore 64, Amiga Topaz, APL, Braille — all in the
  (regrown) `fantasy` bundle (`content_version` bumped 1→2). The wire format is unchanged
  (one script byte); the semantic change is that the firmware now **accepts any index
  `0..0xFE`** — an index it doesn't know, or whose font isn't flashed, renders the normal
  legend instead of NACKing. This **decouples "add a font face" from the protocol**: within
  v10 the script set can grow freely (the host may offer more scripts than a keyboard has;
  older keyboards degrade gracefully), so **adding scripts never bumps the protocol again** —
  only a real wire/semantic change would. `0xFF` stays the query sentinel.
  **v11** reframes the **plain (uncompressed) overlay upload** (cmd `10` / `0x0A`): `modifier`
  and `segment` now share **one** header byte — `(segment << 4) | (modifier & 0x0F)` — so the
  header is 4 bytes (`id, cmd, keycode, packed`) and a full 60-byte segment fits the 64-byte
  report **exactly**. The pre-v11 layout carried modifier and segment in *separate* bytes (5-byte
  header), leaving only 59 bytes for a 60-byte segment, so the firmware `memcpy`'d 60 bytes and
  read **1 byte past the report** — harmless on the no-MMU RP2040 but the last byte of each
  segment was undefined (the old FW-7 finding; fixed in the wire format instead of a bounce
  buffer). The firmware unpacks the byte in `hid_com.c` case 10 *before* `set_fragment_context_key`,
  so `adjust_overlay_idx_to_mod` is unchanged; **compressed (`0x10`/`0x11`) and ROI (`0x12`/`0x13`)
  paths are untouched** (their headers already fit).
  **v13** adds `GET/SET_GLYPH_SIZE` (cmd `34` / `0x22`): the size a key's MAIN legend
  is drawn at — `0` small (the original 27 px face), `1` medium, `2` large; `0xFF`
  queries. Persisted in `poly_eeconf_t.glyph_size`, synced via `poly_sync_t.glyph_size`;
  also reachable from the board via `KC_GLYPH_SIZE` on the settings layer.
  ⚠️ **Its range is CLOSED and an unknown value NACKs — the deliberate OPPOSITE of the
  glyph script's open-ended index one command over, and that asymmetry is the thing to
  understand before "fixing" either.** An unknown SCRIPT index falls through to the
  normal legend, so accepting it costs nothing and buys the host freedom to ship faces a
  keyboard lacks. A SIZE names a rendering TIER whose relocation base and baseline the
  firmware must know, so accepting an unknown one would store, sync and persist a
  setting that silently renders small. The two HIL tests assert opposite things about
  their neighbouring commands on purpose (`test_glyph_size_round_trip` /
  `test_glyph_script_expansion`). See "Keycap legend size" below.
  **v14** adds `GET_LAYER_NAMES` (cmd `35` / `0x23`): a read-only reply of
  `[total][count]` followed by `count` NUL-terminated ASCII names of at most 8 chars,
  split across as many reports as they need (54 bytes / one report today). `total` is
  the whole payload length, that byte included.
  The count is deliberately the SAME `DYNAMIC_KEYMAP_UPDATE_MAX_LAYER_COUNT` that
  `id_dynamic_keymap_get_layer_count` already answers with — the host editor sizes
  its tab strip from that command and labels the tabs from this one, so two counts
  could let it draw a tab it has no name for. See "Layer names over the wire" below.
  **v15** adds **macros**: `MACRO_INFO` (cmd `36` / `0x24`, read-only — count, label
  stride, capacity u16, bytes-used u16), `MACRO_BODY` (cmd `37` / `0x25`, windowed
  read/write of the shared body buffer: `data[2]` 0 read / 1 write, `data[3..4]` offset
  LE, `data[5]` count, `data[6..]` bytes) and `MACRO_LABEL` (cmd `38` / `0x26`,
  `data[2]` id, `data[3]` 0xFF query else length, `data[4..]` text). All three sit
  behind ONE host feature gate — a host that could read the info header but not the
  bodies would render an editor over data it cannot fetch. See "Dynamic macros" below.
  **v17** adds the **VOLATILE flag** to `SET_UNICODE_MODE` (cmd `20`, `data[3]`):
  non-zero applies the mode in RAM only, leaving EEPROM alone. It exists because at
  Windows logon the host cannot tell "WinCompose is not installed" from "WinCompose
  has not started yet" — so it applies its early reading volatile (plain `Windows`
  IS how the keyboard should type while WinCompose is absent) and re-asserts it
  persistently once it can tell the two apart. Without it, every logon on a
  WinCompose machine wrote `Windows` and then `WinCompose` back over it.
  ⚠️ **The wire change is backwards-compatible in one direction only.** An older
  HOST sends a zero-padded report, so `data[3]` reads 0 = persist — fine. An older
  FIRMWARE ignores `data[3]` and would silently STORE a mode the caller asked not to
  store, which is precisely the transient value the flag exists to keep out of
  EEPROM — so the host gates it (`FEATURE_MIN_PROTOCOL["unicode_mode_volatile"]`) and
  falls back to withholding the ambiguous reading entirely.
  ⚠️ QMK has **no `set_unicode_input_mode_noeeprom()`**; `unicode_config` is `extern`
  and `unicode_input_mode_set_kb()` is the notification the keycap legend rides on,
  so `apply_unicode_mode()` in `hid_com.c` is the persisting path minus one call —
  **no upstream patch**. Note the persisting path never needed help: QMK's
  `eeprom_update_byte` already skips a write when the byte matches, so re-asserting
  the SAME mode has always been free; only the transient wrong value is new.
  ⚠️ **A QMK `*_set_user` hook is a NOTIFICATION, never a setter — and calling one
  to CHANGE state fails in the quietest possible way: the UI moves and the
  behaviour does not.** `unicode_input_mode_set_user()` is what QMK fires *from*
  `set_unicode_input_mode()`, and our override of it (`poly_keymap.c`) does exactly
  one thing: mirror the value into `local_state->unicode_mode` so the language
  layer's Mac/Lnx/Win/WinC/BSD keycaps can draw their ON/OFF switch. Cmd 20 called
  it directly for years, so a host push relabelled those keys while
  `unicode_config.input_mode` — which decides how codepoints are actually typed —
  never moved. Field report 2026-09-08: the layer read **Win ON** at startup while
  emoji still worked (i.e. the keyboard was really in WinCompose mode), and pressing
  the Win key — the one path through the real setter — made behaviour follow the
  legend and broke emoji. **The tell is a state whose display and effect disagree**;
  when you find one, check whether the write went through the setter or the
  callback. The same shape applies to every `*_set_user` QMK exposes, so grep for
  one being called rather than implemented.
  **v18** adds `GET/SET_AI_STATE` (cmd `40` / `0x28`): the agent status the AI key
  wears — `0` off, `1` idle, `2` working, `3` attention; `0xFF` queries. Stored in
  RAM ONLY and synced via `poly_sync_t.ai_state`, deliberately not persisted: a
  "needs you" light that survived a reboot would be claiming something about a host
  process that is gone. ⚠️ Its range is **CLOSED and an unknown value NACKs**, like
  cmd 34 and unlike cmd 30 — every value names a colour the RGB indicator paints and
  a word the keycap spells, so accepting an unknown one would store a setting that
  shows nothing. The status light also FADES: green (idle) and blinking red
  (attention) go out a minute after the state last changed, while breathing amber
  (working) stays for as long as it takes — the curve is pure in `base/ai_light.h`
  (`make test:polykybd_ai_light`).
  ⚠️ **It claimed v17 first and had to be renumbered.** #278's volatile unicode
  mode took 17 on `PolyKybd` while this branch was open, so two unrelated features
  briefly both called themselves v17 — the number is allocated by whatever MERGES
  first, not by whichever branch wrote it down first. Nothing catches this: both
  sides build, both sides' tests pass, and the collision only shows up as a host
  that gates the wrong feature on the wrong number. **Re-check `PROTOCOL_VERSION`
  against the base before merging any long-lived protocol branch.**
  **Bump `FW_VERSION` +
  `PROTOCOL_VERSION` (config.h) and `__protocol__` (PolyKybdHost `_version.py`) in
  lockstep.** ⚠️ The old note here said "the host connect gate is exact-match"; it is
  not, and has not been for a while — the host connects to any protocol `>=
  MIN_SUPPORTED_PROTOCOL` and gates each feature separately through
  `FEATURE_MIN_PROTOCOL` (see `PolyKybdHost/CLAUDE.md`). So forgetting the bump no
  longer rejects the keyboard; it silently leaves the new feature disabled, which is
  quieter and worse.
