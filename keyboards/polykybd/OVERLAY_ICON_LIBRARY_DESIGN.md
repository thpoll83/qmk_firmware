# Design: fewer reports per app switch (icon library + context coding)

**Status:** agreed design, not started (2026-09-29). This covers the firmware,
the host and the generator. The code changes will come as separate PRs.

**Summary.** A cold app switch uploads every overlay image. Two independent
phases cut the number of HID reports this takes:

1. **Icon library (`icons.plyi`).** The icons the templates share live in flash
   on both halves. The host fills an overlay pool slot by sending a
   `(pool slot, icon id)` pair instead of the bitmap. Mapping, enable and
   rendering are unchanged.
2. **Context-coded images.** An image that still has to be uploaded can use a
   fifth encoding: a JBIG-style context model with a trained 1 KB probability
   table. Almost every icon then fits one report, and two can share a report.

Both phases are per-image choices on the host. Whatever is smallest for an image
is sent; firmware without a feature keeps today's path.

This is the per-icon counterpart of
[`OVERLAY_FLASH_CACHE_DESIGN.md`](OVERLAY_FLASH_CACHE_DESIGN.md), which stores
whole per-app sets. The two do not conflict.

## 1. Why

### 1.1 What a cold switch costs today

Counts from PolyKybdHost's own `send_overlays_mru`, captured against a recording
fake device (`polykybd-ctnd/perf/fixtures/capture_app_switch.py`, host
`53294fb`):

| App | Images | Image reports | Mapping | Prepare + enable | Total | Rate-limit pauses |
|---|---|---|---|---|---|---|
| Word | 39 | 67 | 2 | 2 | 71 | 4 (1.2 s) |
| JetBrains (Windows set) | 99 | 201 | 5 | 2 | 208 | 12 (3.6 s) |
| JetBrains before `dc2a69e` (full shortcut coverage) | 37 | 82 | 2 | 2 | 86 | 5 |

A warm switch (every image already in the pool) is 4 reports for Word and 7 for
JetBrains. Cold happens on the first switch into an app after the keyboard
(re)connects, because the pool is RAM, and after the pool evicts an app.

### 1.2 What it costs on hardware

A user log of a cold JetBrains switch (2026-09-29, 89 images uploaded, 10 already
in the pool from a cancelled switch):

- 180 image reports took **16.7 s**. The 12 rate-limit pauses explain 3.6 s. The
  other ~13 s is **about 73 ms per image report**, against a 3 ms HID round trip.
- Two of the five mapping reports each took ~1 s to write.

A write only takes that long when the keyboard is not reading its endpoint, so
the time is firmware work per report: the slave bridge (every image report is
forwarded with CRC32 and up to 10 retries) or rendering. Which one is not known
yet; the perf harness measures it (§8, step 0).

### 1.3 Where the images come from

Numbers from the 41 overlay specs under `PolyKybdHost/polyhost/res/overlay_sources`:

- 1,582 binding cells. 1,403 are Fluent icons, 13 Material Symbols, and 166
  custom drawings, reclaimed art and text labels.
- The generator already draws every binding whose label is a `LEXICON` concept
  through one shared renderer (`concept_to_share()`), so those cells are
  byte-identical across apps and with the generic shortcut path.
- Custom drawings shared across apps are found by **rendered pixels**, not by
  file name. Names mislead: VS Code's `run.png` was the run-and-debug icon, the
  same drawing as JetBrains' `debug.png`.

Host commit `38b3f96` made 16 concepts pixel-identical across the apps that have
them: run, run-and-debug, stop, step over/into/out and toggle breakpoint
(JetBrains, VS Code, Notepad++), find, settings, history, and Photoshop's brush
-/+, fill FG/BG and clone stamp (Krita, paint.net). Blade, mark in/out and
ripple delete were already shared (Premiere, Resolve).

### 1.4 Expected effect

Reports for a cold switch (all rows keep the per-image fallback to today's
encodings):

| | Word | JetBrains |
|---|---|---|
| Today | 71 | 208 |
| Phase 1: icon library | 16 | 66 |
| Phase 2 alone: context coding | 43 | 107 |
| Phase 2 alone, with 2 images per report | ~24 | ~57 |
| Phase 1 + 2 | 11 | 35 |
| Phase 1 + 2, with 2 images per report | ~9 | ~23 |

Phase 1 under the agreed selection rule (§2.1): Word 34 library icons (2 fill
reports) and 5 uploads; JetBrains 74 library icons (3 fill reports) and 25
uploads, which are its app-only drawings and the ESC mark.

## 2. Phase 1: the icon library

### 2.1 Selection rule

An icon is in the bundle when it is:

- a Fluent or Material Symbols icon used by any shipped template or the generic
  shortcut path, or
- drawn by the shared concept renderer, or
- a custom drawing whose **rendered 72×40 bitmap** is used by two or more apps.
  `sublime` and `sublime_mac` count as one app.

App-only custom drawings stay bitmap uploads. When a second app uses the same
concept, the drawing is unified (one wins, the other app's template changes) and
the glyph joins the bundle.

Concepts that must look the same in every app that has them (run, run-and-debug,
stop, the step icons, breakpoint) are kept identical in the templates, not only
in the bundle. The command palette is deliberately not unified: VS Code, Sublime,
Obsidian, Windows Terminal and JetBrains' Find action keep their own icons.

Estimated size: about 500 glyphs, ~85 KB.

### 2.2 Format: PlyF layout, `PlyI` magic

The file layout is identical to a PlyF bundle (`base/fontpack.h`): a 32-byte
header, `fontpack_font_t` records and GFX glyph records, with the CRC32 over
`[32..total_size)`. Only the magic differs: `PlyI` instead of `PlyF`.

- The font loader accepts only `PlyF` and the icon loader only `PlyI`, so the
  type is in the data. `fontpack_load()` appends every font of every valid slot
  to `g_all_fonts`; with a shared magic, icon ids starting at 0 would sit in the
  legend lookup next to ASCII.
- A font bundle written into the icon slot, or the reverse, fails validation.
  The slot reads as version 0 and the host flashes the right file next time.
- An old tool that reads PlyF rejects the file instead of misreading it.
- The unused header `flags` field was considered and rejected: a tool that
  ignores it would treat the file as fonts.

`validate_and_append()` takes the expected magic as a parameter, so validation
stays shared.

### 2.3 Icon ids

- Icon id = glyph index, starting at 0. No PUA base, because the bundle never
  enters the legend lookup.
- **Ids are append-only and frozen**, like `lang/iso_lang_country.py`. The table
  (`icon_ids.yaml`: id, source, name) is mirrored byte-identically to the host
  and added to the `check-mirrored-artifacts` skill. A retired icon keeps its id
  and glyph.
- New icons append and bump `content_version`.
- Ids below 512 fit the 9-bit fill pairs (§2.6). About 500 icons today; beyond
  511, those pairs travel at 10 bits and nothing else changes.

### 2.4 Glyphs are cut from the generator's own cells

The bundle is built from the generator (`scripts/generate_app_overlays.py`), not
by re-rendering source art:

- Each library cell is the 72×40 mask the generator draws today. The glyph is
  that mask cropped to its ink bounding box; `xOffset`/`yOffset` are the box's
  position in the 72×40 frame.
- So **placement is baked into the glyph**. The firmware draws every icon at the
  same origin, and no x/y travels on the wire.
- The generator then draws the template cells from the bundle's glyphs. Host
  templates and keyboard use the same bytes, so a mismatch is impossible.
- No `fontconvert`, FreeType or cairosvg in the bundle build. Two rasterisers
  never agree to the pixel (measured: 81 of 2,880 pixels differ between cairosvg
  and FreeType on the same Fluent art), and custom drawings have no font anyway.
- The same icon rendered at two sizes (templates use regions of 40×36 and
  36×32) is two glyphs. Unifying region sizes across specs is optional cleanup.

### 2.5 Flash layout (font-pack layout v2)

Take a **256 KB** icon slot from the tail of `latinbig`, which uses 162,528 B of
its 0x97000 (618,496 B) slot:

| Bundle | id | Offset (rel. 0x400000) | Size | Change |
|---|---|---|---|---|
| latinbig | 7 | 0x169000 | 0x57000 (356,352) | shrunk, still 2.2× its use |
| icons | 8 | 0x1C0000 | 0x40000 (262,144) | new |

- No other slot moves, and `latinbig` keeps its start and contents. An existing
  board only has to flash the new bundle; the font bundles are not reshipped.
- 256 KB holds about 1,500 glyphs at ~170 B, three times today's estimate.
- `FONTPACK_LAYOUT_VERSION` 1 → 2 in the generator and `bundles.json`
  (informational today; neither side checks it).
- `FONTPACK_BUNDLE_COUNT` 8 → 9. The GET_ID `V` block grows 2 B (about 11 B
  spare; `_Static_assert` in `hid_com.c`).
- Transport: the existing BEGIN/CHUNK/COMMIT (0x50–0x52) with bundle id 8.

### 2.6 The fill command

New core command **42 `FILL_POOL_FROM_ICON`**, `PROTOCOL_VERSION` 20, host gate
`FEATURE_MIN_PROTOCOL["overlay_icons"] = 20`. (Cmd 41 and v19 went to phase 2,
which was implemented first.)

It uses cmd 33's width-packed pair format:

| Byte | Content |
|---|---|
| 0 | `P` |
| 1 | 42 |
| 2 | value width in bits, 8..11 |
| 3..63 | 61 bytes of `(pool slot, icon id)` pairs, each value at that width (`map_codec`) |

- 61 bytes = 488 bits: 27 pairs at 9 bits, 24 at 10. Like cmd 33 there is no
  count; padding repeats the last pair.
- The host allocates icon fills from pool slots 0–511, so a pair fits 9 bits
  while icon ids are below 512. It plans reports with
  `plan_mapping_reports()`, which already groups pairs by the width they need.
- The pool slot is addressed directly, without the `N%90, N//90` keycode trick
  the image uploads use.
- Reply `P\x29.` when every pair applied. Otherwise `P\x29!` and the index of
  the first failed pair. The host uploads that pair's image and the rest as
  bitmaps.

Firmware behaviour:

1. Decode pairs with `map_codec_read()`; validate slot < 600 and the icon id
   against the loaded `PlyI` records.
2. For each pair: clear the 360 B pool slot, then blit the glyph from XIP flash
   at its baked offsets (row-major, MSB-first, `bit = y*72 + x`).
3. Bridge the report to the slave as a `{width, bytes, data}` block, like
   `USER_SYNC_OVERLAY_MAP_DATA` (under `RPC_M2S_BUFFER_SIZE` = 96; add a
   `static_assert`). The slave fills from its own flash. The handler records the
   request and housekeeping executes it (the split-thread rule in
   `SPLIT_SYNC.md`).
4. Always fill on both halves: unlike image bridges, this costs no bandwidth.
5. Classify the bridge result with `sync_succeeded()`. A failed report goes
   into a small retry queue (4 × 64 B) drained by housekeeping, the same shape
   as the mapping repair. Unlike an image, a fill can be repaired, because the
   master still has the request.
6. Add 42 to `doom_hid_frozen()` and to the `note_overlay_activity()` switch in
   `hid_com.c`.
7. Icon bundle absent: reply `!` with index 0, write nothing.

### 2.7 Bundle versions from both halves

The `V` block reports **`min(master, slave)`** per bundle, for all nine bundles.

- The master reads the slave's per-bundle `content_version` at boot and after
  each COMMIT (in the `FLASH_STAGE_STATUS` reply or a sibling transaction;
  18 B, under `RPC_S2M_BUFFER_SIZE` = 72).
- A half that missed a flash reads as behind, and the host's normal autocheck
  flashes that bundle again. The transport writes both halves, so the mismatch
  repairs itself.
- Until the slave answers, the master reports its own versions, then calls
  `poly_state_touch()`. The host sees the `G` counter move and re-reads `V`, so
  the autocheck must re-run on a generation change, not only on connect.
- No slave attached: report the master's versions.
- The GET_ID layout does not change. Once the host sees icon version ≥ N, both
  halves have it, so the fill command needs no version field.

### 2.8 Host

1. **Bundle manifest.** `bundles.json` gets the `icons` entry (id 8,
   `icons.plyi`). `fontpack_bundle.py`, `polyctl fontpack status`, the font-pack
   inspect dialog and the `reship-fontpack-bundle` skill learn the `PlyI` magic.
2. **Generator.** Builds `icons.plyi` and `icon_ids.yaml` from the templates'
   cells (§2.4) and draws library cells from the bundle. Beside each template it
   writes `<stem>.icons.json`: `(variant, keycode) → icon_id`.
3. **Send path.** In `send_overlays_mru`, a cell with a sidecar entry becomes a
   fill pair when the device's protocol is ≥ 20 and its reported icon version
   covers the id. Everything else uploads as today. The MRU content key for a
   fill is `("@icon", id)`, so apps sharing an icon share its pool slot.
4. **Generic shortcut path.** `icon_catalog.render_overlay` concepts that are in
   the bundle go out as fill pairs under the same rule.
5. Template PNGs keep every cell's pixels, so older firmware and a board without
   the bundle work unchanged.
6. **Licensing.** A third-party NOTICE (Fluent System Icons, MIT; Material
   Symbols, Apache-2.0) in the host repo and beside the bundle.

## 3. Phase 2: context-coded images

### 3.1 The encoding

A fifth image encoding next to plain, RLE, ROI and RLE-ROI:

- The ROI (as today) is coded pixel by pixel. Each pixel's probability comes
  from a table indexed by its 10 already-decoded neighbours (the JBIG template:
  two rows above, two pixels to the left), so 1,024 contexts.
- The table is **static**: 1,024 trained 8-bit probabilities, 1 KB.
- A binary range coder turns the probabilities into bits. Decoding a bit is a
  multiply by the 8-bit probability, a compare and a renormalise: no division.

Measured on the real icons, with the table trained on the other apps' icons only
(the app being measured was never in the training set):

| | Word | JetBrains | All 724 template images |
|---|---|---|---|
| Today (best of four) | 67 reports | 201 | 1,332 |
| Context model, adaptive, empty start | 39 | 110 | 761 (57%) |
| **Context model, static trained table** | **39** | **100** | – |
| Average size (static table) | 25.7 B | 30.1 B | – |
| Images that fit one report | 39/39 | 98/99 | – |

Alternatives measured and rejected:

| Encoding | All 724 images |
|---|---|
| ROI + Elias-gamma run lengths | 92% |
| deflate (LZ77 + Huffman) | 92% |
| 8×8 tile dictionary in flash (1,023 tiles) | 88% (icons share few tiles: 5,373 distinct of 10,551 uses) |
| ROI, row XOR the row above, then run lengths | 83% |

Seeding an adaptive model from the trained table came within 1% of the static
table, so the static table is used: no per-image state, the simplest decoder.

> **Implemented** as **PRC (Predictive Range Coding)**, cmd 41, protocol v19:
> qmk_firmware#316 and PolyKybdHost#283 (branch `claude/overlay-context-coding`). It differs from the plan
> below in three places: records are addressed by keycode + modifier like cmds
> 16-19 rather than by pool slot, a record carries only context-coded images
> (larger ones keep the old commands), and the decode runs on core0 because the
> HID receive is already gated on core1 being idle. `PROTOCOL_HISTORY.md` v19
> has the record layout. Measured through the host send path: cold-switch image
> reports over all 118 templates fall from 4348 to 1753 (40%).

### 3.2 Per-image choice

The host adds the context coder as a fifth candidate in
`send_smallest_overlay()`'s `min()`, and only picks it when it saves a report.
Older firmware never sees it (protocol gate).

### 3.3 Multi-image reports

At ~26–30 B per image, two images fit one 62-byte payload. A packed report
carries records, each with its own encoding:

| Field | Content |
|---|---|
| pool slot | 10 bits |
| encoding | 3 bits (ROI, RLE-ROI, context) |
| ROI box | as today's ROI header |
| length | 1 byte |
| payload | encoded ROI |

Images that do not fit a shared report go out on their own, as today. Exact
field packing is pinned in the implementation PR.

### 3.4 Firmware

- Decoder ~1 KB of code plus the 1 KB table.
- Runs on core1 like RLE (`core1_decompress_fragment`). A 36×40 ROI is ~1,400
  pixels at roughly 100 cycles each: ~0.7 ms per icon at 200 MHz.
- The slave receives the same compressed record over the bridge and decodes it
  itself. Smaller records also shorten the bridge transfer.
- New command id and a protocol bump of its own, independent of phase 1 (done:
  cmd 41, v19).

### 3.5 The table

- Trained by a host tool from the shipped templates, reproducibly (same input,
  same bytes).
- Versioned. Host and firmware must use the identical table, like the frozen ISO
  index table. A retrained table is a new table id; the firmware keeps the ones
  it supports, and the host only uses an id the device reports.

## 4. Rollout

0. **Measure first** (polykybd-ctnd #99): replays the recorded Word and JetBrains
   switches on the rig, cold and warm, and splits firmware time into bridge,
   render and rest. That says how much of the ~73 ms per image report fewer
   reports actually save.
1. **Phase 1 firmware:** layout v2, `PlyI` loader, slave versions and `min()` in
   `V`, cmd 42 with bridge and retry queue, unit tests (pair parsing via
   `map_codec`, the blit), a HIL test (`add-hil-test`).
2. **Phase 1 bundle and host:** `icon_ids.yaml`, bundle build from the
   generator, sidecars, regenerated templates, fill path, generic path, NOTICE.
3. **Phase 2:** the table tool, the encoder and a golden-vector test shared by
   host and firmware, the decoder, then multi-image reports.
4. **Releases:** each protocol bump ships both artifacts, host first, then
   firmware (CLAUDE.md → Releases).
5. **Docs site** after the release that carries it.

Host work already done: per-switch report counting (`871aaee`) and the 16 shared
concepts in the templates (`38b3f96`), both on PolyKybdHost branch
`claude/overlay-icons-flash-wcohhi`.

## 5. Open points

- **Retry queue depth:** 4 reports covers one app switch.
- **Context table location:** compiled into the firmware (simplest; a new table
  needs a firmware release) or carried in `icons.plyi` (updates with the bundle,
  but then phase 2 depends on phase 1). Leaning towards compiled in.
- **Legends from icons:** out of scope. A later change could let the legend path
  read `PlyI` explicitly. Icons must not join `g_all_fonts` implicitly.
