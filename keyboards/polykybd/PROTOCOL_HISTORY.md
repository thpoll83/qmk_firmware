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
  **v18** adds the **idle TIMEOUT** (cmd `40` / `0x28`): `data[2]` 0xFF queries,
  otherwise it is a preset index from `enum poly_idle_timeout`
  (`base/idle_timeout.h`) — 15 s / 30 s / 45 s / 1 min / 2 min / 5 min. The reply is
  `data[3]` = the preset and `data[4..5]` = its duration in SECONDS, little-endian.
  It replaces what was the compile-time `FADE_OUT_TIME`, 2 minutes on every board,
  which is `IDLE_TIMEOUT_2MIN` and the default — so an untouched keyboard behaves
  exactly as it always did.
  - ⚠️ **The SET range is CLOSED, the deliberate opposite of the glyph SCRIPT one
    command family over (v10).** An unknown script degrades to the normal legend, so
    accepting it costs nothing and lets the host offer faces a keyboard lacks; an
    unknown timeout would be stored, synced and persisted while the board silently
    resolved it to some other duration. Same reasoning as v13's `GlyphSize`.
  - **The reply carries SECONDS for exactly one reason**: a firmware NEWER than the
    host can add a preset, and the menu should read "10 min" rather than "preset 6".
    That is the only forward-compatibility concession — reading is open, writing is
    not, and the two are not in tension because the host can only offer what it can
    also name.
  - ⚠️ **TURN_OFF_TIME is NOT scaled by it.** They answer different questions: when
    the screensaver starts, and when the panels give up entirely (10 min, still
    fixed). `state.c` `_Static_assert`s per preset that the longest one still leaves
    `FADE_TRANSITION_TIME` inside that deadline — the housekeeping chain tests the
    fade branch before the suspend branch, so a preset past it would reach suspend
    having never entered the idle style at all, and every `IDLE_STYLE_*` would
    silently do nothing.
  - **Persisted as the enum BIASED BY ONE** (`poly_eeconf_t.idle_timeout`), so a
    byte reading 0 — what wear levelling hands back for a byte no build ever wrote —
    is unambiguously "never chosen". That deliberately replaces a second sentinel
    byte of the `idle_style_fmt` kind: that one had to exist because `PULSE` is 0 and
    an explicit choice was indistinguishable from an unwritten byte. Biasing removes
    the collision at the source, costs one byte instead of two, and means a future
    change of the default cannot overwrite a real choice.
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
  **Bump `FW_VERSION` +
  `PROTOCOL_VERSION` (config.h) and `__protocol__` (PolyKybdHost `_version.py`) in
  lockstep.** ⚠️ The old note here said "the host connect gate is exact-match"; it is
  not, and has not been for a while — the host connects to any protocol `>=
  MIN_SUPPORTED_PROTOCOL` and gates each feature separately through
  `FEATURE_MIN_PROTOCOL` (see `PolyKybdHost/CLAUDE.md`). So forgetting the bump no
  longer rejects the keyboard; it silently leaves the new feature disabled, which is
  quieter and worse.

## HID protocol (host → firmware)
- 64-byte raw HID reports; byte 0 = Report ID, byte 1 = Command ID, byte 2+ = payload
- All responses are prefixed `"P\xNN."` (ACK) or `"P\xNN!"` (NACK)
- **`PROTOCOL_VERSION`** (`config.h`, reported in the GET_ID string) gates host
  features. The per-version rationale is
  [`keyboards/polykybd/PROTOCOL_HISTORY.md`](PROTOCOL_HISTORY.md)
  — **read it before changing any of these commands**, because several were shaped
  by a contrast with their neighbour that the wire format does not show. What each
  version added:

  | v | command | what it did |
  |---|---|---|
  | 2 | `27` GET_LANG_LIST_PACKED | 2-byte ISO index pair per language; the ASCII cmd `8` is RETIRED and NACKs |
  | 3 | `21` SEND_OVERLAY_MAPPING | made silent (no per-chunk ACK), like the other bulk overlay commands |
  | 4 | `28` GET/SET_IDLE_STYLE | idle anti-burn-in style; `0xFF` queries |
  | 5 | `13` SET_BRIGHTNESS | volatile / host-auto flag byte |
  | 6 | `6` GET_ID | appends the per-bundle font-pack version block `['V'][count][u16 × count]` |
  | 9 | `30` GET/SET_GLYPH_SCRIPT | glyph-script override; `0xFF` queries |
  | 10 | `30` | script index becomes OPEN-ENDED — an unknown index renders the normal legend instead of NACKing, so new faces need no protocol bump |
  | 11 | `10` plain overlay upload | modifier+segment packed into ONE header byte, so a 60-byte segment fits the report exactly |
  | 12 | `33` SEND_OVERLAY_MAPPING_W | variable-width mapping (8/9/10/11 bits), silent like cmd 21 |
  | 13 | `34` GET/SET_GLYPH_SIZE | keycap legend size 0/1/2; range CLOSED, unknown NACKs |
  | 14 | `35` GET_LAYER_NAMES | read-only `[total][count]` + NUL-terminated names |
  | 15 | `36`/`37`/`38` | macros: info / body window / label, behind ONE host feature gate |
  | 16 | `39` | crash record read + clear |
  | 17 | `20` SET_UNICODE_MODE | VOLATILE flag in `data[3]` — apply in RAM, leave EEPROM alone |

  ⚠️ **v13's CLOSED range is the deliberate OPPOSITE of v10's open one, one command
  over.** An unknown SCRIPT falls through to the normal legend, so accepting it costs
  nothing and lets the host ship faces a keyboard lacks. A SIZE names a rendering tier
  whose relocation base and baseline the firmware must know, so accepting an unknown
  one would store, sync and persist a setting that silently renders small. The two HIL
  tests assert opposite things about neighbouring commands **on purpose** — do not
  "make them consistent".

  ⚠️ **A QMK `*_set_user` hook is a NOTIFICATION, never a setter — and calling one to
  CHANGE state fails in the quietest possible way: the UI moves and the behaviour does
  not.** `unicode_input_mode_set_user()` is what QMK fires *from*
  `set_unicode_input_mode()`, and our override only mirrors the value for the keycap
  legend. Cmd 20 called it directly for years, so a host push relabelled those keys
  while `unicode_config.input_mode` never moved (field, 2026-09-08: the layer read
  **Win ON** while emoji still worked). **The tell is a state whose display and effect
  disagree**; when you find one, check whether the write went through the setter or the
  callback. Grep for any `*_set_user` being CALLED rather than implemented.

  **Bump `FW_VERSION` + `PROTOCOL_VERSION` (config.h) and `__protocol__`
  (PolyKybdHost `_version.py`) in lockstep.** ⚠️ The connect gate is NOT exact-match —
  the host connects to any protocol `>= MIN_SUPPORTED_PROTOCOL` and gates each feature
  through `FEATURE_MIN_PROTOCOL` — so forgetting the bump no longer rejects the
  keyboard, it silently leaves the new feature disabled. Quieter, and worse.
- **Cmd `32` = main-loop profiler control — present ONLY in a
  `POLYKYBD_LOOP_PROFILE` build, and bumps NO `PROTOCOL_VERSION`** (dispatched
  independently like cmd 31 / the fontpack commands). Sub-commands `0` RESET / `1`
  READ (binary snapshot, `data[3]` = page) / `2` LOG. ⚠️ The whole `case 32` is
  inside `#ifdef POLYKYBD_LOOP_PROFILE`, so a normal build **NACKs** it — that
  NACK is the deliberate capability signal telling a host "no profiler here"
  instead of handing back a page of zeros. Consumed by the rig's automated perf
  run; see `keyboards/polykybd/profiling/README.md`.
- Overlay transmission: each keycap overlay (360 bytes) is split into 6 × 60-byte segments (cmd `0x0A`, protocol 11+: modifier+segment packed into one header byte), or sent RLE-compressed in 1–2 packets (cmds `0x10`/`0x11`)
- ROI updates (cmds `0x12`/`0x13`) allow partial refresh of a keycap's display area
- Overlay index = `keycode_slot + 90 * modifier_variant` (9 variants: bare, Ctrl, Shift, Ctrl+Shift, Alt, Ctrl+Alt, Alt+Shift, Ctrl+Alt+Shift, GUI)
- ⚠️ **That flat index is the only ADDRESS an overlay upload has, and it is
  resolved through `overlay_map[]` — so `reset_overlay_mapping()`'s identity
  default is LOAD-BEARING FOR WRITES, not just a display convenience.** All three
  write sites in `fill_overlay.c` (plain / compressed / ROI) run the same pair the
  render path does — `adjust_overlay_idx_to_mod()` then `get_overlay_mapping()` —
  and the host addresses pool slot N by sending the (keycode, modifier) pair whose
  flat index *is* N (`OverlayMRUCache.pool_slot_to_firmware_address`: `kc = N % 90`,
  `mod = N // 90`). It uploads every image **before** sending the real display→pool
  mapping, so the identity must hold throughout that window. Zeroing the table
  "because the pool is no longer variant-indexed" sent every image to slot 0:
  nearly every keycap blank, the whole set piled onto Esc (field, 2026-08-01 —
  cost a hardware round). The pool being smaller (600) than the flat index space
  (810) only changes the identity's **extent**: indices `< NUM_OVERLAY_SLOTS` are
  identity, the rest are a 0 fill that can never be an upload destination.

## Telling the host something changed ON THE BOARD

Most state flows host → keyboard, so the host knows what it set. The reverse
direction — the user changes something with a keycode, records a macro, remaps a
key — has no natural notification, and the host's caches then go stale. There are
exactly three ways to close that, and the ranking is not obvious:

| | extra HID reports | latency | new machinery |
|---|---|---|---|
| a counter on a reply the host ALREADY polls | **0** | ≤1 s | none |
| a dedicated command the host polls | 1 per interval | the interval | one command + an RPC method |
| an unsolicited report pushed by the firmware | 1 per event | instant | a reader, framing, drain routing |

⚠️ **Check what the host already asks for BEFORE reaching for a back channel.** The
host's reconnect probe sends **GET_ID and GET_LANG every second**, forever, whenever
a keyboard is attached (`PolyKybdHost` `poly_core.py`, `RECONNECT_CYCLE_MSEC = 1000`).
So a byte on the GET_ID reply reaches the host within a second at **zero** additional
cost, and both other options are solving a problem that does not exist. This was
nearly missed twice — once by designing a MACRO_INFO field the editor would have had
to poll, once by proposing a console line — because the existing poll is invisible
from the firmware side.

- ⚠️ **UNSOLICITED raw HID is not a drop-in, and the cost is NOT bandwidth.** The event
  rate for anything a human does on the board is tens per day against the ~173,000
  exchanges/day the probe alone already generates, so volume is a non-issue and should
  not be the argument. What stops it is that **nothing reads that interface except a
  pending command**: `send_and_read_validate` writes, then reads until it matches the
  expected prefix and **drains everything else**, so an unsolicited report is discarded
  by the next probe within a second. Its comment states the invariant the drain rests
  on — *"Since protocol v3 the firmware sends no unsolicited replies, so a stale reply
  here means one thing only"* — and v3 was the change that made `SEND_OVERLAY_MAPPING`
  silent precisely to reduce escaped ACKs. Push makes a stale reply mean two things, in
  the code path with the stale-reply bug history. Do not add it without a distinguishable
  prefix, routing in the drain, and an idle reader.
- **The CONSOLE is push-shaped and already tapped** (`CrashScanner` on the host, the
  rig's `ConsoleTap`), so it is the cheapest push — but it is lossy by construction
  (QMK drops output nobody drains, and nothing drains it during a flash), it does not
  survive a re-enumeration, it arrives as report-sized FRAGMENTS rather than lines, and
  **any local process can read it**, which is why keystroke logging is gated on
  `debug_enable`. So: **the console may announce, never define.** Anything it says must
  also be answerable over raw HID, and the pull is the truth. `crash_record` is the
  model — the console line announces, cmd 39 reads the same record back — and nothing
  breaks when the line is lost.

**The mechanism: `['G'][u16 state_generation]` in the GET_ID reply**, bumped by
`poly_state_touch()` whenever the BOARD changes something the host may be caching. One
counter covers macros, glyph script, glyph size, idle style, the OS pin, the default
layer and a board-side key reassignment; the host re-reads whatever it has open when
the value moves. It does not say WHAT changed, which is all "refresh what is on screen"
needs.

- ⚠️ **It goes AFTER the `V` font-pack block, never before it.** The host finds that
  block positionally — `parse_id_version_block` (`hid_fontpack.py`) requires `'V'` at
  exactly `nul + 1` — so prepending anything makes every deployed host read "no bundles
  on the device" and **re-flash all eight bundles on every connect**. Both blocks are
  tag-led, so a new host parses `V` first and then looks for `G`.
- **The budget is a `_Static_assert` in `hid_com.c`, not a number in a comment** —
  `sizeof(POLY_GET_ID_STR) + 2 + FONTPACK_BUNDLE_COUNT*2 + 3 <= HID_REPORT_SIZE`. Every
  term moves (the version string grows; the `V` block grows TWO BYTES PER BUNDLE), so a
  measured figure would go stale, and both emitters DROP their block rather than
  truncate if it does not fit — which would cost the host its font-pack versions
  silently and re-flash every bundle on every connect. Mutation-checked: lowering the
  bound fails the build with the assert's own message. Roughly 11 bytes spare at 8
  bundles, i.e. five more.
- **A missing `G` block means "no generation available"**, so an older firmware degrades
  to the previous behaviour (the host re-reads when a view is opened) rather than
  failing.
- **Bump it for host-initiated changes too.** Distinguishing them saves one re-read and
  costs a rule someone has to remember.
- ⚠️ **This IS an enumerated list of call sites, which is the shape that goes stale here
  — and it is acceptable ONLY because of how it fails.** Forgetting a `poly_state_touch()`
  leaves the host's view stale until something else refreshes it, i.e. exactly today's
  behaviour; it can never corrupt state or mis-classify anything. Contrast
  `sync_is_link_fault()`, where a forgotten case produces a WRONG answer, and which is
  therefore written as a complement rather than a list.

