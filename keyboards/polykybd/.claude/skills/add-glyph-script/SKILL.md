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
python3 .claude/skills/add-glyph-script/emit_sequence.py   # edit its SCRIPTS list
```

## 2. Preview at 72×40 for sign-off — BEFORE wiring

Generate the block, then render it exactly as the hardware does (baseline-align,
per-glyph bitmap) via the host loader:
```bash
FONTCONVERT=/tmp/fontconvert_pinned python3 fonts/generate_fonts.py --only gscript
python3 .claude/skills/add-glyph-script/preview_block.py 0xEA40 26   # base, count
```
Fix mapping/legibility here and get the user's OK. (Missing numerals show as
Unifont hex-boxes — that means the script has no numerals; set `digits:false`.)

## 3. Firmware — enum + PUA block row (NO protocol bump)

- `state.h`: append `GLYPH_<NAME> = N,` to `enum poly_glyph_script` (append-only —
  it's persisted + on the wire; never reorder). `GLYPH_SCRIPT_COUNT` stays last.
- `poly_keymap.c`: add one row to `glyph_script_blocks[]`:
  `[GLYPH_<NAME>] = { 0x<BASE>u, <true|false> },` — `<BASE>` is the next free
  0x40-aligned PUA block (blocks are 0x40 apart starting 0xE800; must match the
  `-F` base in `fonts.yaml`). `digits` = whether the script has numerals.
- `config.h`: bump **`FW_VERSION`** only. Leave `PROTOCOL_VERSION`. Update its v10
  note only if the contract itself changes (it won't).

## 4. fonts.yaml — the gscript entry

```yaml
- {category: gscript, variant: _<Name>_, source: <src>, extra_args: ['-F0x<BASE>'],
   sequence: '<CP, CP, ...>'}   # from step 1; same <BASE> as glyph_script_blocks[]
```
Keep the whole `fantasy` bundle's ranges disjoint (each script its own block).

## 5. Regenerate the fantasy bundle (byte-repro) + reship to the host

Full regen is required to refresh `all_fonts_order.json` + manifests + the bundle
(`--only` won't). Use the **pinned** fontconvert and pass **every** bundle version:
```bash
FONTCONVERT=/tmp/fontconvert_pinned python3 fonts/generate_fonts.py \
  --emit-bundles /tmp/b \
  --bundle-version symbol=5 --bundle-version mideast=1 --bundle-version syllabic=1 \
  --bundle-version asia=1 --bundle-version flags=4 --bundle-version emoji=1 \
  --bundle-version fantasy=<N+1>          # bump fantasy minimally over shipped
```
⚠️ **Full regen drifts the emoji headers** (distro NotoColorEmoji ≠ commit-time) —
revert them and don't reship emoji:
```bash
git checkout -- base/fonts/generated/emoji_fonts.h base/fonts/generated/emoji_fig_fonts.h
# confirm scope: only gscript_fonts.h (new) + all_fonts_order.json + both manifests
# + gfx_used_fonts.h changed; emoji.plyf/symbol.plyf etc IDENTICAL to the host's.
```
Reship only the fantasy bundle:
```bash
cp /tmp/b/fantasy.plyf ../../PolyKybdHost/polyhost/res/fontpack/fantasy.plyf   # path per repo
# update bundles.json fantasy: content_version, size, sha256=sha256(data)[:16]
```
Verify firmware↔host agree on the fantasy slot (offset/size) + version.

## 6. Host — one enum value + one label

- `polyhost/device/command_ids.py`: append `<NAME> = N` to `GlyphScript`
  (byte-identical to the firmware enum).
- `polyhost/gui/host.py`: add `GlyphScript.<NAME>: "<Generic Label>"` to
  `GLYPH_SCRIPT_LABELS`. The tray menu + `polyctl glyph-script` build from the enum
  automatically. No `__protocol__` bump.
- Add the value to `tests/device/poly_kybd_glyph_script_test.py`
  `test_glyph_script_expansion_values`.

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
  is open-ended (v10+). Bump only `FW_VERSION` (+ the host `__patch__` if you ship
  a host change).
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
  onto unassigned slots (Unifont hex-boxes). Aurebesh/SGA use standard numerals.
- **The `GlyphScript` enum is byte-identical across firmware + host** (wire + EEPROM
  value) — append-only, never reorder.
- Selecting a script the keyboard can't render (unknown index or unflashed font)
  correctly shows the normal legend — that's the graceful-degrade, not a bug.
