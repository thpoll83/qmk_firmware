# Design: overlay icon library in flash (`icons.plyi`)

**Status:** agreed design, not started (2026-09-28). This covers the firmware,
the host and the generator. The code changes will come as separate PRs.

**Summary.** Ship the Fluent and Material icons that our overlays use as one flash
bundle, `icons.plyi`, on both halves. The host then fills an overlay pool slot by
sending `(pool slot, icon id, x, y)` instead of the bitmap. The firmware draws the
glyph from XIP flash into the 360 B pool slot. Mapping, enable and rendering are
unchanged.

This is the per-icon counterpart of
[`OVERLAY_FLASH_CACHE_DESIGN.md`](OVERLAY_FLASH_CACHE_DESIGN.md), which stores
whole per-app sets. The two do not conflict. This one needs no per-app flash
state and works for every app built from library icons, including the generic
shortcut overlays.

## 1. Why

Numbers measured on the host tree at `53294fb` (host 1.4.0):

- 41 overlay specs carry 1,582 binding cells. 1,403 cells (89%) are Fluent
  icons and 13 are Material Symbols. The other 166 are custom drawings, Breeze,
  reclaimed GIMP art and text labels.
- Distinct source icons: 431 Fluent, 8 Material, 139 other.
- The 41 specs render those icons at different sizes (`region` 40×36 in 33 specs,
  36×32 in 8, plus one-offs). As a result the 578 source icons become 937
  distinct bitmaps.
- A cold app switch uploads a median of 61 reports and a maximum of 201
  (JetBrains). The host pauses 0.3 s after every 15 reports
  (`settings.py:166-167`). That is 1.2 s of sleep for the median app and 3.9 s
  for JetBrains.
- The pool is RAM. `reset_all_caches()` empties the host's MRU mirror on every
  reconnect, so every reconnect pays the cold cost again. That feeds the
  wipe-and-resend oscillation described in `OVERLAY_FLASH_CACHE_DESIGN.md` §1.

A fill entry is 6 bytes, so one report carries 10 entries. A 40-icon app goes
from about 75 reports to 4, which stays under the 15-report pause.

This is a transport saving. The firmware side of an overlay burst is render-bound
(`OVERLAY_FLASH_CACHE_DESIGN.md` §6: render 117 ms of a 190 ms 8-key burst), and
this design does not change rendering. Phase 0 measures both paths.

## 2. The bundle

### 2.1 Contents

Every Fluent and Material icon that an overlay spec or the generic shortcut
path (`FLUENT_ICONS` / `LEXICON` in `shortcut_icons.py`) uses today: about 440.
The 139 "other" sources are app-specific and stay as bitmap uploads.

Frequency is not the selection rule. A flash reference saves that icon's upload
on every switch into its app and on every reconnect, whether or not another app
shares it.

Size estimate: about 36×36 px cropped (162 B) plus the glyph record, so roughly
75 KB for 440 icons. The first build gives the real number.

### 2.2 Format: PlyF layout, `PlyI` magic

The file layout is identical to a PlyF bundle (`base/fontpack.h`): a 32-byte
header, `fontpack_font_t` records and GFX glyph records, with the CRC32 over
`[32..total_size)`. Only the magic differs: `PlyI` instead of `PlyF`.

Why a different magic:

- The font loader accepts only `PlyF` and the icon loader only `PlyI`, so the
  type is in the data. Today `fontpack_load()` appends every font of every valid
  slot to `g_all_fonts`. With a shared magic, icon ids starting at 0 would sit in
  the legend lookup next to ASCII.
- A font bundle written into the icon slot, or the reverse, fails validation.
  That slot reads as version 0, and the host flashes the right file on its next
  check.
- An old tool that reads PlyF rejects the file. It cannot misread it as fonts.
- The icon format can change later (a default placement per icon, dropping
  `xAdvance`/`yAdvance`/gidx) without touching the font ABI.

The unused header `flags` field (reserved 0) was considered instead. It was
rejected because a tool that ignores it would treat the file as fonts.

`validate_and_append()` takes the expected magic as a parameter, so the
validation code stays shared.

### 2.3 Icon ids

- Icon id = glyph index. The first font record starts at 0. There is no PUA
  base, because the bundle never enters the legend lookup.
- A bundle may carry several font records, one per source font (Fluent TTF,
  Material Symbols TTF). Each covers a contiguous id range `first..last`, and the
  ranges do not overlap. The loader finds the record whose range holds the id.
- **Ids are append-only and frozen**, like `lang/iso_lang_country.py`. The table
  (`icon_ids.yaml`: id, source, name) is the one source of truth. It is mirrored
  byte-identically to the host and added to the `check-mirrored-artifacts`
  skill. A retired icon keeps its id and its glyph.
- New icons append at the end and bump `content_version`.

### 2.4 Rendering

- One pixel size for every icon, chosen at the preview sign-off (§6, step 2).
  The 36×32 and one-off `region` sizes in the templates go away for library
  icons. That is an accepted, visible change for those 8+ specs.
- Generated with the pinned `fontconvert` from the Fluent TTF the host already
  pins (`icon_catalog.py` `FLUENT_REF` `9cf8af0f…`) and the Material Symbols
  TTF. The output is grid-fitted, so it differs from today's cairosvg renders.
  The top 30 get a side-by-side preview (`oled_preview.py`) before the full set
  is built.
- The generator lives with the font generator (`fonts/generate_fonts.py`), so the
  same pinned FreeType/HarfBuzz build makes it byte-reproducible.

## 3. Flash layout (font-pack layout v2)

Take the icon slot from the tail of `latinbig`, which uses 162,528 B of its
0x97000 (618,496 B) slot:

| Bundle | id | Offset (rel. 0x400000) | Size | Change |
|---|---|---|---|---|
| latinbig | 7 | 0x169000 | 0x77000 (487,424) | shrunk |
| icons | 8 | 0x1E0000 | 0x20000 (128 KB) | new |

- No other slot moves, and `latinbig` keeps its start offset and contents. An
  existing board only has to flash the new bundle; the eight font bundles are
  not reshipped.
- 128 KB holds about 750 icons at the estimate above, which leaves room for new
  apps.
- `FONTPACK_LAYOUT_VERSION` 1 → 2 in the generator and `bundles.json`. Neither
  the firmware nor the host checks the value today; it is informational.
- `FONTPACK_BUNDLE_COUNT` 8 → 9. The GET_ID `V` block grows 2 B. The measured
  budget has about 11 B spare, and the `_Static_assert` in `hid_com.c` guards it.
- Transport: the existing BEGIN/CHUNK/COMMIT (0x50–0x52) with bundle id 8. The
  slave receives every CHUNK first, as for fonts.

## 4. Bundle versions from both halves

Today the `V` block reports only the master's slots, so the host cannot tell
whether the slave received a bundle (`slave-unconfirmed`). This design changes
that for all nine bundles:

- At boot, and after each COMMIT, the master reads the slave's per-bundle
  `content_version`. The reply can join the `FLASH_STAGE_STATUS` probe
  (`split_fw_up.c`) or a sibling transaction. Nine versions are 18 B, well under
  `RPC_S2M_BUFFER_SIZE` = 72.
- The `V` block reports `min(master, slave)` per bundle. A half that missed a
  flash reads as behind, and the host's normal autocheck flashes that bundle
  again. The write reaches both halves, so the mismatch repairs itself.
- **Until the slave answers**, the master reports its own versions. When the
  slave's versions arrive, it calls `poly_state_touch()`. The host already polls
  GET_ID every second, sees the `G` counter move and re-reads `V`. The host
  autocheck must therefore re-run on a generation change, not only on connect.
  Reporting 0 while waiting would reflash all nine bundles on every slow boot.
- **No slave attached** (a half on USB alone): report the master's versions.
- The GET_ID layout does not change. An older host just gets more accurate
  numbers.

Once the host sees icon version ≥ N in `V`, both halves have it. The fill command
therefore needs no version field.

## 5. The fill command

### 5.1 Wire format

New core command **41 `FILL_POOL_FROM_ICON`**, `PROTOCOL_VERSION` 19, host gate
`FEATURE_MIN_PROTOCOL["overlay_icons"] = 19`.

| Byte | Content |
|---|---|
| 0 | report id |
| 1 | 41 |
| 2 | entry count, 1..10 |
| 3 | reserved, 0 |
| 4.. | entries of 6 B: `u16 pool_slot` (LE, 0..599), `u16 icon_id` (LE), `u8 x`, `u8 y` |

- `x`, `y` is where the glyph's bitmap top-left lands in the 72×40 frame. The
  host computes it from the glyph record and the anchor it wants. The firmware
  clips to the frame and needs no anchor logic.
- The pool slot is addressed directly. The `N%90, N//90` keycode/modifier trick
  (`OverlayMRUCache.pool_slot_to_firmware_address`) is not needed here.
- Reply `P\x29.` when every entry applied. Otherwise `P\x29!` followed by a
  byte with the index of the first failed entry. The host uploads that entry and
  every later one as bitmaps. At about 4 reports per switch, the round trip
  (p50 3 ms) is affordable, and a failure becomes visible.

### 5.2 Firmware behaviour

1. Validate count, slot range and icon id against the loaded `PlyI` records.
2. For each entry: clear the 360 B pool slot, then blit the glyph from
   `XIP_BASE + FW_RESOURCE_OFFSET + 0x1E0000` into it. The format is row-major
   MSB-first (`bit = y*72 + x`), the same as the uploads write.
3. Bridge the report to the slave on a new transaction (`USER_SYNC_ICON_FILL`,
   ~64 B plus CRC32, under `RPC_M2S_BUFFER_SIZE` = 96; add a `static_assert`).
   The slave runs the same fill from its own flash. The bridge handler records
   the request, and housekeeping executes it; the handler itself does not draw
   (the split-thread rule in `SPLIT_SYNC.md`).
4. Always fill on both halves. Unlike `resolve_upload_side()` for bitmaps, this
   costs no bandwidth.
5. Classify the bridge result with `sync_succeeded()`. A failed report goes into
   a small retry queue (4 × 64 B) that housekeeping drains, as for the mapping
   repair. An image bridge cannot be repaired this way; a fill can, because the
   master still holds the request.

Also:

- Add 41 to `doom_hid_frozen()`, since it writes the pool.
- Add 41 to the `note_overlay_activity()` switch in `hid_com.c:245`, so the burst
  coalesces into one render.
- If the icon bundle is absent, reply `!` with index 0. Do not write the slot.

## 6. Host

1. **Bundle manifest.** `bundles.json` gets the `icons` entry (id 8, file
   `icons.plyi`). `fontpack_bundle.py`, `polyctl fontpack status`, the font-pack
   inspect dialog and the `reship-fontpack-bundle` skill learn the `PlyI` magic.
2. **Preview sign-off** of the top 30 icons: today's render against the
   fontconvert render.
3. **Generator.** `scripts/generate_app_overlays.py` draws every library cell
   from the glyph bitmaps in `icons.plyi`, not from the SVG. Host and keyboard
   then draw from the same bytes, so the two match 1:1 by construction. Every
   template is regenerated once. Beside each PNG it writes a sidecar
   `<stem>.icons.json`: `(variant, keycode) → (icon_id, x, y)`.
4. **Send path.** In `send_overlays_mru`, a cell with a sidecar entry becomes a
   fill entry when the device's protocol is ≥ 19 and its reported icon version is
   ≥ the version the sidecar needs. Every other cell uploads as today. The MRU
   content key for a fill is `("@icon", id, x, y)`, so apps that share an icon
   share its pool slot.
5. **Generic shortcut path.** `icon_catalog.render_overlay` draws library
   concepts from `icons.plyi` as well, and emits fill entries under the same
   rule.
6. The template PNGs keep every cell's pixels, so older firmware and a board
   without the bundle work unchanged.
7. **Licensing.** Add a third-party NOTICE (Fluent System Icons, MIT; Material
   Symbols, Apache-2.0) to the host repo, and to the firmware repo beside the
   generated bundle.

## 7. Rollout

0. **Measure first** (`measure-firmware-perf`): a real full-app burst, today's
   path against 40 fill entries, with host wall time. The existing baseline uses
   8 blank keys and says nothing about this.
1. **Firmware:** layout v2, `PlyI` loader, slave versions and `min()` in `V`,
   cmd 41 with bridge and retry queue, unit tests for the blit and the entry
   parser, and a HIL test (`add-hil-test`). The HIL test fills a slot, then reads
   GET_ID `V` on both halves.
2. **Bundle:** `icon_ids.yaml`, generator, `icons.plyi`, preview sign-off.
3. **Host:** manifest, autocheck on generation change, sidecars, regenerated
   templates, fill path, generic path, NOTICE.
4. **Release order:** protocol 19 means both artifacts ship, host first, then
   firmware (see CLAUDE.md → Releases).
5. **Docs site** after the release that carries it.

## 8. Open points

- **Pixel size:** decided at the step 2 sign-off.
- **Retry queue depth:** 4 reports covers one app switch (~40 icons). A deeper
  queue only matters if the split link drops more than one report per burst,
  which the measured zero error rate does not suggest.
- **Legends from icons:** out of scope. A later change could let the legend path
  read `PlyI` explicitly. Icons must not join `g_all_fonts` implicitly.
