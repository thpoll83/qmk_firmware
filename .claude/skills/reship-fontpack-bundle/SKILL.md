---
name: reship-fontpack-bundle
description: Regenerate PolyKybd font-pack bundles from the committed firmware headers and reship the changed ones to the host (PolyKybdHost) — WITHOUT needing the pinned fontconvert / FreeType-HarfBuzz build. Use when a bundle's shipped .plyf must be refreshed: after a fonts.yaml render/size tweak, after the shadowed-glyph dedupe, when host and firmware bundle bytes have drifted, or when asked to "reship the symbol/emoji/fantasy/… bundle", "resync the font packs to the host", or "regenerate bundles.json". Handles the finicky parts the manual procedure gets wrong: dedupe parity, the --bundle-version=0 reset trap, minimal version bumps, the per-field bundles.json rebuild (size/sha256), and the firmware committed manifest. NOT for adding a NEW bundle or changing fonts.yaml itself (that needs fontconvert), and NOT for making a font resident (use make-font-resident).
---

# Reship a font-pack bundle to the host

The external-flash font pack ships as N per-family bundles (`symbol`, `mideast`,
`syllabic`, `asia`, `flags`, `emoji`, `fantasy`). Each is a `.plyf` shipped to
**both** the firmware (as the committed `fontpack_bundles.manifest.json` ABI
contract) and the host (`PolyKybdHost/polyhost/res/fontpack/<id>.plyf` +
`bundles.json`, which the host flashes on connect). There is **no ship script** —
this skill is the recipe, and `reship_bundles.py` (beside this file) is the actuator.

**Key fact that makes this cheap:** bundles derive **deterministically from the
committed category headers** (`base/fonts/generated/*.h`). `fontconvert` only
*regenerates those headers* from the TTFs. So for any reship where the headers are
already committed — a dedupe bump, a version-only resync, copying a
firmware-side render change to the host — you do **not** need the pinned
FreeType/HarfBuzz `fontconvert` build at all. The helper rebuilds the `.plyf`
straight from the headers (mirroring the build's default-on `prune_shadowed_glyphs`
dedupe), so it works in any container.

Repos (auto-discovered; override with `--qmk` / `--host`):
- qmk: `<this-skill>/../../../..` (the qmk_firmware root)
- host: sibling `PolyKybdHost/`

## When to use / not use

| Situation | This skill? |
|---|---|
| A `fonts.yaml` render tweak changed a bundle's glyphs; headers already regenerated + committed | ✅ yes |
| The dedupe (or any `fontpack.py` change) altered shipped bytes | ✅ yes |
| Host `.plyf` drifted stale vs firmware (see the "silent lag" note in qmk CLAUDE.md) | ✅ yes |
| Adding a brand-new bundle, or changing which TTF/size a font uses | ❌ needs `fontconvert` + `generate_fonts.py --emit-bundles` |
| Making a font render with no pack | ❌ use `make-font-resident` |

## Procedure

1. **Report what changed** (read-only). From the qmk root:
   ```bash
   python3 .claude/skills/reship-fontpack-bundle/reship_bundles.py --check
   ```
   It regenerates every bundle from the committed headers + dedupe and compares to
   the shipped host `.plyf`, printing one of: `identical`, `DIFFERS (glyph bytes)`
   (real change → reship + bump), or `version-byte only`. For a glyph-byte change it
   also prints a few per-glyph **WxH** diffs — that's how you tell a *render-size
   drift* (metadata `first`/`last`/`yAdvance` match, only bitmap dims differ) from a
   structural change.

2. **Pick the version bumps.** Bump **only** the bundles that changed, **minimally**
   (+1 over the shipped value — check `bundles.json`; no bundle has ever been
   deployed so any increment >shipped works, keep it monotonic and small). Leave all
   others alone — the helper preserves their current shipped version automatically
   (this is the fix for the `--bundle-version` defaults-to-0 reset trap).

3. **Apply the reship** (write mode). Name every changed bundle with its new version:
   ```bash
   python3 .claude/skills/reship-fontpack-bundle/reship_bundles.py --apply symbol=6 emoji=2
   ```
   This: rebuilds the firmware `fontpack_bundles.manifest.json` + `fontpack_layout.h`
   (reflecting the dedupe), copies each changed `<id>.plyf` to the host, and rebuilds
   `bundles.json` (`content_version` / `size` / `sha256=sha256(data)[:16]`) for the
   named bundles only.

4. **Verify**:
   ```bash
   # host resource integrity + decode round-trip
   cd ../PolyKybdHost && .venv/bin/python -m unittest discover -s tests/services -p "fontpack*_test.py"
   # cross-repo shared manifests must stay byte-identical
   cd ../qmk_firmware/keyboards/polykybd
   cmp fonts/noto-fonts.yaml ../../../PolyKybdHost/polyhost/res/fonts/noto-fonts.yaml
   cmp base/fonts/generated/fontpack_render_settings.json ../../../PolyKybdHost/polyhost/res/fontpack/fontpack_render_settings.json
   cmp base/fonts/generated/lang_flags.json ../../../PolyKybdHost/polyhost/res/fontpack/lang_flags.json
   # sanity: a re-run of --check should now report every bundle "identical"
   cd ../.. && python3 .claude/skills/reship-fontpack-bundle/reship_bundles.py --check
   ```
   The `fontpack_reader_test` validates each `bundles.json` `size`/`sha256` against
   the actual `.plyf`, so a green run proves the reship is self-consistent.

5. **Commit on BOTH repos** (only when asked — per repo rules):
   - qmk (`PolyKybd` base): the changed `fontpack_bundles.manifest.json` +
     `fontpack_layout.h` (+ any `fontpack.py`/`fonts.yaml`/header changes that
     motivated the reship).
   - host (`main` base): the changed `<id>.plyf` + `bundles.json`.
   They are companions — land them together so host↔firmware stay in sync.

## Output format

Report per bundle: its new size, new `content_version`, and whether its glyph bytes
changed. State clearly which bundles you reshipped (id + old→new version + reclaimed
or added bytes) and which you left untouched, then the verification result (tests
pass, cross-repo `cmp` clean, `--check` now all-identical).

## Pitfalls

- **Always pass EVERY changed bundle to `--apply`; never bump one you didn't
  actually change.** The helper keeps unnamed bundles at their current version, but
  if you `--apply` a bundle whose bytes didn't change you ship a pointless
  re-flash-on-connect. Trust the `--check` `DIFFERS (glyph bytes)` list.
- **Mirror the build's dedupe.** The shipped bytes reflect `prune_shadowed_glyphs`
  (default-on in `generate_fonts.py`). The helper runs it; if you ever hand-roll a
  regen, run it too or the bundle re-inflates and diverges from what's shipped.
- **`content_version` lives in the `.plyf` header AND `bundles.json`** — they must
  match. The helper writes both from one number; never hand-edit only the JSON.
- **This reships from the COMMITTED headers.** If you changed `fonts.yaml` or a TTF,
  regenerate the headers first (`generate_fonts.py`, pinned `fontconvert`) and commit
  them — otherwise you reship the *old* glyphs.
- **Host `.venv`** is required for the verify step (system python lacks numpy/PIL);
  create it per PolyKybdHost/CLAUDE.md if absent.
- **`flags` uses a pinned high gidx band** (`PACK_EXTRA_GIDX_BASE`), so appending a
  tail font no longer perturbs it — expect `flags` to stay `identical` unless you
  actually regenerated it.
