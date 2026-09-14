# Code review conventions — the long form

Extracted from `CLAUDE.md` 2026-09-14. The prose is unchanged; only heading levels
and relative links were adjusted to suit a standalone file.

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

