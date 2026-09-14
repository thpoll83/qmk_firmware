# Mirrored skills, and what actually costs context

Moved out of `CLAUDE.md` 2026-09-14. Verbatim.

## Mirrored skills (`qmk_firmware` ↔ `PolyKybdHost`)

Six skills exist in **both** repos and are kept **byte-identical**:
`add-gated-hid-command`, `mutation-test-suite`, `polykybd-github-release`,
`session-retro`, `triage-pr-review`, `update-polykybd-docs`. A skill loads only from the repos a session has attached,
so one that describes cross-repo work is unreachable from a session opened on the
other repo alone — which is what happened to `mutation-test-suite`, extended to
cover Python/unittest suites while living only in the firmware repo.

⚠️ **They had already drifted, and every difference was pure loss — not repo-specific
tailoring.** Measured 2026-09-07 before harmonising: `session-retro` lacked the whole
open-PR-sweep section on the host side, `update-polykybd-docs` lacked its Images
section there, and `polykybd-github-release` was missing the shallow-clone warning on
the host side and the corrected WinCompose `status.txt` ordering on the firmware side —
i.e. each copy was the newer one for a different note. Nothing anywhere flagged it,
because a skill has no build, no test and no reviewer.

**So the rule is copy, never fork**: edit one, `cp` it to the other, and check with

```bash
for s in add-gated-hid-command mutation-test-suite polykybd-github-release session-retro triage-pr-review update-polykybd-docs; do
    cmp -s /home/user/qmk_firmware/.claude/skills/$s/SKILL.md \
           /home/user/PolyKybdHost/.claude/skills/$s/SKILL.md \
      && echo "$s: ok" || echo "$s: DRIFTED"
done
```

A firmware-specific section in the host's copy (or the reverse) costs a reader one
skipped paragraph; a fork costs a note that only one repo ever sees. Take the first.
If a skill ever genuinely needs to differ per repo, split the differing part into a
separate skill rather than forking the shared one.


✅ **FIVE skills lived at `keyboards/polykybd/.claude/skills/` and did NOT load — MOVED
to the repo root `.claude/skills/` on 2026-09-09, and the fix is confirmed.** Measured
before the move: the session's available-skills list contained none of
`add-glyph-script`, `add-polykybd-shortcut-hint`, `keycap-layout-preview`,
`status-oled-layout` or `tune-lang-lut-cells`, while all seventeen at the repo root
loaded normally. **Do not theorise the mechanism** — what was measured is the location
and the absence. A skill added later goes at the repo root; one under `keyboards/` is
unreachable.

- **The cost is silent and it was paid the same day.** `keycap-layout-preview` is
  exactly the model-the-draw-path-and-measure-ink-against-ink loop that the RGB legend
  work (qmk#281) re-derived by hand for twelve legends; it even ships a `half_ink()`
  for `HINT_HALF`, the op that shaped that whole layout. Nothing anywhere said the
  skill existed, because the file that mentions it is this one and the pointer looked
  live.
- ⚠️ **The move was NOT a `git mv` — TWO helper scripts encoded their nesting as
  hardcoded `../` depths**, and a relocated script then fails in a way that reads as a
  broken skill rather than a wrong path. `add-glyph-script/preview_block.py` walked
  **six** levels up to reach `PolyKybdHost/tools` and three to reach
  `base/fonts/generated/`; `status-oled-layout/measure_bands.py` walked three to reach
  `tools/`. Both now **derive** the qmk root by walking parents for `keyboards/polykybd`,
  which is depth-independent and is the pattern to copy. Four `SKILL.md`s also carried
  invocation strings relative to `keyboards/polykybd/` (`python3 .claude/skills/…` →
  `python3 ../../.claude/skills/…`).
  - ⚠️ **`keycap_preview.py` was NOT `../`-coupled, and this note said it was.** It
    already walked up for `keyboards/polykybd` and needed no edit at all — asserted
    here by analogy with the other two rather than checked, in the note whose whole
    subject is a stale pointer. **Baseline every script BEFORE relocating it**, so a
    post-move failure cannot be confused with one that never worked; all five produced
    identical output afterwards.
- ⚠️ **The payoff IS verifiable in-session, and this note claimed the opposite.** The
  five appeared in the available-skills list within the same session as the `git mv`,
  so discovery is re-scanned rather than fixed at session start — and that is what
  confirmed the move had worked. The claim was falsified minutes after being written,
  by the very action it was advising to defer: a "you cannot check this here" statement
  is worth one attempt at checking before it goes in the file.
- ⚠️ **Making a skill reachable is what finally got its helper READ, and
  `keycap_preview.py` had been parsing the WRONG SETTINGS ROWS the whole time.**
  `_poly_settings()` split `poly_settings` by ordinal — `per = len(rows) // 6` — on
  the premise that `lang_lut.c` emits the six H/V offset blocks. It emits **fifteen**
  (six H/V, three `altgrhalf`, six held-offset), so `per` was 400 instead of 160,
  each "block" spanned two and a half real ones, every language name appeared three
  times inside it, and the last write won. Measured: `setting(S_LETTER_H, 'en-US',
  VAR_SHIFT)` returned **35** out of `{num.hoffset}` where the real value is
  `HIDE_KEY` — so the model drew an en-US letter a shift preview the firmware hides,
  contradicting the docstring three lines above it. Every collision number the skill
  had ever produced was measured against offsets from the wrong rows.
  - **Key a generated block by its LABEL, never by its ordinal.** The generator
    already writes `// {letter.hoffset}` markers, so splitting on those is immune to
    a row being added — which is exactly what happened when `altgrhalf` landed
    (2026-09-02) and silently invalidated the parser. The fix also raises on a
    missing label rather than returning an empty dict, since the failure mode being
    replaced was a plausible number rather than an error.
  - ⚠️ **The check that catches this class is a DOCUMENTED value, not a schema
    test.** A structural assertion (160 rows per block, 4 values per row) passes on
    the broken parse. What fails is asking it something the firmware's own notes
    already answer: en-US hides the letter shift preview, `{letter.altgrhalf}` is set
    on exactly 27 layouts and `{sym.altgrhalf}` on 18. All three now match.
  - **The AltGr hint was missing from `legend_ink()` too** — the module docstring
    promised "base glyph, shift preview and AltGr preview" and the code composed the
    first two, so a corner mark was measured against two thirds of the legend it can
    collide with. It now mirrors the firmware's whole pair rule (the per-category
    half-size opt-in, the `ALTGR_HALF_MIN_INK_H` mark guard, the four-edge clamp,
    the shift stagger and the pull-left), and the pull is mutation-checked: disabling
    that branch leaves the shift where it was, which the ink sets show.

### Where shared content can live, and what actually costs context

⚠️ **A shared `CLAUDE-SHARED.md` imported by both repos would NOT reduce context.**
Claude Code's memory docs say it twice: *"imported files still load and enter the
context window at launch"*, and *"Splitting into `@path` imports helps organization
but doesn't reduce context, since imported files load at launch."* The same is true
of a `.claude/rules/` file with no `paths:` frontmatter. So single-sourcing the text
fixes **drift** and buys nothing in tokens — worth knowing before the idea is
proposed again, because it is an obvious-looking saving that is not one.

- **The prize is small in any case — measured 2026-09-10.** Across the three
  sections headed *"(all PolyKybd repos)"*, only **2,975 B** is byte-identical
  between `qmk_firmware` and `PolyKybdHost`: the docstring-coverage rule, the
  verify-an-AI-finding rule and the green-board rule. Everything else under those
  headings has diverged into genuinely repo-specific material (qmk's
  inherited-upstream scanner note means nothing in the host repo; the host's
  Sourcery `nosemgrep` note means nothing in the firmware), and the two branching
  rules are the same rules written twice in different words.
- ⚠️ **A shared file also has nowhere safe to live.** It must sit inside a repo to
  be version-controlled, and then a session that attached only the OTHER repo cannot
  resolve it — the same silent absence that left `mutation-test-suite` unreachable
  from host-only sessions and the five `keyboards/**/.claude/skills/` skills
  invisible. The docs add a second silent failure: an import resolving outside the
  working directory raises a one-time approval dialog, and *"If you decline, the
  imports stay disabled and the dialog doesn't appear again."*
- ✅ **What DOES reduce context**, in increasing order of saving: a `docs/*.md` file
  read on demand; a **path-scoped rule** (`.claude/rules/*.md` with `paths:`
  frontmatter), which loads only when Claude reads a matching file; and a **skill**,
  which costs nothing at all until it is invoked. The docs are explicit — *"If an
  entry is a multi-step procedure or only matters for one part of the codebase, move
  it to a skill or a path-scoped rule instead."* **No path-scoped rule exists in
  either repo yet**, and it is the obvious home for anything that only matters while
  editing one directory.
- **Block-level HTML comments are STRIPPED before injection**, so `<!-- … -->` in a
  CLAUDE.md costs nothing — usable for maintainer notes, or as machine-readable
  fences round a block that is meant to stay identical across repos.
- ⚠️ **Both files are still past the documented target of "under 200 lines", and
  that is the standing argument for EXTRACTION over adding.** Measured 2026-09-10,
  before and after a deliberate pass: qmk **6,178 -> 2,296 lines (485 -> 176 KB)**,
  host **3,198 -> 1,623 (266 -> 136 KB)**. Nothing was deleted — eighteen subsystems
  moved WHOLE into `docs/` (host) or `keyboards/polykybd/*.md` (firmware), plus the
  reviewer forensics into the `triage-pr-review` skill, each leaving a pointer that
  carries only the rules binding code outside its own file. The notes are
  measurements nobody can re-derive, so the trade is size against re-derivability;
  extraction settles it without giving anything up. `/doctor` proposes trims, and
  `claudeMdExcludes` skips a file wholesale if one is ever in the way.
- ⚠️ **A move RE-BASES every relative path in the moved text, and a LINK is the half
  that breaks silently.** Same hardcoded-`../`-depth trap the five relocated skills
  hit, except prose is worse than a script: nothing runs it, so nothing fails.
  Measured this pass — moving the font-pack notes from the repo root to
  `keyboards/polykybd/` turned `](../../../AdafruitGFX/CLAUDE.md)` into a link at
  `keyboards/AdafruitGFX/`. Check the files you touched, never the tree (an upstream
  fork has thousands of `.md`):
  ```bash
  python3 - <<'EOF'
  import pathlib, re
  for f in list(pathlib.Path("keyboards/polykybd").glob("*.md")) + [pathlib.Path("CLAUDE.md")]:
      for m in re.finditer(r'\]\(([^)#][^)]*)\)', f.read_text()):
          t = m.group(1)
          if not t.startswith(("http", "#")) and not (f.parent / t).exists():
              print(f, "->", t, "=>", (f.parent / t).resolve())
  EOF
  ```
  ⚠️ A path into a **sibling repo** resolves outside the checkout and always reports
  missing in a session that did not attach it — read the RESOLVED path, not the
  exists() bit.

