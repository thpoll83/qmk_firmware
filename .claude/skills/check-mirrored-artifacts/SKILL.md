---
name: check-mirrored-artifacts
description: Verify the files PolyKybd keeps byte-identical across repos have not drifted — the frozen ISO index table (3 copies), the font render manifests, QMK's keycodes.h, the mirrored skills — and, the half no `cmp` can cover, that each GENERATED artifact's generator still resolves its input. Use when a change touches any mirrored or generated file, before a release, when a preview or a decoded value looks wrong for no reason, at the end of a cross-repo session, or when asked "are the copies in sync / did something drift / is this file stale". NOT for syncing font-pack bundles to the host (that is reship-fontpack-bundle) and NOT for keeping two skills identical after you edit one (just `cp`).
---

# Check the mirrored and generated artifacts

Several files must be **byte-identical across repos**, and several others are
**generated**. Both go stale silently, but they fail differently and only one of them is
answerable by `cmp`:

- A **copy** has a counterpart to compare against. A mismatch decodes wrong languages on
  the rig, or pre-fills the wrong controls in the host's font editor.
- A **generated artifact** has no counterpart — only the question *"does its generator
  still resolve its input?"*, and nothing asks it. `layer_names.yaml` rotted through
  **two renames** that way and mislabelled the layout editor's layer tabs against an enum
  that no longer existed. Nothing failed; nothing could.

## 1. The copies — run `check_mirrors.sh`

```bash
bash .claude/skills/check-mirrored-artifacts/check_mirrors.sh
```

It checks twelve pairs and prints `ok` / `DRIFTED` per line, exiting non-zero on any
drift or missing file. Measured clean on 2026-09-14 — a clean result is the normal one,
and is evidence the discipline is working rather than that the check is unnecessary.

| artifact | copies | what drift costs |
|---|---|---|
| `iso_lang_country.py` | qmk `lang/`, host `polyhost/services/`, ctnd `station/` | the rig and host decode **wrong languages** |
| `noto-fonts.yaml` | qmk `fonts/`, host `polyhost/res/fonts/` | the host's "Download Noto…" fetches a different catalogue |
| `fontpack_render_settings.json` | qmk `base/fonts/generated/`, host `polyhost/res/fontpack/` | the font-pack **edit** dialog pre-fills the wrong render options |
| `lang_flags.json` | same two | the editor cannot rebuild a flag glyph correctly |
| `keycodes.h` | qmk `quantum/`, host `polyhost/res/` | the layout editor shows wrong keycode names, files new ones under the wrong tab, and offers values the firmware moved |
| the nine mirrored skills | qmk + host `.claude/skills/` | each copy becomes the newer one for a different note |

⚠️ **The skills are the case where drift is PURE LOSS, not tailoring.** Measured before
they were harmonised: each copy was ahead of the other on a different note, and nothing
flagged it, because a skill has no build, no test and no reviewer. **Copy, never fork.**

⚠️ **`keycodes.h` was an unguarded copy until 2026-09-25.** The upstream merge that day
brought QMK keycodes 0.0.9 into the firmware while the host stayed on 0.0.8. Nothing
compared them, so the gap surfaced only because someone asked.

## 2. The generated artifacts — the half `cmp` cannot see

For each, the check is not "does it match" but "does its generator still run, and does
the output still match what is committed":

| artifact | generator | the check |
|---|---|---|
| `base/fonts/generated/*.h` + `gfx_used_fonts.h` | `fonts/generate_fonts.py` | `python3 fonts/generate_fonts.py --check` |
| `fontpack.manifest.json` | same | covered by `--check` |
| host `polyhost/res/layer_names.yaml` | its export script | ⚠️ **fallback only** since the rot — the keyboard states its own names over HID cmd 35 |
| host preview data | `scripts/export_preview_data.py` | `python3 tools/preview_doctor.py` |
| `lang_lut.c` and the cog blocks | `cog -r` over `lang_lut.xlsx` | re-run cog; the diff must be empty |

**Run the generator, don't trust the artifact.** ⚠️ And when a generator needs an input
path, confirm the path still **resolves** — that is the failure mode, and it produces no
error. A generator whose input has been renamed away silently writes nothing new, and the
committed artifact keeps looking current forever.

⚠️ **Two committed artifacts were already stale before anyone looked**, and
`generate_fonts.py --check` flags them: `fontpack.manifest.json`'s `total_size` (the
manifest is built *before* `prune_shadowed_glyphs` runs, so it emits the unpruned figure)
and `fontpack_render_settings.json`'s missing `latin` records. If `--check` is ever wired
into CI, fix the manifest/prune ordering rather than hand-editing the committed value.

## 3. When something HAS drifted

- **Pick the correct side deliberately, not by timestamp.** For the frozen ISO table the
  qmk copy is canonical (the cog imports it). For `keycodes.h` the qmk copy is canonical
  too: it changes on every upstream merge that bumps `QMK_KEYCODES_VERSION`, and the
  host copy follows (see `merge-upstream-into-branch` step 6c). After copying, check
  where new names land in `categorize()` (`qmk_keycode_helper.py`). For the skills, read both and merge — each
  is likely newer for a different note.
- **`cp`, then re-run the check**, then say in the commit which direction you copied and
  why. A silent re-sync leaves the next person unable to tell which copy was wrong.
- ⚠️ **Adding a standard ISO language needs no table change** — the code is already
  there. Only a new **private pseudo-code** (`hw`, `ku`) appends an entry, and then all
  three copies move together in one commit.
