# CLAUDE.md — qmk_firmware (PolyKybd)

This file provides guidance to Claude Code (claude.ai/code) when working in this QMK fork. The PolyKybd-specific firmware lives at `keyboards/polykybd/`.

For cross-repo context (how this repo relates to `PolyKybdHost/` and `AdafruitGFX/`), see [`../CLAUDE.md`](../CLAUDE.md).

## Code review conventions (all PolyKybd repos)

- **Docstring coverage: ignore CodeRabbit's "Docstring Coverage … threshold 80%" pre-merge check.** That 80% target is a CodeRabbit default, **not** a project policy — the check is non-blocking and we deliberately do not chase it. Do **not** add docstrings to existing functions just to satisfy it (out-of-scope churn). Document new code where a docstring genuinely helps a reader, and no more.
- **Verify an AI reviewer's finding against the code before acting on it — several
  arrive confidently wrong.** Of 7 CodeRabbit findings on one PR (2026-08-01), 3
  were false and **two were refuted by their own evidence**: a "PACK_VERSION 3
  needs a matching host change" (the host never parses the PlyX version — it
  checks magic + slot fit and defers the ABI/RAM contract to the firmware loader
  by design); a "the unpacker is not defined" whose own analysis script had
  returned 159 bytes of output, i.e. it reasoned without the code (the decoder
  was 90 lines above in the same file); and an `int8_t` "signed-overflow UB" that
  the StackOverflow answer it quoted explicitly contradicts (a sub-`int` operand
  promotes to `int`, so the narrowing back is *implementation-defined*, not UB —
  though a real non-termination hazard did lurk nearby, so the fix was taken for
  a different stated reason). **The rule is verify, not dismiss:** the same review
  round produced one genuinely valuable finding (a bulk repair loop running inline
  in `raw_hid_receive()`, worth seconds of blocked main loop) that was adopted.
  Reply to the false ones with the evidence so they are not re-raised.

- ⚠️ **A security-scanner finding on a file under `.github/` or `lib/` is
  probably UPSTREAM's, and two checks settle it in minutes: is it stock, and is it
  reachable?** This is a fork of a 30k-commit project, so almost everything a
  path-based scanner walks was written by someone else and most of it is
  unreachable here. The checks, in order:
  1. **Is it stock?** `diff` the same path against upstream. Byte-identical means
     the finding is upstream's to fix (or not) and ours only if we can reach it:
     ```bash
     curl -sSL "https://raw.githubusercontent.com/qmk/qmk_firmware/master/<path>" \
       | diff - "<path>" && echo "IDENTICAL TO UPSTREAM"
     ```
     ⚠️ **That compares against upstream's CURRENT master, which moves — so read
     the two outcomes asymmetrically.** Identical is conclusive: we did not write
     it. A **difference is not proof we own it** — upstream may simply have
     changed the file since our last catch-up merge. Check
     `keyboards/polykybd/UPSTREAM_PATCHES.md` (the maintained list of upstream
     files we patch) and `git log --oneline -- <path>` before concluding we wrote
     it; getting this backwards means "fixing" an inherited file and buying a
     conflict at the next merge. When the distinction actually decides a finding,
     compare at the **merge base** instead:
     ```bash
     git remote add upstream https://github.com/qmk/qmk_firmware   # usually absent here
     git fetch -q upstream master
     git show "$(git merge-base HEAD upstream/master):<path>" | diff - "<path>"
     ```
     ⚠️ **Run the `--is-shallow-repository` check first** — the container clone is
     shallow, `git merge-base` then returns an **empty string** rather than
     failing, and the command above silently degrades to `git show :<path>`. That
     trap is written up under "Building & flashing"; it is why `master` is the
     default recipe here and the merge base the deliberate escalation.
     ⚠️ **For workflows, don't guess which are ours — ask upstream.** A 404 means
     the file does not exist there, i.e. we wrote it. (This one asks about
     *existence*, not content, so the moving-master caveat above does not apply —
     upstream deleting a workflow we still carry is the only way it misleads.)
     Measured 2026-08-29:
     **only 5 of 23 are ours** — `bump-version.yml`, `cppcheck.yml`,
     `polykybd-unit-test.yml`, `qmk-test.yml`, `release.yml`. Re-derive rather
     than trusting that list:
     ```bash
     for f in .github/workflows/*.yml; do b=$(basename "$f")
       c=$(curl -sSL -o /dev/null -w '%{http_code}' \
           "https://raw.githubusercontent.com/qmk/qmk_firmware/master/.github/workflows/$b")
       [ "$c" = 404 ] && echo "OURS: $b"; done
     ```
  2. **Is it reachable?** For a workflow, read its triggers and its callers — a
     `workflow_call`-only file is dead unless something calls it, and upstream
     gates several on `if: github.repository == 'qmk/qmk_firmware'`, which is
     **permanently false** in a fork. For C, grep the `SRC`/`OBJ` lists: a file no
     build includes cannot have a vulnerability in the shipped image.
  Worked example (2026-08-29): an external audit reported a **critical** template
  injection in `.github/workflows/ci_build_major_branch_keymap.yml`
  (`qmk find -km ${{ inputs.keymap }}`). It is byte-identical to upstream, is
  `workflow_call`-only, and its sole caller is repo-gated — so the chain cannot
  execute here at all. Two commands, no change, finding closed.
  - ⚠️ **The inverse is the part that makes this worth doing: the scanner looked
    at the inherited file and NOT at ours.** The one genuine injection in the same
    sweep was in `bump-version.yml` — a file we wrote — interpolating
    `github.event.pull_request.labels.*.name` straight into a `run:` body, where a
    PR label is attacker-controllable text. Nothing flagged it. **So when a report
    names an upstream path, treat it as a prompt to audit the sibling files we own,
    not just to dismiss the one it named.** The fix pattern is to pass every
    `${{ }}` through `env:` and read it as `"$VAR"` / `os.environ[...]`, which is
    what `bump-version.yml` now does.
    - ⚠️ **That property holds for `bump-version.yml` ALONE — do not read it as
      repo-wide.** A sweep the same day found **31 interpolations inside `run:`
      bodies across 8 workflows**, `qmk-test.yml` and `release.yml` among them.
      None is known to be exploitable — they carry build outputs, `env:` constants
      and `matrix.*` values the workflow itself wrote, not user text — but that is
      a judgement per site, not an audited fact, and the two files have not been
      gone through line by line. `polykybd-ctnd/.claude/skills/verify-security-finding/`
      carries the checker and the triage procedure.
  - **Record the disposition even when nothing changes** — dismissed findings that
    leave no artifact get re-raised in full by the next scan. The tracker is
    `polykybd-ctnd/docs/SECURITY_AUDIT.md` § *"Checked and NOT vulnerable — don't
    re-litigate"*. For a vendored tree the note belongs beside the code instead:
    `keyboards/polykybd/doom/engine/PROVENANCE.md` carries the disposition of the
    `textscreen/` findings, because that tree is a **verbatim upstream snapshot**
    and must not be patched in place.
  - ⚠️ **A THIRD direction, and it is the one that silences review rather than
    misdirecting it: an inherited upstream POLICY file tells a reviewer to REFUSE
    the files this fork owns.** `.github/copilot-instructions.md` is byte-identical
    to upstream's and says *"This review applies only to changes within the
    `keyboards/` folder … defer to a QMK Collaborator"*. That is written for
    somebody submitting a keyboard to `qmk/qmk_firmware`, where a contributor
    cannot self-approve core changes. Here it means CodeRabbit reads the inherited
    tree normally and declines to assess `release.yml`, `qmk-test.yml` and
    `CLAUDE.md` — **exactly the files that are ours** — deferring them to a role
    this repository has nobody to fill. Measured on #282 (2026-09-09): two findings,
    both "defer to a QMK Collaborator", neither about the code.
    - **The two commands that settle it** are the ones already above, read in the
      opposite direction: `diff` against upstream says the *policy* is inherited,
      and a **404** says the *flagged file* is not.
      ```bash
      curl -sSL "https://raw.githubusercontent.com/qmk/qmk_firmware/master/.github/copilot-instructions.md" \
        | diff - .github/copilot-instructions.md && echo "POLICY IS UPSTREAM'S"
      curl -sSL -o /dev/null -w '%{http_code}\n' \
        "https://raw.githubusercontent.com/qmk/qmk_firmware/master/.github/workflows/release.yml"   # 404 = OURS
      ```
    - ✅ **Reply with that evidence — CodeRabbit withdrew both and stored a repo
      learning**, the general form of which is *"verify whether an instruction
      applies to the fork and the changed file before using it as review scope"*.
      One reply, a permanent correction, same payoff as the stale-netlist reply
      recorded in `PolyKybdHost/CLAUDE.md`. Declining silently buys nothing and the
      finding returns on the next PR that touches `.github/`.
    - ⚠️ **Do not "fix" it by deleting the inherited file.** It is stock upstream,
      so removing it buys a conflict at the next catch-up merge for a problem one
      reply solves. If it becomes tiresome, the place to scope CodeRabbit is its
      own configuration, not upstream's file.

- ⚠️ **A green board is NOT evidence a reviewer read your code, and every bot here
  has a way of going quiet that looks like a clean pass.** The standing check, in
  full, is `pull_request_read` `get_reviews` plus the summary comment's body:
  - a review counts only when its **`commit_id` equals the PR head sha** AND its
    **body is not a refusal notice** (a quota / diff-too-large refusal is a real
    review object carrying the head sha, so the sha alone reads as reviewed);
  - **and the absence of a review object proves nothing either** — a CLEAN
    CodeRabbit pass creates none, it edits its summary comment to say *"No
    actionable comments were generated"*. So read the summary body's `📥 Commits`
    range alongside `get_reviews`.
  - **No check run answers this question.** A green `Sourcery review` /
    `Greptile Review` / CodeRabbit status has accompanied a PR that nothing read,
    measured, more than once.
  **The full field guide — which bot goes quiet in which disguise, the sticky
  walkthrough, the Merge Risk sha, the false `✅ Addressed in <sha>` attribution,
  the quota shapes and the rate-limit arithmetic — is the `triage-pr-review`
  skill**, mirrored in both repos. Load it when you are actually triaging a PR;
  it is ~51 KB that does not belong in every session's context.

- ⚠️ **After changing a function SIGNATURE, grep THIS FILE for other prose
  references to it — nothing in CI reads Markdown, so a stale API example ships
  silently.** `CLAUDE.md` narrates dozens of APIs across sections that have no
  reason to be open when you edit one, and an example that no longer compiles is
  worse than no example: it reads as authoritative and the next session copies it.
  Changing `kdisp_set_gfx_scanline(bool)` to `(bool, uint8_t)` updated the section
  being edited and left the one-arg form in two *other* notes — the `kdisp_plot_ink`
  account of the EDEN dimming bug and the Eden-screensaver section (#267,
  2026-09-02). Every build was green under `-Werror` precisely because the **code**
  call sites were all correct. The check is one command, before you push:
  ```bash
  grep -n "kdisp_set_gfx_scanline" CLAUDE.md    # …and any other renamed symbol
  ```
  - **It was caught by Greptile, and that is the pattern rather than the luck.**
    A cross-file consistency claim in prose is what an LLM reviewer is genuinely
    better at than every other check here — cppcheck cannot read the file, the
    compiler cannot see it, and the author is the one person guaranteed not to
    re-read the sections they did not open. The same reviewer caught a note
    contradicting one two paragraphs above it earlier (`PolyKybdHost/CLAUDE.md`).
    So when a reviewer raises a *documentation* inconsistency, verify it like any
    other finding — but expect it to be right more often than its severity label
    suggests.
  - ⚠️ Applies to a **renamed or re-typed** symbol just as much as a new argument,
    and to the sibling repos' `CLAUDE.md` files when the symbol is one they mirror
    (`iso_lang_country.py`, `noto-fonts.yaml`, the font-pack render manifests).

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
- ⚠️ **Both files are far past the documented target of "under 200 lines"** (2026-09-10:
  qmk 4,927, host 1,579). That is a deliberate trade — these notes are measurements
  nobody can re-derive — but it is why EXTRACTION keeps being the right move, and it
  is the standing argument for pushing another subsystem into `docs/` rather than
  adding to either file. `/doctor` proposes trims, and `claudeMdExcludes` skips a
  file wholesale if one is ever in the way.

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

## Branching (all PolyKybd repos)

- **Give every branch a name that hints at its content** (a short descriptive slug, e.g. `claude/fix-slave-layer-after-fw-apply`, not just the auto-generated `claude/<random-scientist>-<id>`) so the branch list reads as a changelog.
- **Always start new work on a FRESH branch cut from the updated default branch — never keep committing to a branch whose PR has already merged.** Once a PR is merged, that branch is done: `git fetch origin PolyKybd` then `git checkout -b claude/<new-slug> origin/PolyKybd` for the next change. Cherry-pick only the still-unmerged commits onto the fresh branch if needed. This keeps each PR a clean, focused diff against the current default (**`PolyKybd`** here; `main` in the host/rig repos) and avoids a new PR accidentally re-including already-merged commits.
- ⚠️ **A cross-repo feature can leave one repo with commits PUSHED and NO PR — and
  nothing surfaces it.** A PolyKybd feature routinely spans 4–6 repos (firmware,
  host, docs, rig, AdafruitGFX, hardware), and every "is everything saved?" check
  passes on the repo you forgot: the branch is committed, pushed, in sync with its
  upstream, and `git status` is clean. Only the *absence of a PR* is wrong, and no
  local command looks for that. On 2026-08-22 the legend-size work had four PRs open
  and reviewed while **AdafruitGFX** sat on two pushed commits with none — one of them
  the `fontconvert -o` sign fix that the whole `latinbig` relocation depends on. It
  was found only because the user asked whether anything was left to open.
  **Sweep every repo before calling cross-repo work done** — `git status` is not the
  check; commits-ahead-of-default plus "does a PR exist for this branch" is. Run a
  `git fetch` in each repo first (below), then confirm a PR exists for every repo that
  prints:
  ```bash
  seen=0
  for e in qmk_firmware:PolyKybd PolyKybdHost:main polykybd-docs:main \
           polykybd-ctnd:main Adafruit-GFX-Library:master PolyKybd:master; do
      r=/home/user/${e%%:*}; d=origin/${e##*:}
      [ -d "$r/.git" ] || continue
      git -C "$r" fetch -q --no-recurse-submodules origin \
          || { echo "!! $(basename "$r"): fetch failed - NOT inspected"; continue; }
      git -C "$r" rev-parse --verify -q "$d" >/dev/null \
          || { echo "!! $(basename "$r"): $d missing"; continue; }
      seen=$((seen+1))
      n=$(git -C "$r" rev-list --count "$d"..HEAD)
      [ "$n" != 0 ] && echo "$(basename $r): $n commit(s) ahead of ${d#origin/} — PR?"
  done
  [ "$seen" = 6 ] || echo "!! inspected only $seen/6 repos — result is NOT trustworthy"
  ```
  ⚠️ **Three ways the obvious version of this loop FAILS OPEN — it prints nothing,
  which reads identically to "all clean".** All three were hit writing it (2026-08-24):
  - **`~` is `/root`, not `/home/user`** — `$HOME` is root's in this container, so a
    `~/qmk_firmware` path matches no repo and the loop skips all six in silence. Use
    absolute `/home/user/...`, and keep the `seen` counter so a zero-repo sweep is
    loud rather than reassuring.
  - **`origin/HEAD` is UNSET in every clone**, so auto-detecting the default via
    `git symbolic-ref refs/remotes/origin/HEAD` yields nothing and any `|| origin/main`
    fallback silently reports 0 for the firmware (default `PolyKybd`) and AdafruitGFX
    (`master`). Hence the explicit `repo:default` table.
  - **Without the fetch, stale remote refs cry WOLF the other way** — right after a
    merge, an un-fetched repo still shows the merged branch as ahead of its old
    `origin/main`. That direction is at least visible; the first two are not.
    ⚠️ **A FAILED fetch is a fourth mode, and it is the one that reads as inspected.**
    It cannot fake a clean result — measured, not reasoned: a stale `origin/<default>`
    is *behind* the true remote, so `$d..HEAD` can only count the same or MORE, never
    fewer (post-merge it reports 1 where a fresh ref reports 0). But the repo is then
    compared against unknown-age data while still incrementing `seen`, which is
    precisely what that counter exists to prevent — hence the `|| continue` on the
    fetch, so an unreachable repo trips the `seen` guard instead of passing quietly.
  - ⚠️ **A FIFTH mode, and it is the loop's own blind spot: `$d..HEAD` inspects only
    the branch that happens to be CHECKED OUT.** Anything pushed to another branch is
    invisible, so standing on a merged branch makes the sweep print a clean board for
    a repo that has work sitting on a different one. Measured 2026-09-01: the loop
    reported all six repos clean while this repo's `claude/firmware-feature-gaps-jvp9hz`
    was 13 commits ahead — found only because that branch was restarted for unrelated
    reasons. (Benign in the event: the commits were superseded, their content already
    on `PolyKybd`. But the sweep could not have told me that either way.)
  - ⚠️ **Do NOT "fix" it by sweeping every remote `claude/**` branch — measured, that
    is unusable.** A squash- or merge-merged branch reads as ahead of the default
    forever, so across the five repos there are **525** `claude/*` branches, **95**
    read as "ahead", and only **3** of those had an open PR. A ~95% false-positive
    rate is a check nobody will read twice.
    ```bash
    # what the numbers came from — per repo, count branches vs branches "ahead"
    for b in $(git -C "$r" for-each-ref --format='%(refname:short)' 'refs/remotes/origin/claude/*'); do
        [ "$(git -C "$r" rev-list --count "$d".."$b")" != 0 ] && echo "$b"
    done
    ```
    **So "commits ahead" cannot answer this question at all, on one branch or on all
    of them — the only reliable signal is whether a PR EXISTS**, which is a GitHub
    query and not a git one. That is what this note already prescribes and what the
    loop never implemented; the loop is a cheap prompt to go and check, never a clean
    bill of health. Treat a silent sweep as "nothing obvious on the branch I am
    standing on", nothing more.

## Building & flashing

**The ARM toolchain is installable in the dev / remote container — do not claim it is unavailable.** Verified end-to-end (`split72:default` → `.uf2`, exit 0) on 2026-05-29.

- **Toolchain, qmk CLI and submodules** — the once-per-container setup is
  [`keyboards/polykybd/BUILD_ENVIRONMENT.md`](keyboards/polykybd/BUILD_ENVIRONMENT.md):
  `gcc-arm-none-eabi`, `pip install qmk` + `QMK_HOME`, and the submodule init, whose
  three failure modes are each written up there (the git proxy 403 that `add_repo`
  fixes, a `lib/*` dir that is full of files and still uninitialised, and a fresh
  container where all five clones fail and then succeed retried one at a time).
  ⚠️ `git submodule status` must show a **leading space** on every line; an
  uninitialised module is prefixed `-`, and a build against one dies on
  `#error "obsolete or unknown configuration file"` rather than a missing file.
  ⚠️ An upstream merge BUMPS the pins — re-init after merging or you link new QMK
  against old ChibiOS, which compiles cleanly and fails at runtime.
- **Build**: `qmk compile -kb polykybd/split72 -km default` (or `make polykybd/split72:default`). Output `.uf2` lands in the repo root and `.build/`.
- **Deliverable for testing is the `.bin`, NOT the `.uf2`** — the user flashes over HID via PolyKybdHost's firmware updater (`polyhost/device/hid_fw_up.py`), which takes the raw RP2040 image: `arm-none-eabi-objcopy -O binary .build/<target>.elf .build/<target>.bin`. The `.uf2` is only for manual bootloader-drive recovery.
  - ⚠️ **Put the commit sha in the FILENAME — every test build reports the same
    `FW_VERSION`, so they are otherwise indistinguishable once flashed.** `FW_VERSION`
    only moves on the post-merge auto-bump, so a session with several hardware rounds
    hands over N files that all answer `0.13.1` to `polyctl fw version` and carry
    near-identical names (`…_fix` / `…_legend` / `…_invert`). That cost a full round
    (2026-08-13): a correct build was reported as "I did not see the new behavior",
    and the only way to settle it was to md5 the delivered file against a fresh
    rebuild and grep the image for a changed string literal. `split72_<sha>_<slug>.bin`
    takes the ambiguity away. Better still, when a change alters something **visible
    on a keycap**, say which pixel tells the builds apart — that is a check the user
    can run without any tooling.
  - ⚠️ **A branch-built `.bin` reports a DIFFERENT `FW_VERSION` from the one CI and
    the HIL rig show for the SAME commit — and that is normal, not a stale build.**
    CI builds the PR *merged into* its base, so it picks up every auto-bump that has
    landed on `PolyKybd` since the branch was cut; a local `qmk compile` builds the
    branch alone. On 2026-08-22 the delivered image answered `0.15.7` while the rig
    logged `Split72 0.15.10 P13` on commit `d8bb98ca` — a 12-commit base drift. It
    reads exactly like handing over the wrong file, so **settle it by diffing, not by
    rebuilding**:
    ```bash
    git log --oneline HEAD..origin/PolyKybd                 # what the branch lacks
    git diff --name-only HEAD...origin/PolyKybd             # EVERY path, not just ours
    git diff HEAD...origin/PolyKybd -- keyboards/polykybd/config.h   # only FW_VERSION?
    ```
    If the only firmware delta is `config.h`'s version string, the `.bin` carries every
    real change and just names itself older. If it is more than that, the branch is
    genuinely behind and the test build is missing base fixes — merge before delivering.
    ⚠️ **Read the CONTENT and the UNRESTRICTED path list — `--stat` scoped to
    `keyboards/polykybd` proves neither half of that sentence.** `--stat` reports line
    counts, so `config.h | 2 +-` is equally consistent with a version bump and with a
    changed `#define` beside it; and the image links this fork's **patched upstream
    files** too, which a PolyKybd-scoped diff hides — `keyboards/polykybd/
    UPSTREAM_PATCHES.md` is the maintained list of them (today `usb_descriptor.h`,
    `usb_main.c`, `oled_driver.c`, `transport.h`, `rp2040.c`), and a catch-up merge
    landing on `PolyKybd` is exactly what moves them. Read that file rather than
    hardcoding the set here — it is the thing that stays current. Even then this is a
    drift check, not proof of binary equivalence: if anything outside `config.h` shows
    up, rebuild on the merged base rather than reasoning about whether it mattered.
- **Docker is NOT usable** in the remote container (no daemon) — use the native toolchain above, not the qmk docker image.
- ⚠️ **NEVER run two `qmk compile` invocations at once — every flavour of a board
  shares ONE `.build/` tree, and the collision presents as a CODE error.** Backgrounding
  the pack build and starting the monolith beside it made the pack link die on
  `undefined reference to doom_shim_menu_key_tile` (2026-09-03) — a symbol the pack
  flavour genuinely does not define locally, so it reads exactly like a real
  missing-shim bug rather than two builds overwriting each other's objects in
  `.build/obj_polykybd_split72_default`. Serially, both link clean and nothing else
  changes. `build_pack.sh` is safe because it sequences the two flavours itself.
- The `firmware-size-diff` skill builds HEAD vs working tree and diffs sizes / `.text`.
- ⚠️ **In the session container `qmk` is at `/root/.qmk_venv/bin/qmk` and is NOT on
  `PATH`.** `build_pack.sh` (and anything else shelling out to `qmk`) dies with
  `qmk: command not found`. Prefix every build:
  `export QMK_HOME=$PWD && export PATH="/root/.qmk_venv/bin:$PATH"`. The
  `deliver-test-firmware` skill wraps this.
- ⚠️ **The checkout can be SILENTLY RESET to an older commit** when the web/remote
  container is reclaimed — your commits survive on `origin`, but the working tree
  and `HEAD` roll back, and nothing announces it. It happened **three times** in one
  session (2026-08-01); once it sent a code review chasing a `NUM_VARIATIONS` /
  pool-size mismatch that existed only in the reverted tree. **Run
  `git log --oneline -1` before trusting any grep or "the code says…" conclusion**,
  especially at the start of a turn or after a long build. Restore with:
  ```bash
  git fetch origin <branch> && git reset --hard origin/<branch>
  ```
  Uncommitted work is lost, so push early. This applies to every repo in the
  session, not just this one.
- ⚠️ **The container's clone is SHALLOW, and that makes `git merge-base` return an
  EMPTY STRING rather than an error** — so anything comparing this branch to
  upstream silently produces nonsense instead of failing. Seen 2026-08-11: the
  clone was 198 commits deep, `git merge-base HEAD upstream/master` printed
  nothing, and `git rev-list --count $MB..0.33.13` with the empty variable
  degraded to `HEAD..0.33.13` and reported **29,576 commits**, i.e. "these
  histories are unrelated" — for a fork whose merge base is one of its own
  branches. **Before ANY merge, diff-vs-upstream, or `git describe` reasoning:**
  ```bash
  git rev-parse --is-shallow-repository        # true = every history answer below is a lie
  git fetch origin --unshallow --no-recurse-submodules   # ~1-2 min on this repo
  ```
  `--no-recurse-submodules` matters: the submodule repos aren't proxy-authorized
  (see above), so a plain fetch spews `Could not access submodule 'lib/chibios'`
  and buries the real result. Once unshallowed, the merge base resolved to
  exactly the fork's own `master` — which is the sanity check that it worked.
  - ⚠️ **It breaks a RELEASE RANGE the same way, and that one reads as plausible
    history rather than as nonsense.** `git log <tag>..origin/PolyKybd` on a shallow
    clone walks truncated history and returns a wrong set **with no error** — asked
    for 0.15.2→0.15.14 it returned commits from the **0.9.54** era, complete with
    believable bump-commit boundaries (2026-08-26). The 29,576-commit case above at
    least screams; this one would simply have shipped release notes describing the
    wrong versions. So the `--is-shallow-repository` check belongs in front of ANY
    tag-anchored history question, not just merge-base reasoning — and note the tag
    itself resolves fine (`git rev-parse <tag>` succeeds), so a tag-exists check
    proves nothing. Unshallowing took **45 s** here; the count went 84 commits.
- **`-Wcast-align` is on for PolyKybd's OWN sources, and it exists for the
  HID-apply brick class.** `fw_staging`'s page buffer was `static uint8_t
  page_buf[256]` (alignment 1) word-copied through a `(uint32_t *)` cast; the
  linker put it at a byte offset, the unaligned `STMIA` HardFaulted the M0+ in a
  function that never returns, and it shipped in a release. The warning names
  exactly that — *"cast increases required alignment of target type"* — and with
  `-Werror` already on it is a build failure on the PR that writes it. That is
  the only place a bisect can find this class, because the brick itself was a
  **layout** effect: a macro PR grew `.bss` and moved the buffer, so the guilty
  commit never touched the failing code.
  - ⚠️ **Scoped by path via the `$<` per-recipe filter** (`rules.mk`, the same
    mechanism the doom `EXTRAFLAGS` block uses). `EXTRAFLAGS` otherwise lands on
    **every** compile line — upstream QMK, ChibiOS, pico-sdk — which is the trap
    that kept CodeQL out of this repo.
  - ⚠️ **The WHOLE `doom/` tree is excluded, not just the vendored engine, and
    the reason for our own sources is worth knowing: `doom_arena_at()` returns
    `uint8_t *` because that signature IS the pack ABI** (`doom_pack_abi.h`,
    handed to a **signed** `.plyx`). So every `(doom_mirror_t *)doom_arena_at(…)`
    is a widening cast the check cannot be satisfied about without editing a
    cross-boundary contract — which is not something to do on a warning's
    account. `void *` would be the better type for untyped arena storage; it was
    tried and reverted for exactly that reason. The offsets are
    `_Static_assert`ed 4-aligned in `doom_arena.h` instead, which is the
    substance, and the one such cast **outside** the doom tree (`split_sync.c`'s
    mirror handler) carries a narrow `#pragma` pointing at those asserts.
  - ⚠️ **A path filter that matches nothing FAILS OPEN** — the flag never applies
    and the guard looks installed while doing nothing. Verify by compiling a
    deliberate misalignment in a PolyKybd source and confirming the build
    **fails**, not by reading the make output.
  - **`void *` casts do not warn** (GCC exempts them), so `bridge_helper.c`'s
    split-link CRC store is unaffected — and is separately safe, since every
    caller passes a struct whose first member is a `uint32_t`.
  - **It found two real latent instances of the same shape**, both now asserted
    rather than assumed: `doom_mode.c` casts the core1 **stack pointer** out of a
    `uint8_t *` pool (where a misalignment is worse than the applier's HardFault
    — 8-aligned base, both offsets multiples of 8, which is also what AAPCS
    demands), and every `doom_arena_at()` consumer relies on arena offsets that
    nothing checked. ⚠️ Do **not** launder such a cast through `uintptr_t` to
    silence the warning — that proves nothing and hides the next one.
  - ⚠️ **Verified the hard way, and it earned its keep immediately**: the first
    build with the flag FAILED on `split_sync.c`, which is simultaneously the
    proof that the path filter matches (it would otherwise fail open) and a real
    find. Do not take a clean build as evidence the flag is active — take a build
    that fails on a deliberate misalignment.
  - The clean-up it required was itself worth having: the OLED helpers took a
    `uint32_t[]`, cast it down to `char *` at the call and back up inside, which
    was safe only by convention. They take `uint32_t *` now.

- ⚠️ **When an upstream merge breaks the build, look at the vendored DOOM engine
  FIRST — a new upstream warning lands there, not on our own sources.** QMK builds
  with `-Werror`, so *any* warning upstream adds to `builddefs/common_rules.mk`
  becomes a hard failure in `doom/engine/` (a third-party rp2040-doom snapshot that
  nobody is going to clean up). 0.33.13 added
  `-Wunused-but-set-variable`/`-parameter` — as collateral of **"GCC 16.1
  compatibility fix" (#26216)**, not a deliberate tightening, so the commit subject
  gives no warning — and six `m_menu.c` menu callbacks that take an ignored
  `choice` parameter failed the build. **The fix site is the `-Wno-error`/`-Wno-`
  demotion block in `keyboards/polykybd/rules.mk`** (the doom-only `EXTRAFLAGS`),
  which already carried the `-variable` half of that exact pair; add the sibling
  there rather than editing vendored code. Keep the demotions doom-scoped so
  PolyKybd's own sources still get the warning.

## Continuous integration (PR checks)

A PolyKybd PR runs a handful of checks — know which ones **gate** and which are
inherited-upstream noise:

- **The mechanics — the HIL tiers and how to ask for them, the FW-APPLY and doom
  tiers, the rig debug loop, the paths filters that decide whether a run starts at
  all, the inherited upstream lint, and how to read a job log — are
  [`keyboards/polykybd/CI_CHECKS.md`](keyboards/polykybd/CI_CHECKS.md).** Four
  things every PR author needs without opening it:
  - ⚠️ **The HIL suite has TWO tiers and the default one deliberately skips the
    deepest checks** — the startup animation, idle engage + the Eden screensaver, a
    450-frame split-link soak, and the reboot power cycle that is the ONLY thing
    verifying user state survives a power loss. **Ask for `TIER_EXTENDED` on anything
    touching EEPROM/persisted state, the split link, the idle/animation paths, or a
    release**: the `hil-extended` PR label, `[hil-extended]` in a pushed commit, or a
    `workflow_dispatch`. The job log says which tier ran (`suite tier: …`) — read it
    before concluding a green board covered the reboot check, because by default it
    did not.
  - ⚠️ **A HIL job that never STARTS is not a red one, and it alerts nobody** — an
    offline rig leaves it `queued` with no conclusion, no log and no timeout, and it
    silently arms a release refusal hours later. **Read `status` before
    `conclusion`.** Do not re-run: the rig runs one job at a time, so a re-run queues
    behind the ones already waiting and cannot make an absent runner appear.
  - ⚠️ **A green board does NOT mean a rig test from an unmerged `polykybd-ctnd` PR
    ran** — CI force-syncs the station to ctnd `main`, so that test does not exist on
    the rig. Land the ctnd PR first, then re-run HIL; verify by grepping the job log
    for the test's own name.
  - **The `diagnose-hil-failure` skill classifies a red rig check**;
    **`debug-firmware-on-rig`** drives a one-off probe (`tier: debug`) when the
    graded suite cannot answer the question.

- **`cppcheck`** (`cppcheck.yml`) also **gates**, and is the only reviewer here that
  is not an LLM — CodeRabbit, Sourcery and the on-demand Claude reviewer share
  training data and therefore blind spots, while dataflow analysis fails elsewhere.
  It has no quota, no star threshold and no file-count limit, so unlike every bot it
  cannot go quiet on the PR that needs it. It earned the slot immediately, finding
  the two no-OLED keys latching a chip-select (see § per-keycap rendering gotchas).
  Scoped to `keyboards/polykybd` + `modules/polykybd`, excluding the vendored doom
  engine, generated font headers, vendored monocypher and the googletest sources.
  - **Analyse with `-DFW_REQUIRE_SIGNATURE`** — the configuration that ships.
    Without it cppcheck reports a false `identicalInnerCondition` in `fw_staging.c`
    that the `#ifdef` itself creates, and there is nothing wrong with the code.
  - ⚠️ **A bare `#` line in `.cppcheck-suppressions` kills the whole run**:
    `cppcheck: error: Failed to add suppression. No id.`, exit 1, **before checking
    anything** — so it presents as "no findings" rather than as a syntax error. A
    comment needs text after the hash; blank lines are fine. Cost a debugging round
    (2026-08-19).
  - Every suppression in that file carries a **written reason**, same discipline as
    the Sourcery `nosemgrep` audit note. Do not add an id there to make the check
    green; if a finding is real, fix it or record why it is deferred.
  - **CodeQL was considered and rejected for this repo**: C/C++ wants a build, and it
    would analyse the whole upstream QMK tree — the same trap as the
    lint-on-upstream-keyboards problem below. The host repo runs CodeQL instead,
    where Python needs no build and the tree is entirely ours.
- **`PR Lint keyboards`** (job `lint`, `.github/workflows/lint.yml`) and **`Pull
  Request Labeler`** (job `triage`, `labeler.yml`, `pull_request_target`) are **stock
  upstream QMK** workflows the fork inherited. `lint` runs `qmk lint --strict` on the
  changed keyboards; the labeler auto-labels by path. They **pass green on every normal
  commit** and are **non-blocking**.
- ⚠️ **A red `lint`/`triage` where BOTH cancelled at the same second (~16 min in) is an
  infra/runner cancellation, NOT a code error** — GitHub surfaces a cancelled run as a
  red "failure". Confirm via the workflow **run history** (they'll be green on the
  prior commits) and reproduce locally: `qmk lint --strict --keyboard polykybd/split72`
  (+ `split42`), `qmk ci-validate-keyboard-targets`, `qmk ci-validate-aliases`. If
  those are clean, just **re-run the two jobs** — there is nothing to fix.
- ⚠️ **Applying N labels in ONE API call fires N `labeled` events, i.e. N workflow
  runs.** `qmk-test.yml` listens for `labeled` (it must, or the `hil-perf` label would
  trigger nothing), so adding `hil-perf` + `bump:minor` together started **two identical
  perf runs** — a wasted rig build + flash each (2026-08-05). The rig executes one job
  at a time so they queue rather than collide, but cancel the duplicate. Apply labels
  one call at a time when one of them is a trigger, or expect to clean up. This is a
  *different* mechanism from the push/pull_request duplication below.
- ⚠️ **A `check_suite.completed` wake can name a SUPERSEDED head, and read at face
  value it says "CI is green" about a commit nobody is on.** The envelope's own text
  is *"No third-party check suite on the PR's head_sha is still running or failed"*
  — but `head_sha` is the suite's, not the PR's, and a suite that started before
  your last push completes after it. Three arrived on #282 (2026-09-09) for
  `0e027fb` and `a4dcffbc` while the head was `6f41acc`. **Compare the event's
  `head_sha` against the PR's actual head before believing it**, which the envelope
  also asks for in the same breath ("verify the PR's overall state before acting").
  Same family as the stale-walkthrough traps in `PolyKybdHost/CLAUDE.md`: the signal
  is honest about what it covers and silent about what you assumed it covered.
- The CodeRabbit **Docstring-Coverage** check is ignored per "Code review conventions"
  above.
- ⚠️ **PR CI does NOT build the monolith.** `qmk-test.yml` builds only
  `POLYKYBD_DOOM_PACK=yes` (+ split42); the **monolithic** `POLYKYBD_DOOM=yes`
  flavour — whose objects the `.plyx` is harvested from — is built **exclusively by
  the release workflow**. So a PR can be fully green and still break at *publish*
  time: that is exactly what #172 did, and why v0.9.81 was never built at all. The
  monolith is also the tightest RAM flavour (it had **20 bytes** of `.heap` free at
  v0.9.82), so it is the first to fail on any RAM growth. **Build it locally before
  merging anything that adds statics:**
  `qmk compile -kb polykybd/split72 -km default -e POLYKYBD_DOOM=yes`, or run
  `doom/pack/build_pack.sh`, which builds both flavours.

## Releases

Firmware releases are **GitHub Releases** (tag `PolyKybd-fw-vX.Y.Z`; `FW_VERSION` in
`config.h`), created by **publishing** — *not* by pushing a tag. Use the
`polykybd-github-release` skill to draft the notes and drive the flow. The mechanics
that cost real debugging to learn (2026-07):

- ⚠️ **A `PROTOCOL_VERSION` bump means BOTH artifacts get released, and the check that
  catches it is the PUBLISHED versions, not the in-tree ones.** The existing "bump
  `__protocol__` in lockstep with `PROTOCOL_VERSION`" rule is about the *sources*, and
  it can be perfectly satisfied while the releases are a protocol apart. Measured
  2026-09-09: firmware `PolyKybd` and host `main` both read protocol 17, while the
  newest **published** host (v0.14.18) was still 16 — so a firmware-only release would
  have shipped protocol 17 to every user's protocol-16 app. Read the sibling's newest
  release (`list_releases`, then its `__protocol__`/`PROTOCOL_VERSION` at that tag)
  before drafting.
  - ⚠️ **Nothing downstream catches it, because the connect gate is NOT exact-match.**
    The host connects to any protocol `>= MIN_SUPPORTED_PROTOCOL` and gates each
    feature through `FEATURE_MIN_PROTOCOL`, so an old host pairs with new firmware and
    silently leaves the new features off — quieter than a refusal, and worse to
    diagnose. (The release skill's own pitfall claimed exact-match for a long time,
    which made the pairing read as self-enforcing when it is not.)
  - **Publish the host first, then the firmware** — the host is the side that has to
    understand the new protocol, so that order never leaves a user holding firmware
    their app cannot drive.

- ⚠️ **Publishing is GATED on a green firmware-APPLY run for the commit being
  released** (`tools/require_fwapply_run.py`, the first step of `release.yml`,
  before the build so a refusal changes nothing). The HID-apply brick shipped
  because no release artifact had ever been applied on hardware — the rig flashes
  by UF2 over GPIO BOOTSEL, which bypasses `fw_staging` entirely, and this
  workflow runs no HIL at all. With the fwapply tier now running on every merge
  to `PolyKybd`, the gate is normally a formality; it exists for a hand-made tag,
  a re-publish, or a merge whose rig run went red and was forgotten.
  - ⚠️ **It CANNOT simply demand a run on `github.sha`** — release tags land on
    the auto-bump `[skip ci]` commit, which by construction no workflow ran on, so
    that gate would refuse every release. It walks back through ancestors and then
    **proves the delta to the release commit is only the auto-bump** — meaning
    `FW_VERSION` *or* `PROTOCOL_VERSION` in `config.h` and nothing else, since a
    `bump:protocol` merge increments only the latter and rewrites the semver to
    the same value, producing no `FW_VERSION` diff line at all. Accepting an
    ancestor without that proof would report coverage belonging to different
    firmware, which is worse than no gate; accepting only `FW_VERSION` would
    refuse a well-covered protocol release at publish time (caught in review of
    the PR that added it, #264).
  - **The job name is DERIVED from the checked-out workflow**, not hardcoded — a
    rename would otherwise turn the gate into a silent no-op that reports "never
    covered" for firmware that was. Same reason the ctnd unit-test workflow greps
    its suite names instead of listing them.
  - **Self-tested** (`--selftest`, run as the same CI step) because this repo has
    no Python test harness and untested decision logic in a release gate is the
    thing `fw_up_verdict.c` was extracted to avoid. Mutation-checked against 8
    breaks; one escaped first — deleting the filename check passed every fixture,
    because none of them had a *different* file whose lines mention `FW_VERSION`,
    and `hid_com.c` is exactly such a file. The fixture that closes it is in the
    selftest with that reasoning attached.
  - **Recovery when it refuses**: dispatch *Build and HIL Test* on that commit
    with `tier: fwapply`, wait for green, re-run the release job. The error names
    every commit it checked and why each failed.
- **A pushed tag does NOT create a release.** Release tags land on the auto-bump
  `chore: … [skip ci]` commit (`bump-version.yml`), and `[skip ci]` **suppresses the
  tag-push workflow trigger** — so `release.yml` runs on **`release: published`** (every
  historical run is a `release` event; a bare tag push builds nothing). Publishing
  (UI / `gh release create` / the script below) is what starts the build + asset upload.
- **`scripts/publish_release.py`** — one OS-independent command (stdlib only). It
  publishes the **newest prepared `<TAG>.md` on the `release-notes` branch**, which is
  the **source of truth for what's ready**: the tree version drifts *ahead* of the
  prepared release because every PR merge auto-bumps it, so don't derive the tag from
  the tree. `--dry-run` previews; `--tag` targets a specific prepared tag. It forces
  `encoding="utf-8"` on git output — on Windows the default cp1252 codec crashes on the
  emoji/em-dashes in the notes (`UnicodeDecodeError` → notes read as `None`).
- **Crafted notes** live one-file-per-tag on the unprotected `release-notes` branch
  (`<TAG>.md`, first line `# <title>`, rest = body; never overwritten/deleted — a
  changelog archive). `release.yml` applies them on `release: published` via
  `gh release edit`, then attaches the built `.bin`/`.uf2`/`.plyx` (the `.plyx` is the
  DOOM engine pack — license-OK to ship; the shareware WAD is *not* a release asset, it's
  downloaded on demand by `doom/tools/dl-doom-data.sh`).
- **Version bump is label-driven**: the merged PR's `bump:major`/`bump:minor`/
  `bump:protocol` label (else patch) drives `bump-version.yml`. Protocol PRs often bump
  `PROTOCOL_VERSION` in-source and *omit* `bump:protocol` (the label would double-bump).
  - ⚠️ **A MISSING bump label is silent, and it can make the LIVE DOCS wrong within
    minutes — apply the label before the merge, not after.** The label is read at merge
    time and there is no second chance: the bump lands as a `chore:` commit and the
    version is then whatever it produced. On 2026-09-01 qmk#259 merged without its
    intended `bump:minor`, so `FW_VERSION` went to **0.16.21**; docs#68 merged three
    seconds later naming **v0.17.0** in a `<SupportedSince>` tag and an upgrade note,
    and `polykybd-docs` deploys on push — so the public site was promising a version
    that would never exist while the firmware had not even been released yet.
  - **The asymmetry is what makes it bite**: a docs PR ships the moment it merges, a
    firmware PR only bumps a number and waits for a release. So a cross-repo pair has
    *two* orderings to get right — the label before the qmk merge, and the docs merge
    after the release (already noted in `polykybd-docs/CLAUDE.md`). Getting the second
    right does not save you from the first.
  - ⚠️ **"Before the merge" means AT OPEN — a label applied any later races the
    merge, and the merge wins.** qmk#271 and host#212 (2026-09-04) both asked for
    `bump:minor` in the PR body and both merged without it (fw **0.18.3** and host
    **0.14.17** instead of 0.19.0 / 0.15.0). The label I put on #212 the moment
    #271's merge came through landed **12 seconds after** #212 was merged: a person
    merging a reviewed stack clicks through it in a minute and does not re-read the
    body, and `bump-version.yml` reads the labels at merge time. A request in the
    body is documentation, not a label. `create_pull_request` cannot set labels, so
    the rule is **`issue_write` with `labels: ["bump:minor"]` right after opening**,
    before announcing the PR — and mention the label in the body only as a record of
    what is already set.
  - **Recovering** is a choice, not a fix: either correct the docs to the version that
    actually bumped, or land a `bump:minor` PR that does **not** itself edit `config.h`
    (the workflow bumps *after* merge, so an edited version file would be bumped on top
    of) — the same cosmetic-realignment move described in the host repo's note.
- ⚠️ **From Claude Code on the web you can neither push tags (git proxy returns 403 on
  `refs/tags/*`) nor create a release (no `gh` CLI, no create-release MCP tool)** — stage
  the notes on the branch and hand the user `python scripts/publish_release.py`.

## Firmware overview (`keyboards/polykybd/`)

The firmware runs on a **Raspberry Pi RP2040** (dual-core ARM M0+) and is a heavily customised QMK build. ⚠️ **The clock is 200 MHz by default** (since 0.10.x). It was **125 MHz** before that — never the 133 MHz this file and several code comments used to claim, which was the chip's old *rated maximum*. Nothing in QMK sets the clock; ChibiOS's `hal_lld_init()` (and, earlier in the boot, the double-tap `__late_init`) calls the pico-sdk `clocks_init()`, which reads the compile-time `SYS_CLK_KHZ`, so `rules.mk` sets it. **`-e POLYKYBD_SYS_CLK=125`** opts back out and produces an image **byte-identical** to the pre-200 MHz builds (verified) — the escape hatch if a board ever misbehaves. 200 MHz is the operating point Raspberry Pi certified in 2025 (1200 MHz VCO / 6 / 1), which requires the core voltage raised to **1.15 V** — the vendored pico-sdk predates the SDK's automatic raise and does not compile `hardware_vreg`, so `POLYKYBD_VREG_VSEL` drives it as a register write before the first `clocks_init()` (see `UPSTREAM_PATCHES.md` → `platforms/chibios/bootloaders/rp2040.c`). Peripherals need no rework: SPI (`SPI_DIVISOR`/`CPU_CLOCK`), I2C, the PIO split UART and WS2812 all derive their dividers from the **live** `clock_get_hz(clk_sys)`, and USB is on the separate 48 MHz PLL. The boot banner prints `clk: sys=…Hz vreg_vsel=0x…` so the pairing is verifiable on hardware. The one **fixed** divider is XIP flash — boot2 runs it at `clk_sys/PICO_FLASH_SPI_CLKDIV` (4), i.e. 50 MHz at 200 and 31.25 at 125, both far inside any QSPI part's rating; re-check that list rather than assuming it holds if another clock is ever added. This is **custom hardware with 8 MB of external QSPI flash** (NOT the stock 2 MB). The 8 MB is **partitioned** (see `base/fw_staging.h` for the authoritative map): **0–2 MB running firmware** (the linker `flash1` XIP window), **2–4 MB firmware-update staging**, **4–8 MB resource/overlay data** (`FLASH_TARGET_OFFSET`). So the budget that matters for adding languages/fonts is the **2 MB firmware partition**, of which `split72:default` currently uses ~0.76 MB (~38 %). `FW_STAGING_OFFSET` is kept equal to the linker `flash1` length so a build that exceeds 2 MB fails to *link* rather than silently growing into the staging area (this firmware/staging split was raised from 1 MB → 2 MB in 2026-06 as the image neared the old boundary). ⚠️ **The sectors carved off the TOP of staging (the apply log, the crash archive, the handedness stamp) need an ALIGNMENT assert as well as an overlap one — the overlap asserts do not imply it.** Each is derived by subtraction from the one above (`FW_HAND_STAMP_OFFSET` is `FW_RESOURCE_OFFSET - FW_APPLY_LOG_BYTES - 8192`), so its 4096-alignment rides on constants that can move without any two regions ever overlapping — and `flash_range_erase()` requires the boundary. Caught in review of #282; `fw_staging.c` carries both terms now. The keyboard is split (left + right halves connected via UART) with up to 72 per-keycap OLED displays (72×40 px monochrome, SPI-driven) plus a 128×64 status OLED.

The host software (`PolyKybdHost/`) communicates with this firmware over a custom HID report protocol (64-byte reports, v0.7.0+).

### EEPROM layout: the reclaimed dynamic-keymap tail

`DYNAMIC_KEYMAP_LAYER_COUNT` must stay **12** — QMK asserts it is >= the compiled
layer count (`keymap_introspection.c`) — but only layers **0..7** are ever read or
written from EEPROM; `_SL` and up are served straight out of flash by
`poly_keycode_at()`. QMK's default addresses put the encoder map and the macro buffer
after all twelve, so 640 B of keymap plus 32 B of encoder map sat there addressed by
nothing. `config.h` rebases both on **`DYNAMIC_KEYMAP_UPDATE_MAX_LAYER_COUNT`**:
measured on split72, the macro region went **1787 → 2459 B (+672)**.

⚠️ **The reclaim is only sound while nothing writes layers >= the cap, and QMK's own
`dynamic_keymap_reset()` DOES.** It loops to `DYNAMIC_KEYMAP_LAYER_COUNT` through a
bound check that is also `DYNAMIC_KEYMAP_LAYER_COUNT`, so it writes layers 8..11
straight over both reclaimed regions. Two guards, and both are load-bearing:
- `dynamic_keymap_reset_poly()` **no longer calls it** — it walks the cap itself,
  resets the capped encoder map and zeroes the macro buffer.
- `eeconfig_init_kb()` **repairs after the one call site we cannot remove**
  (`eeconfig_init_quantum`, three lines above ours in the same function,
  unconditional). Because that repair rewrites layers 0..7 from flash and zeroes the
  macros, it IS the state a fresh EEPROM wants rather than a fix-up bolted on. ⚠️ The
  override also has to replicate the weak default's `eeconfig_update_kb(0)`, which
  replacing the body would otherwise drop silently.
Two `_Static_assert`s in `split_sync.c` pin the addresses to the write cap, so a later
edit to either constant fails the build instead of quietly handing the space back.

The **encoder map moves**, so a board flashed over the old layout would read two
layers' worth of keycodes as its encoder assignments — hence the
`KEYMAP_STORAGE_RECLAIMED` bump on the existing `keymap_layers_fmt` gate (`state.h`),
which discards the stored keymap once on the first boot after flashing.

⚠️ **`DYNAMIC_KEYMAP_EEPROM_MAX_ADDR` is derived INSIDE `nvm_dynamic_keymap.c`**, i.e.
it exists in exactly one translation unit — so anything else that needs the same number
(here `poly_macro.c`) cannot see it. `config.h` defines it explicitly and QMK's own
`#ifndef` picks ours up, so the two cannot disagree.

**Reading the real numbers**: the addresses are macros, so they are not symbols in the
ELF and `nm` cannot find them. Append a `const uint32_t probe[] = {…}` to a real
firmware source, build, and read the **object file** — the linker gc's an unreferenced
array out of the final image, but `.build/obj_*/…/<file>.o` still has it:
`arm-none-eabi-objdump -s -j .rodata.probe <obj>`. That is how the +672 above was
measured rather than derived.

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
| `state.c` / `state_store.c` | `poly_sync_t` / `poly_layer_t` — shared state structs with CRC32. Split 2026-08 (#240): `state.c` is the policy half (dirty flags, brightness model, sync snapshots), `state_store.c` the persistence half (every EEPROM read/write, behind `state.c`'s public getters) |
| `multicore_exec.c` | Offloads RLE decompression to RP2040 core1 via FIFO, keeping QMK's core0 responsive |
| `lang/lang_lut.c` | 81-language lookup table (code-generated from `lang_lut.xlsx` via cog) |

### ⚠️ The dynamic keymap is indexed BY LAYER NUMBER, and QMK does not version it

Remove or reorder a layer and every stored layer above it silently changes meaning.
There is no magic, no format byte, no size check in `dynamic_keymap` — the EEPROM block
is just `layer * MATRIX_ROWS * MATRIX_COLS * 2` bytes, so dropping `_FL1` slid `_NL`
7→6, `_UL` 8→7 and `_SL` 9→8, and a board would have come up **running the old `_FL1`
data as its numpad layer**. No error, no log line, just wrong keys on three layers.

`poly_eeconf_t.keymap_layers_fmt` is the gate: `keyboard_post_init_user` compares it to
`KEYMAP_LAYERS_FL_MERGED` and, on a mismatch, runs `dynamic_keymap_reset_poly()` and
stamps it. **Bump that constant whenever a layer is added, removed or reordered — never
when a layer's CONTENTS change**, which needs no reset. Zero means "written by an older
build", which is also what a fresh EEPROM reads (QMK's wear levelling normalises cleared
bytes to zero — the fact that made `latin_assign` read as "every key hosts 'a'"), and
both want the reset. The stamp is written straight through rather than via the
suspend-only dirty-flag path, so a power cut cannot cost the user a *second* reset.

⚠️ **Two hand-kept numbers move with the enum, and both are now asserted rather than
remembered** (`state.h`): `DYNAMIC_KEYMAP_LAYER_COUNT` must cover every compiled layer,
and the write cap **IS** the first flash-served layer, so `_SL == DYNAMIC_KEYMAP_UPDATE_MAX_LAYER_COUNT`.
A stale cap does not error — it just tells the host it may write to a layer
`poly_keycode_at()` serves from flash. Mutation-checked: setting the cap back to 9 fails
the build with the assert's own message.

⚠️ **Every mutation goes through a `*_poly` wrapper in `split_sync.c`** —
`dynamic_keymap_set_buffer_poly`, `dynamic_keymap_set_keycode_poly`, and
`dynamic_keymap_reset_poly()` (added purely so the reset has one too). That is
deliberate: the alternative was a list of call sites to remember to invalidate the F-row
cache at, which is the guard shape this repo keeps getting caught by (`sync_is_link_fault()`,
the CI suite names, the log-source registry). The invariant is "all keymap mutation goes
through a `_poly` function", not "these four places also call the invalidator".

### `poly_keycode_at()` is the ONE resolver for both the render and the key-event path

`display_keycode_at()` (legend) and `keymap_key_to_keycode()` (action) both bottom out
there, so anything derived inside it **cannot** make a keycap show one thing and type
another. That makes it the correct — and only correct — seam for a runtime keycode
derivation. The F-row alignment (`fl_aligned_keycode`, split72's `POLY_FL_ALIGN_FROW`)
lives there for exactly that reason.

Both callers feed it the **synced** `def_layer` from `get_local_layer()`, not live state
for one and synced for the other. ⚠️ This is the OPPOSITE of the glyph-size key's
deliberate asymmetry, and the difference is what changes at human speed: *modifiers*
change within a single keypress so the action must follow the finger, while `def_layer`
only moves on a deliberate layout switch — so the one-housekeeping-pass lag is
irrelevant and two sources could render F6 on a keycap that types F7.

⚠️ **The host layout editor CANNOT see anything derived there — it reads through
`dynamic_keymap_get_buffer()` (`hid_com.c`), straight out of EEPROM.** So a derivation
on a host-remappable layer would show one keycode in the editor while the board typed
another, with nothing on screen to explain it. That constraint, not taste, is why the
F-row alignment is **all-or-nothing and self-disabling**: it applies only while all 14
slots still hold exactly what was compiled, and any edit hands the whole row back to the
stored values. Two options were rejected — deriving unconditionally (fights the editor)
and moving `_FL` above the write cap (costs remappability entirely). Any future runtime
derivation on a remappable layer faces the same three-way choice.

⚠️ **The derivation is cached** (`fl_row_is_pristine`, 14 dynamic-keymap reads) because
`poly_keycode_at()` runs per keycap per render, not just per keypress. See the `_poly`
wrapper invariant above for how it is invalidated.

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

⚠️ **The two variants also share the MCU SCHEMATIC, so an MCU-level question is
never answered from `variations/poly_corne/` — that directory contains no
processor.** In the hardware repo (`thpoll83/polykybd`) split42's sheets live in
`poly_kybd/variations/poly_corne/` and are the board-specific ones only
(`poly_corne_split42_{left,right}`, `shift_registers`, `ni_buffer2`,
`SSD1306_TO_SPI`); the RP2040 sheet is one level up at `poly_kybd/rp_pico.kicad_sch`
and split42 pulls it in as a hierarchical sheet (`Sheetfile` = `../../rp_pico.kicad_sch`).
Verified 2026-09-09: **exactly one `rp_pico*.kicad_sch` exists in the whole repo**, and
it is the only file containing `VBUS_SENSE`. So `R8 5.6K / R15 10k / D2 1N5819WS` — the
VBUS divider on GP24 — is on **both** boards, identically.
- ⚠️ **A grep over `variations/poly_corne/*.kicad_sch` therefore reports EVERY MCU net
  as absent, and reads as a hardware fact.** That is how "split42 has no VBUS_SENSE net"
  was asserted here (and used to scope a feature to split72) when the boards are
  identical — the search covered five sheets, none of them the processor. **An empty
  grep is evidence only once you have shown the search covered the thing you asked
  about**; one `ls` of the directory settles it. The independent tell was already
  available: split42's keymap `config.h` defines `USB_VBUS_PIN GP24` and its master
  detection works, which cannot be true of an unwired pin.
  ```bash
  # the check that actually answers it, from the hardware repo root
  grep -rl "VBUS_SENSE" --include=*.kicad_sch .        # -> poly_kybd/rp_pico.kicad_sch
  find . -name "rp_pico*.kicad_sch"                    # -> exactly one
  grep -o '"Sheetfile" "[^"]*"' poly_kybd/variations/poly_corne/poly_corne_split42_left.kicad_sch
  ```
- ⚠️ Minor, unresolved: the **right** sheet references a bare `rp_pico.kicad_sch` with no
  `../../`, and no such file exists beside it. Whether KiCad resolves that from the project
  root or the reference is simply stale was **not** established — don't read it as either.

### ⚠️ A release-edge action fires up to THREE times on a ONE-SHOT layer

`post_process_record_user()`'s big `switch` lives inside `if (!record->event.pressed)`
— every settings/utility keycode acts on the **release** edge. That is free on a
`TO()` layer and **not** free on an `OSL()` one:

- `process_action()` (`quantum/action.c`, the `do_release_oneshot` block at the very
  end) re-dispatches a key pressed while a one-shot layer is active as a synthetic
  release: `record->event.pressed = false; layer_on(oneshot); process_record(record);`.
  That inner `process_record` runs the whole chain, `post_process_record_user`
  included — **dispatch 1**.
- It mutated the **same record**, so when it returns, the outer `process_record`'s own
  `post_process_record_quantum(record)` also sees `pressed == false` — **dispatch 2**.
- The finger then lifts and the real release arrives — **dispatch 3**.

So one tap of `KC_GLYPH_SIZE_UP` stepped the legend size **three** tiers. It was
reported as "triggered twice" (field, 2026-08-25) because the third step clamps at the
end tier — a step key reads as ×2, an inc/dec (`KC_DDIM`/`KC_DBRI`) as ×3, and a
**toggle** (`KC_DAUTO`) reads as *doing nothing at all*, which is the shape that would
have been hardest to diagnose. These keys had lived on `_SL`, entered with `TO()`, for
years; moving them to the `OSL()`-entered `_UL` is what exposed it.

**Rule: a custom PolyKybd keycode is handled and SWALLOWED in
`process_record_user()`, never left to `post_process_record_user()`.** QMK has no
per-press dedupe for `post_process_record_*` — the docs describe it only as "runs
after each key press" — but it does not need one, because `process_record()` returns
**before** `process_action()` when `process_record_user()` returns false, so the
synthetic release is never generated at all. QMK compensates for the swallow in the
same early-return path (`clear_oneshot_layer_state(ONESHOT_OTHER_KEY_PRESSED)`), so
`OSL()` still resolves after one key. `poly_custom_key_action()` in `poly_keymap.c`
holds the whole settings switch (both edges) and returns whether it owned the
keycode; `process_record_user()` calls it last, before `display_wakeup()`.
- ⚠️ **A REAL keycode cannot use this** — swallowing it would stop it reaching the
  host. The shifts, the `_LL` F-keys and `RM_NEXT`/`RM_PREV` therefore stay in
  `post_process_record_user()`; all three are idempotent repaints, so the extra
  dispatches are harmless, and a modifier never gets them (`process_action()`
  excludes `IS_MODIFIER_KEYCODE` from the re-dispatch).
- ⚠️ A per-key "armed on press, consumed on release" bitmap was written first and
  **replaced** — it worked, but it is a bespoke guard for something the framework
  already answers.

### HID protocol (host → firmware)
- 64-byte raw HID reports; byte 0 = Report ID, byte 1 = Command ID, byte 2+ = payload
- All responses are prefixed `"P\xNN."` (ACK) or `"P\xNN!"` (NACK)
- **`PROTOCOL_VERSION`** (`config.h`, reported in the GET_ID string) gates host
  features. The per-version rationale is
  [`keyboards/polykybd/PROTOCOL_HISTORY.md`](keyboards/polykybd/PROTOCOL_HISTORY.md)
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

### Telling the host something changed ON THE BOARD

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
- **How a legend is actually DRAWN — glyph resolution and the baseline align, the
  column-native bitmap layout, the plotter modes, the courtyard, the whole `HINT_*`
  display-list vocabulary, the three size tiers and the bbox walker — is
  [`keyboards/polykybd/LEGEND_RENDERING.md`](keyboards/polykybd/LEGEND_RENDERING.md).**
  Its sibling `LEGEND_LAYOUT.md` covers where the elements GO; that one is
  placement, this one is the drawing primitives. Read it before touching
  `base/disp_array.c`, `base/font_lookup.c`, `base/legend_plan.c` or
  `keycode_helper.c`. Four rules from it apply even if you never open it:
  - ⚠️ **Adding an op is TWO walkers, not one — THREE counting the host.** The draw
    dispatch (`disp_array.c`) and the measurement (`font_lookup.c`) must clear the
    same flags and skip the same arguments, or the bbox describes a legend the draw
    does not produce, and `plan_main_legend()` and `roll_idle_offset()` are then
    working from fiction. `PolyKybdHost`'s `oled_preview.py` + `SUPPORTED_OPS` is
    the third edit, and skipping it is silent: a refused op makes the layout editor
    fall back to the keycode TEXT, which looks exactly like the op not working.
  - ⚠️ **Nudge-run arithmetic is unverifiable by any test in this repo.** A 5-nudge
    lift transcribed as a 4-nudge macro pushed a descender two rows off the panel
    while `-Werror`, 52 bbox tests, cppcheck and `qmk lint --strict` were all green.
    Render every legend you touched through `PolyKybdHost/tools/oled_preview.py` and
    require **0** pixels outside the 72x40 window.
  - ⚠️ **Keep every glyph of one legend in ONE font.** `kdisp_write_gfx_char`
    baseline-aligns by `font->yAdvance - fonts[0]->yAdvance`, so a legend built from
    two faces sits on two baselines — `a»ñ` put its `a` 7 px high. Drawing a lone
    icon through a **single-font array** makes that adjustment 0, which is why the
    language flags use `{ &flag_font }`.
  - ⚠️ **The resident C1 icon band `0x80–0x9F` is FULL (32/32), and `0xA0+` is not
    an option** — it collides with printable Latin-1 and `IconsFont` is
    `g_all_fonts[0]`, so a custom icon parked there silently shadows the real glyph.
    The next resident icon has to go in the pack, or free a slot.
    `python3 tools/check_icon_slots.py` is the only thing that can answer "is this
    slot free?".
- ⚠️ **`render_key()` and `to_static_text()` are a PAIR — both must normalise the
  keycode the same way, or a key draws its chrome and NO legend.** `update_displays()`
  consults `render_key()` exactly when `to_static_text()` returned NULL, which is
  **every letter** (the language translation lives inside `render_key()`). So when
  `to_static_text()` unwrapped a mod-tap keycode and `render_key()` did not,
  `RSFT_T(KC_A)` (`0x3204`) fell through every branch there — `is_letter` is false for
  it and `translate_keycode()` has no row — and the keycap rendered its modifier badge
  in an otherwise **empty cell** (field, 2026-08-18). The unwrap had been in
  `to_static_text()` for years, one function away. This is the sibling of the already-
  documented "`render_key()` is only consulted when `to_static_text()` returns NULL"
  (Intl-remap section): that one is about a key with a legend *bypassing* `render_key`,
  this one is about the same seam producing *no* legend at all. Any future keycode
  rewriting (a second wrapper class, an alias) has to land in **both**.
- ⚠️ **A THIRD seam: `update_displays()` can carry a bespoke `else if (keycode ==
  X)` branch that makes the keycode's legend DEAD — grep for your keycode there
  before believing a legend edit does anything.** `KC_EDEN` had one: it drew its own
  hardcoded `U"Reset"` / `U"Eden"` through `mid_fonts`, so the `keycode_helper.c`
  case was never consulted for the awake keycap and **two successive commits edited a
  string that never rendered**. Worse, the branch sat *after* the `text != NULL` test,
  so the `KC_SETTINGS_MORE` gate's empty string routed straight into it and the key
  stayed visible while every other gated key hid (field, 2026-08-26). Both are gone —
  `HINT_MID` made the size expressible in an ordinary legend, so the special case
  could be **deleted** rather than gated. **Prefer that: a per-keycode branch there
  is a legend the rest of the pipeline cannot see.**
- ⚠️ **`get_local_layer()` is the SYNCED snapshot, not live state — never gate
  RENDERING on it.** It lags a layer change by up to one housekeeping pass, so a
  render landing inside that window reads the OLD layer. The `KC_SETTINGS_MORE` gate
  originally required `get_highest_layer(get_local_layer()->layer) == _SL`; a render
  in that window concluded the gate did not apply and drew the advanced keys, and
  `_SL` usually gets no later refresh to correct it. The clause was also redundant —
  every gated keycode is mapped exactly once, only on `_SL`, in BOTH keymaps — so it
  could never hide anything the keycode list did not, and could reveal what it
  existed to hide. Gate on the keycode and the synced *value*, not on the layer.
- ⚠️ **"Hidden" is TWO invariants — blank AND inert — and the gate covered only the
  drawing half for the two keycodes on that row that cannot be undone.**
  `process_record_user()` intercepts `QK_REBOOT` and `QK_BOOTLOADER` in its
  pressed-edge switch (the bootloader announce, and the reboot's bridged handoff so
  the slave restarts too) and returns `true` from there, while `settings_more_hidden()`
  sat **~200 lines further down**. So the advanced row rendered blank and the blank
  Restart keycap still rebooted the board — which reached the field as *"two crashes in
  a row with the multisplash RGB matrix"* (2026-09-09). The log held no crash: it held
  two presses of a key nobody could see.
  - **The comment beside the gate asserted the premise that made it wrong** — that all
    three of `QK_BOOTLOADER` / `QK_REBOOT` / `QK_DEBUG_TOGGLE` are left to
    `process_action()`. True of `QK_DEBUG_TOGGLE` alone, which is precisely why it was
    the one of the three that really was gated. A comment naming a set is worth
    checking against the set.
  - **The fix is ONE check ABOVE the switch, not a test inside each case** — otherwise
    a third irreversible keycode added later inherits the same hole, which is the
    enumerating-guard shape this file keeps recording. When you gate a keycode for
    *display*, grep `process_record_user()` for it in the same pass.
- **Where the base glyph, the Shift preview and the AltGr hint GO — and how they are
  kept off each other and on the panel — is
  [`keyboards/polykybd/LEGEND_LAYOUT.md`](keyboards/polykybd/LEGEND_LAYOUT.md).**
  Read it before touching `lang_lut.xlsx`, `render_key()` or `base/legend_plan.c`;
  the `tune-lang-lut-cells` and `keycap-layout-preview` skills drive that work. Four
  things live there, each with the measurement that produced it: the Shift/AltGr pull
  (24 keys across 19 layouts drew the two hints through each other, and it reads as a
  MISSING GLYPH rather than a layout bug), the build-time Shift-preview suppression
  bitmap (⚠️ **emptying a Shift CELL to hide the preview destroys the uppercase** — the
  cell is both), the four-edge clamp through `legend_plan_clamp()`, and the
  per-category half-size AltGr opt-in.
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
  - ⚠️ **TWO PHYSICAL KEYS have no OLED at all — 74 keys, 72 OLEDs — and the
    in-code comment about them is misleading.** The inner key at matrix **(3,7)**
    on the left half and **(8,0)** on the right have neither an OLED nor an RGB
    LED (both read `NO_LED` in `g_led_config`; both sit at y=2 on the inner edge).
    They are the only two `NO_LED` slots that exist as keys — the other six are
    matrix positions with no key. `invert_display()`'s comment says "on the right
    side of the split layout the first 4 rows have no key", which is true of
    (5,0)/(6,0)/(7,0) — they are absent from `keyboard.json` — but **(8,0) is a
    real key**, so reading the comment as "col 0 never happens" is wrong.
  - **Anything that maps a key to a display must gate on `key_has_display(r,c)`
    FIRST** (declared in each variant header; split42 returns unconditional
    `true`, since all 42 of its keys have OLEDs). `invert_display()` deliberately
    does *not* carry this knowledge — it stays a general "invert the display at
    matrix (r,c)" primitive, and the three callers (the split72 scan loop,
    `hid_com.c`, `split_sync.c`) screen the keys out. ⚠️ **A bounds check is not
    a substitute**: the right key underflows the `c--` fold to 255 and
    `LAYOUT_TO_INDEX` truncates it to **23**, the left indexes **31** directly —
    both in range, both the phantom inner column, so each press *and* release
    latched a chip-select for a slot the key does not own. The old
    `if (disp_idx != 255)` guard was written for exactly this and could never
    fire: it sat *after* the indexed read, and 255 needs `r%5==0`, which no col-0
    key satisfies. Found by cppcheck (2026-08-19), invisible on hardware in both
    directions because the target slots are phantoms.

- **The status OLED — both variants, the composers, the flicker fix, the telemetry
  screen and split42's portrait layout — is
  [`keyboards/polykybd/STATUS_OLED.md`](keyboards/polykybd/STATUS_OLED.md).**
  `oled_helper.c` dispatches, each variant's `status_oled.c` composes into the
  shared kdisp scratch buffer, and `oled_write_raw` blits. Four rules worth carrying:
  - ⚠️ **It is a DIFFERENT BUS from the per-keycap displays** — I2C on `I2CD0`
    (GP0/GP1) at 400 kHz, against the SPI panels `disp_array.c` drives. Each half
    drives its own status OLED locally.
  - ⚠️ **Never reintroduce a per-frame `oled_clear()`.** `oled_write_raw` already
    diffs byte-for-byte and dirties only changed blocks; an `oled_clear()` marks all
    16 dirty every 66 ms tick and is what made a full repaint dribble out band by
    band. A composer that needs a full swap ends with `oled_render_dirty(true)`
    instead, which is a no-op when nothing changed.
  - ⚠️ **split42's panel is mounted rotated 90 degrees and the poly pipeline BYPASSES
    QMK's `OLED_ROTATION`** — it blits a raw page-format buffer, so setting
    `OLED_ROTATION_90` does nothing. That variant composes in a logical 32x128
    portrait space and software-rotates each lit pixel, with its own portrait
    primitives.
  - **Layout work MEASURES the pixel bands** rather than eyeballing the render — the
    `status-oled-layout` skill wraps it. `--diag` only catches pixels off the panel;
    it cannot see two rows colliding, which is what it found (a layout name's
    descenders overlapping the row below by 2 px).

### Split synchronisation
Seven custom QMK transaction IDs (`USER_SYNC_POLY_DATA`, `USER_SYNC_OVERLAY_DATA`, `USER_SYNC_COMPRESSED_DATA`, `USER_SYNC_ROI_DATA`, etc.) carry state and overlay data to the slave half over UART with CRC32 validation and up to 10 retries.

⚠️ **`RPC_M2S_BUFFER_SIZE` is a SILENT CEILING on every one of them, and it is a
CAPACITY, not a transfer size.** Two independent facts, both easy to get backwards:

- **Outgrowing it does not fail loudly.** `transaction_rpc_exec()`
  (`quantum/split_common/transactions.c`) checks `initiator2target_buffer_size >
  RPC_M2S_BUFFER_SIZE` and **returns false before sending anything** — while the bulk
  `send_to_bridge()` call sites discard the ack (the *discarding* sibling of the
  "never bool-test `send_to_bridge()`" rule). So a struct that grows past the cap
  produces a master that applies the change and a slave that never hears it, with
  **nothing in the log**. Caught in review, 2026-08-13: `latin_sync_t` went 63 → 90 B
  when the Intl remap gained the punctuation targets. `state.h` now carries a
  `static_assert(sizeof(latin_sync_t) <= RPC_M2S_BUFFER_SIZE)`; add one for any
  struct that can grow.
- **Raising it costs RAM and nothing else — measured, not reasoned.** The constant
  appears in exactly three places in QMK: the array declaration and the two rejection
  checks. Both ends size the real transfer from `rpc_info.payload.m2s_length`, i.e.
  the caller's own byte count. Verified by building the same tree at 96 and 128 and
  diffing the disassembly: `.text` identical in size, `.bss` +32 (exactly the delta),
  and of 954 differing lines **922 are `.word` RAM address literals**; the only real
  instruction changes are the `cmp` bounds check and two `adds` offsets into shmem.
  **No length, loop-count or transfer-size instruction changes anywhere in the
  image** — so unrelated traffic (matrix scan, pointing pull, overlay bursts) is
  byte-for-byte unaffected. Raised 72 → 96 in the same change; the monolith's `.heap`
  went 3852 → 3828.

⚠️ **The overlay path sits 3 bytes under the old cap — check it before adding a
field.** The 72 was sized for exactly these, all derived from `HID_REPORT_SIZE` 64:
`overlay_sync_t` 67 B, `overlay_map_sync_t` / `dynamic_keymap_sync_t` 68 B, and
**`compressed_overlay_sync_t` / `roi_overlay_sync_t` 69 B**. One more field, or an
`HID_REPORT_SIZE` bump, and an app switch would hit the silent rejection above and
present as missing keycap images. At 96 that path has 27 B of headroom.

### ⚠️ The self-apply's page buffer must be `uint32_t` — a `uint8_t` one bricked the board

`fw_staging_do_apply()` copies the staged image a page at a time through a static
buffer, with `ram_word_copy()` — which takes `uint32_t *`, so the compiler emits
word loads and stores. The buffer was declared `static uint8_t page_buf[256]`,
whose alignment requirement is **1**, so the linker packed it at whatever byte
offset the previous `.bss` symbol happened to leave. An unaligned `STMIA` is a
**HardFault on Cortex-M0+**, taken with `PRIMASK` set inside a function that never
returns — an instant lockup, no console line, no breadcrumb. The board comes back
only via BOOTSEL+UF2.

⚠️ **This is why the bisect and the disassembly disagreed, and the disassembly was
the misleading one.** A HID update that reported success then bricked the keyboard
bisected cleanly to the macro PR (#234) — which touches nothing in the applier. And
`fw_staging_do_apply` really was **byte-identical** across the regression: same
address, same size, same instructions. What #234 changed was `.bss`: its label cache
shifted `page_buf` from a 4-aligned address to `…ce3`, and the first page of the
first sector faulted the core. Ten rounds of probes were aimed at the applier's
*code* on the strength of that byte-identical comparison. **When a bisect blames a
commit that cannot have touched the failing code, check whether it moved the failing
code's DATA** — `arm-none-eabi-nm -S <elf> | grep <buffer>` and look at `addr % 4`.

**The fix is the type, not an `aligned(4)` attribute**: the buffer is declared
`static uint32_t page_buf[FLASH_PAGE_SIZE / 4]` and the `(uint32_t *)` casts are
gone, so no later edit can silently reintroduce the hazard. `flash_range_program`
takes `(const uint8_t *)page_buf`.

Two things that made this diagnosable, and are worth keeping:
- **The in-flash progress log** (`FW_APPLY_LOG_OFFSET`, `FW_STAGING_OFFSET + 1 MB`):
  erased at the start of every apply, one page written per completed sector, plus
  bracket markers around the first sector. It lives past the image, so it survives
  the BOOTSEL recovery that reading it requires — the watchdog scratch does not
  (scratch survives a watchdog reset, **not** a power cycle).
- ⚠️ **The log markers worked while the copy did not, and that asymmetry IS the
  clue.** GCC knew `page_buf` was byte-aligned, so it compiled
  `((uint32_t *)page_buf)[0] = marker` into four `strb`s — which are fine unaligned.
  Only the `uint32_t *`-typed helper got word instructions. So "flash writes work
  here but that one copy dies" was pointing at alignment the whole time.

### Firmware signing enforcement & the on-keycap confirmation (FW-2)

`rules.mk` sets `-DFW_REQUIRE_SIGNATURE`, so `fw_staging_finalize()` only stamps the
staging header for an image carrying a valid Ed25519 signature over `base/fw_pubkey.h`.
An image that fails that check is **not refused outright** — the keyboard asks:

- **The board becomes the dialog.** `poly_sync_t.fw_confirm` (synced, so both halves
  render) makes `update_displays()` blank every keycap except one per half: a 2×-scaled
  **A / ACCEPT** on the left home-row index key and **R / REJECT** on the right, both at
  local matrix `(FW_CONFIRM_ROW, FW_CONFIRM_COL)` — the *same* local position on both
  halves, and a matrix position, so the non-rectangular display grid and the right
  half's `c--` display fold don't apply. `process_record_user` swallows every other key
  while it is up. Side is decided by `is_left_side()`, not by which half is master, so
  the prompt never moves between flashes.
- **⚠️ COMMIT must NOT block waiting for the answer.** It runs inside
  `raw_hid_receive()` on the main loop, which is also what scans the matrix — a
  busy-wait would guarantee the keypress is never seen. So it is a state machine:
  the first COMMIT raises the prompt and answers **`?`**; the host re-polls COMMIT
  (~1 Hz) until a keypress or `FW_CONFIRM_WINDOW_MS` (60 s) resolves it to `.` or `S`.
  Re-running finalize is free — `s_buf_fill` is 0 and the CRCs are untouched — but
  COMMIT skips **re-bridging to the slave** while `fw_staging_confirm_in_progress()`,
  or every poll would re-erase and re-stamp the slave's 4 KB staging header sector.
- **Only the master runs `process_record`** (the slave's matrix is pulled over the
  split link), so a press on *either* half arrives there; the matrix row says which.
- **Accept is physical, cancel may be remote.** The threat model is any process that
  can talk the HID flash protocol, so an acceptance sent over HID would be forgeable by
  exactly the attacker signing defends against. A **cancel** (COMMIT with `'x'` in
  `data[2]`) can only ever deny, so it *is* exposed — the host's abort path and the HIL
  rig (no fingers) use it instead of leaving the board modal for the full window.
- ⚠️ **An UNSIGNED image (`sig == 0`) gets the prompt; an INVALID one (`sig == -1`) is
  refused outright.** They are opposite events: the first is "you compiled this
  yourself", the second is a file that is not what it claims to be. Offering a keypress
  for the second would hand an attacker the one thing the physical gate exists to
  withhold — a user who has been told to press A. The host tells the two apart from a
  single `S` status because it knows whether it sent a signature.
- ⚠️ **`clear_keyboard()` before ANY path that swallows keys or does not return.** Two
  different mechanisms stranded a held key on the host, both fixed the same way (the
  call `doom_begin()` already made, for the same reason): the prompt swallows the
  *release* of a key that was already down, and the apply path never scans the matrix
  again once `fw_staging_apply_and_reboot()`/`mcu_reset()` is entered, so the release is
  never even produced. Either way the host keeps the keycode registered and auto-repeats
  it until USB drops — field-reported as "a few hundred repetitions until the keyboard
  rebooted". On the apply path the following `oled_fw_apply_screen()` conveniently gives
  the cleared report ~26 ms to leave over USB.
- **Answer the prompt on the RELEASE, not the press.** `split72.c`'s `matrix_scan_kb`
  inverts a keycap on press and un-inverts on release *independently of
  `process_record`*, so acting on the press tears the prompt down and redraws the normal
  legend while that keycap is still inverted — and it stays inverted until the finger
  lifts.
- ⚠️ **A visual cue set on a path that never returns is never painted.**
  `rgb_matrix_indicators_kb` had picked orange while `commit_pending` for a long time,
  but it only runs from the next `rgb_matrix_task()` — and on the apply path there is no
  next task. The cue the code appeared to implement had, in practice, **never been
  seen**. `poly_flash_rgb_now()` pushes it synchronously (`rgb_matrix_update_pwm_buffers`),
  the same way `oled_fw_apply_screen()` flushes the status OLED in one pass. Generalise:
  anything that must be *visible* before a blocking self-flash / reset has to be flushed
  by the code that draws it, not left to a periodic task.
- Housekeeping calls `fw_staging_confirm_tick()` **outside** the `!fw_up_active` gate
  and holds `update_performed()` while pending, so the idle fade can't dim the prompt
  out from under the user (`update_displays` early-returns once `DISP_IDLE` is set, so
  it would never be redrawn either).
- `kdisp_draw_glyph_double_at()` (`base/disp_array.c`) is the 2× mirror of
  `kdisp_draw_glyph_half_at()` — the keycap fonts top out at the 27 px `_Base_` face,
  so it is the only way to fill a 72×40 panel with one character. Both take the literal
  top-left of the **ink** (no baseline align, no `xOffset`).
- Layout is measured from the font metrics at runtime, not hardcoded: "REJECT" descends
  2 px below the baseline (the J) and "ACCEPT" does not, so a fixed bottom baseline
  clips one of them. Preview the cells with `PolyKybdHost/tools/gfx_font.py`.
- Full user-facing story: `keyboards/polykybd/tools/SIGNING.md`. BOOTSEL/UF2 bypasses
  `fw_staging` entirely, so enforcement can never brick a board.
- ⚠️ **Signing gates the FIRMWARE image only — it does NOT close the code-execution
  surface, and this section reads as though it does.** `fw_staging_check_signature()`
  is called exclusively in the `FW_TARGET_FIRMWARE` branch of `fw_staging_finalize()`;
  the **resource region** (4–8 MB) has no signature check at any target. That matters
  because one of the things flashed there is **executable code**: `doom_pack_load.c`
  validates the `.plyx` engine pack with magic / ABI / size / RAM-pairing / **CRC32
  only**, then calls `init(&s_fw_api)` — branching to an offset the pack itself names,
  on an M0+ with no MPU, so the loaded code is unconfined. The whole chain is remote
  over HID with no keypress: flash a crafted `.plyx` (cmds `0x50`–`0x52`) → set
  `IDLE_STYLE_IDDQD` (cmd 28) → the next idle runs it. So the A/ACCEPT prompt guards
  the firmware image while an unguarded path loads code beside it. Tracked as **FW-9**
  (open, high) in `polykybd-ctnd/docs/SECURITY_AUDIT.md`, with the fix sketch — verify
  the pack with the Ed25519 machinery already compiled in, **at load time, not at
  COMMIT** (flash can be rewritten after a COMMIT succeeds). Interim mitigation:
  build without `POLYKYBD_DOOM_PACK`. `.whx` / `.plyf` ride the same unsigned
  transport but are data, not code.

- **Crash diagnostics — the NOLOAD crash record, the flash archive, the 8 s
  watchdog, the phase breadcrumb and the crash-test triggers — are
  [`keyboards/polykybd/CRASH_DIAGNOSTICS.md`](keyboards/polykybd/CRASH_DIAGNOSTICS.md).**
  A fault, an unhandled exception or a hang is recorded, rebooted through and
  announced on the next boot: console line, HID **cmd 39** (protocol v16), and the
  slave's own record pulled over the split link. Four rules that bind code outside
  `base/crash_record.c`:
  - ⚠️ **A new blocking path longer than 8 s needs a `crash_watchdog_feed()` inside
    it** (`CRASH_WATCHDOG_MS`), or it produces a `kind=watchdog` record — which is
    the point, but know which one you are choosing. Two places disarm it
    deliberately: `shutdown_user()` and `fw_staging.c` right before
    `fw_staging_do_apply()`.
  - ⚠️ **Never move `crash_record_init()` after the core1 launch.** It archives to
    flash WITHOUT the `fw_staging` core1 lockout, which is sound only because core1
    has never been launched at that point; run it later and releasing the lockout
    does a bounded RELAUNCH whose unbounded FIFO handshake finds core1 already
    running and blocks forever — a keyboard that hangs on the boot after every crash.
  - ⚠️ **`WATCHDOG.REASON.TIMER` alone is NOT a hang** — the bootrom's post-UF2-copy
    reboot is a watchdog reboot, so the first boot after every BOOTSEL flash reads
    TIMER. The discriminator is `watchdog_enable_caused_reboot()`.
  - **The phase enum is mirrored in the host's `PHASE_NAMES`** (`crash_report.py`);
    keep the numbers in step, or a phase added here reads as `phase N` there.

- **The four idle anti-burn-in styles and the Eden screensaver are
  [`keyboards/polykybd/IDLE_STYLES.md`](keyboards/polykybd/IDLE_STYLES.md)** —
  `IDLE_STYLE_PULSE` (0), `JITTER` (1), `IDDQD` (2, the DOOM attract demo) and
  `EDEN` (3), in `poly_eeconf_t.idle_style` / HID cmd 28. Four rules that reach
  outside those files:
  - ⚠️ **The looping idle frame is TIME-SLICED — never render it as one blocking
    unit.** A frame is ~36 keycaps and ~150 ms of measured CPU; rendered whole, a tap
    that starts and ends inside one is never seen (field: *"Eden doesn't wake on the
    first keypress"*), and on the slave it stalls that half's scan and the master's
    matrix pull too.
  - **`update_displays()` early-returns while `DISP_IDLE` is set** — the idle painter
    owns the keycaps from that point, so anything that must stay visible during idle
    has to hold `update_performed()` (what the FW-2 prompt and the macro recorder do)
    rather than expecting a redraw.
  - ⚠️ **The DEFAULT is board-dependent and gated on the SAME macro the renderer
    compiles on**, so a default whose renderer is a no-op stub is not expressible.
    That mattered: EDEN on split42 would have been an anti-burn-in setting that
    freezes the legends instead of moving them, and the enum's own comment claimed it
    "behaves like PULSE". **Before defaulting anything to a feature with stubs, check
    what the stub path actually leaves running.**
  - **Boot-intro-done persistence rides the suspend-only dirty-flag EEPROM model** —
    `mark_boot_intro_done()` sets `g_boot_dirty`, never a direct write.

### Intl latin-variation picker (`poly_keymap.c`, `_ADDLANG1`)
Holding **Intl** shows each letter's selected accented variation; tapping **Ctrl**
(`LATIN_PICKER_MOD`) turns the number row into a picker of that letter's variations.
The mechanism is worth knowing because it is not the obvious implementation:

- ⚠️ **The picker modifier is `MOD_MASK_CTRL` and must not go back to Alt.** The
  picker swallows the keys it handles (`process_record_user` returns false), so the
  host only ever sees the modifier go down and back up — and a bare **Alt** tap is
  how Windows activates the menu bar, so picking a variation yanked focus out of the
  text field in a lot of programs. A bare Ctrl tap does nothing in the same apps.
- **The layer must PASS the modifier through.** `_ADDLANG1` masked `KC_LCTL` with
  `KC_NO`, so Ctrl never reached the base layer and the picker could not be opened at
  all — and that mask is *why* it was on Alt originally (Alt was the one modifier the
  layer let through). A masked modifier draws an **empty** keycap and is otherwise
  indistinguishable from a code bug, which is why `boot_diag.c` now reports
  `intl: … ctrl=pass shift=pass alt=masked -> picker OK`, read out of the compiled
  keymap. ⚠️ Verify such a fix against the **compiled `keymaps[]` in the ELF**, not by
  counting columns in the `LAYOUT` macro — column-counting produced a first attempt
  whose Ctrl was still masked, and the same banner scan (every position, **both**
  hands) later found two more masks nobody had spotted on split42.
- **The picker LATCHES by registering the REAL modifier** (`register_mods(MOD_BIT(
  KC_LEFT_CTRL))`), not by setting a private flag. Two reasons, both binding: the
  slave draws the picker digits on *its* own keys and only ever sees `poly_layer_t.mods`,
  which is already synced; and **both flag bytes in `base/com.h` are full** (all 8 bits
  used in each), so a private flag has nowhere to ride. `s_picker_latched` records only
  that *we* registered it.
- ⚠️ **Gate the release swallow on OWNERSHIP, not on the keycode.** The latch toggles
  on the press, so the release of the arming tap must be swallowed or QMK unregisters
  the modifier before the finger lifts. But a Ctrl **already held** when Intl was
  pressed is registered by QMK — swallowing *that* release leaves it registered
  forever and turns every later keystroke into Ctrl+key. `if(addlang && !pressed &&
  s_picker_latched && ...)` is the correct condition; the other two cases fall through
  harmlessly. `layer_state_set_user()` also unlatches on layer exit, for the same
  "never release a Ctrl the user is really holding" reason.
- **Nothing may overlay this layer.** Its letters *are* the payload (`render_key()`
  draws the variation) and the picker modifier is Ctrl, so both overlay sources would
  paint the *Ctrl view* over it — `copy_overlay_to_buffer()` the app's Ctrl-modifier
  image and `keycode_to_disp_overlay()` the built-in Ctrl-shortcut hints, on every key
  at once. ⚠️ The `!add_lang` guard must wrap **both** arms of the display_overlays
  if/else; folding it into the first condition (`!add_lang && display_overlays`) looks
  equivalent and is not — the `else` then fires on the Intl layer and paints the
  hardcoded hint straight back over the variation.
- The armed indicator is the inverted Ctrl keycap — see the two rendering bullets
  above (render it, don't `kdisp_invert()`; and pass `cy_radius` 0).

### Intl letter remap — a key can host ANOTHER letter's row (`KC_LAT_REMAP`)

French needs `è é ê` at once, which one letter's picker cannot give: the picker
chooses another *form* of the letter a key already hosts. So a key can now be
**reassigned to a different base letter**. Hold Intl, tap the remap key (split72
`[4,1]`, beside the Ctrl at `[4,0]`; split42 `[3,4]`), press the key to change (it
inverts), then the letter it should host. `e`, `q` and `j` can then carry `é`, `è`
and `ê` — and the sparse letters (`q` has one variation, `j` two) stop being dead
keys on this layer.

- **The storage splits two things the code had conflated.** The ROW comes from the
  letter a key HOSTS (`latin_ex_map`); the PICK comes from the KEY's own slot
  (`latin_sync_t.ex`). They are the same number only while nothing is remapped,
  which is why one index sufficed for years. Two keys on the same letter share a
  row but need independent picks — storing the pick at the row index has one key
  silently overwrite the other's choice. `latin_sync_t.assign[20]` holds one 6-bit
  base letter per target key, **shared across case** so Shift follows the remap
  (`r → e` gives E's upper-case form, not `<`); the pick stays per case because the
  two rows are not parallel (lower `n` has 12 variations, upper `N` 11).
- Pick fields are **case-INTERLEAVED** (`slot*2 + case`), not case-blocked, so
  growing `LATIN_TARGETS` appends fields instead of inserting a block mid-array.
  Extending the targets to the punctuation keys is the obvious next step —
  `KC_MINUS 0x2D … KC_SLASH 0x38` is a contiguous run of 12 printable punctuation
  keycodes, so `kc - KC_MINUS + 26` needs no table. It costs ~43 bytes (the pick
  array grows too), which is why `POLY_EECONFIG_USER_RESERVED` was taken 128 → 256
  in the letters-only change: that relocation resets the dynamic keymap once, and
  paying it early means punctuation later costs no second reset.
- **Re-assigning a key to its OWN letter is the per-key reset** (stored as
  `LATIN_ASSIGN_NONE`), so it needs no gesture of its own. **Shift+remap clears
  everything** — once the board is remapped the Intl legends no longer match the
  printed letters, so there has to be one way back that does not depend on
  remembering what was changed.

⚠️ **Four traps this feature hit, all of which generalise beyond it:**

- **An unwritten EEPROM byte reads `0x00` here, NOT `0xFF` — never infer "never
  written" from the bytes.** The assignment map was designed to need no migration
  sentinel because `LATIN_ASSIGN_NONE` is all-bits-set and "erased flash reads
  0xFF". QMK's **wear-levelling normalises its backing store so cleared bytes
  arrive as ZERO** (`quantum/wear_leveling/wear_leveling.c` clears its cache with
  `memset(...,0)` and its header requires a 0xFF-based store to return the
  *complement* "such that this wear-leveling algorithm receives zeros"). The map
  therefore read back all-zero = "every key hosts letter 0", and **every Intl
  keycap rendered a variation of `a`** (field, first flash). Gate such a field on
  the `latin_pick_migrated` **format version**, which is what that byte is for.
  - ⚠️ **A version gate alone does not HEAL an already-flashed board** — the broken
    build persisted the zeros at the next suspend *and stamped them valid*. The
    recovery is to **retire the version value**: `0xC3` now means "picks are fine,
    discard the map" and `0xD7` is current. Walk every version a field EEPROM can
    present before shipping such a fix.
  - ⚠️ The reservation bump relocates the **dynamic keymap** only. `poly_eeconf_t`
    sits *before* it at a fixed address and is **not** reset — which is exactly why
    the stale zeros survived the flash that was supposed to clear everything.
- **`render_key()` is only consulted when `to_static_text()` returns NULL.** A key
  that HAS a legend bypasses it completely. The remap prompt blanks the board from
  inside `render_key()`, so every non-letter *with a legend* — including the remap
  key itself — sailed past and kept drawing normally: never blanked, never
  inverted, so nothing on the board said the latched mode was open. Any future
  "the board becomes a dialog" mode must **also suppress `text`** in
  `update_displays()`, not just return false from `render_key()`.
- **Keep every glyph of a multi-glyph legend in ONE font.**
  `kdisp_write_gfx_char` baseline-aligns by `font->yAdvance - fonts[0]->yAdvance`,
  so glyphs from different fonts land on different baselines. `a` is in `_Base_`
  (yAdvance 37 → −3 px) while `»`/`ñ` are in `_SupAndExtA_` (44 → +4 px): the
  legend `a»ñ` sat its `a` **7 px** high. `Á»Æ` is even only because all three of
  its glyphs are Latin-1, i.e. one font — hence `INTL_REMAP_LEGEND` is **`à»ñ`**.
  ⚠️ `oled_preview.py` **cannot** show this (it models `xOffset`/`yOffset` but not
  the baseline-align shift), so both legends render identically there — the check
  is the font metrics, not the preview.
- **The "gate the release swallow on OWNERSHIP" rule applies to LAYER keys too.**
  The remap block returned `false` for every release, which swallowed the release
  of **`MO(_ADDLANG1)` itself** — QMK never unregistered the layer, so Intl went
  down and never came back up and the mode could not be escaped at all; a held
  Shift was stuck the same way. Modifiers and layer keys
  (`IS_MODIFIER_KEYCODE` / `IS_QK_MOMENTARY` / `IS_QK_TO`) must fall through. This
  is the same rule already written up for the picker's Ctrl latch, one function
  away, and it was still missed.

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

### Layer names over the wire (`layer_names.c`, HID cmd 35, protocol v14+)

The host layout editor labels its layer tabs. It used to read those labels from
`PolyKybdHost/polyhost/res/layer_names.yaml`, a build-time artifact generated from
this repo's `layers.h` — and that generator's default source path had been dead
through **two** renames, so nothing regenerated it and nothing failed. The committed
file still listed 14 layers ending `EMJ0`/`EMJ1`, a split this firmware had not had
in a very long time (found 2026-08-26). The editor was mislabelling tabs against an
enum that no longer existed.

**A name the keyboard states itself cannot drift from the keyboard**, which is the
whole point of cmd 35. Three things are worth knowing:

- ⚠️ **The count is NOT a second opinion.** It is the same
  `DYNAMIC_KEYMAP_UPDATE_MAX_LAYER_COUNT` that cmd 17 already reports, echoed back so
  the reply parses standalone. Do not "improve" it to
  `DYNAMIC_KEYMAP_LAYER_COUNT` (12): layers at or above the write cap are served from
  flash and have no editor tab to label, so naming them would invite the editor to
  draw tabs it cannot write to.
- **All three name widths live in ONE record** (`layer_names.c`). split72's status
  OLED wants the full name, split42's 32 px panel wants ≤5 chars, and the wire wants
  ≤8 — and before this those were three hand-kept lists, two of which carried a
  "keep in sync with the other" comment. That is the guard shape this repo keeps
  getting caught by, so a layout now cannot be added without giving it every form.
  A `_Static_assert` pins the named set to exactly the remappable range;
  mutation-checked by dropping `_UL` from the table, which fails the build with the
  assert's own message.
- **Names are capped at 8 chars, so "Colemak DH" ships as `ColemkDH`.** The cap is
  the host tab label's budget; the emitter clamps rather than trusting the table, so
  an over-long name can never run into the next record.
- ⚠️ **The TOTAL is what makes the reply decodable, and it is why the length is not
  in the records.** The host reads byte 0, keeps reading until it holds that many
  bytes, and only then splits on the NULs — so termination is arithmetic rather than
  a scan, and the report's zero fill is never examined. Two encodings were tried
  first and shipped briefly; both are worse, and the reasons generalise:
  - **Fixed-width 8-byte records** give the same arithmetic length but cost 65 bytes
    and therefore a **second report**, for a command whose whole payload otherwise
    fits one.
  - **Terminated records with no total** force the decoder to find the end by
    scanning, and the only way to tell a real terminator from the zero fill is "an
    empty name means padding" — which makes an **UNNAMED layer** (`poly_layer_name_wire()`
    returning NULL, i.e. a bare terminator) indistinguishable from the fill and
    silently truncates the list. That is the case that decided it.
  - ⚠️ **The claim that a terminated form is UNSAFE against truncation is FALSE, and
    was asserted here for a while on the strength of a bad test fixture.** The fixture
    fed the decoder a **short non-final report**, which the emit loop cannot produce —
    a short report is always the last one. Measured across every reachable failure
    mode (last report lost, first lost, reordered, nothing arrives), fixed-width,
    naive-terminated and total-prefixed behave **identically**. Robustness is a wash
    under real HID behaviour, because whole 64-byte reports arrive or nothing does;
    the encoding choice is about size, report count, and whether an unnamed layer is
    expressible. Don't re-derive a robustness argument here without checking which
    scenarios the firmware can actually emit.
- **`LAYER_NAMES_PAYLOAD_MAX` is `_Static_assert`ed to stay under 255**, since the
  total is one byte. Mutation-checked by widening `POLY_LAYER_NAME_MAX` to 32, which
  fails the build with the assert's own message. At 12 layers × 8 chars it is 110.
- **Names are capped at 8 chars, so "Colemak DH" ships as `ColemkDH`.** The cap is
  the host tab label's budget; the emitter clamps rather than trusting the table, so
  an over-long name can never run past the buffer.

### The settings-layer RGB row (`poly_keymap.c`, `keycode_helper.c`, `split72/config.h`)

⚠️ **A LEGEND IS NOT EVIDENCE A KEYCODE DOES ANYTHING — the four RGB effect presets
drew a keycap for years and were dispatched nowhere.** `RGB_M_P` / `RGB_M_B` /
`RGB_M_R` / `RGB_M_SW` (`0x782B`–`0x782E`) are the legacy **underglow** mode keycodes.
QMK routes that range through `process_underglow()` even on an RGB-matrix-only board,
but its switch covers only toggle / next / previous / hue / sat / val / speed — the four
mode presets have **no case there, none in `process_rgb_matrix()`**, and
`IS_RGB_KEYCODE` / `RGB_KEYCODE_RANGE` are defined in `keycodes.h` and dispatched
nowhere at all. So the keys rendered, felt real, and did nothing (fixed 2026-09-09,
qmk#281). **Before believing a key works because it has a legend, grep for a `case`
that handles its keycode** — the display pipeline and the action pipeline share
nothing, and this repo has now been caught by that seam in both directions (the
settings-gate note above is the same split with the halves reversed).

- ⚠️ **The effect must be enabled on BOTH variants, because the handler is in the
  shared keymap.** `RGB_MATRIX_CYCLE_SPIRAL` is the closer match for "Swirl" and is
  **split72-only**, so the preset maps to `CYCLE_PINWHEEL` (18) instead; the other
  three are `SOLID_COLOR` (1), `BREATHING` (5), `RAINBOW_MOVING_CHEVRON` (15). Read
  the indices out of the compiled object (`nm -S` + `objcopy`) rather than counting
  the enum by hand — the set depends on which effects each variant compiles in.
- ⚠️ **`val_to_percent()` scaled against 255 while the value is CAPPED at
  `RGB_MATRIX_MAXIMUM_BRIGHTNESS` (100), so a fully-lit matrix reported 39%** and the
  status-OLED row could never reach 100 whatever the user did. It scales against the
  cap now; since `RGB_MATRIX_VAL_STEP` is 1, one step is exactly one percent.
  Saturation genuinely is a `/255` value — the two share a row and do **not** share a
  scale.
- ⚠️ **New RGB defaults reach only a FRESH eeconfig.** QMK writes them in
  `eeconfig_update_rgb_matrix_default()`, so an existing keyboard keeps its stored
  brightness and speed and sees no change; only the corrected percent is immediate.
  Adopting defaults on deployed boards would need a one-time migration sentinel (the
  `idle_style_fmt` shape) — say so in release notes rather than implying the value
  moved for everyone.

- **Macros — storage, the playback state machine, on-keyboard recording, the
  keycap look and the label faces — are
  [`keyboards/polykybd/MACROS.md`](keyboards/polykybd/MACROS.md).** HID cmds
  36/37/38 behind ONE `"macros"` feature gate, protocol v15+. Five rules that reach
  beyond that file:
  - ⚠️ **Playback is OURS and must stay a state machine.** QMK's
    `dynamic_keymap_macro_send()` runs the whole macro inline with a
    `while (ms--) wait_ms(1)` delay; here that loop also scans the matrix, drives the
    split UART, services USB HID and pushes 72 SPI displays, so a macro with a
    half-second delay would freeze the board and drop the link. `poly_macro_tick()`
    runs at most ONE step per housekeeping pass. No time-slicing is needed — steps
    have to be SPACED for the host to see distinct events, so the pacing IS the yield.
  - ⚠️ **Capture is installed from HOUSEKEEPING, not `keyboard_post_init_user()`.**
    `protocol_post_init()` runs after the post_init hooks and installs the USB
    driver, so a shim set there is overwritten a moment later and the recording
    captures **nothing** — no error, no missing key, just an empty macro.
  - ⚠️ **`clear_keyboard()` before starting playback and on abort**, the same rule
    the FW-2 prompt and `doom_begin()` follow: a DOWN step leaves a modifier
    registered, and the bank Shift would otherwise leak into the macro's own output.
  - **Swallowed in `process_record_user()`**, never left to the release edge — `_UL`
    is an `OSL()` layer, which re-dispatches a release-edge action up to three times
    (§ *A release-edge action fires up to THREE times*), i.e. the macro plays twice
    or three times over.
  - ⚠️ **A new custom keycode with NO legend renders a BLANK KEYCAP**, which is
    indistinguishable from "the feature did not ship" — `KC_MACRO_REC` reached the
    field that way. The build is green either way: a missing legend is a missing
    `case`. **Grep both legend switches** (`to_static_text()` and
    `keycode_to_static_text()`) for a new keycode before calling it done.

### LTR-559 light+proximity sensor (`modules/polykybd/polymod_ltr559/`) — ENTIRELY OPTIONAL

An **entirely optional** ambient-light + proximity sensor (Pimoroni LTR-559, I2C
addr `0x23`) on the expansion port. It **shares the Cirque I2C0 bus** (GP0/GP1) — no
new pins. It is a **clean no-op when no sensor is fitted**: the probe fails and the
driver disables itself after a few bounded retries (`LTR559_MAX_RETRIES`). So anyone
who solders the part gets it and nobody else pays more than ~30 s of cheap probes.

- ⚠️ **The DRIVER is a community module (`polykybd/polymod_ltr559`), not a
  `keyboards/` source file** — it moved out of `base/ltr559.c/.h` (2026-08-12).
  Consequences, all easy to trip over:
  - **Listing it in `keyboard.json` `modules` is the entire enable.** There is no
    `SRC +=` line and no `-DPOLYKYBD_LTR559`; the build defines
    **`COMMUNITY_MODULE_POLYMOD_LTR559_ENABLE`** for you, and that is what
    the consumer code gates on. `POLYKYBD_LTR559_DRIVE` survives
    unchanged as the separate gate for the PolyKybd **policy** (auto-brightness +
    idle-inhibit + the `USER_SYNC_SLAVE_DATA` slot), so a board can carry the driver
    without the policy. Since #237 that policy lives in **`ltr559_policy.c`**
    (the lux→contrast curve, the drive tick, the proximity wake, the slave-pull
    handler); `poly_keymap.c` only registers the split handler and calls
    `poly_ltr559_drive()` from housekeeping.
  - **The module probes and polls itself** from `keyboard_post_init_polymod_ltr559` /
    `housekeeping_task_polymod_ltr559`. `poly_keymap.c` no longer calls
    `ltr559_init()`/`ltr559_task()` — **don't re-add them**, that would double-probe.
    The ordering is safe because `quantum/keyboard.c` runs `*_modules()` **before**
    `_kb`/`_user`, so post_init's `ltr559_available()` and housekeeping's reading are
    both current.
  - **It has 19 unit tests** (`make test:polymod_ltr559`) driving the real driver
    against a mock LTR-559 + mock I2C bus — the bounded retry, the config-write
    refusal, the ALS byte order, the invalid-sample rule, the growing-then-rolling
    average. Wired into the harness via `builddefs/testlist.mk` +
    `builddefs/build_test.mk`. Run them after touching the driver; they are ~1 s.
  - Both variants list it. Precedent: `polymod_crc32` / `polymod_rle`.
- **Side-agnostic** — auto-detected on **whichever half it's soldered to**. The
  module's hooks run on **both** halves; the one that answers uses it, the other
  gives up after the bounded retries. ⚠️ Do **not** re-gate on
  `is_right_side()` — it was, and a left/master-soldered sensor was never read (field).
- **Slave→master backchannel** — brightness/idle decisions are master-only, but the
  sensor may be on the slave, so the master **pulls** its values over a **generic
  op-dispatched RPC** `USER_SYNC_SLAVE_DATA` (a `kind` byte selects the payload;
  `SLAVE_DATA_SENSOR` → `{avg lux, prox}`). Works in either USB orientation and is
  reusable for other slave-side data; consumes one split-transaction slot, guarded by
  `POLYKYBD_LTR559_DRIVE`. If the master holds the sensor it reads locally instead.
- **Auto-brightness** (`poly_ltr559_drive()`, master-only, every `LTR559_DRIVE_MS`)
  feeds the 5 s average lux through the **same volatile/host-auto path the host
  daylight feature uses** (`set_brightness_auto_mode`/`set_auto_brightness_value`). So
  the sensor drives **only while auto mode is on**; it engages auto **once** (first
  real reading); a **manual** change (preset keys / host explicit set) turns auto OFF
  and the sensor **backs off** (its per-tick push no-ops while auto is off and the
  `engaged` static never re-engages) — **manual always wins** until auto is re-enabled
  or reboot. Refreshing ~0.5 s vs the host's ~10-min daylight, it overrides host
  daylight values while auto is on.
- **Boot dark-screen guards** (learned the hard way) — `ltr559_avg_lux()` is a
  **growing-window** average (0 only until the first VALID sample). Two guards keep it
  off the near-off floor: (1) `poly_ltr559_drive()` doesn't engage while `avg == 0`
  (first ~1 s), so boot holds the manual/restored brightness instead of dipping; (2)
  `lux_to_contrast()` floors at `LTR559_MIN_CONTRAST` so it never drives below a
  visible dim level (never `B=1`/`DISP_OFF`).
- **Proximity → idle-inhibit** — 11-bit **relative reflectance** (not calibrated
  distance; ~5–6 cm max). `prox > LTR559_NEAR_THRESHOLD` wakes the displays +
  `update_performed()`. Uses the PS channel (works in the dark), NOT the ambient-shadow
  drop on the ALS channels. ⚠️ The resting baseline is **housing-dependent** — ~129 on
  the open bench but ~325 once mounted (enclosure walls reflect IR back); re-check
  `PRX` and the threshold after any housing/hole change.
- **Measured tuning** (hardware): proximity resting ~129 bench / ~325 housed, ~5 cm
  400, ~1 cm 1000, hole covered ~2000 (saturated) → `NEAR_THRESHOLD` 350. Lux (sqrt
  curve) `LUX_FULL_REF` 100 → B≈4 dark room, 26 @ 28 lux, 35 @ 50 lux, full @ 100+ lux;
  night floor `MIN_CONTRAST` 4; `LTR559_ALS_GAIN` 4×.
- **Telemetry** — a 10-min `uprintf` heartbeat in housekeeping, gated on
  `ltr559_available()` so only the sensor half logs (`LTR-559: lux=.. avg=.. prox=..
  ch0=.. ch1=.. B=..`). The status-OLED test readout + I2C bring-up diagnostics were
  removed once it worked; the bus scan is kept as a disabled `#if 0` reference block in
  `polymod_ltr559.c`. No shared timed-log framework yet — see `readme.md` "Diagnostics" →
  "Timed console logs".

### Community modules (`modules/polykybd/`)

Self-contained, keyboard-independent code lives in **QMK community modules** rather
than `keyboards/polykybd/`: currently `polymod_crc32` and `polymod_rle` (both ~55 LOC
pure-algorithm libraries), with `polymod_ltr559` (the LTR-559 driver) extracted the
same way. The mechanics are not obvious from the QMK docs alone:

- **Declared in `keyboard.json`, not `keymap.json`.** Both variants carry a
  `"modules": ["polykybd/polymod_crc32", …]` array. The docs describe the
  `keymap.json` route (and External Userspace); the keyboard-level array is what this
  fork uses, so a module lands on every keymap of that board.
- **Listing a module IS the enable — the build defines
  `COMMUNITY_MODULE_<NAME>_ENABLE`** (upper-cased directory name) for you, plus
  `COMMUNITY_MODULES_ENABLE`. So a module needs **no `SRC +=` line and no bespoke
  `-D<FEATURE>` in `rules.mk`**; gate consumer code on the generated define instead of
  inventing a parallel one. `modules/<ns>/<name>/<name>.c` is compiled automatically
  (matching the directory name); any *other* source file needs `SRC +=` in the module's
  own `rules.mk`.
- ⚠️ **Module hooks run BEFORE `_kb`/`_user`.** `quantum/keyboard.c` calls
  `keyboard_post_init_modules()` then `keyboard_post_init_kb()`, and
  `housekeeping_task_modules()` then `_kb` then `_user`. **This is what makes a
  self-driving module safe**: a module that probes hardware in its post_init hook is
  already done by the time `keyboard_post_init_user()` runs, and one that polls in its
  housekeeping hook has produced *this* pass's sample before `housekeeping_task_user()`
  reads it. Verify this before deleting explicit init/task calls in favour of hooks —
  it is the whole argument.
- ⚠️ **Overriding the non-suffixed hook means you must call the `_kb` link yourself.**
  The build generates a weak `<api>_<module>()` → `<api>_<module>_kb()` →
  `<api>_<module>_user()` chain. Defining `housekeeping_task_<module>()` replaces the
  top of that chain, so it must call `housekeeping_task_<module>_kb()` or the keyboard/
  keymap specialisations are silently dropped. `modules/qmk/hello_world` is the pattern.
- **This fork is on module API 1.1.2.** The available hooks are the union of
  `data/constants/module_hooks/*.hjson` (0.1.0 → 1.1.2); read those files rather than
  the docs table, which stops at 1.1.0. 1.1.1 added LED/RGB matrix effects, **1.1.2
  added custom split data sync** (`SPLIT_TRANSACTION_IDS_MODULE_<MODULE>`) — relevant
  here, where several subsystems carry their own split transactions. Assert the floor
  you rely on with `ASSERT_COMMUNITY_MODULES_MIN_API_VERSION(1, 0, 0);` (commas, not
  periods) after `#include "community_modules.h"`.
- **What is worth extracting**: code with no PolyKybd types and no display/protocol
  coupling. Surveyed 2026-08: the remaining strong candidates are `base/crypto/`
  (vendored Monocypher Ed25519), `base/multicore/` (RP2040 core1 launch + FIFO),
  `os_actions.c` (per-OS chord table — the best *community* candidate, since
  `qmk_module.json` `keycodes` is built for exactly that), and with a decoupling pass
  the idle-timestamp half of `base/update.c` and `base/fw_staging.c`. **Not**
  `poly_keymap.c` / `hid_com.c` / the overlay + display stack — that is the product.
  The `extract-qmk-module` skill drives the whole conversion.

- **Wiring a unit-test suite — the two registrations, the standalone-test gotchas
  and the mutation-testing traps — is
  [`keyboards/polykybd/UNIT_TESTS.md`](keyboards/polykybd/UNIT_TESTS.md).**
  `make test:polymod_ltr559` is the pattern for a driver against a mock bus,
  `make test:fw_up_verdict` for DECISION logic. Four rules kept here:
  - ⚠️ **`make test:<name>` with a name not in `TEST_LIST` exits 0 and prints
    NOTHING** — no "unknown target", no output, just a clean prompt. **Judge a run by
    the `[  PASSED  ] N tests.` line, never by the exit code.**
    `grep -rn "TEST_LIST +=" --include=testlist.mk .` is the authoritative list.
  - ✅ **These run in CI via `polykybd-unit-test.yml`, NOT upstream's
    `unit_test.yml`** — upstream's filters on paths a PolyKybd change never touches,
    so for as long as these suites existed CI ran none of them. Do not "fix" that by
    adding our paths to the stock workflow; it would conflict at the next catch-up
    merge. Suite names are DERIVED by grepping the testlist files, so a new suite
    needs no workflow edit.
  - **Extracting the decision from the I/O is worth doing on its own terms.**
    `base/fw_up_verdict.c`, `base/macro_decode.c` and `base/macro_record.c` are pure
    (no quantum.h, no EEPROM, no timer) because in each case the arithmetic was the
    part with a bug history and the only part unreachable — it shared a function with
    the transport.
  - ⚠️ **Mutation-test the suite before trusting it**, and know the three fail-open
    traps that all report "your tests caught nothing": ANSI escapes in gtest output
    defeating a `^\[` grep, a mutation that never APPLIED, and a
    `git diff --quiet` guard that compares against HEAD on a tree that is already
    dirty. The `mutation-test-suite` skill carries the recipe.

### Notable QMK features enabled
RGB matrix (72 LEDs, 35 effects), dynamic keymap (9 host-remappable layers), unicode input (Linux/macOS/Windows/BSD), Cirque trackpad (split72 variant), `USE_CORE1` multicore.

⚠️ **VIA is NOT supported and must not be advertised as such.** `VIA_ENABLE` is
unset on both variants — only `DYNAMIC_KEYMAP_ENABLE` is on, and remapping happens
through PolyKybdHost's own layout editor over our raw-HID channel. The one residue
is `poly_keymap.c`'s `#include "quantum/via.h"`, which is where QMK happens to
define the `id_dynamic_keymap_*` command IDs the dynamic keymap uses; that include
is a QMK header path, not a VIA feature. Don't reintroduce "VIA-compatible" wording
in docs, UI strings or comments.

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
- **`fonts/gen-lang-fonts.sh`** — generates `base/fonts/flag_fonts.h` for the language-selection layer (`_LL`): country flags from NotoColorEmoji, one per `LANG_*` at codepoint `0xE000 + enum index`, via fontconvert's `-F`; the country list is derived from `lang_lut.xlsx` automatically. (The `_Tiny_` lang-code label font moved to `fonts/gen-status-fonts.sh` — see "Standalone UI text fonts" below.) These are **not** in `fonts.yaml`/`ALL_FONTS` — like the status-OLED fonts they're used via dedicated single-font arrays. `render_lang_flag_key()` in `poly_keymap.c` draws the flag (top 28 px) + the `xx-YY` code (bottom 12 px) per key, with a frame on the selected language. Re-run only when the language list changes. It also emits **`base/fonts/generated/lang_flags.json`** — the flag font's render record (source NotoColorEmoji, the `-s20 -g -r54 -W72 -O1 -Dfs -e-0.10` options, `seq_first` 0xE000, and the per-flag regional-indicator `sequence`). The flag font isn't in `fonts.yaml`, so `generate_fonts.py` emits no render record for it; this sidecar lets the host font-pack **editor** rebuild a single flag (sequence mode). ⚠️ Mirrored **byte-identically** in `PolyKybdHost/polyhost/res/fontpack/lang_flags.json` — keep both in sync (`cmp`).
- **The keycap `latin` category is built grid-fitted (`hinting: auto` in
  `fonts.yaml` → `fontconvert -Hauto`).** Same reason as the status fonts: NotoSans
  ships no hinting bytecode, so without it the ASCII/Cyrillic/Greek keycap legends
  render ungridfitted. `latin` is `resident: true`, so this changes only the
  compiled-in font — **every font-pack bundle stays byte-identical, so there is NO
  `.plyf` reship and no `content_version` bump**. The gain is real but modest at
  27 px (the `_Base_` size): measured mirror-asymmetry improves 12.4% → 7.1%,
  versus 14.9% → 4.4% at the 15 px status size.
- **Everything is grid-fitted EXCEPT emoji.** `hinting: auto` is set on every
  category — `latin`, `latinbig`, `hebrew`, `jp`, `kr`, `arabic`, `devanagari`,
  `bengali`, `telugu`, `tamil`, `thai`, `georgian`, `armenian`, `bopomofo`,
  `vietnamese`, `ethiopic`, `canadian`, `cherokee`, `tengwar`, `gscript` and
  `symbols`. Only **`emoji` / `emoji_fig`** (and the `flags` `pack_extra`) stay
  on `native`.
  - **It is NOT decorative at the bigger keycap sizes — measured, not assumed.**
    The intuition that grid-fitting only matters for small text is wrong here:
    counting stems that come out unequal WITHIN one glyph (the tell that a stem's
    two edges rounded independently), `latinbig` improves 26.1% → 21.4% at the
    33 px tier and **59.2% → 19.0%** at 39 px. The large tier is the bigger win,
    not the smaller one. Also note `latinbig` is sized with **`-p` (pixels)**, not
    `-s`: points-at-141-DPI can only land on even ppem, so 33/35/37/39 are simply
    not expressible in points — and those odd sizes are exactly what the 40 px
    ink ceiling forces.
  - ⚠️ **Do not "finish the job" by setting it on emoji — it is a measured no-op.**
    The autohinter assigns each glyph to a script by codepoint range and applies
    that script's blue zones (stems, x-/cap-height, baselines). Emoji codepoints
    match none of its ranges, so they get the no-script style with no zones:
    **0 of 1156 emoji glyphs and 0 of 51 emoji_fig glyphs change**. Setting it
    there only rewrites the provenance comment in the header (the flag is echoed
    into it) — the bitmaps are byte-identical. `symbols` by contrast is line art
    read as glyphs (arrows, modifier symbols, util icons) and 134 of 1060 do change.
  - **Reship cost so far**: `mideast` 1→2, `syllabic` 1→2, `asia` 1→2, `fantasy`
    3→4 (the text scripts), then `symbol` 6→7 (119 glyphs). `flags` and `emoji`
    stayed byte-identical throughout. Use the `reship-fontpack-bundle` skill — its
    `--check` is what tells you which bundles actually moved.
- ⚠️ **Two committed generated artifacts were already STALE before this work and
  `--check` flags them**: `fontpack.manifest.json`'s `total_size` (the committed
  480140 is the *post-dedupe* size, but the script builds that manifest **before**
  `prune_shadowed_glyphs` runs, so it emits the unpruned 492328), and
  `fontpack_render_settings.json` was missing all 12 `latin` records. Regenerating
  corrects both. If `--check` is ever wired into CI, fix the manifest/prune ordering
  rather than hand-editing the committed value.
- **Byte-reproducible output requires the pinned `fontconvert` build (FreeType 2.13.3 / HarfBuzz 2.6.7, the CMake ExternalProject)** — the distro fast-path build renders ~1px differently on some glyphs. The committed headers are built with the pinned toolchain.
- ✅ **`generate_fonts.py --check` PASSES on a clean checkout (2026-08-11) — if it
  drifts, something is genuinely wrong.** It had failed on every header for a long
  time, which was a **formatting** drift, not a rendering one: the committed headers
  were emitted during the column-native (PolyColGfx) work by a fontconvert built from
  a work-in-progress tree, so the current emitter wrote the same bytes differently
  (`Bitmaps[]PROGMEM` vs `[] PROGMEM`, 16 vs 12 bytes per line, glyph column widths,
  and `0` vs the running length in a **gap** record's dead `bitmapOffset`). The tree
  has now been regenerated with the pinned toolchain, so the committed headers are
  the emitter's native output and the interim `normalize_header_format.py` is gone.
  Verified across **156 fonts / 6714 glyphs** that the reformat changed no data.
  - **Two real bugs were hiding behind the permanent drift**, both fixed:
    `manifest_from_texts()` built `fontpack.manifest.json` **without** the dedupe the
    bundle path applies, so its `total_size` could never match the committed
    (post-dedupe) value; and `--only <cat> --check` reported every *other* committed
    header as `STALE`, because only the write path skipped the stale sweep under
    `--only`.
  - ⚠️ **`parse_gfx_header()` CANONICALISES every glyph's `bitmapOffset` to the
    running cumulative length**, so a purely cosmetic header change cannot reach the
    `.plyf` bytes. Without it, reformatting the tree changed **4 shipped bundles**
    (same size, ~535 single-byte diffs, all in dead gap offsets) and would have forced
    a reship + `content_version` bump for zero visual change. With it, all 7 bundles
    stay byte-identical to the shipped host copies. Don't "simplify" it away.
  - Regenerating needs **all** source fonts (`fonts/dl-fonts.sh`, ~75 MB, 21 entries)
    plus the pinned fontconvert at `/tmp/fontconvert_pinned` (the path is echoed into
    each header's provenance comment).
- **Adding codepoints to an existing `latin` font is the cheap case, and the cheapest
  sub-case is filling a GAP.** `latin` is `resident: true` and in no bundle, so the
  change is confined to the firmware image: **no `.plyf` reship, no `content_version`
  bump** — provided no *pack* font covers the new codepoints (check before assuming;
  a new resident glyph that a pack font draws identically would be pruned by
  `prune_shadowed_glyphs` on the next regen and change that bundle). A font emits one
  **contiguous** `first..last` table with gap records for unassigned slots, so a
  codepoint **inside** the existing span costs only its bitmap — the record already
  exists. `_LatinExtAdd_` spans `0x1E62..0x1EF9` with 144 gaps, which is why the Welsh
  `Ẁẁ Ẃẃ Ẅẅ Ỳỳ` + `Ẽ Ỹ` addition (2026-08-11) grew the table by **zero** entries.

See [`AdafruitGFX/CLAUDE.md`](../AdafruitGFX/CLAUDE.md) for `fontconvert` build and usage details.

- **The font pack — the resident set, the eight `PlyF` bundles, the slot layout,
  the HID flash transport and the reship procedure — is
  [`keyboards/polykybd/FONT_PACK.md`](keyboards/polykybd/FONT_PACK.md).**
  `fontpack_assemble()` builds `g_all_fonts = resident ++ pack` at boot; with no
  pack, only the resident set is present, so the keyboard always renders ASCII.
  Five rules that reach outside that file:
  - ⚠️ **`g_all_fonts` is scanned FRONT TO BACK and resident is always in front.**
    A resident font therefore WINS over an overlapping pack copy — which is how a
    single UI glyph is made resident (a tiny dedicated font covering just that
    codepoint), and why a second face at native codepoints can never be reached, so
    the bigger legend tiers are RELOCATED into private PUA instead.
  - ⚠️ **Adding a whole new resident FONT shifts every pack font's gidx and forces a
    full-pack reship.** For one or two glyphs, extend the resident `IconsFont`
    instead (`base/fonts/gfx_icons.h`) — that shifts nothing and ships with the
    firmware. Better still, use a pack glyph or a base-font character.
  - ⚠️ **Never do heavy work in a split-transaction handler.** The slave's font-pack
    COMMIT runs inside a ~20 ms RPC callback; re-CRCing the whole ~459 KB pack there
    (~50 ms) made the master time out and report a perfect flash as a CRC failure.
    `fw_staging_finalize_defer_reload()` ACKs on the O(1) transport CRC and defers
    the reload to housekeeping.
  - ⚠️ **Because FONTPACK writes IN PLACE, a slot is a valid current bundle as soon
    as the last chunk lands — COMMIT is not what makes it so.** A complete stream
    whose COMMIT ack was lost reads back at the shipped `content_version`, so the
    version comparison alone must never decide a re-flash. The host consequences are
    in `PolyKybdHost/CLAUDE.md`.
  - **Reshipping needs no `fontconvert`** — bundles derive deterministically from the
    committed category headers. The `reship-fontpack-bundle` skill wraps it; its
    `--check` reports which bundles actually moved.

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

## Hard-won lessons (and where the history lives)

### Troubleshooting principle: don't take shortcuts — mechanical, auditable steps beat clever guesses

**Lesson (2026-07-14, from the split42 rebuild):** when a bug resists the "smart"
theories, do the **dumb, exhaustive, fully-auditable exercise** instead — even when
it feels like busywork. The methodical path repeatedly turned out to be the *right*
path here, and the clever shortcuts actively cost time.

What happened: split42 (the 42-key variant) was broken and had never worked on real
hardware; the same firmware also misbehaved on split72, so it was a shared-firmware
problem, not split42-specific hardware. The productive move — which the **user** pushed
for against the instinct to "just fix it" — was to **rebuild split42 from the working
split72 in tiny, separately-committed steps** (delete → copy split72 → re-derive every
pin from the authoritative KiCad schematic → build), then **bisect the remaining
differences one subsystem at a time**. Two concrete ways shortcuts bit:
- An earlier "rebuild" had silently **reused old split42 files** instead of genuinely
  copying split72 — a shortcut that hid the real diff and produced "same problem as
  before". Only the transparent delete/copy/apply-with-a-commit-per-step exercise
  exposed it. **If you claim you copied/reset something, actually do it from the
  source — a reviewer (or the next session) must be able to verify each step from the
  git history.**
- The bug turned out to be a **disabled subsystem** causing implicit problems — the
  exact class of cause that no amount of reading the *enabled* code paths would find.
  Re-enabling the subsystems split42 had dropped vs split72 (RGB matrix, Cirque
  pointing device, LTR-559) as **separate commits** made split42 work, and dropping
  them back one at a time is what isolates *which* one. You only get that bisect for
  free if each change was its own commit.

**Practical rules this encodes** (apply to any hard PolyKybd bug, not just split42):
1. **One change per commit** so any subset can be flashed/reverted to bisect. The
   deliverable of an investigation is often the *commit sequence*, not just the fix.
2. **Derive facts from the authoritative source, not memory or an old file** — pins
   from the KiCad schematic (in the `PolyKybd` hardware repo), not a stale header.
3. **Suspect what's *absent/disabled*, not only what's present.** A missing/disabled
   subsystem (RGB/pointing/sensor, a `#define` not set, a build flag dropped) can
   change timing, linker layout, split transactions, or init order in ways that break
   an unrelated-looking feature. Diff a broken variant against a working one for
   *removed* config, and re-add it to test.
4. **Don't over-narrate conclusions before the test.** State what a build contains and
   what each outcome would imply; let the hardware result decide. ("no early
   conclusions about the result" — the user's standing instruction during this work.)

The split42 rebuild + subsystem bisect itself lives on branch
`claude/split42-literal-split72-copy` (RGB `6694d7f6`, pointing device `4c10b0d2`,
LTR-559 `d74e7e11`, trackpad-removed bisect step `b25f2045`, trackpad restored after
the bisect confirmed it).

### Rules that came out of closed investigations

The narratives moved to
[`keyboards/polykybd/INVESTIGATION_HISTORY.md`](keyboards/polykybd/INVESTIGATION_HISTORY.md)
(11 resolved bugs, the split42 split-link saga, the split-link integrity analysis
and the HIL language-timeout note). **Read it when you need the evidence behind one
of these rules, a root cause to compare a new symptom against, or the reasoning
somebody is about to reverse.** What must stay in context is the rules themselves:

- ⚠️ **Never bool-test `send_to_bridge()` — classify with `sync_succeeded()`.**
  Every return value is non-zero, give-up included, so `if(!send_to_bridge(...))`
  is dead code: the master advanced `global` to `local`, produced no diff, and
  never re-fired the lost sync. The diff IS the retry queue, and only a successful
  sync may advance `global`. The *discarding* sibling applies to the bulk overlay
  sends, which threw the ack away entirely.
- ⚠️ **The split UART has NO payload integrity check of its own.** QMK's transport
  checks a 1-byte handshake token and nothing else; the per-transaction CRC32 in
  `split_sync.c` is the only thing between wire noise and the slave applying
  garbage. Do not remove it on the theory that the transport covers it.
- **Ack byte values are Hamming-spaced (min pairwise distance 4), built as
  complement pairs at popcount 4**, because the 1-byte reply carries no CRC. A
  seventh value must keep distance 4 or single-bit tolerance degrades for the whole
  set. `sync_succeeded()` is a deliberate **whitelist** so a new failure value is a
  failure at every existing call site with no edits.
- **`sync_is_link_fault()` is a COMPLEMENT, not an enumeration of its siblings** —
  a link fault is "nobody answered" or "what reached the slave was corrupt", and
  every other byte means the slave answered with a verdict. Listing the non-fault
  values is the guard shape that goes stale. `nack` is excluded from `err%` for the
  same reason: `SYNC_BUSY` arrives on every erase re-poll of a flash.
- **The split link is full-duplex two-wire** (`SERIAL_USART_FULL_DUPLEX`, TX GP5 /
  RX GP4, `SERIAL_USART_PIN_SWAP` giving the crossover by role at runtime so one
  image serves both halves). Steady-state error rate measured **zero** after the
  migration. ⚠️ `SELECT_SOFT_SERIAL_SPEED` is **0 = 460800 baud**, not the 230400
  the historical half-duplex analysis quotes.
- ⚠️ **split42 needs `POLY_SPLIT_SHMEM_RPC_GUARD`** — an 8-byte pad at the pointing
  member's position in `split_shared_memory_t`, in front of the RPC buffers
  (`transport.h`, tracked in `UPSTREAM_PATCHES.md`). **Do not remove it**: the
  latent writer it guards against was never found.
- **EEPROM persistence is the suspend-only dirty-flag model.** Never write EEPROM
  inside a split-transaction handler (a ~50 ms wear-levelling consolidation erase
  there costs the UART its response window), and never re-add a per-housekeeping
  default-layer drain — `save_all_dirty()` flushes at suspend / reset / store.
- **`g_user_brightness` is the MANUAL brightness and is updated only at deliberate
  set-points**; idle/suspend transients must never be persisted as it. Host-auto
  mode plus its last value persist separately in `poly_eeconf_t.auto_brightness`,
  with a **known bit** so engaging auto before the host pushes cannot bank the
  default as if real.
- **Idle tracking is a `bool` plus a `uint32_t` timestamp**, never a signed
  sentinel: `is_idle_tracking()` / `disable_idle_tracking()` /
  `backdate_last_update()`. The old signed `last_update` silently disabled idle for
  the ~25-day window past uptime 2³¹, and underflowed near boot.
- **An overlay mapping chunk is ONE-SHOT — nothing re-fires it**, unlike the
  periodic state syncs where the diff is the retry queue. The master holds the
  authoritative tables, so a lost chunk arms a repair that drains from
  **housekeeping**, never inline in the HID handler. The two *image* bridges cannot
  be repaired: `resolve_upload_side()` means the master never had the bytes.
- **`DISPLAY_OVERLAYS` belongs in `OVERLAY_SYNCED_STATE_FLAGS`**, and every
  mapping-apply site pairs `set_10bit_overlay_mapping()` with
  `request_disp_refresh()` — on both halves.
- **core1 runs with interrupts masked** (`cpsid i` at the top of `core1_entry`).
  Empirically the only fix for the overlay/ROI hang; the mechanism was never
  established, and it is safe because core1 polls the FIFO rather than waiting on an
  IRQ. Do not remove it on the strength of a theory.

