# The font pack: resident fonts and the external-flash bundles

How fonts reach a keycap — the small resident set compiled into the image, the
eight independently-versioned `PlyF` bundles in the 4-8 MB resource region, the HID
flash transport, and every trap in reshipping one. Moved out of `CLAUDE.md` on
2026-09-10: ~27 KB read while you are in `base/fontpack.c`, `fonts/fontpack.py`,
`hid_fontpack.c` or reshipping a `.plyf`.

Generation itself stays in `CLAUDE.md` § *Font generation* — this file starts once
a header exists and asks how it gets into the image or onto the board.

⚠️ **The single fact that decides most questions here: `g_all_fonts` is scanned
FRONT TO BACK and the resident set is always in front.** So a resident font WINS
over an overlapping pack copy, a second face at native codepoints can never be
reached (which is why the bigger legend tiers are RELOCATED into private PUA), and
adding one resident font shifts every pack font's gidx and forces a full-pack
reship.

**The `reship-fontpack-bundle` skill wraps the reship** — `--check` is what tells
you which bundles actually moved, and you do NOT need `fontconvert` to reship, only
the committed headers.

---

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
  ordered bundles (currently 8: `symbol`, `mideast`, `syllabic`, `asia`, `flags`,
  `emoji`, `fantasy`, `latinbig` — the last carrying the bigger keycap-legend faces,
  see "Keycap legend size" above), each a standalone `PlyF` flashed to its **own fixed sector-aligned slot**
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
      FreeType/HarfBuzz build. The **`reship-fontpack-bundle` skill** wraps exactly
      this (`--check` to report drift, `--apply ID=N` to reship). (Used 2026-07 for
      the dedupe + fantasy reship.)
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
    Latin-1 is shadowed anymore** (¢£¤¥ render from NotoSans again).
    - ⚠️ **The C1 band `0x80–0x9F` is now FULL — 32/32 slots.** The brightness-key
      unification (2026-08-25) took the last nine: the five gaps `0x89 0x8A 0x93
      0x9A 0x9B`, the dead `ICON_BACKSPACE` slot `0x8B`, and `0x9D 0x9E 0x9F` by
      raising `last` `0x9C → 0x9F`. There is no room left for a tenth resident icon,
      and `0xA0+` is not an option — see the shadowing trap above. The next one has
      to go in the **pack** (a real PUA / an existing symbol codepoint), or free a
      slot by migrating an existing icon there.
    - **`python3 tools/check_icon_slots.py` is the gate, and it is the only thing
      that can answer "is this slot free?"** — the named_glyphs sheet's own
      "Distance Helper" column measures the sheet against *itself*, so a codepoint
      that holds a real glyph but has no macro reads as free space. The script cross-
      checks `gfx_icons.h` against `named_glyphs.h` in both directions (every glyph
      named, every macro pointing at a real glyph) and exits 1 on either mismatch.
      Run it after touching either file; picking an occupied slot otherwise fails
      **silently**, because `IconsFont` is `g_all_fonts[0]` and simply wins.
    - ⚠️ **A macro you want GONE cannot just be deleted — most of `named_glyphs.h`
      is COG-GENERATED** (the block from `/*[[[cog` to `//[[[end]]]`, lines 9–1927,
      comes from the glyph sheet). `ICON_BACKSPACE` lived there, so removing the line
      would have come back on the next `cog -r lang/named_glyphs.h` and silently
      re-aliased `0x8B` to a brightness sun. It is `#undef`'d in the hand-written
      tail instead, which survives regeneration and turns any stale use into a
      **compile error** rather than a wrong glyph.
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
- **Standalone UI text fonts** (not in `fonts.yaml`/`ALL_FONTS`, each used via a
  dedicated single-font array) are all generated by **`fonts/gen-status-fonts.sh`**.
  There are **three**: `_Small_` 15 px (`NotoSans_Medium_Base_8pt.h`, the
  status-OLED rows carrying the numbers), `_Mid_` 19 px (`util_font.h`,
  `mid_fonts[]` — the status-OLED **top row**, the fw-update screens, the DOOM HUD
  and misc utility-key text; a full `ll-CC` fits one line here but overflows 72 px
  at 14 px) and `_Nano_` 10 px (`nano_font.h`, the lang-code labels **and**
  split42's layout name — see the 32 px width-budget note below). The Base
  headers previously had **no generator at all** (hand-made from a long-gone local
  `NotoSans-Medium.ttf`); `gen-lang-fonts.sh` now owns only the flag font.
  - ⚠️ **These four are built `-Hauto` (grid-fitted) and sized with `-p` (pixels),
    and that is load-bearing — do not regenerate them with plain `-s`.** NotoSans
    ships as a variable font with **no hinting bytecode** (`maxSizeOfInstructions
    == 0`, no `fpgm`, a 7-byte `prep` that only sets dropout control), and FreeType
    does **not** fall back to its own autohinter when a face has even that stub
    `prep` — so without `-Hauto` they render completely ungridfitted. At 11–21 px a
    stem is 1–2 px, so the two edges of one stem then round independently: the same
    stem lands 1 px on one side of a glyph and 2 px on the other, bowls go lopsided
    and crossbars drop out. That was the "numbers and smaller text look strange"
    report (2026-07); the digits `0 6 8 9` and the 11 px `S` were the worst.
    `fontconvert.c`'s `TT_INTERPRETER_VERSION_35` does **not** cover this — there is
    no bytecode for it to interpret.
  - **The `-p` sizes are measured, not guessed.** Grid-fitting snaps cap-height to
    whole pixels so the reachable heights come in steps, and `-s` (points at a fixed
    141 DPI) only lands on even ppem — 15 px and 11 px are simply not expressible in
    points. Each size was picked to hold the previous header's **string widths**
    while gaining grid-fitting: the status-OLED row gaps went 3/2/3 + 3/3/3 → 4/3/3
    + 4/3/4 (every gap +1 px, nothing moved, bottom still pinned at 63). Re-run
    `.claude/skills/status-oled-layout/measure_bands.py 72` after any size change
    (from the repo root, or anywhere — it derives `tools/` from its own location;
    needs an interpreter with Pillow, e.g. `/root/.qmk_venv/bin/python`).
  - Symbols are named for their **real** size (`NotoSans_Regular_Small_15px7b`,
    `..._Nano_10px7b`, `..._Mid_19px7b`). The old `…8pt7b`/`…6pt7b` names were
    fiction — the "pt" is the 141 DPI convention, so "8pt" was 16 px.
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
  - **FONTPACK_COMMIT has THREE status bytes** (`hid_fontpack.h` `FONTPACK_COMMIT_*`):
    `.` both halves finalized, **`R`** the master's finalize *rejected* the image (staged
    CRC / not a valid PlyF), **`L`** the master committed but the slave did not ACK within
    the bridge's 10 retries — a *link* failure, where the master's copy is live and
    `reply[3..4]` carries its `content_version`. Before the split (2026-08-17) `ok =
    slave_ok && master_ok` collapsed both into `!`, so the host reported *"CRC mismatch or
    the font pack was rejected"* for a pack whose CRC was perfect and whose data was
    already live — sending the field diagnosis after the data for two rounds while the real
    culprit was the split link (`giveup=44` in that window). **This is the same mistake
    `FW_UP_COMMIT` was split into four statuses to fix**, one command over; don't collapse
    them back. Bumps **no** `PROTOCOL_VERSION`: the font-pack commands are dispatched
    independently of it, an old host reads any non-`.` as failure, and a new host maps the
    old `!` to "unspecified" — so it degrades in both directions.
    - **The status selection is a pure `static inline fontpack_commit_status()` in
      `hid_fontpack.h`, unit-tested** (`make test:fw_up_verdict`,
      `FontpackCommitStatusTest`): master rejection outranks a healthy slave, a slave
      *refusal* is `'R'` and not `'L'`, a lost ack is `'L'`, the three bytes are
      distinct, none reuses the legacy `'!'`, and none is a hex digit (the
      string-literal trap below). This is the firmware half of a contract the host
      tests from its side — and the half that matters, since a host fixture can only
      catch the host *misreading* a status, never this end emitting the wrong one.
    - ⚠️ **A status letter that is a HEX DIGIT breaks the literal**: `"P\x52C"` is a single
      `\x52C` escape, not three bytes. `R`/`L` are safe; anything in `[0-9a-fA-F]` needs a
      split literal (`"P\x52" "C"`).
      - ⚠️ **Do NOT try to verify that by grepping the ELF for `PRR`/`PRL`** — an earlier
        version of this note said `strings` shows them "exactly once each", and it does
        not show them at all. The COMMIT reply is **assembled at runtime**, byte by byte,
        by `fontpack_reply_status()` (`data[0]='P'; data[1]=cmd; data[2]=status;`), so no
        such literal exists in any build. Their absence is the *expected* state and reads
        exactly like a broken image — it cost a double-take while verifying a delivered
        `.bin` (2026-08-18). The escape hazard is a **compile-time** property, so check it
        where it lives: read the source literal, or `make test:fw_up_verdict`
        (`FontpackCommitStatusTest.StatusBytesAreSafeInAStringLiteral` pins it). Grep the
        ELF only for status bytes that genuinely ARE emitted as literals.
    - **Re-running COMMIT is free, which is what makes `L` actionable.**
      `fw_staging_finalize_impl` leaves `s_staged_crc`/`s_image_crc`/`s_next_offset`
      untouched and only clears `s_commit_pending`/`s_fw_up_active`, and the slave's
      `flash_stage_commit` is likewise idempotent — so a second COMMIT re-runs the bridge
      with fresh retries and re-reloads, and the host retries instead of re-streaming the
      pack. Unlike the FIRMWARE target there is no header sector to re-erase (FONTPACK
      writes in place), so re-bridging is safe.
  - ⚠️ **Because FONTPACK writes IN PLACE, a slot is a valid, current bundle as soon as the
    last chunk lands — COMMIT is not what makes it so.** `fontpack_load()` validates each
    slot with the pack's own CRC32 over everything after the 32-byte header, so a *complete*
    stream reads back as present at the shipped `content_version` even if COMMIT never
    succeeded (a truncated one fails that CRC and reads as absent, which is why a partial
    write cannot fake a version). The host consequences — never trusting the version
    comparison alone to decide a re-flash — are written up in `PolyKybdHost/CLAUDE.md`
    under the font-pack bundles note.
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
