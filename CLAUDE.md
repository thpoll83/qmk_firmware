# CLAUDE.md — qmk_firmware (PolyKybd)

This file provides guidance to Claude Code (claude.ai/code) when working in this QMK fork. The PolyKybd-specific firmware lives at `keyboards/polykybd/`.

For cross-repo context (how this repo relates to `PolyKybdHost/` and `AdafruitGFX/`), see [`../CLAUDE.md`](../CLAUDE.md).

## Code review conventions (all PolyKybd repos)

- **Docstring coverage: ignore CodeRabbit's "Docstring Coverage … threshold 80%" pre-merge check.** That 80% target is a CodeRabbit default, **not** a project policy — the check is non-blocking and we deliberately do not chase it. Do **not** add docstrings to existing functions just to satisfy it (out-of-scope churn). Document new code where a docstring genuinely helps a reader, and no more.

## Branching (all PolyKybd repos)

- **Give every branch a name that hints at its content** (a short descriptive slug, e.g. `claude/fix-slave-layer-after-fw-apply`, not just the auto-generated `claude/<random-scientist>-<id>`) so the branch list reads as a changelog.
- **Always start new work on a FRESH branch cut from the updated default branch — never keep committing to a branch whose PR has already merged.** Once a PR is merged, that branch is done: `git fetch origin PolyKybd` then `git checkout -b claude/<new-slug> origin/PolyKybd` for the next change. Cherry-pick only the still-unmerged commits onto the fresh branch if needed. This keeps each PR a clean, focused diff against the current default (**`PolyKybd`** here; `main` in the host/rig repos) and avoids a new PR accidentally re-including already-merged commits.

## Building & flashing

**The ARM toolchain is installable in the dev / remote container — do not claim it is unavailable.** Verified end-to-end (`split72:default` → `.uf2`, exit 0) on 2026-05-29.

- **Toolchain**: `sudo apt-get install -y gcc-arm-none-eabi binutils-arm-none-eabi` → `arm-none-eabi-gcc` (13.2.x). This is what `qmk setup` installs on Debian/Ubuntu; the PyPI `qmk` package is only the bootstrapper (`config/clone/console/env/setup`) and does **not** bundle the compiler. There is no `bin/qmk` in this fork — the full CLI lives in `lib/python`.
- **qmk CLI**: `pip install qmk` (use a venv if system pip errors building `halo` — a Debian setuptools quirk), then `qmk config user.qmk_home=<repo>` (or `export QMK_HOME=<repo>`) so it discovers `compile`/`flash` from the repo's `lib/python`, plus `pip install -r requirements.txt`.
- **Submodules** (empty in a fresh clone): `make git-submodule`. The minimum for split72 is `lib/chibios lib/chibios-contrib lib/pico-sdk lib/printf lib/lufa` (printf and lufa are needed even on RP2040 — `quantum/logging` and the ChibiOS USB stack pull them in).
  - ⚠️ **In a web/remote container `make git-submodule` (and `qmk git-submodule`) 403s** — the injected git proxy (`127.0.0.1:*/git/…`) only serves the session's *authorized* repos, and `qmk/*` aren't in it, so the submodule clone is rejected. **This is NOT a real "build unavailable" — do not give up here.** GitHub *git-clone* is also egress-denied (403), but the **`codeload.github.com` tarball endpoint is allowed (200)**. Fetch each submodule at its pinned SHA (from `git submodule status`) directly: `curl -sSL https://codeload.github.com/qmk/<Repo>/tar.gz/<sha> | tar xz -C lib/<name> --strip-components=1` (repos: `ChibiOS`, `ChibiOS-Contrib`, `lufa`, `printf`, `pico-sdk`). Then `qmk compile` works normally — verified `split72:default` + `split42:default` → `.uf2`, 2026-06-25.
- **Build**: `qmk compile -kb polykybd/split72 -km default` (or `make polykybd/split72:default`). Output `.uf2` lands in the repo root and `.build/`.
- **Deliverable for testing is the `.bin`, NOT the `.uf2`** — the user flashes over HID via PolyKybdHost's firmware updater (`polyhost/device/hid_fw_up.py`), which takes the raw RP2040 image: `arm-none-eabi-objcopy -O binary .build/<target>.elf .build/<target>.bin`. The `.uf2` is only for manual bootloader-drive recovery.
- **Docker is NOT usable** in the remote container (no daemon) — use the native toolchain above, not the qmk docker image.
- The `firmware-size-diff` skill builds HEAD vs working tree and diffs sizes / `.text`.

## Firmware overview (`keyboards/polykybd/`)

The firmware runs on a **Raspberry Pi RP2040** (133 MHz dual-core ARM M0+) and is a heavily customised QMK build. This is **custom hardware with 8 MB of external QSPI flash** (NOT the stock 2 MB). The 8 MB is **partitioned** (see `base/fw_staging.h` for the authoritative map): **0–2 MB running firmware** (the linker `flash1` XIP window), **2–4 MB firmware-update staging**, **4–8 MB resource/overlay data** (`FLASH_TARGET_OFFSET`). So the budget that matters for adding languages/fonts is the **2 MB firmware partition**, of which `split72:default` currently uses ~0.76 MB (~38 %). `FW_STAGING_OFFSET` is kept equal to the linker `flash1` length so a build that exceeds 2 MB fails to *link* rather than silently growing into the staging area (this firmware/staging split was raised from 1 MB → 2 MB in 2026-06 as the image neared the old boundary). The keyboard is split (left + right halves connected via UART) with up to 72 per-keycap OLED displays (72×40 px monochrome, SPI-driven) plus a 128×64 status OLED.

The host software (`PolyKybdHost/`) communicates with this firmware over a custom HID report protocol (64-byte reports, v0.7.0+).

### Key source files

| File | Role |
|------|------|
| `poly_keymap.c` | **Shared keymap logic, compiled for every variant** — rendering (`render_key`, `update_displays`, `to_static_text`), HID/overlay handling, language selection, idle/suspend, split sync glue, the firmware-update state machine, and all QMK `*_user`/`*_kb` callbacks. Holds the keymap-side cog blocks (the language tables). |
| `hid_com.c` | `raw_hid_receive()` — main HID command dispatcher (21 command IDs, `0x01`–`0x15`) |
| `fill_overlay.c` | Receives overlay segments from host, decompresses RLE, writes to overlay memory |
| `base/overlay.c` | Overlay memory: `overlays[810][360]` — 90 keycap slots × 9 modifier variants × 360 bytes |
| `base/disp_array.c` | Per-keycap OLED driver: `kdisp_write_gfx_char()`, `kdisp_draw_bitmap()`, `kdisp_invert()` |
| `base/shift_reg.c` | Shift-register multiplexing — selects which keycap OLED receives the next SPI write |
| `split_sync.c` | CRC32-validated transactions that synchronise overlays and state to the other half |
| `state.c` | `poly_sync_t` / `poly_layer_t` — shared state structs with CRC32, persisted via EEPROM |
| `multicore_exec.c` | Offloads RLE decompression to RP2040 core1 via FIFO, keeping QMK's core0 responsive |
| `lang/lang_lut.c` | 81-language lookup table (code-generated from `lang_lut.xlsx` via cog) |

### Keyboard variants & the shared keymap (`poly_keymap.c`)

Two hardware variants share one firmware: **`split72`** (72-key, RGB matrix,
Cirque trackpad, 128×64 status OLED) and **`split42`** (42-key CRKBD footprint,
no RGB, no trackpad, 128×32 status OLED). **`split42` was renamed from `corne42`
in 2026-06** — same hardware/PID/`LAYOUT_crkbd`; old `corne42` paths are gone.

All behaviour lives in the keyboard-level `poly_keymap.c` (compiled for both via
`rules.mk` `SRC`). Each variant's `<variant>/keymaps/default/keymap.c` is **data
only**: `keymaps[]`, `encoder_map[]`, and (RGB variants) `g_led_config`. Variant
differences resolve at compile time:
- `polykybd.h` `#include`s the active variant header (selected by QMK's
  `-DKEYBOARD_polykybd_<variant>`), so `QMK_KEYBOARD_H` reaches
  `struct display_info` + the `BITMASK*` macros.
- Per-variant header macros: `POLY_DISP_ROW_0/3` (scan-start displays) and
  `POLY_SPLASH_R1/R2/R2_ROW` (boot splash).
- `RGB_MATRIX_ENABLE` / `POINTING_DEVICE_ENABLE` guard the RGB and trackpad paths.

**Consequence:** a feature added to `poly_keymap.c` (e.g. a language via cog)
lands on both keyboards at once — they can't drift apart. Don't re-introduce
per-variant copies of the keymap logic (that drift is exactly what this
extraction fixed: `corne42` had silently fallen ~98 languages behind split72).
`run_cog.sh` targets `poly_keymap.c`.

### HID protocol (host → firmware)
- 64-byte raw HID reports; byte 0 = Report ID, byte 1 = Command ID, byte 2+ = payload
- All responses are prefixed `"P\xNN."` (ACK) or `"P\xNN!"` (NACK)
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
  paths are untouched** (their headers already fit). **Bump `FW_VERSION` +
  `PROTOCOL_VERSION` (config.h) and `__protocol__` (PolyKybdHost `_version.py`) in
  lockstep** — the host connect gate is exact-match.
- Overlay transmission: each keycap overlay (360 bytes) is split into 6 × 60-byte segments (cmd `0x0A`, protocol 11+: modifier+segment packed into one header byte), or sent RLE-compressed in 1–2 packets (cmds `0x10`/`0x11`)
- ROI updates (cmds `0x12`/`0x13`) allow partial refresh of a keycap's display area
- Overlay index = `keycode_slot + 90 * modifier_variant` (9 variants: bare, Ctrl, Shift, Ctrl+Shift, Alt, Ctrl+Alt, Alt+Shift, Ctrl+Alt+Shift, GUI)

### Language list encoding (`lang/iso_lang_country.py`)
The packed list (cmd `27`) maps each 4-char code to two 1-byte indices: the
language's position in the ISO 639-1 table and the country's in ISO 3166-1
alpha-2. `lang/iso_lang_country.py` is the **frozen, append-only** index table —
generated once from the `iso-codes` package then frozen (indices never reorder;
new ISO codes append at the next free slot; private pseudo-codes with no ISO
639-1 entry, e.g. `hw`, live in a reserved block above the standard codes). The
`hid_com.c` case-27 cog imports it and emits the index bytes, so the table is a
**build-time artifact only** — it is *not* compiled into the firmware.
- ⚠️ **Single source of truth across three repos**: this file is byte-identical
  to `PolyKybdHost/polyhost/services/iso_lang_country.py` and
  `polykybd-ctnd/station/iso_lang_country.py`. When it changes, copy it to all
  three (verify with `cmp`); a mismatch silently decodes wrong languages on the
  host/rig. Adding a standard ISO language needs no table change (the code is
  already present); only a new private pseudo-code requires appending an entry.
- Re-run `cog -r hid_com.c` after any change to the list or the table (needs
  `cogapp` + `openpyxl`).

### Display rendering pipeline
1. Host sends compressed bitmap → `fill_overlay.c` decompresses (optionally on core1) → `overlays[idx][360]`
2. On key event, `split72.c` selects the keycap via shift-register bitmask and calls `kdisp_invert()` for instant visual feedback
3. Active window change → host sends new overlay set → firmware swaps all 72 keycap images

**Per-keycap rendering gotchas (`base/disp_array.c`)** — learned the hard way:
- **`kdisp_write_gfx_char` baseline-aligns every glyph to `fonts[0]`**:
  `y += currentFont->yAdvance - fonts[0]->yAdvance`. So drawing a *single* icon
  whose font differs in height from `g_all_fonts[0]` (IconsFont, yAdvance 40)
  shifts it vertically by the difference. This was the **language-flag gap-at-top
  regression** when flags moved into the pack (flag yAdvance 54 − 40 = +14 px down,
  filling 0..39 → 14..53). **Fix pattern: draw such a glyph through a *single-font
  array* `{ that_font }`** so `fonts[0]` is the glyph's own font (adjustment 0), as
  the old compiled-in `{ &flag_font }` path did. `kdisp_gfx_glyph_font(fonts, n, cp,
  &out_font)` returns the glyph **and** its owning font in one scan for exactly this
  (`kdisp_gfx_glyph` is the `out_font = NULL` wrapper).
- **GFXfont bitmaps are continuous-bit-packed, byte-padded per *glyph* — NOT per
  scanline.** Index bits as `bit = yy*w + xx; byte = bitmapOffset + bit/8; msb-first`.
  A per-scanline-stride reader produces garbage that *looks* like dithering noise.
- **To preview a keycap faithfully, use `PolyKybdHost/tools/oled_preview.py`** (its
  `gfx_font` loader + `oled_to_rgb`) — it parses the generated headers correctly and
  renders the real 72×40 OLED look. A hand-rolled renderer cost two wrong "flag
  offset" guesses this session before the real cause (the baseline-align above) was
  found. `gfx_font.load_all_fonts(base/fonts)` includes `flag_fonts.h`, so it can
  render pack/flag glyphs too. **Caveat:** the preview models glyph `xOffset/yOffset`
  but NOT the `kdisp` baseline-align shift, so it won't reproduce that bug — reason
  about `fonts[0]` separately.
- **The per-keycap DISPLAY grid is NOT a rectangle** (split72). Only the **bottom
  row (display row 4) is a full 8-wide row**; the upper rows (0–3) have panels at
  **cols 0–6 only** — display **col 7 is a routing phantom** (a `BITMASK` entry
  exists in `split72.c` `key_display[]` but there is no OLED behind it, so writing
  it shows nothing). The two inner **thumb keys** per half live *only* on the bottom
  matrix row (left disp cols 6/7, right 0/1), stacked vertically (same x, different
  y) yet on the same matrix row — so they can't be part of a rectangular block on
  the rows above. Also: `LAYOUT_TO_INDEX(row,col)=row*8+col` **wraps** — `col ==
  MATRIX_COLS` folds into the next row's col 0 (bound `disp_col` to
  `[0, MATRIX_COLS-1]`); and the right half applies a `c--` display-index shift on
  its upper rows (5–8) but not its bottom row (9). ⚠️ **Model placement from the
  OLED chip-select, NOT the RGB `g_led_config` x-order** — they do **not** match:
  because of the `c--` fold, **disp_col 0 is the OUTER edge on the LEFT half but the
  INNER edge on the RIGHT**, so a sweep that looks left→right in RGB space runs
  backwards on the right half's OLEDs. Reasoning from RGB position produced several
  wrong IDDQD-screensaver revisions before this was caught. The composed model +
  verifier is committed as `doom/tools/keycap_dispmap.py` (run it after any
  placement change); full write-up in `doom/README.md` § anti-burn-in placement.

### Split synchronisation
Seven custom QMK transaction IDs (`USER_SYNC_POLY_DATA`, `USER_SYNC_OVERLAY_DATA`, `USER_SYNC_COMPRESSED_DATA`, `USER_SYNC_ROI_DATA`, etc.) carry state and overlay data to the slave half over UART with CRC32 validation and up to 10 retries.

### Idle anti-burn-in styles (`poly_keymap.c`)
When the keyboard idles, the keycap legends would otherwise burn the **same**
pixels in. Two styles (EEPROM `poly_eeconf_t.idle_style`, HID cmd 28, enum
`poly_idle_style` in `state.h`):
- **`IDLE_STYLE_PULSE` (0, default, legacy):** `kdisp_idle()` only modulates each
  keycap's SSD1306 contrast register (a per-key out-of-phase "breathing"). The
  buffer is never re-rendered, so the lit pixels never move — the burn-in risk.
- **`IDLE_STYLE_JITTER` (1):** keeps the pulse, but **each key independently**
  relocates its own legend to a fresh random spot the instant that key's
  out-of-phase pulse dims it to black — so the lit pixels migrate per key, not in
  lockstep. Mechanics (all in `kdisp_idle()`):
  - `kdisp_idle()` already computes a **per-key brightness** (`to_brightness((contrast
    + per-key phase) % 50)`) and walks every key on this half with the shift register
    selecting each in turn. On the **lit→dark edge** (`idle_brightness==0` and the
    `s_idle_was_dark[r][c]` latch was clear) in JITTER style it **switches that key's
    panel OFF first, then** calls **`render_idle_key(kc, led_state, seed)`** to redraw
    *that one key* straight into the currently selected (now-dark) display. Writing the
    new frame *after* the off-switch is what makes the move invisible — the glyph
    reappears already at its new spot on the next bright cycle (~once per ~15 s per key);
    writing before the off-switch flashed it at the old contrast first (a visible jump
    just before the key dimmed out). `s_idle_was_dark` gates it to once per dark episode
    (a 1-bit-per-key latch, this-half-only). `render_idle_key()` **returns false without
    touching the buffer** when the keycode has no plain-text legend (a language flag,
    emoji, region tab, MRU control — full-bleed images that can't be jittered), so those
    keys keep their current frame and just pulse instead of being blanked (the
    language-layer flags no longer disappear on the first idle cycle).
  - **No shared offset, so nothing extra crosses the UART.** Each half runs
    `kdisp_idle()` on its own keys with the synced pulse `contrast`; only the **style
    bit** is synced (`poly_sync_t.idle_style`, set from `housekeeping_task_user()` on
    the master, adopted by the slave's `copy_local_state`) so the slave jitters iff
    the master's style says so. The legend is **re-derived from the keycode** on every
    relocation — nothing is stored in the OLED's own memory (the panel only holds the
    last frame we send).
  - `update_displays()` **early-returns while `DISP_IDLE` is set** (it would otherwise
    fight `kdisp_idle()` and redraw the awake chrome) — the keycaps already hold the
    last centred awake render when idle begins, and `kdisp_idle()` owns all idle
    visuals from there. `render_idle_key()` draws **only the resting normal legend** —
    no shift/AltGr preview, no overlay image, no tab/MRU chrome. The relocated keycode
    is resolved through **`display_keycode_at()`** — the shared helper (also used by the
    awake `update_displays`) that honours the active momentary stack **and the default
    layer** (`def_layer`, folded in so a Colemak/Neo base shows its own legends, not
    `_BL`) with a one-level transparent fallback — so a jittered key matches what was on
    screen rather than snapping to the base layer.
  - **The travel range is derived per glyph from its own on-screen slack** — there is
    deliberately **no global `±N` offset envelope**. `render_idle_key()` measures the
    legend with `kdisp_gfx_text_bbox()` (full x+y box, mirroring the draw's cursor
    rules and per-glyph yAdvance shift; `kdisp_gfx_text_bounds()` is now a wrapper over
    it) and `roll_idle_offset()` rolls a **uniform random position within that glyph's
    free space** inside the visible window `[BUFFER_X, BUFFER_X+SCREEN_WIDTH-1] × [0,
    SCREEN_HEIGHT-1]` (= `[28,99]×[0,39]`). So a slim `i` roams its full free width
    while a wide `w` (or a full-width CJK legend) moves only as far as it can without
    clipping — each uses all *and only* the room it has, for any script. A fixed cap
    would be counter-productive: it would throttle the slim glyph and edge-bias the
    wide one (most rolls clamping to the same boundary). A glyph with no slack in an
    axis simply doesn't move in it — no clipping, no special-casing. `SET_PIXEL_CLIPPED`
    in `disp_array.c` remains the memory-safety backstop, but is not relied on for
    visibility.
  - The per-key latch is cleared by **`reset_idle_jitter()`** on every wake/suspend
    path (`display_wakeup`, `poly_suspend`, `suspend_wakeup_init_kb`, cmd 15
    stop-idle), so a fresh idle session starts from the centred awake legend and
    relocates every key cleanly. (This **replaces** the earlier global-offset jitter,
    where the master picked one `idle_dx/idle_dy` per ~15 s cycle, synced it, and all
    keys shifted together — the per-key version is the nicer effect *and* drops the
    synced offset.)
  A "Matrix-style" idle animation was considered but shelved — it defeats the
  "glance at the dimmed legend and resume typing" hint the pulse preserves; jitter
  was chosen as the default-preserving, legibility-preserving fix.

### Glyph-script override (`poly_keymap.c`, HID cmd 30, protocol v9+; expanded v10)
An OS-independent **override** of the language-layer legends with an alternative
script (fantasy / retro). State: `poly_eeconf_t.glyph_script` (persisted, appended
tail byte like `os_state`; `EECONFIG_USER_DATA_SIZE` grew 64→65, still ≤ the 128-byte
`POLY_EECONFIG_USER_RESERVED` so **no keymap relocation / user reset**) +
`poly_sync_t.glyph_script` (master-authoritative, synced like `active_os`;
`housekeeping_task_user()` sets it and `request_disp_refresh()`s on change). `enum
poly_glyph_script` in `state.h` — append-only: `GLYPH_STD=0`, `GLYPH_TENGWAR=1`, then
the v10 expansion `GLYPH_RUNES=2, GLYPH_AUREBESH=3, GLYPH_SGA=4, GLYPH_CIRTH=5,
GLYPH_IBMVGA=6, GLYPH_C64=7, GLYPH_AMIGA=8, GLYPH_APL=9, GLYPH_BRAILLE=10`.
- **Open-ended index (v10+): cmd 30 accepts ANY value `0..0xFE`; unknown → normal.**
  `set_glyph_script()`/`note_glyph_script()`/`load_user_eeconf()` store the byte
  verbatim (only the erased-EEPROM `0xFF` maps to `GLYPH_STD`); `hid_com.c` case 30 no
  longer NACKs an out-of-range index. `glyph_script_codepoint()` returns 0 for any
  `script >= GLYPH_SCRIPT_COUNT`, so an index this firmware doesn't know falls through
  to the normal legend (same path as a known script whose font isn't flashed). This is
  what lets the host offer scripts a given keyboard lacks and lets **new font faces ship
  without a protocol bump** — DON'T re-add a range NACK. Storing verbatim also means a
  choice made before the matching font-pack update survives it. Adding a `GLYPH_*` value
  therefore needs NO `PROTOCOL_VERSION` change — just the enum entry, the
  `glyph_script_blocks[]` row, the font, and the host `GlyphScript`/label.
- **Render hook — one choke point in `render_key()`** (`poly_keymap.c`): right after
  `local_state` is fetched, when `glyph_script != GLYPH_STD` and the key is a plain
  letter/digit on the normal layer (not the `_ADDLANG1` latin-variation layer), it
  draws the override glyph centered and **returns**, so it replaces the *whole* base
  legend — including the unshifted view's shift-preview (Tengwar is caseless, so the
  shift preview is deliberately dropped). Overlays and OS-hints
  (`keycode_to_disp_overlay`) are drawn on **separate paths** (`update_displays` /
  overlay memory) and are genuinely untouched. Two fall-throughs to the real legend:
  when an **AltGr** key is held (`mods & MOD_RALT` — the AltGr symbol is a different
  character, not a cased letter, so it wins), and when the glyph isn't in `g_all_fonts`
  (the `fantasy` bundle isn't flashed), so a pack-less keyboard shows Latin, never blanks.
- **Codepoints are relocated, NOT native.** The `flags` bundle already occupies the
  CSUR PUA `0xE000+`, so raw script codepoints would render a language flag. Each
  script's font is emitted (fontconvert sequence `-F` remap, `fonts.yaml`) into its
  own **dense private PUA block** matching `glyph_script_blocks[]` (a table indexed by
  `poly_glyph_script`) in `poly_keymap.c`: Tengwar `0xE800`, Runes `0xE840`, Aurebesh
  `0xE880`, SGA `0xE8C0`, Cirth `0xE900`, IBM VGA `0xE940`, C64 `0xE980`, Amiga `0xE9C0`,
  APL `0xEA00`, Braille `0xEA40` (0x40 apart). Letters `a..z` → `base+0..25`; scripts
  with their own numerals (`digits:true`) put `1..0` at `base+26..35`, others leave the
  digit keys as the normal numeral (runes/Aurebesh/Cirth have no native numbers). The
  per-key glyph choice lives only in the font's generation sequence, so the firmware
  just needs the base + dense index.
- **Fonts** (all in the `fantasy` bundle; keep user-facing strings generic — trademark
  caveat on the fictional scripts, though the *fonts* are fine to embed): Tengwar =
  Alcarin (OFL, no Noto Tengwar exists); Runes = Noto Sans Runic (OFL); Aurebesh /
  Cirth = GNU Unifont CSUR (GPL + font-embedding exception; kept on the blocky 16 px
  bitmap because no license-clean smooth outline font exists for those CSUR blocks —
  the free Aurebesh/Cirth outline fonts are personal-use-only); APL / Braille = DejaVu
  Sans (Bitstream Vera + Arev, permissive — smooth outline, replacing Unifont's 16 px
  bitmap; the APL quad U+2395, absent from DejaVu, maps to U+25A1 □); SGA = the CC0
  `standardgalactic/alphabet` font; IBM VGA/CP437 = VileR PxPlus (CC-BY-SA-4.0, Debian
  `fonts-pc`); C64 = KreativeKorp **PetMe64** (KSRFL, solid ROM font — the OFL
  Homecomputer "Sixtyfour" was rejected for its baked-in CRT scanlines); Amiga = OFL
  Homecomputer "Workbench" (Debian `fonts-amiga`; scanline look kept for a clean
  license — solid Topaz conversions were license-uncertain). ZX Spectrum was dropped
  (no license-clean font found). Sources fetched by `fonts/dl-fonts.sh` (google/fonts
  + CC0 raw URLs; the Debian-packaged ones via `apt-get download` + `dpkg-deb -x`, no
  root). Host: HID cmd 30 in `PolyKybd.get/set_glyph_script`, tray "Glyph Script"
  submenu (`GLYPH_SCRIPT_LABELS`) + a "Reset glyph script to Standard" button in the
  settings dialog; `polyctl glyph-script [standard|tengwar|runes|…|braille]`. Rig:
  `test_glyph_script_round_trip` (`min_protocol: 9`) + `test_glyph_script_expansion`
  (`min_protocol: 10`, walks values 2/6/10 + out-of-range NACK).

### Notable QMK features enabled
RGB matrix (72 LEDs, 35 effects), dynamic keymap (9 layers, VIA-compatible), unicode input (Linux/macOS/Windows/BSD), Cirque trackpad (split72 variant), `USE_CORE1` multicore.

## Font generation

Fonts for the per-keycap OLEDs are generated using the `fontconvert` tool from the [`AdafruitGFX/`](../AdafruitGFX/CLAUDE.md) repo. Generation is **config-driven** via `keyboards/polykybd/fonts/` — full docs in [`fonts/README.md`](keyboards/polykybd/fonts/README.md).

- **`fonts/fonts.yaml`** — single source of truth: an ordered list of font entries (font file, size, variant, codepoint ranges, weight, bits, …) grouped into categories with shared defaults. The list order **is** the `ALL_FONTS[]` priority (front-to-back lookup; first match wins on overlapping ranges) — categories only decide which header a font lands in.
- **`fonts/generate_fonts.py`** — reads the YAML, runs `fontconvert` per entry, writes one header per category to `base/fonts/generated/`, and composes `base/fonts/gfx_used_fonts.h` (the `ALL_FONTS[]` table, with `IconsFont` prepended). `--check` flags stale headers for CI. Needs PyYAML + `fontconvert` on PATH (or `$FONTCONVERT`). It also emits **`base/fonts/generated/fontpack_render_settings.json`** — a `global ALL_FONTS index → fonts.yaml render options` map (the `RENDER_SETTINGS` output, via `render_settings()`; sequence-mode entries also get `composite` + `seq_first` derived from their `-C`/`-F` extra_args, so the host editor can rebuild matra/combining-mark glyphs without guessing). This is mirrored byte-identically in the host at `PolyKybdHost/polyhost/res/fontpack/fontpack_render_settings.json`, where the font-pack **edit** dialog reads it to pre-fill the controls a glyph was generated with (the `.plyf` itself carries no render options). Keep both in sync (`cmp`); `--check` enforces it stays consistent with the headers.
- **`fonts/dl-fonts.sh`** — downloads the Noto source fonts first. The font list
  (url + dest) lives in **`fonts/noto-fonts.yaml`** (single source of truth); the
  script just parses it (PyYAML) and fetches each entry. ⚠️ `noto-fonts.yaml` is
  mirrored **byte-identically** in the host repo at
  `PolyKybdHost/polyhost/res/fonts/noto-fonts.yaml` (its "Download Noto…" button in
  the font-pack extend dialog reads the same catalog) — keep both in sync (`cmp`).
- `create_fonts.sh` is now a thin deprecated wrapper that forwards to `generate_fonts.py`.
- **`fonts/gen-lang-fonts.sh`** — generates the two standalone headers for the language-selection layer (`_LL`): `base/fonts/flag_fonts.h` (country flags from NotoColorEmoji, one per `LANG_*` at codepoint `0xE000 + enum index`, via fontconvert's `-F`; the country list is derived from `lang_lut.xlsx` automatically) and `base/fonts/lang_label_font.h` (a 6 px NotoSans label font). These are **not** in `fonts.yaml`/`ALL_FONTS` — like the status-OLED fonts they're used via dedicated single-font arrays. `render_lang_flag_key()` in `poly_keymap.c` draws the flag (top 28 px) + the `xx-YY` code (bottom 12 px) per key, with a frame on the selected language. Re-run only when the language list changes. It also emits **`base/fonts/generated/lang_flags.json`** — the flag font's render record (source NotoColorEmoji, the `-s20 -g -r54 -W72 -O1 -Dfs -e-0.10` options, `seq_first` 0xE000, and the per-flag regional-indicator `sequence`). The flag font isn't in `fonts.yaml`, so `generate_fonts.py` emits no render record for it; this sidecar lets the host font-pack **editor** rebuild a single flag (sequence mode). ⚠️ Mirrored **byte-identically** in `PolyKybdHost/polyhost/res/fontpack/lang_flags.json` — keep both in sync (`cmp`).
- **Byte-reproducible output requires the pinned `fontconvert` build (FreeType 2.13.3 / HarfBuzz 2.6.7, the CMake ExternalProject)** — the distro fast-path build renders ~1px differently on some glyphs. The committed headers are built with the pinned toolchain; `generate_fonts.py --check` passes against it.

See [`AdafruitGFX/CLAUDE.md`](../AdafruitGFX/CLAUDE.md) for `fontconvert` build and usage details.

### Font pack: resident fonts (compiled-in) + external-flash pack

Fonts are split into a small **resident** set compiled into the firmware image and
a large **pack** (`PlyF`) that lives in the **4–8 MB resource region** and is
flashed over HID separately. `fontpack_assemble()` builds `g_all_fonts = resident
++ pack` at boot; with no pack, only the resident set is present. Files:
`base/fontpack.c/.h` (C loader), `fonts/fontpack.py` (build-side serializer),
`base/fonts/generated/fontpack.manifest.json` (committed pack ABI contract),
`hid_fontpack.c` + `PolyKybdHost/polyhost/device/hid_fontpack.py` (HID transport),
`polyhost/cli/polyctl.py` (`fontpack status|sync|flash <id>|wipe [id]` — per-bundle
since the split-pack change; `status` shows device-vs-shipped versions, `sync`
flashes all stale bundles, `flash <id>` force-flashes one).

- **Split pack (protocol 6+): the pack is N independently-versioned BUNDLES, not
  one blob.** `fonts/fonts.yaml` `bundles:` groups the non-resident categories into
  ordered bundles (currently 6: `symbol`, `mideast`, `syllabic`, `asia`, `flags`,
  `emoji`), each a standalone `PlyF` flashed to its **own fixed sector-aligned slot**
  in a **2 MB** window at `FW_RESOURCE_OFFSET` (`fontpack_layout.h`, generated). The
  set of valid slot headers **is** the directory — there is **no separate directory
  sector** (avoids a consistency class of bug). Each bundle's per-font record carries
  the font's **gidx sort key** (the spare `reserved` u16 — a dense ALL_FONTS position
  for normal fonts, a pinned high band for `pack_extra`; it is a *sort key*, not a
  dense array position — see the gidx note below); `fontpack_load()` reads every slot
  and `fontpack_assemble()` insertion-sorts all present bundles' fonts by it back into
  global priority order, reproducing the old single-pack `g_all_fonts` exactly. The build emits per-bundle `.plyf` + `fontpack_bundles.manifest.json`
  (ABI contract) + `fontpack_layout.h` (the X-macro slot table firmware **and** host
  share) via `generate_fonts.py --emit-bundles DIR` / `--bundle-version ID=N`.
  - **Auto on connect:** the firmware reports every bundle's `content_version` in the
    `GET_ID` v6 block; the host (`fontpack_bundle.py` + `PolyCore._fontpack_autocheck_job`)
    flashes only the bundles the device is missing/behind on, each to its slot. The
    bundles ship in `PolyKybdHost/polyhost/res/fontpack/<id>.plyf` + `bundles.json`.
  - **Adding/regenerating a bundle:** bump that bundle's `content_version` (so the
    host re-flashes it) and reship the `.plyf` + `bundles.json`. `latin` stays
    **resident** (it is `resident: true`), so it is NOT a bundle — the keyboard always
    renders ASCII text with no pack. The build-time guard fails if a bundle overflows
    its slot. Order in `bundles.list` is **append-only** (the index is the on-wire id
    and the slot order; growth-prone `emoji` is last with `slot_kb: rest`).
  - **Shadowed-glyph dedupe is DEFAULT-ON in the build** (`generate_fonts.py`,
    `--no-dedupe` opts out; `fonts/fontpack.py` `prune_shadowed_glyphs`). Before
    emitting bundles it **empties** (turns into a `{off,0,0,0,0,0}` gap) any pack
    glyph a **higher-priority font already draws byte-identically** — front-to-back
    precedence means it can never render, so it's dead weight in flash. Runs
    build-side (not host-side) because only the build sees the **resident** set,
    which can shadow a pack glyph a host-only view would miss. It asserts the
    assembled front-to-back render is unchanged afterwards. ⚠️ **The shipped bundle
    bytes + `fontpack_bundles.manifest.json` already reflect the prune**, so any
    regeneration must run it too (a stale `fontpack.py` without `prune_shadowed_glyphs`
    re-inflates the bundle and diverges from what's shipped). First landed 2026-07:
    73 glyphs / 13,313 B reclaimed — only `symbol` (33,980→33,788) and `emoji`
    (227,460→214,344) shrank; all other bundles were byte-identical.
  - **The per-font `reserved` gidx is a SORT KEY, not a dense array position.**
    `fontpack_assemble()` (`base/fontpack.c`) places the resident set first, then
    **insertion-sorts the pack fonts by their stored gidx** — nothing indexes an
    array *by* gidx, so gaps / sparse / out-of-order values are all fine, and the
    order only changes a *lookup* for two pack fonts that share a codepoint. The
    build keeps pack ranges **disjoint across bundles** (verified: 0 cross-bundle
    `[first,last]` overlaps), so for the pack the gidx order is functionally
    irrelevant — a stale gidx in an un-reshipped bundle is **harmless**. ⚠️ The one
    invariant: if two pack fonts intentionally overlap, keep them in the **same
    bundle** (intra-bundle order is fixed and never goes stale) — never split an
    overlapping pair across bundles.
  - **Appending a hint/glyph font only reships the EDITED bundle (since the
    pack_extra pin).** Appending a font at the tail of `fonts.yaml` used to shift
    the trailing `pack_extra` (flags) font's dense gidx → `flags.plyf` changed too,
    forcing a second reship+bump (e.g. symbol v3→v4 *and* flags v2→v3). Fixed in
    `fonts/fontpack.py`: `pack_extra` fonts get a **fixed high gidx band**
    (`PACK_EXTRA_GIDX_BASE = 0xF000`) instead of their dense position, so a tail
    append no longer moves them. flags is disjoint PUA (0xE000+) and still sorts
    last, so the assembled order is byte-identical (asserted during the change).
    The first flags regen after this lands adopts the pinned gidx (a one-time
    `flags.plyf` reship); thereafter only the bundle you actually edited changes.
  - **Reshipping a bundle to the host — there is NO ship script.** Regenerate with
    `generate_fonts.py --emit-bundles DIR --bundle-version ID=N …`, copy the changed
    `<id>.plyf` to `PolyKybdHost/polyhost/res/fontpack/`, then hand-rebuild
    `bundles.json` from the firmware `fontpack_bundles.manifest.json` (id / index /
    slot_offset / slot_size) + each `.plyf` (`size = len(data)`, `sha256 =
    sha256(data).hexdigest()[:16]`) + the version map. ⚠️ **`--bundle-version`
    defaults UNSPECIFIED bundles to `content_version 0`** — pass *every* id
    (`symbol=4 mideast=1 syllabic=1 asia=1 flags=3 emoji=1`) or you silently reset
    the others. `cmp` each regenerated `.plyf` against the shipped one to see which
    actually changed, and bump+reship only those (see the gidx note above re: why
    appending a glyph now changes only the edited bundle).
    - **You do NOT need `fontconvert` to reship** — bundles derive deterministically
      from the **committed** category headers. `--emit-bundles` re-runs fontconvert
      only to *regenerate* those headers; if the headers are already committed (no
      `fonts.yaml`/TTF change, just a reship / a dedupe bump), build the `.plyf`
      straight from them in a throwaway script: `order =
      fontpack.all_fonts_order(fonts_dir)`, `resident =
      fontpack.resident_symbols(cfg, fonts_dir)`, `parsed = {}` then
      `parsed.update(fontpack.parse_gfx_header(h.read_text()))` for every
      `base/fonts/generated/*.h` + `parsed.update(fontpack.extra_pack_fonts(cfg,
      fonts_dir))`, `sym2cat = fontpack.symbol_categories_from_tree(fonts_dir, cfg)`,
      `fontpack.prune_shadowed_glyphs(order, resident, parsed)` (mirror the build!),
      `fontpack.build_bundles(order, resident, parsed, sym2cat, cfg,
      content_versions={all ids})`. This reproduces the shipped `.plyf` byte-for-byte
      and also re-emits `fontpack_bundles.manifest.json` (`bundles_manifest_json`) +
      layout header — the only way to reship inside a container without the pinned
      FreeType/HarfBuzz build. (Used 2026-07 for the dedupe + fantasy reship.)
    - **A host `.plyf` can silently LAG a firmware `fonts.yaml` render-size tweak.**
      Because the reship is manual, a firmware-side render change (e.g. "render
      Aurebesh smaller") changes a bundle's bitmap bytes but leaves the host copy
      **stale at the same `content_version`** until someone reships it — so no
      keyboard ever re-flashes the corrected glyphs. `cmp` alone flags it; to confirm
      it's a *render* drift (not a version-byte diff), decode both packs and diff
      **per-glyph WxH** — the font metadata (`first`/`last`/`yAdvance`) matches while
      only the bitmap dims differ. Seen 2026-07: `fantasy` was 604 B / 124 glyphs
      stale across Aurebesh/Cirth/APL/Braille vs 3 firmware "render smaller" commits;
      fixed by reshipping from the committed headers and bumping v2→v3.
    - **Bump `content_version` MINIMALLY (+1 over the shipped value), don't jump.**
      No font-pack bundle has ever been deployed to a device, so the version only
      needs to exceed what a device already has (0 / nothing) — any increment works,
      and a small, monotonic step keeps the diff-vs-base readable and the host's
      `decide_stale_bundles` comparison obvious. Don't ratchet a version up across
      iterations (e.g. 4→7→8 while tuning); land the reship at base+1 (symbol 4→5,
      2026-07). ⚠️ The value lives in the `.plyf` header *and* `bundles.json` — they
      must match, so changing it means regenerating the `.plyf` with the new
      `--bundle-version`, not just editing the JSON.
  - **Flash UX (split72):** while any flash runs the status OLED shows an "Updating
    fonts/firmware — do not unplug" screen with a full-width progress bar, and the RGB
    matrix breathes (cyan = font pack, orange = firmware/bootloader = "can't type");
    `poly_prepare_for_flash()` (HID BEGIN) drops to the base layer + bridges it to the
    slave so typing still works. See `oled_helper.c`, `poly_keymap.c` (`flash_rgb_tick`,
    `rgb_matrix_indicators_kb`), `base/fw_staging.c` (`fw_staging_active_target`).

- **Make a pack font resident** (so UI chrome renders with no pack): add its
  generated symbol name to `index.resident_fonts` in `fonts.yaml`, then regenerate.
  It moves out of the pack into `RESIDENT_FONTS[]`. **Front-to-back precedence means
  a resident font WINS over an overlapping pack copy**, so for a *single* glyph
  inside a big pack range (e.g. GUI ❖ U+2756 in the 12 KB `_SymBmp4_`, emoji-layer 😀
  U+1F600 in `_Emojis0_`) add a **tiny dedicated resident font** (`_GuiKey_`,
  `_EmjLayer_`) covering just that codepoint rather than making the whole big font
  resident. The current resident UI-chrome set (≈9 KB) is the modifier symbols
  (Technical/Technical2 = Ctrl/Alt/GUI/Option/Del/Backspace/Esc/PrintScreen), the
  menu icons (Settings ⚙, World 🌐), Brightness moons, Hyper/Meh, GuiKey, Util
  (screenshot/calc/my-computer/paste), EmjLayer, plus the always-resident Arrows.
- **A single bigger/custom glyph → inject it into the resident IconsFont
  (`base/fonts/gfx_icons.h`), NOT a new resident font.** `IconsFont` is `g_all_fonts[0]`
  (prepended), so *extending it with another glyph* (append bitmap bytes + a `GFXglyph`
  record, bump the font's `last`) shifts **no pack index** and needs no reship — it
  ships with the firmware — the OS logos, mouse buttons and lock-key glyphs at
  `0x94`–`0x99` etc. are exactly this. ⚠️ Adding a whole **new resident *font***
  instead (an extra entry in `index.resident_fonts`) prepends ahead of the pack →
  **every pack font's gidx shifts** → a full-pack reship; avoid that for one or two
  glyphs. (Conversely, when a hint can use a *pack* glyph or a base-font character,
  prefer that over a resident icon — the Win+R `>_` was reverted from a bespoke
  16 pt `0x9A`/`0x9B` pair to the plain base-font `">_"` + a drawn frame, and the
  Win+`+`/`-` magnifier from resident `0x9E`/`0x9F` to the pack 🔍 with a
  programmatically-drawn `+`/`-`, reclaiming those C1 slots — 2026-07.)
  - ⚠️ **IconsFont is a range font `0x80..last`; slots `0xA0`+ COLLIDE with printable
    Latin-1** (`0xA0` nbsp, `0xA2..0xA5` = ¢£¤¥, …). Because `IconsFont` is
    `g_all_fonts[0]` it **wins** the lookup, so a custom icon parked at e.g. `0xA4`
    *shadows* the real ¤ — and `CURRENCY_SIGN` (U+00A4) is used in real legends, so
    those keys render the icon instead of the currency glyph (field/CodeRabbit,
    2026-07). **Put custom resident icons in the non-printable C1 range `0x80–0x9F`
    (or a real PUA), never `0xA0+`.** The Win-hint wave-D glyphs violated this
    (`0xA2–0xA5` = settings/cast/sliders/restart) — **RESOLVED 2026-07**: all four
    migrated to the pack (settings→⚙ U+2699, cast→📶 U+1F4F6, sliders→🎛 U+1F39B,
    gfx-restart→🖵 U+1F5B5 + a half-scaled 🗘 overlay), so `IconsFont`'s `last` was
    dropped from `0xA5` to `0x9F` — the whole `0xA0+` tail is gone and **no printable
    Latin-1 is shadowed anymore** (¢£¤¥ render from NotoSans again). The only mid-range
    gap left is `0x9D` (C1 control, harmless).
  - **Removing a glyph from the MIDDLE of the range** (e.g. after migrating a hint
    to the pack): you can't delete it (the array must stay contiguous `first..last`).
    Turn its record into a **gap** `{off,0,0,0,0,0}` and drop its bitmap bytes, then
    **shift every later glyph's `bitmapOffset` down by the removed byte count**. Gap
    glyphs (w==h==xAdvance==0) are skipped by the renderer and fall through to the
    next font — so gapping `0xA0/0xA1` (the old snap arrows) actually *un-shadowed*
    the real nbsp/¡. (The host preview `tools/gfx_font.py` skips gaps too.) **If the
    removed glyphs are the TAIL of the range** (as `0xA2/0xA3/0xA5` were, with the
    intervening `0xA0/0xA1/0xA4` already gaps), just lower the `GFXfont` `last` past
    them instead of leaving trailing gaps — that un-shadows every codepoint above the
    new `last` at once.
  - **A shortcut-hint string is a mini DISPLAY LIST, not just text** (2026-07). The
    hint returned by `keycode_to_disp_overlay()` is interpreted by
    `kdisp_write_gfx_text_cy()` (`disp_array.c`), which understands control-code ops
    on top of the plain glyphs — so extra art (frames, composited icons, drawn signs)
    lives **in the hint string**, and `update_displays()` has **no per-keycode
    special-case** (the old `keycode_hint_wants_frame/_gfx_restart/_mag` gates were
    removed). The ops, built via the `HINT_*` macros in `lang/named_glyphs.h`:
    - `HINT_MOVE(pos)` = `\x0E` + 2 codepoints (x,y) — move the cursor to buffer coords.
    - `HINT_HALF` = `\x0F` — draw the NEXT glyph at half size (2×2-OR downsample via
      `kdisp_draw_glyph_half_at()`; keeps thin strokes plain decimation drops; **round
      the halved dims up** `(w+1)/2` + bounds-check, or an odd-width glyph loses its
      last column — the 🗘 reload is 27×35). Used for the Win+Ctrl+Shift+B monitor+🗘.
    - `HINT_FRAME(sz)` = `\x12` + 2 codepoints (w,h) — 2px nested rounded rect at the
      cursor (the Win+R run-dialog box). `HINT_RESET` = `\x18` resets to the origin.
    - Magnifier `+`/`-` are just base-font `"+"`/`"-"` MOVE-positioned into the lens —
      no bespoke primitive (dropped the `\x10`/`\x11` draw ops as too special-purpose).
    - Fixed positions/sizes are named `HINT_POS_*` / `HINT_SZ_*`. ⚠️ **You cannot write
      decimal coords in a `U"…"` literal** (no way to turn a number into a byte), hence
      named position macros holding `\xHH\xHH`; and **each `\xHH` escape must be
      followed by `\x`/`\u` or a split literal** or the compiler greedily merges the
      hex into one huge codepoint. Derive buffer coords from `tools/gfx_font.py` (it
      replicates the baseline-align math + the ops, so its render matches hardware).
  - **Pack-category headers (`symbol_fonts.h`, etc.) are NOT compiled into the
    firmware** — only `RESIDENT_FONTS[]` + `IconsFont` are `#include`d. So adding pack
    glyphs (⍇/⍈, 🖧) does **not** grow the image; *removing* a resident glyph shrinks
    it. Confirmed by grep: no firmware `.c` includes `symbol_fonts.h`.
- **Regenerate** with `FONTCONVERT=<pinned> python3 generate_fonts.py`. **Byte-repro
  gotcha:** the per-category headers embed the fontconvert *binary path* in a
  provenance comment, so run from the **same path** the committed headers used
  (`/tmp/fontconvert_pinned`) or every category header shows a 1-line diff. Flipping
  a font resident↔pack should change **only** `gfx_used_fonts.h`,
  `fontpack.manifest.json`, `all_fonts_order.json` (and the new font's category
  header) — if other category headers diff, the toolchain/source drifted.
- **Standalone label fonts** (not in `fonts.yaml`/`ALL_FONTS`) are generated by
  `gen-lang-fonts.sh` and used via dedicated single-font arrays: `_Tiny_` 6 px
  (`lang_label_font.h`, lang-code labels) and `_Mid_` 10 px (`util_font.h`,
  `mid_fonts[]` — a size between Tiny and Base for misc utility-key text; a full
  `ll-CC` fits one line at 10 px but overflows 72 px at 14 px).
- **HID flow** (`BEGIN`/`CHUNK`/`COMMIT`, cmds `0x50`–`0x53`): reuses the
  `fw_staging` machinery (deferred sector erase, slave bridge). `FONTPACK_BEGIN`
  carries a **`bundle_id` byte** (data[10]); the master resolves it to the slot via
  `fontpack_slot()`, bounds the pack to the slot size, and `fw_staging_set_fontpack_slot()`
  points the stager at `FW_RESOURCE_OFFSET + slot_off`. The slave resolves the same
  slot from the bridged `fw_up_begin_sync_t.bundle`. ⚠️ **The slave's
  `COMMIT` runs `fw_staging_finalize()` *inside* the `USER_SYNC_FW_UP_COMMIT`
  split-transaction callback (~20 ms window).** For the FONTPACK target that
  re-CRCs the whole ~459 KB pack (`fontpack_load_at`, ~50 ms) → the master timed out
  and mis-reported `COMMIT` as a CRC failure even though the pack loaded (same class
  of bug the master-side finalize comment warns about, "run 6"). **Fix:**
  `fw_staging_finalize_defer_reload()` ACKs on the O(1) transport CRC (already proves
  byte-identity with the master's verified pack) and defers the heavy reload to
  `fw_staging_process_fontpack_reload()` in housekeeping. **Never do heavy work in a
  split-transaction handler.**
- **Wipe** = flash a 32-byte **empty pack** (`font_count == 0`), a valid empty PlyF
  sentinel → that slot contributes no fonts. `polyctl fontpack wipe [id]` wipes one
  slot, or **all** slots when `id` is omitted. ⚠️ **The FONTPACK COMMIT gates success
  on `fontpack_slot_present(slot_off)` (the just-flashed slot loaded as a valid PlyF,
  empty sentinel included), NOT on the whole-pack `fontpack_present()`** — the
  multi-slot loader defines `fontpack_present()` as "≥1 bundle has fonts", which is
  false after a full wipe and falsely failed the last bundle's COMMIT (fixed; was a
  field bug). The pack persists across *firmware* flashing (different flash region).
- **The old 127-font pack still loads on newer firmware** (ABI unchanged,
  `font_count` is read from the header); resident wins on any overlap, the duplicate
  pack copies are harmless. No need to re-flash the pack after a resident change.

See [`AdafruitGFX/CLAUDE.md`](../AdafruitGFX/CLAUDE.md) for `fontconvert` build and usage details.

---

## Future language candidates

Adding a language requires: (1) a new `LANG_*` entry in `lang/lang_lut.c` (code-generated from `lang_lut.xlsx` via cog), (2) re-running `fonts/gen-lang-fonts.sh` to generate the flag glyph and update `flag_fonts.h`, (3) updating the host's `LANG_REGION` map in `PolyKybdHost/polyhost/services/lang_regions.py` if the country code isn't already there. The host map covers all standard ISO 3166-1 alpha-2 country codes; only non-standard or private-use codes need a new entry added manually. Full mechanics in [`lang/FUTURE_LANGUAGES.md`](keyboards/polykybd/lang/FUTURE_LANGUAGES.md) (the "Implementation playbook").

> **STATUS (2026-06-10): `NUM_LANG` is now 156** (11 GET_LANG_LIST ASCII packets) after the
> **2026-06 Europe + Americas minority/sibling batch (Wave 1)** — 13 Latin locales (no new
> font): Europe `eu-ES gl-ES rm-CH cy-GB ga-IE mt-MT lb-LU se-NO`, Americas `gn-PY qu-PE
> ay-BO nv-US nh-MX`. Mostly clones of es-ES/de-CH/fr-CH/en-GB/es-MX; Maltese & Northern
> Sami are genuine new xkb mappings, Welsh/Irish/Navajo add AltGr letters. Only Nahuatl
> needed a frozen-table pseudo-code (`nh`); only Luxembourg needed a host fold (`lu=ch`).
> Wave 2 (Pashto, Cherokee, Inuktitut, Cree — all need new fonts) is pending. See the
> "Europe + Americas minority/sibling batch" section in `lang/FUTURE_LANGUAGES.md`.
>
> **STATUS (2026-06-10): `NUM_LANG` is now 143** (10 GET_LANG_LIST ASCII packets) after the
> **2026-06 compat easy-win batch** — 62 fold/clone locales (no new font), 4–15 per region
> tab, ranked by computer users; see the "2026-06 compat easy-win batch" section in
> `lang/FUTURE_LANGUAGES.md`. Distinct-layout entries are **clones** (AltGr legends inherited);
> US-QWERTY locales are folds. Adding more fold/clone languages needs no `LANG_REGION` edit
> (all ISO country codes are already mapped) and no frozen-table edit (all standard ISO codes).
>
> **STATUS (2026-06-10): the whole Oceania + Africa candidate set below is IMPLEMENTED**, together
> with two extra computer-user picks per non-Europe region tab (see the
> "2026-06 world batch" section in `lang/FUTURE_LANGUAGES.md`): Americas `en-CA` `es-AR`,
> Middle East `ar-IQ` `ku-IQ` (Sorani), Africa `en-NG` `ar-MA`, Asia `ms-MY` `uz-UZ`,
> Oceania `en-PG` `ty-PF`. 23 new entries, `NUM_LANG` 58 → 81 (6 GET_LANG_LIST packets).
> Protocol codes are fixed 2+2 chars, so ISO-639-2/3 languages use pseudo-codes stored
> verbatim: Hawaiian = **`hw-US`** (not `haw`), Sorani = **`ku-IQ`** (not `ckb`), and PNG is
> covered as **`en-PG`** (Tok Pisin has no 2-letter code and types on plain Latin anyway).
> `am-ET` got a real Ethiopic column (xkb `et(olpc)`, new NotoSansEthiopic font);
> the plain-QWERTY locales (`en-AU/NZ/ZA/CA/PG`, `fj-FJ`, `tl-PH`, `sw-KE`, `ms-MY`) are
> id-ID-style folds (flag + OS locale switch, en-US keycaps).

### Oceania
| Code | Language / Country | Notes |
|------|--------------------|-------|
| `en-AU` | English / Australia | Largest tech market in Oceania; distinct locale (date format, spelling) |
| `en-NZ` | English / New Zealand | High tech adoption; ~5 M users |
| `tl-PH` | Filipino / Philippines | Largest Pacific-adjacent user base; geographically SE Asia — host places it in **Asia** submenu via `PH` |
| `mi-NZ` | Māori / New Zealand | Official NZ language; Latin + macrons (ā ē ī ō ū) + okina; active digital revitalisation |
| `hw-US` | Hawaiian / United States | Polynesian; Latin + okina (ʻ) + kahakō macrons. Implemented as pseudo-code `hw-US` — the HID protocol carries fixed 4-char codes, so ISO-639-2 `haw` cannot be stored. Placed in **Oceania** by geographic override (host `LANG_REGION_OVERRIDE` + firmware `REGION_LANGS`), not the US country code's Americas. |
| `sm-WS` | Samoan / Samoa | Most widely spoken Polynesian language; large diaspora in NZ/AU; Latin with macrons |
| `fj-FJ` | Fijian / Fiji | Most developed Pacific island nation outside AU/NZ; Latin-based |

### Africa
| Code | Language / Country | Notes |
|------|--------------------|-------|
| `en-ZA` | English / South Africa | Largest tech ecosystem on the continent |
| `ar-EG` | Arabic / Egypt | ~90 M internet users; complements existing `ar-SA` with Egyptian locale |
| `sw-KE` | Swahili / Kenya | ~200 M speakers across East Africa; Kenya is the continent's leading tech hub; genuinely distinct from existing entries |
| `am-ET` | Amharic / Ethiopia | Unique Ge'ez (Ethiopic) script; ~120 M people; fast-growing tech sector |
| `yo-NG` | Yoruba / Nigeria | ~50 M speakers; Nigeria has Africa's largest developer community; Latin with tone diacritics |
| `af-ZA` | Afrikaans / South Africa | Germanic/Latin; well-established digital presence; distinct from `en-ZA` |

---

## Investigations in progress

### Bug: second half of keyboard becomes unresponsive (slave stops sending key events)

**Symptom**: Intermittently, the right/slave half stops recognising keystrokes. Only keys on the master (USB) side still work. Reconnecting (replugging) or reflashing restores it. Happens "once in a while", not on every boot.

**Root cause identified (2026-04-29)**: Two separate EEPROM-write paths can block the slave's UART long enough to miss a split transaction response window, causing the master to declare the slave unresponsive. Both were introduced in commit `98ed47612d` ("eeprom refactoring — still needs testing", 2026-04-24). On the RP2040, EEPROM is wear-leveled flash; most writes are fast journal appends, but when the journal fills the firmware does a page consolidation (~50 ms blocking erase) — which is when the symptom occurs.

**Path 1 — blocked inside sync transaction handler (rare: only on default-layer change)**:
`eeconfig_update_default_layer()` was called directly inside `user_sync_layer_data_handler()`, a split UART transaction callback. Blocking there guaranteed a UART timeout whenever `def_layer` changed.

**Path 2 — blocked in housekeeping on the slave side (normal typing, brightness keys)**:
`mark_settings_dirty()` was called on the slave from `user_sync_poly_data_handler()` whenever the master synced a contrast or lang change (i.e. after any brightness key press). Five seconds later `brightness_save_if_pending()` fired on the slave in `housekeeping_task_user()`, writing EEPROM. The slave has no need to persist these values — the master is the authoritative owner and syncs them on every boot. This is the likely cause of occurrences during normal typing days with no layout switch.

**Fix applied (2026-04-29)**:
- `split_sync.c` `user_sync_layer_data_handler()`: replaced blocking `eeconfig_update_default_layer()` with `defer_default_layer_save()` — moves the flash write out of the UART transaction callback into housekeeping.
- `state.c` / `state.h`: added `defer_default_layer_save(layer_state_t)` and `default_layer_save_if_pending()`.
- `keymap.c` `housekeeping_task_user()`: added `default_layer_save_if_pending()` call on both sides.
- `brightness_save_if_pending()` was already deferred (5 s debounce, housekeeping) — no change needed there.

**Superseded (2026-06, PR #63 "unify emoji & language layers")**: persistence moved to a
**suspend-only dirty-flag model**. `defer_default_layer_save()` now just sets `g_def_layer_dirty`
(+ pending value) and the actual write is folded into the centralized `save_all_dirty()` in
`state.c`, which flushes every dirty block (settings / latin / default layer / MRU) at the real
flush points only: USB suspend (`suspend_power_down_kb`), the host shutdown signal
(`shutdown_user`), the firmware-update / `mcu_reset` paths in housekeeping, and the manual store
key (`KC_STORE_EE` → `request_eeprom_save` → `save_all_if_requested`). Consequently
`default_layer_save_if_pending()` was **removed** and is no longer called from
`housekeeping_task_user()` — do NOT re-add a per-housekeeping default-layer drain (that was the
old model and reintroduces the frequent in-housekeeping EEPROM write this very bug was about).
Base-layer changes apply immediately and persist on the next suspend/reset/store.

**How to confirm the fix worked**: reproduce by switching the default layer while typing on both halves. If the slave stays responsive, the sync-handler path is fixed.

**If the bug reappears after this fix**, the remaining risk is the RP2040 wear-leveling consolidation (~50 ms page erase) coinciding with a split UART transaction window, triggered by `brightness_save_if_pending()` firing in housekeeping 5 s after a brightness key press. This is a statistical coincidence, not a guaranteed block. Mitigations to try in order:

1. Also defer `save_user_latin()` in `user_sync_latin_ex_data_handler()` — still a direct EEPROM write inside a sync handler (triggered on language changes).
2. `eeprom_update_block()` in `dynamic_keymap_set_buffer_poly()` — also inside a sync handler, only during keymap remapping (VIA), lowest priority.
3. **Proper fix: offload EEPROM writes to core 1.** The keyboard already uses core 1 for RLE decompression via `multicore_exec.c` and the FIFO dispatch. Instead of calling `save_user_settings()` / `save_user_latin()` / `eeconfig_update_default_layer()` directly on core 0, post the write as a job to core 1 via the FIFO. Core 1 does the blocking flash operation while core 0 (QMK main loop, UART, USB) keeps running uninterrupted — eliminating the framing-corruption risk entirely. Main caveat: core 1 is currently single-purpose (RLE decompression), so the two job types must not collide; check that core 1 is idle before posting, or add a small job queue. EEPROM writes and RLE decompression are unlikely to overlap in practice since both are rare and burst-style.

**Relevant files**:
- `keyboards/polykybd/split_sync.c` — all `user_sync_*_data_handler` functions
- `keyboards/polykybd/state.c` / `state.h` — deferred-write helpers
- `keyboards/polykybd/poly_keymap.c` — `housekeeping_task_user()`

---

### Bug: key displays turn on when keyboard is suspended/sleeping

**Symptom**: Per-keycap OLED displays briefly light up (or stay lit) when the keyboard should be in suspend/sleep state.

**Root cause identified (2026-04-29)**: `poly_suspend()` in `keymap.c` clears `STATUS_DISP_ON` and `DISP_IDLE` but did not clear `IDLE_TRANSITION`. If the keyboard was in the fade-out phase (IDLE_TRANSITION set in local_state but not yet propagated to global_state) when USB suspend was triggered, `sync_and_refresh_displays()` — called immediately after `poly_suspend()` from `suspend_power_down_kb()` — detects `back_from_idle_transition = true` (IDLE_TRANSITION in local but not global) and restores `contrast = ee.brightness` from EEPROM, overwriting the `DISP_OFF` value `poly_suspend()` had just set. This triggers `contrast_changed = true` → `set_displays(ee.brightness, false)` → `kdisp_enable(true)` on both master and slave — keycap displays turn on for one suspend cycle before the next iteration corrects it.

**Fix applied (2026-04-29)**: Added `IDLE_TRANSITION` to the flags cleared in `poly_suspend()`:
```c
local_state->flags &= ~((uint8_t)STATUS_DISP_ON) & ~((uint8_t)DISP_IDLE) & ~((uint8_t)IDLE_TRANSITION);
```

**If the bug reappears**: Check whether a split transport failure (the other bug above) is preventing the suspend state from reaching the slave — if the slave never receives `STATUS_DISP_ON=0` it will keep its displays on indefinitely. The two bugs can look identical from the outside.

**Relevant files**:
- `keyboards/polykybd/poly_keymap.c` — `poly_suspend()`, `suspend_power_down_kb()`, `sync_and_refresh_displays()`
- `keyboards/polykybd/base/com.h` — flag bit definitions (`STATUS_DISP_ON`, `IDLE_TRANSITION`, `DISP_IDLE`)

---

### Bug: core1 hangs whenever overlay/ROI data is processed (post-merge regression)

**Symptom**: After merging upstream QMK master into the `PolyKybd` branch (May 2026), the master half hangs whenever the host sends overlay/ROI data over HID. Simple HID commands (GET_ID, brightness, language) still work. Core0 pushes a `CORE1_CMD_*` to the FIFO successfully; core1 starts processing then stops mid-work; core0 blocks in its busy wait for `core1_decomp_count` to catch up, which never happens; that wait loop starves the USB main loop on master, freezing master entirely. Slave keeps running because slave is autonomous (its own scan loop) — slave keypress inversion still works while master is frozen.

**Status (2026-05-15): WORKAROUND APPLIED, ROOT CAUSE NOT YET IDENTIFIED.**

**Workaround (currently in tree)**: `__asm volatile("cpsid i" ::: "memory");` at the top of `core1_entry` in `multicore_exec.c`. Sets PRIMASK=1 — masks all configurable-priority exceptions on core1. Empirically eliminates the hang completely; ROI and DECOMPRESS commands process across many keys/mods with steady `tick` growth and matching counts. Safe because core1 in this codebase has no IRQ-driven work — `multicore_fifo_pop_blocking` polls FIFO_ST, doesn't need an IRQ to wake.

**What we know decisively**:
- `cpsid i` is the actual cure — replacing it with a pure memory clobber (`__asm volatile("" ::: "memory")`) does NOT fix the hang. So PRIMASK=1 is doing the work, not compiler ordering.
- An explicit clear of `NVIC->ICER[0] = 0xFFFFFFFF`, `NVIC->ICPR[0] = 0xFFFFFFFF`, `SysTick->CTRL = 0`, and `ICSR PENDSV/SysTick CLR bits` at core1 entry — without setting PRIMASK — does NOT fix the hang. So the offending exception is NOT one we can prevent by disabling/clearing the standard sources.
- The hang point is **deterministic per build** but **shifts with the workload per inner-loop iteration** (heavier loop body → earlier stop). E.g. tick=101 with full inner loop, tick=143 with writes stubbed, tick=493 in `rle_decompress`. That points to a wall-clock-time-driven event, not iteration count.
- Our state captures (live, sampled inside the inner loop on every iteration) consistently show `ISER=0`, `SysTick CTRL=0`, `ICSR.PENDSVSET=0`, `ICSR.PENDSTSET=0`, `VECTACTIVE=0`. The exception fires and clears between samples — invisible in pre/post snapshots.
- ICSR bit 22 (`ISRPENDING`) is set and ISPR shows many IRQs pending at entry (`0x818a61` = TIMER_IRQ_0, USBCTRL_IRQ, XIP_IRQ, PIO1_IRQ_0, DMA_IRQ_0, SIO_IRQ_PROC0, SIO_IRQ_PROC1, I2C0_IRQ). With ISER=0, none should fire.
- An override of `_unhandled_exception` (the ChibiOS weak fallthrough used by all unhandled vector entries) never fires (`core1_fault_signal=0`). So whatever fires has a *strong* handler installed elsewhere.

**The contradiction**: PRIMASK=1 masks configurable-priority exceptions (NVIC IRQs, SysTick, PendSV). It does NOT mask NMI or HardFault. cpsid eliminates the hang ⇒ the exception is maskable ⇒ SysTick / PendSV / NVIC IRQ. But we've ruled out all three at sample time. The exception must fire so briefly between our inner-loop captures that pending/active bits aren't observable, and the handler that runs must come from a *strong* override (not falling through to our `_unhandled_exception` shim).

**Top remaining suspect — ChibiOS context switch via NMI**. ChibiOS `ARMv6-M-RP2` port with `CH_CFG_SMP_MODE=TRUE` (set in `platforms/chibios/boards/GENERIC_RP_RP2040/configs/chconf.h`) and `CORTEX_ALTERNATE_SWITCH=FALSE` (default) uses **NMI as the context-switch vector** (strong `NMI_Handler` in `lib/chibios/os/common/ports/ARMv6-M-RP2/chcore.c`) and a strong `Vector80` (SIO_IRQ_PROC1) FIFO drain in the same file whose `CH_IRQ_EPILOGUE` triggers NMI via `__port_exit_from_isr` writing `ICSR.NMIPENDSET`. But: NMI is unmaskable by PRIMASK, so this *can't* be what cpsid is preventing — unless the chain is "Vector80 fires (IRQ 16, maskable) → handler triggers NMI". In that case, masking Vector80 (the IRQ) prevents NMI from being triggered. PRIMASK=1 would do that. Catch: `NVIC->ISER` bit 16 is consistently 0 in our captures, meaning Vector80 shouldn't fire. Either our capture has a timing gap that misses a transient ISER bit being set, or some other path triggers it.

**What's currently in tree (post-cleanup, 2026-05-15; updated 2026-05-16)**:
- `keyboards/polykybd/multicore_exec.c` — `__asm volatile("cpsid i" ::: "memory");` at the top of `core1_entry` (with an explanatory comment pointing at this doc). Also: `core0_decomp_count` changed from plain `static uint32_t` to `static volatile uint32_t` (real correctness fix — the compiler could otherwise hoist the load out of the wait loops).
- `keyboards/polykybd/base/multicore/core1.c` — `CORE1_STACK_SIZE` set to 384 (originally 256, briefly bumped to 1024 during the investigation, then sized based on measurement: peak observed ~164 bytes via the `CORE1_STACK_HWM` probe — see `keyboards/polykybd/readme.md` "For developers" → "Diagnostics"). The same file ships an `#ifdef CORE1_STACK_HWM` painting/walking probe that is off by default.
- All diagnostic instrumentation has been removed from `multicore_exec.c`, `base/overlay.c`, and `base/rle.c` apart from the gated HWM probe.
- **Reverted as not-actually-a-race (2026-05-16)**: an earlier cleanup added a `core0_decomp_count != core1_decomp_count` wait to `core1_roi_start()` framed as a "race fix". On re-analysis it was redundant — `CORE1_CMD_RESET_BIT_IDX` only mutates `core1_bit_index`, FIFO ordering guarantees any in-flight DECOMPRESS/ROI_UPDATE finishes atomically before RESET runs, and every caller immediately follows `core1_roi_start()` with `core1_update_roi()` which has its own wait + buffer-write + dmb + push. Now reduced to the bare `multicore_fifo_push_blocking(CORE1_CMD_RESET_BIT_IDX)`.

**Local divergence from SDK that may matter**:
- `keyboards/polykybd/base/multicore/core1.c` reimplements `multicore_launch_core1_*` locally and does NOT call `irq_init_priorities()` (the post-merge SDK version of `core1_wrapper` does, see `lib/pico-sdk/src/rp2_common/pico_multicore/multicore.c:89`). Unverified whether this matters — `irq_init_priorities` only sets `NVIC->IPR` priorities and doesn't enable IRQs, but the priorities affect handler interaction.
- The local `core1_wrapper` has `runtime_run_per_core_initializers()` commented out (function doesn't exist in post-merge SDK anyway).

**Vector address lookup completed (2026-05-15)**. With `cpsid i` reinstated (firmware working), captured the handler address at each vector slot from the live `VTOR=0x10000100` and resolved against the `.elf` symbol table:

| Vector slot | Captured addr | Symbol |
|---|---|---|
| NMI | `0x10011649` | `NMI_Handler` (ChibiOS RP2 port, `lib/chibios/os/common/ports/ARMv6-M-RP2/chcore.c:85`) — strong override; the context-switch handler when `CORTEX_ALTERNATE_SWITCH=FALSE` |
| HardFault, SVC, PendSV, SysTick (and all weak vectors 0x20–0x78) | `0x100002c7` | shared body in `lib/chibios/os/common/startup/ARMCMx/compilers/GCC/vectors.S` that does `bl _unhandled_exception` |
| SIO_IRQ_PROC1 (Vector80) | `0x10011721` | `Vector80` (ChibiOS RP2 port, same file:167) — strong override; drains FIFO_RD, calls `CH_IRQ_EPILOGUE` which can trigger NMI via `__port_exit_from_isr` writing `ICSR.NMIPENDSET` |
| `_unhandled_exception` | `0x10001858` | OUR strong override in `multicore_exec.c` that increments `core1_fault_signal` and infinite-loops |

`core1_fault_signal` stays at 0 across ALL tests. That proves no exception going through the weak `bl _unhandled_exception` shared body ever fires on either core — eliminating HardFault, SVC, PendSV, SysTick, BusFault, MemManage, UsageFault, and Vector20–78. The cure must therefore be masking one of the two strong overrides: **`NMI_Handler`** (unmaskable by PRIMASK — ruled out) or **`Vector80`** (NVIC IRQ 16, maskable by PRIMASK).

**That makes Vector80 the only candidate consistent with `cpsid i` being the fix.** The mystery: every live capture of `NVIC->ISER` reads bit 16 as 0, which says Vector80 should not be deliverable to core1. Either there's a transient enable between our inner-loop samples (e.g. something in the Vector80 handler chain re-enables itself, or a fast handler that runs and finishes between two captures), or RP2040 silicon delivers SIO_IRQ_PROC1 via a path that bypasses ISER (check the FIFO/SIO interrupt model in the RP2040 datasheet — there are NVIC `FORCE` registers and `IPSR` semantics worth re-examining). The chain we suspect: Vector80 fires → its `CH_IRQ_EPILOGUE` writes `ICSR_NMIPENDSET` (see `chcoreasm.S:142–147` `__port_exit_from_isr` for `CORTEX_ALTERNATE_SWITCH=FALSE`) → NMI fires on return → ChibiOS NMI handler runs context-switch logic on a core with no thread state → hang.

**Web search hint (pico-sdk issue #284, "Unable to disable FIFO_IRQ_PROC0")**: there's a known quirk where the SIO FIFO interrupt on RP2040 behaves abnormally — "FIFO_IRQ_PROC (15) keeps firing continuously, and disabling it from the NVIC seems to ignore it", plus "writing `1<<15` to NVIC ISER causes a hard fault". This is exactly the behaviour pattern that fits our observations (`ISER` bit 16 reads 0 in every sample, yet PRIMASK=1 is the only thing that stops the IRQ from being taken). It looks like an SDK / silicon oddity around the SIO FIFO IRQs, not something specific to PolyKybd. That makes `cpsid i` the right shape of fix — there isn't a cleaner per-IRQ disable available.

**If revisiting**:
1. Consider whether the local copy of `multicore_launch_core1_*` in `base/multicore/core1.c` should be replaced with the pico-sdk one (or at least updated to call `irq_init_priorities()`). Unclear it matters given the `cpsid i` mask, but it's a known divergence from the SDK.
2. Try setting `CORTEX_ALTERNATE_SWITCH=TRUE` in the polykybd chconf — that moves ChibiOS's context-switch handler from NMI to PendSV. Wouldn't change whether Vector80 fires on core1, but would make the trap go through PendSV (a maskable exception) instead of NMI, making the failure mode more predictable.
3. If the FIFO IRQ behaviour is investigated further, search for the RP2040 silicon errata / pico-sdk discussions around how `SIO_IRQ_PROC0` / `SIO_IRQ_PROC1` are enabled — they may need to be cleared/disabled via a peripheral-side register rather than NVIC alone.

**Separate but related issue surfaced during this debugging**: when the slave half is flashed with the same firmware as master, master → slave UART split-sync repeatedly fails ("Bridge sync retry … Failed to sync … for transaction UserCompressed / UserRoi"). Flashing slave with a *known-working* firmware (older) cleans up these retries. Deferred — this is a different code path (split_sync.c / split UART transport) from the core1 hang. Worth investigating but out of scope for the core1 fix.

---

### Bug: key display brightness drops to 0 on boot / wake (post-PR-#63 regression)

**Symptom**: Keycap OLED brightness intermittently comes up as 0 on keyboard start and after wake from suspend, without the user having set it to 0.

**Root cause (2026-06-10 — FIXED)**: PR #63's suspend-only persistence flushes `save_user_settings()` at exactly the moments `l_state.contrast` holds a *transient* value, persisting it as the user brightness:
- `suspend_power_down_kb()` calls `poly_suspend()` (sets `contrast = DISP_OFF`) **before** `save_all_dirty()` — a dirty flag set any time since boot persisted brightness 0.
- The slave was hit on *every* suspend: the master syncs `contrast = 0` before the flush, and `user_sync_poly_data_handler()` marked settings dirty on any contrast diff — including the suspend sync itself — then copied 0 into local state.
- The idle paths (`TURN_OFF_TIME` → `poly_suspend()`, fade transition, 0–49 pulsing) also leave transients in `contrast` that a later flush persisted.

**Fix**: `state.c` keeps a `g_user_brightness` snapshot that is updated **only** at deliberate set-points — `inc/dec_brightness()`, the new `set_user_brightness()` (used by the `KC_D*` preset keys and HID cmd 13), `note_user_brightness()` at boot-time EEPROM load, and on the slave when adopting an *awake* master's synced contrast (`contrast > DISP_OFF` and `DISP_IDLE|IDLE_TRANSITION` clear). `save_user_settings()` persists `~g_user_brightness` instead of `~l_state.contrast`. All idle/suspend *restore* paths (`back_from_idle_transition`, fade target, `display_wakeup()`, `suspend_wakeup_init_kb()`, HID stop-idle) now read `get_user_brightness()` instead of re-loading EEPROM — which also means an unflushed brightness change survives an idle/wake cycle (EEPROM was stale there under the suspend-only flush model). The suspend-only flush model itself is unchanged.

**Relevant files**:
- `keyboards/polykybd/state.c` / `state.h` — `g_user_brightness`, `set/note/get_user_brightness()`
- `keyboards/polykybd/split_sync.c` — `user_sync_poly_data_handler` awake-guard
- `keyboards/polykybd/hid_com.c` — cmd 13 (set brightness), cmd 15 (stop idle)
- `keyboards/polykybd/poly_keymap.c` — preset keys, idle/wake restore paths, boot seeding (shared by split72 + split42)

**Follow-up (2026-06-23): host-auto state now persists across reboots.** The
`g_user_brightness` model above keeps the *manual* brightness clean, but it is
**only** updated at deliberate set-points — host-auto/daylight (VOLATILE) pushes
never touch it. So once `g_user_brightness` held a low value (e.g. an old
pre-v5 host that pushed daylight values as plain *persisted* sets wrote a
night-time `2`, or the `KC_DMIN` preset), auto mode *masked* it at runtime but
every reboot re-exposed it: the keyboard boots in **manual** mode (auto is
RAM-only) at the stale `~g_user_brightness` until the host re-engages — "both
halves came up at 2 after a firmware reboot" (field, 2026-06-23). Fix: the
**host-auto mode + last auto value are now persisted** in the freed
`poly_eeconf_t.auto_brightness` byte (`pack_auto_brightness`/`load_auto_brightness`
in `state.c`, bit7 = mode engaged, **bit6 = a real host value is known**, bits0-5 =
value). The known bit is essential: engaging auto *before* the host pushes a value
must NOT persist the default `g_last_auto_brightness` as if real — else the next
boot snaps to it (the FULL_BRIGHT jump `get_active_brightness` guards at runtime).
On load, auto-on-but-not-known comes up in auto mode but falls back to the manual
brightness until the host pushes. `set_brightness_auto_mode` /
`set_auto_brightness_value` set `g_brightness_dirty` so the state flushes at the
next suspend/store; `keyboard_post_init_user` calls `load_auto_brightness()` so a
reboot while host-auto was engaged comes up at the **last auto value** (with
`g_auto_value_known` set) instead of the stale manual one — `set_displays()` now
uses `local_state->contrast` (the restored active brightness), not `ee.brightness`.
The stale `g_user_brightness` stays in EEPROM but is no longer shown while auto is
on. Old EEPROMs read the byte as 0 (auto off) — clean migration. ⚠️ This is the
**one** place an auto-derived value is persisted; it is kept SEPARATE from
`g_user_brightness` (the manual value), so the brightness-0 separation above is
intact. Also: the slave's `user_sync_poly_data_handler` adopt no longer
`mark_settings_dirty()` — it tracks the master's awake contrast in RAM (for
idle/wake restore) but never persists it (the master is authoritative and syncs
brightness every boot), so the slave can't independently bank a stale auto value.

---

### Bug: slave does not show overlay icons after MRU program switch until modifier change

**Symptom**: After the host switches to a new program using the MRU overlay path, the slave half's keycap OLEDs do not display overlay icons. Keys on the master half show correctly. A layer or modifier change (which triggers a full display refresh) makes them appear.

**Root cause (2026-05-17 — FIXED)**: Two missing `request_disp_refresh()` calls, plus `DISPLAY_OVERLAYS` not being included in `OVERLAY_SYNCED_STATE_FLAGS`.

**Fix 1 — slave mapping handler** (`split_sync.c` `user_sync_overlay_map_data_handler`): when the master bridges an overlay mapping chunk to the slave, the slave called `set_10bit_overlay_mapping()` (setting usage bits and pool→display mappings) but never called `request_disp_refresh()`. Added the call so the slave redraws after each mapping chunk lands.

**Fix 2 — master mapping handler** (`hid_com.c` case 21): symmetric gap — the master also called `set_10bit_overlay_mapping()` without a following `request_disp_refresh()`. Added it.

**Fix 3 — ESC (and any key in a later mapping chunk) not appearing** (`base/com.h`): The MRU host sends overlay mappings in chunks of 24 pairs per HID report. For programs with many overlays (e.g. an IDE with all A–Z + numbers), ESC (display_flat_idx=37) falls in the second chunk. Fix 1's per-chunk refresh fires after chunk 1 lands — at that point ESC's usage bit is still 0 — so ESC shows fallback text. Chunk 2 fires another refresh and should correct it, but this creates a transient window. The reliable fix: add `DISPLAY_OVERLAYS` to `OVERLAY_SYNCED_STATE_FLAGS` so that `enable_overlays()` (called by the host after **all** mapping chunks are confirmed ACK'd) force-syncs state to the slave via case 11. The slave detects `state_diff`, calls `request_disp_refresh()`, and renders with all chunks already in place — guaranteed final correct refresh.

**Relevant files**:
- `keyboards/polykybd/split_sync.c` — `user_sync_overlay_map_data_handler`
- `keyboards/polykybd/hid_com.c` — case 21
- `keyboards/polykybd/base/com.h` — `OVERLAY_SYNCED_STATE_FLAGS`

---

### Bug: slave half stuck in the idle pulsing frame — keypress/shift won't wake it, only a brightness key does

**Symptom (field, 2026-06-18)**: After the displays went into the idle *pulsing*
animation, the **slave** half froze on one pulse frame ("some keycaps off, others
very dim") and **did not update at all** — neither a keypress nor Shift brought it
back. The master woke normally and kept logging key events. Pressing a manual
brightness key restored the slave.

**Root cause**: `send_to_bridge()` returns the slave's reply ack **byte**, or
`SYNC_CRC32_ERR` once it exhausts its retries. **All three returns are non-zero**
(`SYNC_ACK 0xCA`, `SYNC_ACK_SIG 0x4D`, `SYNC_CRC32_ERR 0x35`), but three callers in
`poly_keymap.c` `sync_and_refresh_displays()` tested it as a bool —
`if(!send_to_bridge(...))`. `!0x35 == false`, so the failure branch
(`state_diff/layer_diff = false`, "failed to send") was **dead code**: on a
give-up the master fell through, ran `copy_global_state()`/`copy_global_layer()`,
**advanced `global` to `local`**, and so produced no diff next pass → the lost
sync was **never re-fired**. (The accompanying comment block — "the diff IS the
retry queue; global only advances on a successful sync" — described the *intended*
behaviour that the `!` test silently defeated.)

Why it only bit the *pulsing→awake* transition: the pulsing contrast changes every
housekeeping pass, so a dropped frame is replaced by the next fresh diff and is
invisible. **Wake-from-idle is single-shot** (`display_wakeup()` clears
`DISP_IDLE` + restores `contrast` once). If that lone sync's give-up was
mis-classified as success, the master stopped re-sending and the slave — which
only pulses because the master *tells* it to, the idle math is `is_usb_host_side()`
only — stayed on its last received pulse frame indefinitely. A brightness key
mutates `contrast` again → a brand-new diff → fresh send → recovery (matching "the
manual brightness control brought it back"). Also explains why no
`USER_SYNC_POLY_DATA failed to send` line ever appeared in the logs.

**Fix (2026-06-18)**: added `static inline bool sync_succeeded(uint8_t ack)`
(`split_sync.h`, by the `SYNC_*` defines) returning `ack == SYNC_ACK || ack ==
SYNC_ACK_SIG`, and routed all `sync_and_refresh_displays()` send sites through it
(POLY / LAYER / LASTKEY, plus the already-correct MRU send for uniformity). A
genuine give-up now keeps the diff so the send re-fires next pass, as the comments
always claimed. ⚠️ Never bool-test `send_to_bridge()` directly — every return value
is non-zero; classify it with `sync_succeeded()`.

**Relevant files**:
- `keyboards/polykybd/poly_keymap.c` — `sync_and_refresh_displays()` send sites; `display_wakeup()`, `housekeeping_task_user()` (the single-shot wake)
- `keyboards/polykybd/split_sync.h` — `sync_succeeded()` helper + `SYNC_*` values
- `keyboards/polykybd/bridge_helper.c` — `send_to_bridge()` (returns the ack byte / `SYNC_CRC32_ERR`)

---

### Bug: idle mode sometimes never starts; host "start idle" (cmd 15) is a no-op right after boot

**Symptom**: (1) Once in a while the keycaps never enter the idle
fade/pulse/turn-off animation at all — the displays just stay at full brightness
until suspend. (2) The host-side "start idle" HID command (cmd 15, payload ≠ 0)
does nothing when sent within the first ~2 minutes after the keyboard powers on —
the keyboard keeps waiting the full idle timeout instead of idling immediately.

**Root cause (2026-07-07 — FIXED)**: both trace to `base/update.c`'s activity
timestamp `last_update` being a **signed `int32_t` that overloaded a `uint32_t`
timestamp with sentinels** (`-1` = "idle tracking off"), and the housekeeping loop
gating idle on `if(get_last_update() >= 0)`.
- **(1) The 24.86-day sign-bit window.** `update_performed()` stores
  `timer_read32()` (a `uint32_t` ms counter) into the signed `last_update`. Once
  uptime passes ~24.86 days (`timer_read32() ≥ 2³¹`), that value reads back
  **negative**, so `if(update >= 0)` is false and the **entire idle/turn-off block
  in `housekeeping_task_user()` is skipped** — idle silently stops working for the
  ~25-day window until the 49.7-day `uint32` wrap. Intermittent, uptime-dependent →
  "sometimes it doesn't idle".
- **(2) Backdating underflow near boot.** `hid_com.c` case 15's "start idle" set
  `last_update = timer_read32() - FADE_OUT_TIME` to make idle begin one fade-out
  interval "ago". In the first `FADE_OUT_TIME` (120 s) of uptime `timer_read32() <
  120000`, so the signed subtraction went **negative and was clamped to 0** — which
  reads as "just became active", not "idle now", so the fade never triggered. (The
  code even logged `Starting idle in N msec` and then didn't.)

**Fix**: separate the "idle tracking enabled" state from the timestamp.
`base/update.c` now stores `last_update` as a real **`uint32_t`** plus a distinct
`bool idle_tracking` flag; `get_time_since_last_update()` uses `timer_elapsed32()`
(correct modular `uint32` arithmetic at any uptime, including across the wrap).
Housekeeping gates on **`is_idle_tracking()`** instead of the sign of the
timestamp, so idle works for the full 49.7-day timer range. The host "start idle"
path calls the new **`backdate_last_update(FADE_OUT_TIME)`** — modular
`timer_read32() - ms`, correct even when `now < ms`, so idle begins on the next
pass regardless of uptime. The old `set_last_update(-1)` "idle off" calls are now
the clearer **`disable_idle_tracking()`** (suspend / host display-off cmd 24 /
turn-off-reached); `set_last_update(int32_t)` is kept as a thin compat shim (`<0`
disables, `≥0` sets+enables). No wire-protocol change (cmd 15 payload identical),
so no `PROTOCOL_VERSION`/`__protocol__` bump.

**Relevant files**:
- `keyboards/polykybd/base/update.c` / `update.h` — `uint32_t last_update` +
  `idle_tracking`; `is_idle_tracking()`, `disable_idle_tracking()`,
  `backdate_last_update()`
- `keyboards/polykybd/poly_keymap.c` — `housekeeping_task_user()` idle gate
  (`is_idle_tracking()`), the turn-off + `suspend_power_down_kb()` disable calls
- `keyboards/polykybd/hid_com.c` — cmd 15 start branch (`backdate_last_update`),
  cmd 24 display-off (`disable_idle_tracking`)

---

### Bug: keyboard hangs on the boot splash after a firmware apply (slave not rebooted)

**Symptom (field, 2026-06-22)**: After a successful HID firmware flash + apply, the
master rebooted onto the new firmware but **hung on the boot splash** ("SPLIT 72");
no USB enumerated for minutes (`No Interface` in the host log) until the **slave
half was replugged**. Afterwards the split link showed a high steady error rate
(`err=36%`) because master ran new firmware while the slave still ran the old one.

**Root cause**: `CMD_FW_UP_APPLY` (`hid_fw_up.c`) tells the slave to install its
staged image and reboot in lockstep via `send_to_bridge(USER_SYNC_FW_UP_APPLY, …)`,
then arms the master's own reboot **regardless of the slave's ack**. That bridge was
sent with only **5 retries**, so one unlucky drop on this single critical
transaction left the slave on old firmware; the rebooted master then waits for a
slave handshake at split init that never comes → hang. (The master booting alone
into mismatched firmware is exactly why the apply bridges to the slave at all.)

**Fix (2026-06-22)**: bump the slave-apply bridge to **20 retries** and **re-fire the
whole round once** if it still hasn't acked. Safe: the slave apply handler is
idempotent (validates the staged image + arms a *deferred* reboot), and
`send_to_bridge` is **synchronous** (returns only after the slave handled the
message), so by the time the master proceeds to reboot the slave has already armed
its own. Worst case adds ~1 s, only on a bad link.

**Recovery if it recurs**: re-run the flash + **Apply** (re-bridges the install to
the slave, which already has the image staged), or flash the slave directly via
BOOTSEL/UF2. The high `err%` clears once both halves run matching firmware.

**Relevant files**:
- `keyboards/polykybd/hid_fw_up.c` — `CMD_FW_UP_APPLY` (slave bridge retries)
- `keyboards/polykybd/split_fw_up.c` — `user_sync_fw_up_apply_handler` (deferred, ACK-first)

---

### Split-link integrity: wire noise, the app-level CRC32, retries, and the health counter

> **RESOLVED (2026-06-16): migrated the split UART to full-duplex two-wire — the
> ongoing corruption is gone.** `config.h` now sets `SERIAL_USART_FULL_DUPLEX` +
> `SERIAL_USART_TX_PIN GP5` / `SERIAL_USART_RX_PIN GP4` + `SERIAL_USART_PIN_SWAP`.
> GP4 was always wired (a second conductor) but unused — there was no PIO
> full-duplex when the board was brought up; the vendor PIO driver supports it
> now. The cable is **straight** (GP5↔GP5, GP4↔GP4); `SERIAL_USART_PIN_SWAP`
> gives the crossover by swapping TX/RX **only on the master half's init path**
> (`serial_vendor.c`: `serial_transport_driver_master_init` swaps,
> `..._slave_init` does not), so **one identical image** produces the logical
> crossover at runtime by role — no per-side build, no EEPROM handedness. Works
> for the normal single image (USB half = master) and the HIL rig (roles forced
> per image via `POLYKYBD_HIL`, same `is_keyboard_master()`).
>
> **Measured result** via the health counter below: half-duplex was corrupting the
> small frequent syncs (`Failed to sync … UserLayer/UserPoly` lines — i.e. exactly
> the layer-drop + RGB-flash symptoms). On full-duplex, across **858 tx including
> deliberate heavy overlay/RGB load, `crc_err`/`giveup` stayed frozen at the
> boot-only burst (39/13) with `transport_fail=0`** — i.e. **zero** steady-state
> errors; `err%` only decays as the boot burst dilutes (14.3 → 4.5 % and falling).
> The boot burst is unmonitored (it precedes HID-console attach — the counter
> caught what the live log couldn't) and harmless (persistent state, re-delivered
> by the diff re-fire once the link settles). Why it works: full-duplex removes the
> single-wire **bus-turnaround/line-float** hazard and drives push-pull both ways
> (no pull-up), and gives the reply direction its own clean line.
>
> **Consequently the transport-level CRC patch and the upstream QMK PR are SHELVED**
> — they would have fixed *ongoing* payload corruption, which no longer occurs. The
> app-level CRC32 + `PERIODIC_SYNC_RETRIES=3` stay as the cheap backstop that
> absorbs the boot burst. Reopen only if `crc_err`/`giveup` start climbing in
> *steady state* (watch the counter). The analysis below is retained as the record
> of why the link behaves as it does — note the "half-duplex/single-wire/230400/
> 12 mA" descriptions are now historical (pre-2026-06-16).

**The split UART has no payload integrity check of its own — the per-transaction
CRC32 in `split_sync.c` is the only thing that catches a bit flipped by wire
noise in flight.** This is the single most important fact about the link, and
the reason the CRC32 was added (intermittent sync corruption that looked random).

**The link, pre-migration (HISTORICAL — the half-duplex setup in use until the
2026-06-16 full-duplex switch in the RESOLVED note above)**: `SERIAL_DRIVER = vendor` → the RP2040 **PIO
half-duplex, single-wire** driver (`serial_vendor.c`) on **`SERIAL_USART_TX_PIN
GP5`** (no RX pin, no `SERIAL_USART_FULL_DUPLEX` → one shared wire). Baud is
**230400** (`SELECT_SOFT_SERIAL_SPEED 1` in both variants' `halconf.h` →
`serial_usart.h` maps that to 230400; 8× PIO oversampling). TX is driven at
**12 mA** (`GPIO_DRIVE_STRENGTH_12MA`, `serial_vendor.c`) — fast, strong edges
that ring/reflect on a longer split cable.

**What QMK's transport guarantees (almost nothing)** — traced in
`platforms/chibios/drivers/serial_protocol.c`:
- A **1-byte handshake token**: master sends the transaction id, slave echoes
  `tid ^ NUM_TOTAL_TRANSACTIONS`. Proves *a* transaction of that id is starting —
  says nothing about the data bytes.
- A **20 ms** receive timeout (`SERIAL_USART_TIMEOUT`).
- The actual `initiator2target` / `target2initiator` **payload buffers travel
  raw** — no CRC, no checksum, not even parity. A flipped bit inside the 64-byte
  buffer is delivered to the slave callback and the transaction reports
  **success**. Without the app-level CRC32 the slave applies garbage state
  (contrast/flags/layer/overlay bytes) silently. ⚠️ Do **not** remove the CRC32
  thinking the transport covers it — it does not.
- Note the **reply** (`poly_sync_reply_t`, 1 ACK byte) has **no CRC** either; a
  corrupted reply can turn a real `SYNC_ACK` into a non-ACK → master retries (safe,
  idempotent) or, ~1/256, into a false ACK. Low impact, but it's why a tiny
  fraction of `crc_err` counts can be reply corruption rather than payload.

**How CRC32 + retries + noise interact** (the model that drives the retry-count
choice). With `p` = probability a single frame is corrupted (and caught by CRC32),
`N` independent attempts fail this housekeeping pass with prob ≈ `p^N`:
- **CRC32 detects** corruption → slave returns `SYNC_CRC32_ERR` (not `SYNC_ACK`).
- **Retries recover** → `send_to_bridge` re-sends; all handlers are idempotent.
- Retries trade latency/CPU for resilience; **they do not reduce `p`.** A high
  `SPLIT_MAX_CONNECTION_ERRORS` (200, raised for the fw-update erase) is itself a
  tell that `p` is non-trivial.

**Periodic syncs use `PERIODIC_SYNC_RETRIES` (=3)** in `poly_keymap.c`
`sync_and_refresh_displays()` (poly/MRU/layer/last-key). Was briefly cut to **1**
(to avoid the ~400 ms main-loop stall that 10 retries × ~40 ms timeout costs once
`SPLIT_MAX_CONNECTION_ERRORS=200` stops failures fast-failing). **1 was too few**:
the diff re-fire only guarantees eventual delivery of state that *persists* (it
re-sends the current snapshot; global advances only on success), so a *transient*
that reverts to == global before the next successful sync is dropped, and even a
persistent transition leaves the slave visibly stale for a pass+. Field symptoms
at retries=1: layer updates occasionally not propagating (briefly-held momentary
layer lost), and the RGB matrix flashing on the slave for a fraction of a second
(stale disp/RGB until the deferred sync lands). 3 rides through a single glitch
within the same pass while bounding the worst-case stall to ~3 × 40 ms (the active
fw-update path skips this code).

**Measuring `p` — the split-link health counter** (`bridge_helper.c`,
master-side, added 2026-06-16). Every `send_to_bridge` frame is counted and
classified: `ok` / `crc_err` (slave NACK or corrupted reply — payload integrity
miss) / `transport_fail` (timeout/handshake) / `giveup` (retries exhausted).
`send_to_bridge` emits a compact summary every `LINK_STATS_LOG_EVERY` = 200
frames (count-based, no timer — the cadence follows real traffic, so it's dense
during overlay bursts and silent when idle; gated on `debug_enable`):

```text
Split link: 12345 tx crc_err=4 transport_fail=1 giveup=0 err=0.0%
```

`err%` is the all-time detected-error rate over all frames — a direct read on the
wire. **Use it to validate any link change** (baud/cable/drive/termination) by
watching the number move, instead of by feel. `giveup` should stay ~0 with
retries=3; if it climbs, attack `p` at the source.

**Reducing `p` at the source (the real root fix), in order of leverage**:
1. **Lower the baud** — biggest, cheapest software lever. 230400 → 115200
   (`SELECT_SOFT_SERIAL_SPEED 2`) roughly doubles the per-bit sampling margin.
   Cost: overlay transfers (the bulk of UART bytes) ~2× slower; tiny state/layer
   syncs imperceptibly. A/B-test it against the health counter before keeping it.
2. **Driver edge rate** — the 12 mA TX drive in `serial_vendor.c` is strong; a
   slower edge helps signal integrity but lives in QMK core (would be a tracked
   local divergence, not a config knob).
3. **Hardware** — single-wire half-duplex over a TRRS-style cable is the classic
   culprit: ~100 Ω series resistor near the driver (damp reflections), a ground
   conductor twisted with the data line, shorter/shielded cable, solid common
   ground, good connector contact; rule out RGB/SPI/I²C coupling.
4. **Full-duplex two-wire** ✅ **DONE (2026-06-16)** — see the RESOLVED note at the
   top of this section. Removed the single-wire bus-turnaround hazard and drove the
   steady-state error rate to zero, so options 1–3 above were never needed.

**Relevant files**:
- `keyboards/polykybd/split_sync.c` — per-transaction CRC32 (the only payload check)
- `keyboards/polykybd/bridge_helper.c` / `.h` — `send_to_bridge` retries + the link health counters / `LINK_STATS_LOG_EVERY` summary
- `keyboards/polykybd/poly_keymap.c` — `PERIODIC_SYNC_RETRIES`, `sync_and_refresh_displays()`
- `keyboards/polykybd/config.h` — `SPLIT_MAX_CONNECTION_ERRORS`; the full-duplex defines (`SERIAL_USART_FULL_DUPLEX`, `SERIAL_USART_TX_PIN GP5`, `SERIAL_USART_RX_PIN GP4`, `SERIAL_USART_PIN_SWAP`)
- `<variant>/halconf.h` — `SELECT_SOFT_SERIAL_SPEED` (baud)
- `platforms/chibios/drivers/serial_protocol.c`, `drivers/vendor/RP/RP2040/serial_vendor.c` — QMK transport (no payload integrity)

---

### Bug: HIL "get current language" (cmd 7) times out once early in the run — boot-time busy window stalling the main loop

> **⚠️ CORRECTION (2026-06-27): the "flaky rig link" premise this note was written
> on is WRONG.** The rig runs the **same clean full-duplex two-wire split link as a
> shipping keyboard** (identical `config.h` defines; the crossover is done by role
> at runtime via `SERIAL_USART_PIN_SWAP`, not a different cable). There is **no
> "flaky / slow-ACK rig link"** — that phrasing below is superseded. The real
> differentiator is **timing/readiness, not link quality**: the rig fires its first
> HID queries within ~2 s of the master booting, inside the master's boot-time busy
> window (initial 72-keycap OLED render + the one-shot split sync to the
> just-booted slave), and the slave — independently flashed and rebooted on the rig
> (`usb_disconnect()` image) — can still be coming up then. A human user never pokes
> the keyboard that early. The forced-resync analysis below is also partly stale:
> the one-shot gate (`fc6ee693`, `is_transport_connected()`-gated, cleared even on a
> drop) already removed the per-pass spin. Treat the boot-window timing as the cause;
> the exact internal mechanism for the multi-second silence is unconfirmed (no trace).
> The rig-side mitigations live in `polykybd-ctnd` (sustained settle #37, packed-list
> headroom #38).

**Symptom (HIL rig, 2026-06-24)**: The `get current language` test (cmd `0x07`)
times out (`GET_LANG response: None`) and **fails the run**, while the *same*
command answers fine everywhere else in the *same* run — 3× during the runner's
settle phase and again in the later language round-trip read-back. It reproduced
**identically across two consecutive runs** (always test #4, right after the three
GET_IDs), so it is not pure randomness.

**Root cause (diagnosis, not yet fixed)**: the boot-time **forced layer-resync**
in `poly_keymap.c` `sync_and_refresh_displays()`. `g_force_layer_resync` starts
`true` and the master re-sends `USER_SYNC_LAYER_DATA` **every housekeeping pass
until the slave ACKs**, at `PERIODIC_SYNC_RETRIES` (3) per attempt
(`send_to_bridge`). On the rig the slave (the `*_hil_right` image) is **slow to ACK
at boot** (it is independently flashed + rebooted and still coming up — NOT a link
problem; see the correction banner above), so the resync **spins and blocks the master
main loop** for ~3 × the bridge timeout per pass, right in the early window where
the host is issuing its first HID queries — deterministically landing on cmd 7
(test #4). Once the slave finally ACKs, `g_force_layer_resync` clears and the loop
is responsive again (the later `GET_ID stress` shows 0 retries / 3–7 ms latency).

**Why it is almost certainly rig-only (and why it did not block the merge)**: on
real hardware the split link is the reliable **full-duplex two-wire** setup (see
the split-link RESOLVED note above — zero steady-state errors). There the slave
ACKs the **first** attempt, so `g_force_layer_resync` clears on pass 1 with
negligible stall and no HID command is delayed. The flake only manifests on the
rig because of *when* it queries (mid-boot) and the slave's boot latency — not link
quality. PR #85 merged with this HIL test red for exactly this reason.

**What the forced resync is and why it exists** (don't remove it blindly): each
half loads its **own** default layer from EEPROM, and the master only pushes
`USER_SYNC_LAYER_DATA` on a *diff*. So when the active default layer equals the
master's last-synced `global` (e.g. `_L0`/Qwerty = all-zero `global` after a fresh
boot or a fw-apply reboot), a slave that came up with a **stale** default layer
would never be corrected until the next manual layer change. The one-shot resync
forces a single push to fix that. It is gated by `g_force_layer_resync` (set at
boot, cleared on the first successful push); on failure the flag stays set so the
push re-fires — which is exactly the spin that stalls the rig.

**If hardening is wanted** (so it can't spin/stall even when the slave is slow to
come up at boot, without losing the fresh-boot correction): make the forced push a true
one-shot — attempt it **once** (ideally gated on the split transport being
connected so the single try has a real chance) and clear the flag regardless of
ACK, rather than re-firing every pass; or back off its retry cadence instead of
hammering each housekeeping pass. A genuine slave-stale case would then still be
corrected by the next real layer diff. Not done — left optional since real hardware
is unaffected.

**Relevant files**:
- `keyboards/polykybd/poly_keymap.c` — `g_force_layer_resync`, the forced-push branch in `sync_and_refresh_displays()` (`if ( layer_diff || g_force_layer_resync )`)
- `keyboards/polykybd/bridge_helper.c` — `send_to_bridge()` (per-attempt blocking cost = `PERIODIC_SYNC_RETRIES` × bridge timeout)
- `polykybd-ctnd` `station/hil_tests.py` — the `get current language` test (no miss-tolerance, unlike `test_get_id_stress`)
