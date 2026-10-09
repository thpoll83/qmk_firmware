---
name: add-glyph-script
description: >
  Add a fantasy / retro glyph-script override to the PolyKybd keyboard — an
  alternative face for the language-layer letter/digit legends (Tengwar, runes,
  Aurebesh, IBM VGA, C64, Braille, APL, …), selectable over HID cmd 30. Use when
  asked to "add a glyph script / fantasy font / retro typeface", "support <script>
  on the keycaps", "add another option to the Glyph Script menu", or to extend
  enum poly_glyph_script. Handles license-clean font sourcing (Debian packages /
  google-fonts / CC0), the per-key mapping + 72×40 preview for sign-off, the
  firmware enum + glyph_script_blocks[] PUA row, the fonts.yaml -F sequence, the
  byte-repro fantasy-bundle regen + host reship, the host GlyphScript/label, the
  rig test, and docs. NO protocol bump is needed (the index is open-ended since
  v10). NOT for full keyboard languages (use add-polykybd-language), keycap
  shortcut hints (add-polykybd-shortcut-hint), or app overlays (generate-app-overlays).
---

# Add a glyph-script override (fantasy / retro keycap face)

A glyph script replaces the language-layer **letter/digit** legends with an
alternative face while leaving overlays and OS-hints untouched. Selection is HID
cmd 30 (`GET/SET_GLYPH_SCRIPT`), persisted + slave-synced. Every script's glyphs
live in the external-flash **`fantasy`** font-pack bundle, each in its own dense
private-PUA block. The render choke point is `render_key()` in `poly_keymap.c`.

**Key design fact — adding a script does NOT bump the protocol.** Since v10 the
glyph-script byte is an OPEN-ENDED INDEX: `hid_com.c` case 30 ACKs any value
`0..0xFE`, and `glyph_script_codepoint()` returns 0 (→ normal legend) for any
index `>= GLYPH_SCRIPT_COUNT` or whose font isn't flashed. So a new script needs
only: an enum value, a `glyph_script_blocks[]` row, the font, and the host
`GlyphScript`/label. **Do NOT re-add a range NACK or bump `PROTOCOL_VERSION`.**

Reference commit: the v10 "9 more scripts" batch (runes … Braille).

## 0. Source the font — license-clean, for a SOLD product

Prefer, in order: **OFL / CC0 / GPL-with-font-embedding-exception / CC-BY-SA**.
Debian packages are license-vetted and fetchable with no root:

```bash
# no-root Debian font fetch (put the copy under fonts/fantasy/, gitignored):
d=$(mktemp -d); ( cd "$d" && apt-get download <pkg> && dpkg-deb -x ./*.deb ex )
cp "$d/usr/share/fonts/.../Foo.ttf" keyboards/polykybd/fonts/fantasy/Foo.ttf
```

Known-good sources (all embeddable): GNU Unifont (`fonts-unifont`,
`unifont_csur.otf` covers CSUR Tengwar/Cirth/Aurebesh, `unifont.otf` covers
Runic/Braille/APL); VileR PxPlus CP437 (`fonts-pc`, CC-BY-SA); Noto Sans Runic
(google/fonts `ofl/notosansrunic`, static build); SGA (`standardgalactic/alphabet`
raw branch `core`, **CC0**); Commodore 64 = **PetMe64** (KreativeKorp KSRFL, solid
ROM font); Amiga = `fonts-amiga` (OFL).

⚠️ **The OFL "Sixtyfour"/"Workbench" (Homecomputer) fonts are CRT-scanlined by
design — the `SCAN` axis of the variable versions does NOT remove them** (SCAN=0
is default and still striped; BLED=100 is worse). Use PetMe64 for a *solid* C64.
⚠️ Keep user-facing names generic (trademark caveat on fictional scripts — the
*fonts* are fine to embed). ⚠️ Verify coverage before committing to a source:
```bash
python3 -c "from fontTools.ttLib import TTFont; f=TTFont('X.ttf'); c=f.getBestCmap();
print('A-Z',sum(1 for x in range(0x41,0x5b) if x in c),'0-9',sum(1 for x in range(0x30,0x3a) if x in c))"
```
⚠️ **Check the license in the FONT FILE, not only the repo.** A third-party font can
ship with no LICENSE at all (that means all rights reserved), and its OS/2 `fsType`
can forbid embedding even when a license allows it: `4` is "preview & print", and
only `0` (installable) fits a font converted and flashed onto a keyboard. Read both,
plus the license name fields (IDs 13/14), and scan the cmap for logo glyphs, which
are a trademark problem whatever the license says:
```bash
python3 -I -c "from fontTools.ttLib import TTFont; f=TTFont('X.ttf')
print('fsType', f['OS/2'].fsType)
for r in f['name'].names:
    if r.platformID==3 and r.nameID in (0,13,14): print(r.nameID, r.toUnicode()[:120])
print('PUA', sorted(hex(c) for c in f.getBestCmap() if 0xE000<=c<=0xF8FF)[:12])"
```
The C64 keycap font (2026-10) arrived with no license and `fsType` 4, plus the
Commodore logo at U+E000/E001. Its author fixed both after an issue
(szabadkai/c64-keyboard-font#6): CC0 1.0, `fsType` 0, IDs 13/14 set. Pin the
download to that commit (`raw.githubusercontent.com/<owner>/<repo>/<sha>/…`), not
`main`.

**Reading a repo that is not attached to the session:** the GitHub API (and the
GitHub MCP tools) refuse it with 403, but anonymous `git` reads of a public repo go
through the proxy. Clone it into its own empty directory and read it with `python3 -I`:
```bash
git clone -q --depth 1 https://github.com/<owner>/<repo> /tmp/claude-0/src/<repo>
# list a big repo's files without downloading any blob:
git clone -q --depth 1 --filter=blob:none --no-checkout <url> d && git -C d ls-tree -r --name-only HEAD
git ls-remote --tags <url>          # release tags, to pin a download to
```
That is how noto-emoji's move from `fonts/` to `2D/fonts/` was found and pinned.

Add the fetch to `fonts/dl-fonts.sh` (URL or `apt_font`) + a `sources:` line in
`fonts.yaml`.

## 1. Define the per-key mapping → the fontconvert sequence

Key order is **KC_A..KC_Z (a..z)** then, for scripts *with their own numerals*,
**KC_1..KC_0 (1,2,…,9,0)**. Scripts without native numbers (runes/Aurebesh/Cirth)
emit 26 glyphs and leave digit keys as the normal numeral (`digits:false`).

Mapping styles: **cipher/1:1** (Aurebesh/SGA/Braille → the script's own
codepoints), **transliteration** (runes → nearest rune per Latin letter),
**restyle** (VGA/C64/Amiga → ASCII `A..Z 0..9` in a retro face), **symbol-per-key**
(APL → the Dyalog keyboard symbol per key).

Emit the `-S` sequence (source codepoints in key order) with the helper:
```bash
python3 ../../.claude/skills/add-glyph-script/emit_sequence.py   # edit its SCRIPTS list
```

## 2. Preview at 72×40 for sign-off — BEFORE wiring

Generate the block, then render it exactly as the hardware does (baseline-align,
per-glyph bitmap) via the host loader:
```bash
FONTCONVERT=/tmp/fontconvert_pinned python3 fonts/generate_fonts.py --only gscript
python3 ../../.claude/skills/add-glyph-script/preview_block.py 0xEA40 26   # base, count
```
Fix mapping/legibility here and get the user's OK. (A script without numerals
shows BLANK cells after Z: its font has only 26 glyphs, so set `digits:false`.)
The script decodes the column-native (PolyColGfx) bitmaps and centres each glyph
from its bbox in both axes, as `render_key()` does. Before 2026-10 it read them
row-major and every font, shipped ones included, came out as diagonal noise. If a
preview looks like that, suspect the decoder before the font.

## 3. Firmware — enum + PUA block row (NO protocol bump)

- `state.h`: append `GLYPH_<NAME> = N,` to `enum poly_glyph_script` (append-only —
  it's persisted + on the wire; never reorder). `GLYPH_SCRIPT_COUNT` stays last.
- `poly_keymap.c`: add one row to `glyph_script_blocks[]`:
  `[GLYPH_<NAME>] = { 0x<BASE>u, <true|false> },` — `<BASE>` is the next free
  0x40-aligned PUA block (blocks are 0x40 apart starting 0xE800; must match the
  `-F` base in `fonts.yaml`). `digits` = whether the script has numerals.
- `keycode_helper.c`: add the settings-layer key's label to `glyph_script_legend()`
  (≤5 characters, e.g. `"C64K"`). A `_Static_assert` ties that table to
  `GLYPH_SCRIPT_COUNT`, so a missing label is a build error, not a `?` on the key.
- Leave `FW_VERSION` and `PROTOCOL_VERSION` alone. The version is bumped by the
  label-driven auto-bump at merge (no label = patch, which fits a new script).

## 4. fonts.yaml — the gscript entry

```yaml
- {category: gscript, variant: _<Name>_, source: <src>, extra_args: ['-F0x<BASE>'],
   sequence: '<CP, CP, ...>'}   # from step 1; same <BASE> as glyph_script_blocks[]
```
Keep the whole `fantasy` bundle's ranges disjoint (each script its own block).
⚠️ **Put the entry at the very END of the `fonts:` list, not beside the other
gscript entries.** List position is the font's ALL_FONTS index (its gidx), and the
category only picks the bundle. Inserted mid-list, the new font shifts every later
font's gidx, so `latinbig` (and `symbol`'s Mayan font) change bytes and need a
reship too. At the end it moves nothing (C64 keycap, 2026-10).

## 5. Regenerate the fantasy bundle (byte-repro) + reship to the host

The pinned fontconvert is the CMake build in the AdafruitGFX repo
(`thpoll83/Adafruit-GFX-Library`, `fontconvert/`, ~5 min first build); copy the
binary to `/tmp/fontconvert_pinned`. A full regen needs EVERY source font
(`fonts/dl-fonts.sh`), and it stops at the first failed download.

Full regen is required to refresh `all_fonts_order.json` + `gscript_fonts.h` +
`fontpack_render_settings.json` (`--only` won't). Pass **every** bundle at its
CURRENT shipped version from the host's `bundles.json` (an omitted one resets to 0):
```bash
FONTCONVERT=/tmp/fontconvert_pinned python3 fonts/generate_fonts.py -q --emit-bundles /tmp/b \
  $(python3 -c "import json;d=json.load(open('../../../PolyKybdHost/polyhost/res/fontpack/bundles.json'));print(' '.join('--bundle-version %s=%d'%(b['id'],b['content_version']) for b in d['bundles']))")
```
⚠️ **Full regen drifts headers you did not touch**: the upstream source fonts move
under the committed ones (emoji bitmaps; Devanagari glyph NAMES in comments). The
manifests are computed from that drifted in-memory set, so revert the headers AND
re-derive the bundle manifest from the committed headers:
```bash
git diff --stat base/          # expect gscript_fonts.h, all_fonts_order.json,
                               # fontpack_render_settings.json + the two manifests
git checkout -- base/fonts/generated/<every drifted header> \
               base/fonts/generated/fontpack_bundles.manifest.json
cd ../.. && python3 .claude/skills/reship-fontpack-bundle/reship_bundles.py --check
#   -> only `fantasy` should read DIFFERS; then
python3 .claude/skills/reship-fontpack-bundle/reship_bundles.py --apply fantasy=<N+1>
```
`--apply` rebuilds the bundle manifest + `bundles.json` and copies `fantasy.plyf` to
the host. ⚠️ `fontpack.manifest.json` (the full-pack manifest) is NOT rebuilt by it,
and its committed `total_size` was already off by 332 B from the committed headers
in 2026-10. Revert it and apply only your font's entry and its byte delta.
Copy `fontpack_render_settings.json` to the host and `cmp` it (mirrored file).

## 6. Host — one enum value + one label

- `polyhost/device/command_ids.py`: append `<NAME> = N` to `GlyphScript`
  (byte-identical to the firmware enum).
- `polyhost/host.py`: add `GlyphScript.<NAME>: "<Generic Label>"` to
  `GLYPH_SCRIPT_LABELS`, **at the spot in the menu where it belongs**. The tray's
  Keycap Script submenu is built in that dict's order, not the enum's (since
  2026-10), so a related script can sit next to its sibling: "Commodore 64 (keycap)"
  (11) follows "Commodore 64 (screen)" (7). `tests/gui/glyph_script_menu_order_test.py`
  fails if a script is missing from the dict, which would otherwise mean a silently
  absent menu entry. `polyctl glyph-script` still builds from the enum, listing names
  sorted. No `__protocol__` bump.
- Add the value to `tests/device/poly_kybd_glyph_script_test.py`
  `test_glyph_script_expansion_values`.
- Add its `(base, digits)` row to `FIRMWARE_BLOCKS` in
  `tests/services/glyph_script_preview_test.py`, and its name to `SCRIPTS` in
  `tools/glyph_script_demo.py` (that list is zipped against the pack's fonts with
  `strict=True`, so a missing name crashes the docs GIF build).

## 7. Rig — bump the known-max if needed

`polykybd-ctnd/station/hil_tests.py`: if the new script is the highest value, bump
`GLYPH_SCRIPT_MAX`. `test_glyph_script_expansion` (min_protocol 10) already proves
open-ended acceptance; no new test needed per script.

## 8. Docs + build

Update the glyph-script sections (qmk `CLAUDE.md`, host `CLAUDE.md`, ctnd
`CLAUDE.md`) with the new base + font/license. Standalone-compile the mapping
(`gcc` the `glyph_script_codepoint` snippet), run the host device tests, and let
CI build the firmware + run HIL.

## Output

A short summary: the new `GLYPH_<NAME>=N` + PUA base, the font + license, the
reshipped `fantasy.plyf` content_version, and confirmation that NO protocol bump
was needed.

## Pitfalls

- **Never bump the protocol or re-add a range NACK** for a new script — the index
  is open-ended (v10+). Don't hand-edit `FW_VERSION` or the host version either:
  both repos bump them at merge from the PR's label (none = patch).
- **`glyph_script_blocks[]` base MUST equal the `fonts.yaml` `-F` base** and be a
  free 0x40-aligned block; a mismatch renders the wrong/no glyph.
- **`fontconvert` needs an ABSOLUTE `-f` path** — a relative path gives
  `Font load error: 1`. (generate_fonts passes `root / source`; when hand-testing,
  use the absolute path.)
- **Pass EVERY `--bundle-version`** — unspecified bundles reset to content_version
  0, silently un-shipping them.
- **Use `/tmp/fontconvert_pinned`** (FreeType 2.13.3) run from the committed path,
  or every category header shows a spurious 1-line provenance diff.
- **Scripts without numerals: `digits:false`** and emit 26 glyphs — else digits map
  onto unassigned slots (blank keycaps). Aurebesh/SGA use standard numerals.
- **The `GlyphScript` enum is byte-identical across firmware + host** (wire + EEPROM
  value) — append-only, never reorder.
- Selecting a script the keyboard can't render (unknown index or unflashed font)
  correctly shows the normal legend — that's the graceful-degrade, not a bug.
