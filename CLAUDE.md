# CLAUDE.md — qmk_firmware (PolyKybd)

This file provides guidance to Claude Code (claude.ai/code) when working in this QMK fork. The PolyKybd-specific firmware lives at `keyboards/polykybd/`.

For cross-repo context (how this repo relates to `PolyKybdHost/` and `AdafruitGFX/`), see [`../CLAUDE.md`](../CLAUDE.md).

## Code review conventions (all PolyKybd repos)

- **Docstring coverage: ignore CodeRabbit's "Docstring Coverage … threshold 80%" pre-merge check.** That 80% target is a CodeRabbit default, **not** a project policy — the check is non-blocking and we deliberately do not chase it. Do **not** add docstrings to existing functions just to satisfy it (out-of-scope churn). Document new code where a docstring genuinely helps a reader, and no more.
- **On a rapidly-iterating PR, keep CodeRabbit OFF and ask for ONE review at the
  end.** A design/layout PR that lands many small pushes (a preview render per
  tweak) makes CodeRabbit re-review from scratch on every one. Two costs, both hit
  in a single session (2026-07-29, PR #159): it burns the **per-developer review
  rate limit** — five pushes came back `Review limit reached … next review in
  31/41/46 minutes` and were never reviewed at all — and each landed review is
  against a head you have already moved past. So the reviews you *do* spend are
  the least useful ones.
  - CodeRabbit notices this itself and **auto-pauses** the branch ("this branch is
    under active development"), governed by
    `reviews.auto_review.auto_pause_after_reviewed_commits`. ⚠️ Its paused comment
    still renders a walkthrough + pre-merge checks, so it **reads like a completed
    clean review** — check for the "Reviews paused" note before concluding the PR
    was reviewed.
  - Workflow: let it pause (or pause it deliberately), iterate freely, then
    comment **`@coderabbitai review`** on the final commit for a single full-diff
    review; **`@coderabbitai resume`** turns automatic reviews back on. Both
    commands are listed in the paused comment itself.
  - ⚠️ **A push while a review is in flight ABORTS it** — "Review failed: The head
    commit changed during the review from `<a>` to `<b>`". The run is lost, not
    resumed, and re-triggering costs another slot against the rate limit. So once
    a review starts, **hold pushes until it reports** (2026-08, cost a full cycle).
  - ⚠️ **Order matters: `resume` BEFORE `review` makes the review a no-op.**
    CodeRabbit is incremental and "does not re-review already reviewed commits";
    that guard is only relaxed *while reviews are paused*. Resuming first
    un-pauses, so the following `review` finds nothing to do and silently reviews
    nothing. Either `review` first and `resume` after, or use **`@coderabbitai
    full review`**, which re-reviews the whole diff regardless of state — that is
    also the command to reach for after an aborted run, since the failed run
    recorded nothing but the head has already moved.
  - ⚠️ **CodeRabbit SKIPS any PR over 100 changed files, so an upstream-merge PR
    gets NO review at all** — *"Review skipped — Too many files! This PR contains
    N files, which is M over the limit of 100."* This is a second, different tell
    from the rate-limit one above, with the same consequence and the difference
    that it is **guaranteed** on a catch-up merge rather than occasional. The
    0.33.13 merge (#197) was skipped on all four pushes (401 → 425 files), each
    time rendering as an ordinary status comment with a file table, so the PR read
    as reviewed. There is no way to get it reviewed short of splitting the PR — so
    for a merge PR, treat the **build + HIL checks and hardware testing as the only
    real verification**, and don't count the green board as review cover.
  - ⚠️ **A STACKED PR gets no automatic review at all** — *"Review skipped — Auto
    reviews are disabled on base/target branches other than the default branch."*
    This is a **fourth** no-review mode (alongside the rate limit, the <10-stars
    repo, and the >100-file skip) and it is **guaranteed, not occasional**: any PR
    whose base is another feature branch is silently unreviewed for as long as it is
    stacked. Seen on #211 (2026-08-17), stacked on #210. Two ways out, and prefer the
    first: **let the parent merge** — GitHub then retargets the child to `PolyKybd`
    and auto-review applies again (confirm a review actually lands; a base change may
    not itself trigger one). Otherwise spend a slot on `@coderabbitai review`, which
    works on a stacked PR but costs the same org-wide budget as any other request.
    ⚠️ Do **not** read the resulting quiet board as "no findings" — nothing read it.
  - ⚠️ **Sourcery's rate-limit is QUIETER than CodeRabbit's: the `Sourcery review`
    check run goes GREEN (`success`) while no review happened.** When its weekly
    diff-character budget is spent it submits a `COMMENTED` review whose entire body
    is *"you have reached your weekly rate limit of 500000 diff characters"* — and
    that still counts as a completed check. So the PR shows a green Sourcery tick
    with **zero findings**, which reads exactly like a clean review. CodeRabbit at
    least renders a `> [!WARNING] Review limit reached` banner. Both were
    simultaneously unavailable on #203 (2026-08-12), leaving a fully green board
    that **no reviewer had read**. To tell them apart, read the review *body* via
    `pull_request_read` `get_reviews` — do not infer from the check conclusion.
    (The sibling rule "a bot comment is not a review" is in `PolyKybdHost/CLAUDE.md`;
    this is the same failure with a green check instead of a long comment.)
    - ⚠️ **A THIRD shape, and the quietest yet: `Sourcery review` = `success`
      with NO REVIEW OBJECT AT ALL.** On #218 (2026-08-19) the check was green on
      the head commit while `get_reviews` returned exactly one review, submitted
      against the *first* commit of the branch — so the firmware fix and the two
      new CI workflows had been read by nothing. There is no rate-limit body to
      find here, because there is no review. **Check the review's `commit_id`
      against the head sha**, not just that a review exists: a stale review plus a
      fresh green check is indistinguishable from a current one at a glance.
    - ⚠️ **CodeRabbit's COMMIT STATUS does the same thing, so "at least it renders a
      banner" only holds for the comment.** Its status context reads `state: success`
      with the description **"Review rate limited"** (`pull_request_read`
      `get_status`, head `2d61653d`, 2026-08-18) — so a head that nothing read shows
      a green CodeRabbit tick alongside the green build. The banner lives in the
      *comment*, which a status-only view never shows, and which vanishes on a
      re-render anyway (see the sticky-walkthrough note above). **`get_status` can
      only ever tell you a review RAN, never that it read the current head** — pair
      it with `get_reviews` and compare each review's `commit_id` against the head.

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

- ⚠️ **An on-demand Claude reviewer (`@claude review`) was tried and REMOVED
  (2026-08-20) — don't rebuild it.** `.github/workflows/claude-review.yml` +
  `claude-mention.yml` existed in all three PolyKybd repos to cover exactly the
  cases above — CodeRabbit rate-limited, Sourcery's green-check-but-empty weekly
  limit, and the >100-file upstream-merge skip. It published one usable review in
  its life and otherwise posted nothing while still billing the subscription
  (~$4 total for that one review); the deciding detail — which tool the runner
  denied it — is unreadable, because the action logs *"full output hidden for
  security"* and uploads no artifact. Workflows and the `CLAUDE_CODE_OAUTH_TOKEN`
  secret are gone from all three repos.
  - **So on an upstream-merge PR there is genuinely no LLM reviewer.** CodeRabbit
    skips it outright at >100 files, and **Sourcery has its own ceiling that lands
    on the same PRs** — it refuses any diff over **20,000 lines** outright (*"the
    GitHub API does not allow us to fetch diffs exceeding 20000 lines"*, a hard
    limit, not a quota, so waiting does nothing). Treat the
    **build + HIL checks and hardware testing as the only verification**, say so
    on the PR, and don't read the green board as review cover.
    - ⚠️ **A fourth bot, Greptile, now exists — see the Greptile entry in
      `PolyKybdHost/CLAUDE.md` before repeating "no LLM reviewer" as a fact.**
      Whether it reviews a catch-up merge is **untested**; what is measured is
      that it reviews only some PRs and announces a skip nowhere, so its silence
      is not evidence either way. ⚠️ **And its `Greptile Review` check run is not
      the answer** — measured, a green `success` one accompanied a PR it did not
      review, the same trap recorded above for Sourcery. Check `pull_request_read`
      `get_reviews` on the PR in front of you and require **both** that a review's
      `commit_id` equals the head sha **and** that its body is not a refusal
      notice — a Sourcery refusal is itself a review object carrying the head sha,
      so the sha alone reads as reviewed. Never infer from a check run or from
      this paragraph.
  - **cppcheck has no quota, no star threshold and no file-count limit** — and
    is not an LLM, so it doesn't share the others' blind spots. That is why it
    was added, and it matters more now that it is the only automated reviewer
    left. ⚠️ **But it is NOT unconditional, and the exception lands exactly on
    the case above**: `cppcheck.yml` filters `pull_request` on
    `keyboards/polykybd/**`, `modules/polykybd/**` and the workflow itself, so a
    catch-up merge that touches only upstream paths gets **no cppcheck run at
    all** — the check is absent, not green. Don't "fix" that by broadening the
    trigger: analysing the whole upstream tree is the CodeQL trap this scope was
    chosen to avoid. It means an upstream merge really is verified by the build,
    the HIL rig and hardware alone.

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

- **Toolchain**: `sudo apt-get install -y gcc-arm-none-eabi binutils-arm-none-eabi` → `arm-none-eabi-gcc` (13.2.x). This is what `qmk setup` installs on Debian/Ubuntu; the PyPI `qmk` package is only the bootstrapper (`config/clone/console/env/setup`) and does **not** bundle the compiler. There is no `bin/qmk` in this fork — the full CLI lives in `lib/python`.
- **qmk CLI**: `pip install qmk` (use a venv if system pip errors building `halo` — a Debian setuptools quirk), then `qmk config user.qmk_home=<repo>` (or `export QMK_HOME=<repo>`) so it discovers `compile`/`flash` from the repo's `lib/python`, plus `pip install -r requirements.txt`.
- **Submodules** (empty in a fresh clone): `make git-submodule`. The minimum for split72 is `lib/chibios lib/chibios-contrib lib/pico-sdk lib/printf lib/lufa` (printf and lufa are needed even on RP2040 — `quantum/logging` and the ChibiOS USB stack pull them in).
  - ⚠️ **In a web/remote container `make git-submodule` (and `qmk git-submodule`) 403s** — the injected git proxy only serves the session's *authorized* repos, and `qmk/*` aren't in it, so the submodule clone is rejected. **This is NOT a real "build unavailable" — do not give up here.** The fix is **`add_repo`**, once per submodule repo: call it for `qmk/ChibiOS`, `qmk/ChibiOS-Contrib`, `qmk/lufa`, `qmk/printf`, `qmk/pico-sdk` and it answers `read_available` ("the git proxy serves anonymous git reads of public GitHub repos") **without attaching anything**. From then on the ordinary command just works — no tarballs, no manual unpacking:
    ```bash
    git submodule update --init --depth 1 --no-recommend-shallow lib/chibios   # …and the other four
    ```
    ⚠️ **The old `codeload.github.com` tarball recipe is DEAD — it now returns 403**, with a JSON body telling you to use `add_repo` (2026-08-11; it was documented here as "allowed (200), verified 2026-06-25", so believe the error, not this file's history). It also fails *quietly* in a pipeline: `curl -sSL … | tar xz` prints only `gzip: stdin: not in gzip format` while the shell reports success, so a loop over five submodules can look like it worked. `curl -w "HTTP=%{http_code}"` is the check.
  - ⚠️ **An upstream merge BUMPS the submodule pins, and nothing checks them out for you.** The 0.33.13 merge moved `lib/chibios` `8bd61b80→6170ddf9` and `lib/chibios-contrib` `8d863d9e→5a9ad82b`. Re-run the init above **after** the merge (`git submodule status` shows the `-`/`+` prefixes), or you link a new QMK against an old ChibiOS — which compiles cleanly and fails at runtime.
  - ⚠️ **A `lib/*` dir can be FULL OF FILES and still be uninitialised — leftover
    extracted tarballs from the dead codeload recipe, pinned to the wrong revision.**
    This is a third state beyond "empty clone" and "pin bumped", and it looks healthy:
    `ls lib/chibios` shows a complete tree, so the natural conclusion is that
    submodules are fine. The tells: **`git submodule status` prefixes it `-`** (not
    initialised) and **`lib/<m>/.git` does not exist**. The build then dies on a
    *version* mismatch rather than a missing file — the signature is
    ```
    ./lib/chibios/os/hal/include/hal.h:136:2: error: #error "obsolete or unknown configuration file"
    ```
    Fix: `rm -rf` the stale dirs and re-init properly (after `add_repo`, above):
    ```bash
    rm -rf lib/chibios lib/chibios-contrib lib/pico-sdk lib/printf lib/lufa
    for m in lib/chibios lib/chibios-contrib lib/printf lib/lufa lib/pico-sdk; do
        git submodule update --init --depth 1 --no-recommend-shallow $m
    done
    ```
    ⚠️ `make`'s own auto-`git-submodule` step does **not** rescue this: it tries to
    clone into the non-empty dir, prints `destination path … already exists and is not
    an empty directory`, and carries on to a doomed build (2026-08-12).
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

- ✅ **The `.rodata` assembler warning on EVERY build is EXPECTED, and it is OURS —
  even though it names a stock upstream file.** Every build of either variant prints
  `Warning: setting incorrect section attributes for .rodata`, attributed to
  **`quantum/keymap_introspection.c`** — which is stock upstream and contains no
  section attribute at all. It compiles the keymap (`#include KEYMAP_C`), so the
  warning is reported against it while originating in
  `split{42,72}/keymaps/default/keymap.c`:
  `__attribute__((section(".rodata"))) led_config_t g_led_config`. That override
  keeps the table (296 B on split72) in flash instead of RAM; `g_led_config` cannot
  be `const` (it must match upstream's `extern led_config_t g_led_config`), so GCC
  emits `.section .rodata,"aw"` — writable — while `.rodata` already exists as `"a"`.
  The first attributes win, so it really is read-only in flash. Verify rather than
  trust: `arm-none-eabi-objdump -t <elf> | grep g_led_config` must show `.rodata` at a
  `0x10xxxxxx` (flash) address, not `0x20xxxxxx` (RAM). **Nothing to fix** — dropping
  the attribute costs the RAM it saves, and adding `const` clashes with the extern.
  - ⚠️ **This is the exact INVERSE of the security-scanner rule below** ("a finding on
    an upstream path is probably UPSTREAM's"): here the *file named* is upstream's and
    the *cause* is ours. `diff`ing `keymap_introspection.c` against upstream proves it
    identical and proves nothing about the warning. When a diagnostic names an
    inherited file, check what that file **includes** before concluding it is not ours.

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

- **`Build firmware`** and **`HIL test (split72)`** (the polykybd-ctnd rig) are the
  **real** checks; these are what must go green. Use the `diagnose-hil-failure` skill
  for the HIL side.
  - ⚠️ **The HIL suite has TWO tiers, and the default one deliberately skips the
    deepest checks.** The rig's slow checks — the startup animation, idle engage +
    the Eden screensaver, a 450-frame split-link soak, and a reboot power cycle that
    is the ONLY thing verifying user state survives a power loss — are
    `TIER_EXTENDED` (polykybd-ctnd `station/hil_tests.py`) and add ~50 s, so they run
    only when the run asks. **Ask for them on anything that touches EEPROM/persisted
    state, the split link, the idle/animation paths, or a release.** Three ways, the
    same convention as the `hil-perf` label.
    - ⚠️ **The opt-in labels are the `hil-*` set since 2026-08-29** — `hil-extended`,
      `hil-perf` (the perf job, renamed from `perf`) and `hil-doom` (the FW-9 signed-pack
      job). The bare `perf` / `doom` names, and the `[perf]` / `[doom]` commit markers,
      now fire **NOTHING** — a silent no-op, since the `if:` matches only the new
      strings. Anywhere below still saying "`perf` label" means `hil-perf`.
    - the **`hil-extended`** PR label — it starts its own run: `build` excludes
      `labeled` events (so the auto-labeler cannot re-run the pipeline) with a
      deliberate **exception for this one label**, matched on
      `github.event.label.name`. ⚠️ **Do not try to pick the label up by re-running
      an existing run** — a re-run replays the ORIGINAL event payload, so a label
      added afterwards is invisible and the re-run silently repeats the default
      tier (caught by CodeRabbit on #223; it is also why `hil-perf` works on a label
      and this did not until the exception was added);
    - **`[hil-extended]`** in a commit message — PUSH events only (`head_commit` does
      not exist on a `pull_request` event), i.e. after a merge / at release time;
    - a manual **`workflow_dispatch`** (default-branch copy only).
    The job log says which tier ran (`suite tier: …`), and so does the runner
    (`[runner] suite tier: …`) — read it before concluding a green HIL board covered
    the reboot/link checks, because by default it did not. Locally on the rig:
    `python -m station.test_runner --extended` (or `HIL_EXTENDED=1`), or the touch
    UI's **Extended** toggle beside Run Tests.
  - ✅ **`workflow_dispatch` runs all THREE opt-ins at once — the extended HIL tier,
    the perf measurement AND the FW-9 doom set** — because it satisfies every opt-in
    `if:` at once (each names `github.event_name == 'workflow_dispatch'`).
    ⚠️ **Since the `tier` input (2026-09-01) that is what `tier: all` does, and `all`
    is the DEFAULT**, so an ordinary dispatch still behaves exactly as described here.
    The narrower values (`default`, `extended`, `perf`, `doom`, `fwapply`, `debug`)
    each drive one opt-in, and `tier` is read **only** under
    `github.event_name == 'workflow_dispatch'` — push and pull_request keep their
    label/marker gating untouched. `debug` is the odd one out: it **skips the graded
    suite entirely** (the only `if:` on `hil-test`) and runs just the probe, because a
    debug loop pays the rig cost on every iteration. The whole table was verified by
    simulating the conditions across every trigger, not by reading them — that is the
    only way to check a folded-scalar `if:` short of merging and waiting. They are
    otherwise INDEPENDENT — each opt-in drives only its own job, so `hil-extended`
    alone starts no perf run and `hil-perf` alone leaves the HIL suite on its default
    tier. ⚠️ Dispatch is not the *only* way to get them: the matching labels on a PR, or
    **the markers in one pushed commit message** (`… [hil-extended] [hil-perf] [hil-doom]`),
    do it too. **A dispatch is also the way to exercise the doom set when there is no
    open PR to label** (used 2026-08-29, run #894, right after the trilogy merged). The commit-message form is the natural release-time route — a release push
    is a push — but it only fires where the workflow listens for pushes at all,
    i.e. **`PolyKybd`, and only `PolyKybd`**; the same marker in a commit pushed to
    a `claude/**` branch starts nothing. ⚠️ The trigger used to read `[PolyKybd,
    "PolyKybd/**"]` and that second pattern was **unreachable**: git refuses a ref
    that is a PREFIX of another ref, so `refs/heads/PolyKybd/<x>` cannot exist while
    `refs/heads/PolyKybd` does (`git ls-remote --heads origin 'refs/heads/PolyKybd/*'`
    → 0, ever). It read as a staging-branch escape hatch and was not one — git
    refuses to create the branch, and a differently-named one silently runs nothing.
    Dropped from `qmk-test.yml` and `polykybd-unit-test.yml` (2026-09-01).
    What dispatch buys is needing no label bookkeeping, which also sidesteps
    the two-labels-in-one-call trap below (that fires two runs).
    Whichever route, the combination is safe: `perf-test` has `needs: [build-perf,
    hil-test]`, so the rig runs the suite first and measures afterwards rather than
    interleaving two flashes. Measured on run #805 (2026-08-20): ~3 min of rig time
    for the extended HIL suite plus ~1 min for the perf pass, inside a ~8.5 min
    wall-clock run (the two cloud builds are most of it).
  - ⚠️ **The opt-in condition is a YAML FOLDED SCALAR (`>-`), and indenting its
    continuation lines for readability breaks it SILENTLY.** `HIL_EXTENDED` is one
    `${{ … }}` expression spread over four lines. A folded scalar joins lines with a
    space **only while they share one indent level**; a line indented *deeper* than the
    first is treated as more-indented content and keeps its **literal newline**. So the
    prettier-looking form — first line at the block indent, the `contains(...)` clauses
    indented under it — embeds newlines inside the expression and the value stops being
    a valid GitHub expression. Nothing warns: the workflow still parses as YAML, the
    step still runs, and `HIL_EXTENDED` just comes out wrong, so the run quietly executes
    the **default** tier while the label says otherwise. Keep every continuation line at
    the *same* indent as the first (that is why the block looks under-indented), and
    verify rather than eyeball it:
    ```python
    import yaml
    d = yaml.safe_load(open(".github/workflows/qmk-test.yml").read())
    v = d["jobs"]["hil-test"]["env"]["HIL_EXTENDED"]
    assert "\n" not in v, repr(v)
    print(d[True]["pull_request"])   # the `on:` block — see the d[True] note below
    ```
    ⚠️ **Reading the `on:` block needs `d[True]`, not `d["on"]`** — PyYAML resolves the
    bare key `on:` to the **boolean** `True` (YAML 1.1 truthiness), so `d["on"]` raises
    `KeyError` on every GitHub workflow. `jobs` is an ordinary string key; mixing the two
    up raises `KeyError: 'jobs'`, which is how the first version of this very snippet was
    wrong. **Two other blocks use the same `>-` shape and deserve the same check**:
    the `build` job's `if:` (the `hil-extended` label exception) and `build-perf`'s
    (the `hil-perf` opt-in) — a folded `if:` that gains a newline evaluates to a string
    rather than a boolean, so the job silently stops matching its trigger. The
    `build-doom` `if:` (the `hil-doom` opt-in) is the same shape and deserves the same
    check.
  - **The FW-9 doom set is a THIRD opt-in tier (`hil-doom`), and it signs its test
    artifact with an EPHEMERAL key — never the production one.** `build-doom` +
    `doom-test` (mirroring `build-perf`/`perf-test`) build a doom-flavour HIL image
    pair + a matching signed `.plyx`, then the rig drives the load-time Ed25519 gate in
    `doom_pack_load.c` (accept / tampered / unsigned — `TIER_DOOM` in the ctnd suite).
    ⚠️ **The production `FW_SIGNING_KEY` is confined to `release.yml` and must NOT be
    used by a PR-triggered workflow.** So `build-doom` **generates a throwaway Ed25519
    keypair** (`gen_signing_key.py`, which rewrites `base/fw_pubkey.h`), builds the HIL
    images with that disposable pubkey baked in, and signs the pack with the ephemeral
    seed (`sign_doompack.py --privkey`, asserting the size grew by exactly +64). The
    signature code runs against a real — but disposable — key, so the real key never
    touches a PR run; the images are throwaway (a non-shipping pubkey) and the next
    normal run reflashes the shipping image. This is the pattern for any future
    signature-gated HIL test. The accept path is reachable because the `.plyx`'s
    `ram_base` = the pack-flavour ELF's `__overlay_pool_base__`, pinned at `0x20000000`
    by the DOOMPACK ldscript, so a same-commit pack passes the RAM-pairing gate and
    *reaches* the Ed25519 check.
    - ⚠️ **The `.plyx` from `build_pack.sh` is ROOT-OWNED (docker), so signing it
      in place FAILS on the host runner** — `PermissionError [Errno 13]` at
      `sign_doompack.py`'s `write_bytes`. The pack is emitted by the `qmkfm/qmk_cli`
      **container** (runs as root) into the mounted workspace; the next step is a host
      `run:` as the `runner` user and `sign_doompack.py` rewrites the file in place, so
      it needs write access it doesn't have. `release.yml` never hit this because it
      signs the host-produced `.bin` and only *reads* (`cp`) the `.plyx`. Fix:
      `sudo chown "$(id -u):$(id -g)" "$PLYX"` before signing (GitHub-hosted runners
      have passwordless sudo). Found on the first real `hil-doom` run (dispatch #894,
      2026-08-29); build-doom failed at signing so `doom-test` was `skipped` (it
      `needs: build-doom`).
  - ⚠️ **The zizmor `artipacked` / `persist-credentials: false` warning is MOOT on
    these build jobs, and setting it can BREAK the build.** `build`/`build-perf`/
    `build-doom` upload only **named-glob** artifacts (`*.uf2`, `*.plyx`), never the
    repo root or `.git`, so a persisted checkout token cannot ride along in an
    artifact — the exfiltration path the warning describes does not exist here.
    Meanwhile `persist-credentials: false` interacts with `submodules: recursive`
    (recursive submodule auth can depend on the persisted credential), which is exactly
    what these builds need for the ChibiOS/pico-sdk tree. So the build jobs deliberately
    omit it; `build-doom` carries `permissions: contents: read` instead (least
    privilege, matching the rig test jobs). CodeRabbit accepted this and withdrew the
    finding (#244, 2026-08-29). ⚠️ Pinning `docker://qmkfm/qmk_cli` to an immutable
    digest is a *real* hardening but a **repo-wide** one — 10 uses in `qmk-test.yml` +
    more in `release.yml` — so it is a deliberate all-uses-at-once change (ideally with
    Dependabot), never a piecemeal one-job edit.
- **The FW-APPLY set (`build-fwapply` + `fwapply-test`) is the fourth tier, and it
  is the ONLY one that runs unasked — on every push to `PolyKybd`.** It builds a
  HIL image pair, signs the master `.bin` with an **ephemeral** key
  (`gen_signing_key.py` rewrites `base/fw_pubkey.h`, same pattern as `build-doom`
  — the production `FW_SIGNING_KEY` stays confined to `release.yml`), then has the
  rig drive a real HID update through **APPLY** and confirm the board comes back.
  It is also reachable by the `hil-fwapply` label, `[hil-fwapply]` in a pushed
  commit, or `tier: fwapply`.
  - ⚠️ **Why it is not opt-in, when everything else is: the HID-apply brick
    (qmk#258) shipped in a RELEASE, and it was a LAYOUT effect.** `fw_staging`'s
    page buffer was `static uint8_t` (alignment 1) and word-copied; a macro PR grew
    `.bss`, shifted it off a word boundary, and the unaligned `STMIA` HardFaulted
    on the M0+ inside a function that never returns — recoverable only over
    BOOTSEL. So **the guilty PR never touched the applier**, `fw_staging_do_apply`
    was byte-identical across the regression, and the bisect blamed the wrong
    commit. Any PR can move `.bss`, so per-PR is not where this class is catchable;
    what IS catchable is dating it to a **merge** that can still be bisected. A
    release-time-only check would name the release that bricks, with no bisect and
    a release to redo.
  - **The cost is bounded**: the rig already runs a HIL cycle per merge, so this
    adds a cloud build plus one apply+reboot, not a new cadence. PRs are untouched
    — the tier stays opt-in there, so no PR pipeline gets slower.
  - ⚠️ **`--apply-bin` is destructive by design and safe only because the image is
    the one already running** — the rig checks that pairing rather than assuming
    it. And it is safe *at all* only because a brick on the rig is self-recovering
    over GPIO BOOTSEL.
  - ⚠️ **An ephemeral-key image REFUSES a production-signed image over HID** (its
    baked pubkey does not match, so the keyboard raises the A/ACCEPT prompt, which
    the rig cancels). Benign on the rig, which recovers itself — but it is why an
    ephemeral-key build must never be handed to a user to flash.
  - ⚠️ **"On every push" is only as good as GitHub DELIVERING the push event, and it
    does not always — measured, on the very merge that added this tier.** Merging
    #264 (`43cc2559`, 2026-09-01) created **no push-event workflow run at all**:
    not `qmk-test`, not `cppcheck`, not `polykybd-unit-test`. That last one is the
    decisive part — it has **no `paths:` filter whatsoever**, and it had fired on
    #263's `CLAUDE.md`-only merge hours earlier, yet stayed silent on a merge
    carrying nine C files. So this was not a paths filter, and not ours. Ruled out
    too: no GitHub Actions incident that day (the one Git Operations incident ran
    15:00–16:01 UTC, hours earlier); the merge was a **direct click** on *Merge
    pull request* by the repo owner — confirmed with them, not inferred — so
    neither the GITHUB_TOKEN no-recursive-runs rule nor the reported
    auto-merge-skips-CI-on-the-target-branch behaviour applies; and every previous
    merge is followed by the same `[skip ci]` auto-bump at +13–16 s, so the bump
    cannot be it either.
    ⚠️ **`merged_by` alone CANNOT settle that** — an auto-merge is still attributed
    to whoever enabled it, so the field reads identically either way. The only
    signals are the timing (auto-merge fires within seconds of the last check
    turning green; this one landed 10 minutes later) and asking the person.
    The run object simply never existed. GitHub's own troubleshooting says a
    workflow that does not trigger "silently does nothing", and dropped runs under
    load are a documented class — so treat this as a **delivery** failure, not a
    config bug, and do not go looking for a mechanism in the workflow.
    - **The design already absorbs it, which is the reassuring half.** The release
      gate (`tools/require_fwapply_run.py`) refuses to publish firmware no apply
      run has covered, so a dropped push event costs a **manual dispatch at release
      time**, not a bricked release. That is the gate doing exactly its job.
    - **Recovery: dispatch *Build and HIL Test* with `tier: fwapply`.** Verified
      2026-09-01, run #959: build ✅, HIL suite ✅ 2m55, apply round-trip ✅ 3m40
      (its first-ever execution).
      ⚠️ **WHICH ref you dispatch on decides whether the gate can ever see it, and
      "the branch" is only right IMMEDIATELY after the merge.** A dispatch attaches
      the run to whatever its `ref` points at. Dispatching on `PolyKybd` therefore
      covers the release sha **only while the tip is still that version's own bump
      commit** — true if you do it right after the merge, as above, and false as
      soon as another PR lands. It is not a general recipe, because the two ends
      move in opposite directions: `publish_release.py` tags the **oldest** commit
      declaring the version (`commit_for_version()`, deliberately not the head),
      while `require_fwapply_run.py` only ever walks **ancestors** of the release
      sha, bounded by `MAX_BUMP_COMMITS`. A run on a *descendant* is invisible to
      it, so a late branch-tip dispatch produces a green run the gate still refuses.
      **Once the branch has moved on, dispatch on the release TAG instead** — the
      API takes a branch *or* a tag, and the tag exists by then (publishing creates
      it, and the gate runs on `release: published`), so the run attaches to the
      release sha itself. ⚠️ Untested here, and it carries its own trap: a dispatch
      runs the workflow **as of that ref**, so a tag predating the `tier` input has
      no `fwapply` value to select. (Caught by Greptile on #265 — the original
      wording generalised one true observation into a rule that only held on the
      day it was written.)
    - ⚠️ **So don't read "runs unasked on every merge" as a guarantee.** After a
      merge you care about, confirm a run actually exists for the merge sha —
      `actions_list list_workflow_runs` filtered `event: push` — rather than
      assuming. A missing run looks identical to a repo where nothing was
      configured.

- ✅ **The DEBUG LOOP: a firmware bug can now be chased on the rig with nobody
  flashing a `.bin`.** Dispatch `qmk-test.yml` on a branch with **`tier: debug`,
  `probe: <name>`** and the rig builds that branch, flashes both halves, runs a
  probe committed at `keyboards/polykybd/tools/hil_probes/<name>.py`, and pipes
  the firmware's own console into the job log as `[qmk] …` lines. Edit, push,
  dispatch, read the log. **The `debug-firmware-on-rig` skill drives the whole
  loop** (when a probe beats the graded suite, the dispatch call, reading the
  `[qmk]` lines back out, and the measured cost of a turn); the rig side is
  `polykybd-ctnd/station/probe.py` (`--probe`) and the probe format + pitfalls
  are in `keyboards/polykybd/tools/hil_probes/README.md`. Verified end to end
  2026-09-01, run `33559310727`: `Debug probe (split72)` green with every other
  job — `HIL test (split72)` included — correctly skipped, 5m21s wall clock of
  which 4m03s is the cloud build. This existed in pieces before — the rig has echoed `[qmk]` lines
  into the log all along — and what was missing was any way to run an *arbitrary*
  question instead of the fixed suite.
  - ⚠️ **DISPATCH-ONLY, and that `if:` IS the security control.** Triggering a
    dispatch needs write access, so a fork PR can never reach the rig through it —
    which matters because the rig is a self-hosted runner for a **public** repo
    (HIL-2). **Do not add a `hil-debug` label trigger**: a label is applied to a PR
    whose head may be a fork. The containment check in `station/probe.py` is *not*
    this control and its own docstring says so; it is operational (CI has already
    checked out the whole repo and the build jobs have already run code from it).
  - **Dispatch with `ref:` set to a branch runs that branch's workflow and builds
    that branch's firmware**, so a probe iterates entirely unmerged. That is also
    the answer to "workflow_dispatch only exists on the default branch": the
    *entry* must be there, the *code* need not.
  - ⚠️ **The console cannot see a flash window.** QMK drops output nobody drains
    and during a flash nothing does, so a probe observes before and after an
    update, never during. A gap in the `[qmk]` timestamps spanning a flash is
    expected, not a symptom.
  - ✅ **A brick is self-recovering on the rig** — it asserts BOOTSEL over GPIO,
    and BOOTSEL/UF2 bypasses `fw_staging` entirely. So the rig is the right, and
    the only, place to exercise the firmware-apply path: the failure this whole
    area guards against cannot strand the hardware.
  - **A probe is disposable.** Delete it when the bug closes, or promote it into
    `station/hil_tests.py` if the question is worth asking forever.

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
- **A change that cannot alter the firmware does NOT run the build or the rig —
  `qmk-test.yml` path-filters both its `push` and `pull_request` triggers.** Markdown
  since 2026-08-21, then `scripts/` and `.claude/`, then the sibling workflow files
  (#253, 2026-08-29). The rig executes one job at a time, so before this a
  comment-only PR occupied it for a full flash-and-test cycle per push and delayed
  every real build queued behind it (#224 burned three rig runs and three review
  slots that way; #251 later did the same as a workflow-only PR). What follows —
  the last one is still the one that would bite:
  - ⚠️ **It is a `paths` list with `!` negations, NOT `paths-ignore`, and it cannot
    be either one.** The `!` prefix works ONLY in `paths`, and the two filters may
    not both be used for one event — so "exclude the sibling workflows but still
    verify this one" is inexpressible with `paths-ignore`. The list is `**`,
    `!**.md`, `!scripts/**`, `!.claude/**`, `!.github/workflows/**`,
    `.github/workflows/qmk-test.yml`, identical on both triggers.
  - ⚠️ **ORDER IS LOAD-BEARING — the LAST matching pattern decides.** The leading
    `**` is what makes an ordinary source file match at all, and the trailing
    `.github/workflows/qmk-test.yml` is what keeps the gate verifying its own edits.
    Move that line above `!.github/workflows/**`, or drop it, and a change to this
    workflow silently stops being built and rig-tested; drop the `**` and ordinary
    firmware sources stop triggering anything. Both were confirmed by mutating the
    list and re-running a simulation of GitHub's matcher, which is the only way to
    check this without merging and waiting.
  - **A mixed docs+code — or workflow+code — PR still runs the gate in full**, since
    the workflow runs when AT LEAST ONE changed file is included. Nothing can be
    smuggled in behind a README or a CI edit.
  - **The exclusion is scoped to `.github/workflows/**`, not all of `.github/`.**
    Nothing under `.github/` is a build input today — there is no `uses: ./...`
    anywhere in `qmk-test.yml`, every action is external — but the narrower scope
    leaves a composite action added later under `.github/actions/` still triggering
    a run, which is the safe direction.
  - **`workflow_dispatch` has no paths filter**, so a manual run — including the
    both-tiers route above — works on any commit regardless.
  - ⚠️ **A path-filtered `pull_request` trigger applies to `labeled` too**, so
    adding `hil-extended` or `hil-perf` to a docs-only PR now starts nothing at all.
    That is the intent (there is no firmware there to measure), but it is a silent
    no-op rather than an error.
  - ⚠️ **This only works because neither check is a REQUIRED status check.** A
    workflow that never runs never reports, so if `Build firmware` / `HIL test
    (split72)` are ever added to branch protection, a docs-only PR would deadlock
    the merge button. The fix then is a paths-filter job feeding `if:` conditions —
    a *skipped job* satisfies a required check, a never-started workflow does not.
    (Not verifiable from a Claude Code session: no MCP tool reads branch-protection
    settings. The indirect evidence is that PRs report `mergeable_state: clean`
    while their checks are still in flight, which would read `blocked` if any check
    were required.)
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
- ⚠️ **A red `lint` on ONE keyboard with `The file "…" should not exist!` is a
  TRACKED-but-GITIGNORED file — real, and it fails every PR that touches that
  keyboard until someone removes it.** `lib/python/qmk/cli/lint.py` calls
  `git_get_ignored_files()` = **`git ls-files -c -o -i --exclude-from=.gitignore
  keyboards/<kb>/`**, so any committed file the root `.gitignore` matches fails the
  keyboard. The classic trap is **images**: `.gitignore` has `*.png` with only
  `!docs/public/**.png` exempt, so a screenshot/render force-added under
  `keyboards/` is committed *and* ignored. Two split42 evidence PNGs did exactly
  this from 2026-07-17 (`11f37c17`) and turned `lint` red on **nine** consecutive
  commits of an unrelated PR. Fixes: delete the file (blobs stay recoverable via
  `git show <sha>:<path>`), move it to `docs/public/`, or add a negation
  (`!keyboards/polykybd/**/*.png`) — all three verified to make `qmk lint --strict`
  pass. **This contradicts the "lint passes green on every normal commit" line
  above** — that holds only while no such file exists.
- ⚠️ **An upstream-merge PR lints UPSTREAM's keyboards too, so it can go red on
  files this fork does not maintain — and a stable tag inherits a new lint rule
  WITHOUT its post-tag fixes.** `lint.yml` lints every keyboard with a changed file
  outside `keymaps/`, and a catch-up merge puts most of upstream's tree in that set
  (~60 keyboards for the 161-commit 0.33.13 merge). 0.33.13 added a **license-header
  check** to `qmk lint --strict` (`_has_license()` — crudely, the first line must
  start with `/*` or `//`; there is no ignore mechanism), which failed **6**
  keyboards: 1 ours (`split72/keymaps/revision2`, genuinely missing) and 5
  upstream's. Three of those five were fixed upstream in `14774c8482` (#26382) —
  **7 commits AFTER tag `0.33.13`** — so merging the tag brought the rule but not
  the fix; the other two are still unfixed on upstream master today, and are green
  upstream only because upstream lints just the keyboards *its* PR touches.
  - The condition is **self-clearing**: once the merge lands, those keyboards are
    in the base branch, so later PRs no longer see them as changed.
  - Resolution used for 0.33.13 (2026-08-11): cherry-pick upstream's own fix where
    one exists (byte-identical afterwards ⇒ no conflict at the next merge), and for
    the rest add `// Copyright <year> <author>` + SPDX taking the **real** author and
    year from `git log --diff-filter=A` on each file. Don't invent attribution.
  - ⚠️ **Enumerate ALL the failures before fixing any** — `qmk info -l` prints a
    keyboard-layout ASCII diagram per keyboard, so the CI log tail is mostly art and
    any excerpt of it is a partial list. Fixing the 5 files a truncated view showed
    left **13** more in `handwired/onekey` and cost an extra CI round. Run the job's
    loop locally and collect every `☒` line first.
- ⚠️ **Applying N labels in ONE API call fires N `labeled` events, i.e. N workflow
  runs.** `qmk-test.yml` listens for `labeled` (it must, or the `hil-perf` label would
  trigger nothing), so adding `hil-perf` + `bump:minor` together started **two identical
  perf runs** — a wasted rig build + flash each (2026-08-05). The rig executes one job
  at a time so they queue rather than collide, but cancel the duplicate. Apply labels
  one call at a time when one of them is a trigger, or expect to clean up. This is a
  *different* mechanism from the push/pull_request duplication below.
- ⚠️ **A workflow yields TWO check runs — `push` and `pull_request` — whenever the
  branch matches BOTH triggers, and re-running one does NOT touch the other.**
  Here that mostly doesn't happen: `qmk-test.yml` pushes only on `PolyKybd`,
  `unit_test.yml` only on `master`/`develop`, and `lint`/`labeler`
  are PR-only — so a `claude/**` PR gets a single run per workflow. **The sibling
  repos differ**: wincompose's `build.yml` pushes on `main` *and* `claude/**` on
  top of `pull_request`, so every branch PR there carries two, and that is where
  this bit (2026-08-01). Both runs build the same commit, so a code fix clears
  both — but a fix that lives **outside the commit** (a branch/tag/repo-state
  change) has to be re-run per run, and `rerun_failed_jobs` takes a **run id**, so
  it only ever fixes the one you named. On wincompose#3 re-running one turned that
  check green and the PR was reported green off it while the other — same commit,
  same failure — stayed red. **Before calling a PR green, look at every check run,
  not the one you just acted on.**
- **Reproduce the whole `lint` job locally instead of reading the CI log** — it is
  ~5 s and definitive. (The GitHub MCP `get_job_logs` *does* work — see the
  tail-size note below — but a local run is faster and gives the whole picture):
  ```bash
  export QMK_HOME=$PWD
  qmk lint --strict --keyboard polykybd/split42     # and split72
  git ls-files -c -o -i --exclude-from=.gitignore keyboards/polykybd/   # must be empty
  qmk format-text $(git diff --name-only --diff-filter=d origin/PolyKybd...HEAD)
  git diff --quiet -- $(git diff --name-only --diff-filter=d origin/PolyKybd...HEAD) \
      && echo "format clean"        # the job's second half: any diff = "Requires Formatting"
  ```
  ⚠️ **The `-o` in that command lists UNTRACKED ignored files too, and locally that
  is mostly your own build output** — `keyboards/polykybd/tools/__pycache__`, and
  after a `doom/pack/build_pack.sh` run the ~100 files under
  `keyboards/polykybd/doom/pack/build/`. CI checks out clean, so it never sees any
  of them. **What CI actually fails on is a TRACKED ignored file**, so when the list
  is noisy re-run it without `-o`:
  ```bash
  git ls-files -c -i --exclude-from=.gitignore keyboards/polykybd/   # THIS must be empty
  ```
  ⚠️ **That `-o` noise is convincing enough to cause a MISDIAGNOSIS — it did,
  while this very note was being written (2026-08-19).** Running `git add -A` and
  then the `-c -o -i` check printed the whole `doom/pack/build/` tree, which read as
  "`git add -A` just staged 100 ignored files"; the first draft of this bullet said
  exactly that. It is **false** — `git add -A` honours `.gitignore` and cannot stage
  an ignored file without `-f` (verify in 20 s in a throwaway repo). The files were
  listed by `-o`, as untracked, before and after the add alike. **The tell is
  `git diff --cached --name-only`** — what is *actually* staged — not an
  `ls-files` variant that mixes tracked and untracked in one list.
  Still prefer staging the paths you edited by name: `-A` picks up unrelated
  working-tree changes, and it *does* stage a modification to an ignored file that
  is already tracked, which is the state CI fails on.
  - ⚠️ **WHICH formatter runs is decided by the changed PATHS, and clang-format does
    NOT cover `keyboards/**`.** An earlier version of this file said "the lint job runs
    `format-c` as well as `format-text`" and told you to clang-format every C file you
    touched. That is wrong for keyboard work, and following it means reformatting files
    CI never looks at. The actual wiring (read the three workflows, verified
    2026-08-17):
    - **`lint.yml`** ("PR Lint keyboards") triggers on `keyboards/**` and runs
      **`qmk format-text` only**, then fails any changed file that `git diff` shows as
      modified — that is where `File '…' Requires Formatting` comes from.
    - **`format.yml`** ("PR Lint Format") is the one that runs clang-format, and its
      `paths:` are `drivers/ lib/arm_atsam/ lib/lib8tion/ lib/python/ modules/
      platforms/ quantum/ tests/ tmk_core/` — **no `keyboards/`**.
    - **`format_push.yml`** only fires on pushes to `master`/`develop`, i.e. the
      upstream-mirror branches, never on `PolyKybd` or a `claude/**` PR.
    - So the 2026-08-12 clang-format failure was real but path-specific: that change
      extracted the LTR-559 driver into **`modules/`**, which `format.yml` does cover.
      **Rule: clang-format only what you put under those paths.**
  - ⚠️ **Corollary — do NOT clang-format a new file under `keyboards/`.** The
    container's clang-format 18 disagrees with the prevailing style there on ~every
    file (`.clang-format` sets `ColumnLimit: 1000` and `AlignConsecutive*: true`, so it
    unwraps hand-wrapped signatures and collapses aligned `#define` columns). The
    version-skew test in the next bullet needs a base version to compare against, which
    a new file does not have — the check that replaces it is **whether the same
    objection reproduces on a neighbouring committed file**. It does: the aligned
    `#define SYNC_ACK_SIG    0b…` block is flagged identically in
    `origin/PolyKybd:keyboards/polykybd/split_sync.h`, which CI has been passing for
    months.
  - `qmk format-text` itself cannot be run in the container — it needs **`dos2unix`,
    which is not installed** (`FileNotFoundError: 'dos2unix'`). Its whole job is line
    endings + a trailing newline, so check those by hand:
    `grep -qP '\r' <file>` (must not match) and `[ -n "$(tail -c1 <file>)" ]` (must be
    false, i.e. the file ends in a newline).
  - ⚠️ **The container's clang-format is a DIFFERENT VERSION from CI's, so it flags
    files CI accepts — do not "fix" those.** Local is clang-format 18; it wanted to
    reformat a file the lint job had passed. **The test is whether the same file is
    also flagged on the base branch**: if it is, it is version skew, not a finding —
    reformatting it adds churn CI never asked for and (on a moved file) destroys
    git's rename detection.
    ```bash
    git show origin/PolyKybd:<path> > /tmp/base_copy.c && cp .clang-format /tmp/
    (cd /tmp && clang-format --dry-run -Werror base_copy.c)   # flagged too => skew
    ```
    Only reformat what the CI log named.
- **`get_job_logs` works — ask for 150–350 `tail_lines`.** An earlier version of
  this file claimed it "caps its response at ~2 KB *regardless of `tail_lines`*";
  that is **wrong** (`tail_lines: 330` returned ~15 KB, 2026-08). The real problem
  is *what fills the tail*: every job ends with **post-job cleanup** — on a
  submodule-heavy repo that is ~60 lines of `git config`/`submodule foreach`
  spam — and a failing tool often dumps diagnostics **after** its own error
  (GitVersion prints a 100-commit graph, so its exception sat ~150 lines above the
  end). `tail_lines: 40` and `125` both landed squarely in that noise and cost
  four wasted calls before 330 reached the actual message. Prefer
  `failed_only: true` with a **run** id to find the job, then a generous
  `tail_lines` on the **job** id.
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
- **`Performance measurement (split72)` is OPT-IN and never gates.** It builds a
  second pair of HIL images with `-e POLYKYBD_LOOP_PROFILE=yes` and has the rig
  measure main-loop timing, overlay cost (bridge/render/rest) and HID latency, then
  posts a table to the job summary + a PR comment and compares against the baseline
  committed in `polykybd-ctnd` (`perf/baselines/split72.json`). Trigger it with the
  **`hil-perf` PR label** (the way to measure a PR), **`[hil-perf]` in a commit message**
  (PUSH events only — `head_commit` doesn't exist on a `pull_request` event, so it
  does nothing on a PR), or a manual **`workflow_dispatch`** (only available once
  the workflow is on the default branch). ⚠️ The label needs `labeled` in the
  workflow's `pull_request` `types:` — it is **not** in GitHub's default set, so
  without it labelling an open PR fires no run at all; `build`/`hil-test` opt out
  of label events so the auto-labeler can't re-run the whole pipeline.
  ⚠️ It **never fails on a regression** (wall-clock numbers on
  shared hardware — a flaky red check is one people learn to ignore); only a
  *measurement* failure (wrong build flashed, device dead) fails the job. Ordered
  `needs: [build-perf, hil-test]` + `always()` so the two rig jobs can't interleave
  flashes, but a red HIL suite still yields a perf number — often exactly what
  explains a timing-related HIL failure. **Use this instead of asking the user to
  flash a build and paste the console log.** See
  `keyboards/polykybd/profiling/README.md` (§ on-demand control, HID cmd 32) and the
  `polykybd-ctnd` CLAUDE.md § Performance measurement.
- **An upstream merge is the canonical case for the `hil-perf` label.** A catch-up merge
  bumps the ChibiOS / pico-sdk pins and pulls core QMK changes (split transport, USB
  stack, scheduler) — exactly the things that can move main-loop timing with **no
  PolyKybd source changed** — and it is also the PR CodeRabbit skips outright (>100
  files), so `Build firmware` + `HIL test` are the only other verification and
  neither measures timing. The job is report-only, so it cannot redden an already
  unreviewable PR. Apply `hil-perf` as its **own** label call (N labels in one call fire
  N runs — see the labeling note above). ⚠️ **Then move the baseline — but only if
  something actually moved.** `perf/baselines/split72.json` is compared against,
  never auto-updated, so a real shift left unrecorded becomes a phantom regression
  on every later PR; equally, re-baselining on noise creates a phantom regression in
  the other direction. **"Idle — worst iteration" is a max-of-window sample and
  swings ~2× run to run** — the 0.33.13 dispatch read 1.88 ms against a 3.85 ms
  baseline ("-51%") on a window whose histogram was 4055 iterations `<1 ms` + 110 in
  `1-2 ms` and *nothing above 2 ms*, i.e. the old value was one outlier iteration,
  while the main-loop rate over the same window moved -1.3%. Trust the rate/total
  rows; treat the worst-iteration rows as anecdotes. **0.33.13 measured
  performance-neutral** (everything within ~1%), so its baseline was deliberately
  left in place.

## Releases

Firmware releases are **GitHub Releases** (tag `PolyKybd-fw-vX.Y.Z`; `FW_VERSION` in
`config.h`), created by **publishing** — *not* by pushing a tag. Use the
`polykybd-github-release` skill to draft the notes and drive the flow. The mechanics
that cost real debugging to learn (2026-07):

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
  - **Recovering** is a choice, not a fix: either correct the docs to the version that
    actually bumped, or land a `bump:minor` PR that does **not** itself edit `config.h`
    (the workflow bumps *after* merge, so an edited version file would be bumped on top
    of) — the same cosmetic-realignment move described in the host repo's note.
- ⚠️ **From Claude Code on the web you can neither push tags (git proxy returns 403 on
  `refs/tags/*`) nor create a release (no `gh` CLI, no create-release MCP tool)** — stage
  the notes on the branch and hand the user `python scripts/publish_release.py`.

## Firmware overview (`keyboards/polykybd/`)

The firmware runs on a **Raspberry Pi RP2040** (dual-core ARM M0+) and is a heavily customised QMK build. ⚠️ **The clock is 200 MHz by default** (since 0.10.x). It was **125 MHz** before that — never the 133 MHz this file and several code comments used to claim, which was the chip's old *rated maximum*. Nothing in QMK sets the clock; ChibiOS's `hal_lld_init()` (and, earlier in the boot, the double-tap `__late_init`) calls the pico-sdk `clocks_init()`, which reads the compile-time `SYS_CLK_KHZ`, so `rules.mk` sets it. **`-e POLYKYBD_SYS_CLK=125`** opts back out and produces an image **byte-identical** to the pre-200 MHz builds (verified) — the escape hatch if a board ever misbehaves. 200 MHz is the operating point Raspberry Pi certified in 2025 (1200 MHz VCO / 6 / 1), which requires the core voltage raised to **1.15 V** — the vendored pico-sdk predates the SDK's automatic raise and does not compile `hardware_vreg`, so `POLYKYBD_VREG_VSEL` drives it as a register write before the first `clocks_init()` (see `UPSTREAM_PATCHES.md` → `platforms/chibios/bootloaders/rp2040.c`). Peripherals need no rework: SPI (`SPI_DIVISOR`/`CPU_CLOCK`), I2C, the PIO split UART and WS2812 all derive their dividers from the **live** `clock_get_hz(clk_sys)`, and USB is on the separate 48 MHz PLL. The boot banner prints `clk: sys=…Hz vreg_vsel=0x…` so the pairing is verifiable on hardware. The one **fixed** divider is XIP flash — boot2 runs it at `clk_sys/PICO_FLASH_SPI_CLKDIV` (4), i.e. 50 MHz at 200 and 31.25 at 125, both far inside any QSPI part's rating; re-check that list rather than assuming it holds if another clock is ever added. This is **custom hardware with 8 MB of external QSPI flash** (NOT the stock 2 MB). The 8 MB is **partitioned** (see `base/fw_staging.h` for the authoritative map): **0–2 MB running firmware** (the linker `flash1` XIP window), **2–4 MB firmware-update staging**, **4–8 MB resource/overlay data** (`FLASH_TARGET_OFFSET`). So the budget that matters for adding languages/fonts is the **2 MB firmware partition**, of which `split72:default` currently uses ~0.76 MB (~38 %). `FW_STAGING_OFFSET` is kept equal to the linker `flash1` length so a build that exceeds 2 MB fails to *link* rather than silently growing into the staging area (this firmware/staging split was raised from 1 MB → 2 MB in 2026-06 as the image neared the old boundary). The keyboard is split (left + right halves connected via UART) with up to 72 per-keycap OLED displays (72×40 px monochrome, SPI-driven) plus a 128×64 status OLED.

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
  features. **v2** added `GET_LANG_LIST_PACKED` (cmd `27` / `0x1b`): the language
  list as a count byte + one `(ISO 639-1 idx, ISO 3166-1 alpha-2 idx)` **2-byte
  pair per language** instead of the 4 ASCII chars of cmd `0x08` — it halves the
  emitted bytes/lang and the report count. As of the **P2-only cleanup**, cmd `27`
  is the **only** language-list command: the legacy ASCII cmd `0x08` has been
  **retired and now NACKs** (`P\x08!`), dropping its ~570 B `.rodata` table. The
  host (protocol ≥ 2) uses cmd `27` exclusively with **no ASCII fallback**, and
  firmware older than v2 is unsupported; the rig asserts cmd `0x08` NACKs. The
  index↔code tables are the **frozen, append-only** `lang/iso_lang_country.py`
  (see "Language list encoding" below). **v3** made `SEND_OVERLAY_MAPPING`
  (cmd `21`) **silent** — no per-chunk ACK, matching the other bulk overlay
  commands (`0x0A`, `0x10`/`0x11`, `0x12`/`0x13`). The old ACK was informationless
  (always `.`), discarded unread by the host, and arrived only after the blocking
  UART bridge to the slave — escaped ACKs were the main source of stale replies
  the host had to drain. The host (protocol 3) no longer drains after mapping
  sends; ordering for `enable_overlays` (case 11) is preserved because HID
  reports dispatch sequentially and the bridge completes before case 21 returns.
  **v4** added `GET/SET_IDLE_STYLE` (cmd `28` / `0x1c`): selects the idle
  (anti-burn-in) display style — payload `0xFF` queries (reply byte = current
  style), else sets it (`0` = legacy pulse, `1` = jitter); out-of-range NACKs.
  Persisted in `poly_eeconf_t.idle_style` (flushed at the next suspend/store) so
  it survives reboots. The host (PolyKybdHost) toggles it over this command; the
  rig has a v4-gated round-trip HIL test. See "Idle anti-burn-in styles" below.
  **v5** added the brightness flags (`SET_BRIGHTNESS` cmd 13 payload byte: volatile /
  host-auto). **v6** appends a **per-bundle font-pack version block** to the `GET_ID`
  (cmd 6) reply — AFTER the NUL-terminated id string: `['V'][count][u16 little-endian
  content_version × count]` in bundle-slot order. The host reads it to flash only the
  font-pack bundles the keyboard is missing/behind on (no extra query); older hosts
  stop at the NUL and ignore it. See "Font pack" below. **v9** added
  `GET/SET_GLYPH_SCRIPT` (cmd `30` / `0x1e`): a glyph-script **override** that swaps
  the language-layer letter/digit legends for an alternative script (`0` = standard/off,
  `1` = Tengwar), leaving overlays and OS-hints untouched. `0xFF` queries (reply byte =
  current script), else sets it; out-of-range NACKs. Persisted in
  `poly_eeconf_t.glyph_script`, synced via `poly_sync_t.glyph_script`. The Tengwar
  glyphs ship in a new **`fantasy`** font-pack bundle (the host flashes it on connect);
  with no bundle the override falls back to Latin. See "Glyph-script override" below.
  **v10** makes the glyph script an **open-ended index** and ships 9 more scripts,
  values `2..10`: Elder Futhark runes, Aurebesh, Standard Galactic Alphabet,
  Cirth/Angerthas, IBM VGA/CP437, Commodore 64, Amiga Topaz, APL, Braille — all in the
  (regrown) `fantasy` bundle (`content_version` bumped 1→2). The wire format is unchanged
  (one script byte); the semantic change is that the firmware now **accepts any index
  `0..0xFE`** — an index it doesn't know, or whose font isn't flashed, renders the normal
  legend instead of NACKing. This **decouples "add a font face" from the protocol**: within
  v10 the script set can grow freely (the host may offer more scripts than a keyboard has;
  older keyboards degrade gracefully), so **adding scripts never bumps the protocol again** —
  only a real wire/semantic change would. `0xFF` stays the query sentinel.
  **v11** reframes the **plain (uncompressed) overlay upload** (cmd `10` / `0x0A`): `modifier`
  and `segment` now share **one** header byte — `(segment << 4) | (modifier & 0x0F)` — so the
  header is 4 bytes (`id, cmd, keycode, packed`) and a full 60-byte segment fits the 64-byte
  report **exactly**. The pre-v11 layout carried modifier and segment in *separate* bytes (5-byte
  header), leaving only 59 bytes for a 60-byte segment, so the firmware `memcpy`'d 60 bytes and
  read **1 byte past the report** — harmless on the no-MMU RP2040 but the last byte of each
  segment was undefined (the old FW-7 finding; fixed in the wire format instead of a bounce
  buffer). The firmware unpacks the byte in `hid_com.c` case 10 *before* `set_fragment_context_key`,
  so `adjust_overlay_idx_to_mod` is unchanged; **compressed (`0x10`/`0x11`) and ROI (`0x12`/`0x13`)
  paths are untouched** (their headers already fit).
  **v13** adds `GET/SET_GLYPH_SIZE` (cmd `34` / `0x22`): the size a key's MAIN legend
  is drawn at — `0` small (the original 27 px face), `1` medium, `2` large; `0xFF`
  queries. Persisted in `poly_eeconf_t.glyph_size`, synced via `poly_sync_t.glyph_size`;
  also reachable from the board via `KC_GLYPH_SIZE` on the settings layer.
  ⚠️ **Its range is CLOSED and an unknown value NACKs — the deliberate OPPOSITE of the
  glyph script's open-ended index one command over, and that asymmetry is the thing to
  understand before "fixing" either.** An unknown SCRIPT index falls through to the
  normal legend, so accepting it costs nothing and buys the host freedom to ship faces a
  keyboard lacks. A SIZE names a rendering TIER whose relocation base and baseline the
  firmware must know, so accepting an unknown one would store, sync and persist a
  setting that silently renders small. The two HIL tests assert opposite things about
  their neighbouring commands on purpose (`test_glyph_size_round_trip` /
  `test_glyph_script_expansion`). See "Keycap legend size" below.
  **v14** adds `GET_LAYER_NAMES` (cmd `35` / `0x23`): a read-only reply of
  `[total][count]` followed by `count` NUL-terminated ASCII names of at most 8 chars,
  split across as many reports as they need (54 bytes / one report today). `total` is
  the whole payload length, that byte included.
  The count is deliberately the SAME `DYNAMIC_KEYMAP_UPDATE_MAX_LAYER_COUNT` that
  `id_dynamic_keymap_get_layer_count` already answers with — the host editor sizes
  its tab strip from that command and labels the tabs from this one, so two counts
  could let it draw a tab it has no name for. See "Layer names over the wire" below.
  **v15** adds **macros**: `MACRO_INFO` (cmd `36` / `0x24`, read-only — count, label
  stride, capacity u16, bytes-used u16), `MACRO_BODY` (cmd `37` / `0x25`, windowed
  read/write of the shared body buffer: `data[2]` 0 read / 1 write, `data[3..4]` offset
  LE, `data[5]` count, `data[6..]` bytes) and `MACRO_LABEL` (cmd `38` / `0x26`,
  `data[2]` id, `data[3]` 0xFF query else length, `data[4..]` text). All three sit
  behind ONE host feature gate — a host that could read the info header but not the
  bodies would render an editor over data it cannot fetch. See "Dynamic macros" below.
  **Bump `FW_VERSION` +
  `PROTOCOL_VERSION` (config.h) and `__protocol__` (PolyKybdHost `_version.py`) in
  lockstep.** ⚠️ The old note here said "the host connect gate is exact-match"; it is
  not, and has not been for a while — the host connects to any protocol `>=
  MIN_SUPPORTED_PROTOCOL` and gates each feature separately through
  `FEATURE_MIN_PROTOCOL` (see `PolyKybdHost/CLAUDE.md`). So forgetting the bump no
  longer rejects the keyboard; it silently leaves the new feature disabled, which is
  quieter and worse.
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
- **`kdisp_write_gfx_char` baseline-aligns every glyph to `fonts[0]`**:
  `y += currentFont->yAdvance - fonts[0]->yAdvance`. So drawing a *single* icon
  whose font differs in height from `g_all_fonts[0]` (IconsFont, yAdvance 40)
  shifts it vertically by the difference. This was the **language-flag gap-at-top
  regression** when flags moved into the pack (flag yAdvance 54 − 40 = +14 px down,
  filling 0..39 → 14..53). **Fix pattern: draw such a glyph through a *single-font
  array* `{ that_font }`** so `fonts[0]` is the glyph's own font (adjustment 0), as
  the old compiled-in `{ &flag_font }` path did. `kdisp_gfx_glyph_font(fonts, n, cp,
  &out_font)` returns the glyph **and** its owning font in one scan for exactly this
  (`kdisp_gfx_glyph` is the `out_font = NULL` wrapper).
- **GFXfont bitmaps are COLUMN-NATIVE (OLED page format) since the PolyColGfx
  rollout (font-pack ABI 2).** 1 byte = 8 *vertical* pixels, so the firmware blits a
  whole column-byte into the SSD1306 page memory at once. `cb = (h + 7) >> 3`
  page-bytes per column; index `bitmap[bitmapOffset + xx*cb + (yy>>3)]`, bit
  `1 << (yy & 7)` (**LSB = top of the page**); a glyph is `width * cb` whole bytes.
  Canonical types are `PolyColGfx`/`PolyColGlyph` (`base/fonts/gfxfont.h`) with
  `GFXfont`/`GFXglyph` kept as compat aliases. ⚠️ **NOT** the classic Adafruit
  row-major layout (`bit = yy*w + xx`, MSB-first) — a row-major reader (or an
  un-transposed header, see below) produces garbage that *looks* like dithering
  noise. `fontconvert` emits column-native (`emit_buf_col`); the ABI-2 `.plyf` packs
  and every reader (host + firmware) match, so it's a coordinated host↔firmware↔rig
  ABI 1→2 change (a keyboard rejects a mismatched-ABI pack by design). Column
  padding grows the *resident* fonts ~10 KB in the image (only glyphs whose height
  isn't a multiple of 8 grow — IconsFont at h=40 grew 0 B) — the inherent, benign
  cost of one uniform format with no runtime transpose cache.
  - ⚠️ **TWO layouts coexist and a 72×40 image is EXACTLY 360 bytes in BOTH, so
    crossing them fails silently — no size mismatch, no crash, just a scrambled
    read.** Font glyphs are column-native (above, read by `kdisp_write_gfx_char`);
    **overlay images are ROW-MAJOR MSB-first** — 9 bytes/row × 40 (host:
    `np.packbits` over the 40×72 mask; firmware: `kdisp_draw_bitmap`, index
    `pgm_bmp[y*byte_width + (x>>3)] & 0x80`) — versus 5 page-bytes/col × 72
    column-native. **Any helper that reads a bitmap must be paired with the DRAW
    function that owns its layout.** `b69eddcf` moved `kdisp_clear_bitmap_courtyard`
    to column-native for its glyph caller and left the row-major overlay call site
    behind: the courtyard then dilated a garbage mask and wiped **82 %** of the
    keycap (measured on a shipped template cell) instead of the intended 39 %,
    erasing the legend underneath. Hence the split into
    `kdisp_clear_bitmap_courtyard` (column-native) / `kdisp_clear_rowmajor_courtyard`
    (row-major) — named by layout, deliberately not a `bool` parameter, so the
    pairing is visible at the call site.
  - ⚠️ **`base/fonts/gfx_icons.h` (IconsFont + a NULL-bitmap HelperFont) is
    hand-maintained and lives OUTSIDE `fonts.yaml`/`generated/`, so bulk font-header
    tooling silently skips it.** Any glyph-format change (the column transpose, a
    future re-pack) MUST include it explicitly — missing it leaves every resident
    icon (layer / arrows / caps+num lock / OS logos / mouse buttons) rendering
    garbage while pack glyphs look fine (the exact 2026-07 symptom that cost a debug
    round on hardware).
  - ⚠️ **When transposing/parsing a committed header, STRIP the `/* 0x80 ICON_LAYER
    14x16 */` comment tags before pulling `0x..` bytes** — the tag hex pollutes a
    naive byte regex and shifts the whole array. And **verify against the rendered
    glyph shape, not a transform∘inverse round-trip**: a self-check that reads back
    through the same (polluted) data falsely reports "0 mismatches" — that shipped a
    broken IconsFont fix once; ASCII-rendering LAYER/LEFT/RIGHT against the row-major
    source is what actually caught it.
- **Composable plotter modes** (`disp_array.c/.h`, static flags toggled around a
  draw): `kdisp_set_gfx_erase(bool)` makes the glyph plotter **clear** pixels
  instead of setting them, and `kdisp_set_gfx_scanline(bool)` lights **only even
  *absolute buffer* rows** — the gate is `(y + yo + yy) & 1`, i.e. the final buffer
  y, **NOT** a glyph-local row, so two glyphs at different y still interleave to one
  consistent scanline pattern. Used to render the Eden idle legend as a dim
  half-density overlay. Always pair the set with a reset (`(false)`) after the draw.
- **To draw an INVERTED keycap, render it inverted — do NOT reach for
  `kdisp_invert()`.** That is a panel-level SSD1306 command, and `split72.c`'s
  `matrix_scan_kb` already toggles it on every press and un-toggles on release,
  independently of `process_record`. So any *state* driven through it is undone by
  the next keypress on that key — which is exactly what a latched indicator must
  survive. Render it instead: `kdisp_set_buffer(0xFF)` for the ground +
  `kdisp_set_gfx_erase(true)` around the legend draw (paired reset after — the flags
  are static, so leaving erase on blanks every following keycap in that pass). Gate
  it on **synced** state (`poly_layer_t`), not a master-only static, or the slave
  half won't follow. The Intl picker's armed-Ctrl indicator is the worked example.
- ⚠️ **The courtyard clear is WRONG on a deliberately-filled ground — pass
  `cy_radius` 0 there.** `kdisp_write_gfx_text_cy(..., KDISP_CY_DEFAULT)` clears a
  3px margin around each glyph so a legend punches cleanly through whatever is drawn
  *underneath* (a tab frame, a row bar, an overlay image). On an inverted keycap
  there is nothing underneath — the fill **is** the thing you want to keep — so the
  clear eats a dark halo out of it and the key reads as *outlined* rather than
  inverted (field, 2026-08-11: "the inversion looks like it has also a courtyard").
  `kdisp_write_gfx_text` is itself just `_cy(..., 0)`, so radius 0 is the documented
  no-courtyard path, not a new mode. ⚠️ Check **both** draw paths: a bottom/thumb-row
  legend goes through `draw_legend_cx()` (now `draw_legend_cx_cy()`), everything else
  through `kdisp_write_gfx_text_cy()` directly — on split72 `MATRIX_ROWS_PER_SIDE` is
  5, so the Ctrl at `[4,0]` takes the *bottom-row* one and fixing only the obvious
  call site changes nothing.
- ⚠️ **`kdisp_write_gfx_text_cy()` walks the list TWICE when a courtyard is asked
  for, and the second pass is what makes the first correct — do not "optimise" it
  back to one.** The courtyard is cleared per GLYPH, immediately before that glyph
  is plotted, so glyph N+1's 3px margin removed glyph N's ink wherever the two sat
  closer than the radius: every letter cut a slice out of the one before it (field,
  2026-08-26). Pass 1 clears and draws as before; pass 2 redraws with NO clearing
  and restores what was removed. Underlying art stays cleared because nothing
  redraws it, so the courtyard keeps punching the legend through a tab frame / row
  bar / overlay image and stops eating the legend. Safe because every drawing op is
  idempotent (glyphs OR in, badge/frame fills are stable, an erase-mode glyph clears
  the same pixels twice), so pass 2 can only put ink back.
  - **It was never mid-face-specific, which is why it survived so long.** Measured
    ink loss: `SCRIPT:/Rune` **-6.2%**, `Qwerty` -4.1%, `IDLE:/Pulse` -3.0% — but the
    27px `Qwty` lost 10px too. The tighter 19px spacing only made a long-standing
    defect impossible to miss. **The check is a pixel count against the same legend
    drawn with `cy_radius` 0**, not a visual read; all 15 legends now match it exactly.
- ⚠️ **The anti-burn-in jitter offset is applied ONCE at the display-list cursor
  (`gfx_text_run`), NOT inside the drawing primitives — it used to be the other way
  round and that moved only the text.** `s_draw_ox/oy` were added in
  `kdisp_write_gfx_char` and `kdisp_write_gfx_char_half`, which between them cover
  ordinary glyphs and `HINT_SMALL` and nothing else: every **composite op** plots
  through a primitive of its own (`\x0F` HALF, `\x11` THIN, `\x15` ROT →
  `kdisp_draw_glyph_*_at`; `\x13` BADGE, `\x12` FRAME → the rect drawers), none of
  which ever saw the offset. So an idle relocation slid the letters and left the
  composited art pinned to the buffer — reported from hardware as *"the cursor on the
  context menu icon is not moving in the idle modes, but the hamburger menu icon
  does"* (2026-08-31). It is a CLASS bug, not one icon: `ICON_CONTEXT_MENU`'s pointer,
  and the `ICON_SCRLOCK_ON/OFF` + `ICON_MEDIA_STOP` badges, which are composite art
  **only** and so never moved at all.
  - **The fix is the choke point, not five more `+= s_draw_ox` lines.** Offsetting the
    cursor once means a sixth composite op inherits it by construction; adding it per
    primitive is the enumerating-guard shape this repo keeps getting caught by
    (`sync_is_link_fault()`, the log-source registry, the `find_matching_entry` gate).
    ⚠️ A MOVE must **re-apply** it (`x_cursor = sat8(text[1] + s_draw_ox)`) — an
    absolute position is exactly what the cursor's own offset does not reach, and
    assigning the raw coordinate is what pinned the art in the first place.
  - ⚠️ **Fixing the DRAW alone would have made it worse, and that is the half worth
    remembering.** `roll_idle_offset()` measured the slack with the RELATIVE bbox,
    which for `ICON_CONTEXT_MENU` is the hamburger alone — so once the whole cell
    moved as a unit, that slack would have carried the pointer off the panel; and for
    `ICON_MEDIA_STOP`, whose legend is a MOVE plus a BADGE and nothing else, the
    relative box is **empty** (`0,0,0,0`), i.e. "you may move it anywhere". Hence
    `kdisp_gfx_text_bbox_abs()` above, and hence the pair had to land together.
  - **The ROT geometry moved to `font_lookup.c` (`kdisp_gfx_rot_half_extent`) so the
    drawer and the measurement cannot disagree** about which pixels a rotated glyph
    touches — a box that disagrees with the pixels is a legend that clips. ⚠️ Its
    tests derive the expectation from **arithmetic** (a 0° turn is exactly the halved
    size; a 90° turn swaps the axes), because the first version asked the helper what
    to expect and therefore agreed with it by construction — a "don't halve the width"
    mutation sailed straight through. Same trap as the macro-icon preview note below.
  - ⚠️ **The offset was not the only per-primitive property the composite ops
    bypassed — `s_gfx_erase` and `s_gfx_scanline` were the SAME shape, and the
    scanline one was live.** Both are static plotter modes that only the two char
    writers honoured, so under `IDLE_STYLE_EDEN` — which draws the resting legend
    `kdisp_set_gfx_scanline(true)` as a dim half-density ghost — the text came out
    half-density while the composited art stayed fully lit. Measured over the shipped
    legends, three carry composite art AND are reachable as a resting legend (i.e. at
    idle), all three on the split72 default keymap: `ICON_SCRLOCK_ON/OFF` (`KC_SCRL`,
    a 19×19 `HINT_BADGE`), `ICON_MEDIA_STOP` (`KC_MSTP`, **badge only** — so the whole
    keycap ignored the dimming) and `ICON_CONTEXT_MENU` (`KC_APP`, the ROT'd pointer).
    The other three composite legends (`ICON_GFX_RESTART`/`_RELOAD`, and the
    `HINT_FRAME` hints) are held-modifier hints, never drawn at idle.
    - **Same remedy, one level down: `kdisp_plot_ink()`.** Every ink primitive —
      HALF / THIN / ROT / BADGE / FRAME / `_double_at`, and both char writers — plots
      through it, so it is now the single definition of "ink" and a sixth op inherits
      both modes by construction.
    - ⚠️ **Deliberately NOT pushed down into `SET_PIXEL_CLIPPED`.** Ground fills and
      bitmap blits (`kdisp_fill_rect`, `kdisp_draw_bitmap`, the tab/MRU chrome,
      `clear_line`) must stay unconditional, or an overlay image drawn while the flag
      is up would silently scanline-dim. The split is what the primitive **is**, not a
      list of call sites to keep in sync.
    - **It made the image SMALLER**: `.text` 285320 → 284320 (**−1000 B**), `.data`
      and `.bss` byte-identical (monolith `.heap` free 2772 either way) — fourteen
      duplicated per-pixel plot sequences collapsed into one out-of-line call. Verified
      in the compiled image rather than the source: `kdisp_plot_ink` is emitted
      out-of-line and `objdump` shows every composite primitive `bl`-ing it.
    - ⚠️ **`disp_array.c` has NO unit suite** (it owns the scratch buffer), so this
      class is only ever caught by reading the code or by looking at hardware —
      `oled_preview.py` refuses these ops, so the usual "render it" rule does not
      reach them either. That is why the same mistake was made twice in one file.
- **`kdisp_send_window()` vs `kdisp_send_buffer()`**: `kdisp_send_buffer()` pushes
  the full 1024-byte scratch; `kdisp_send_window()` pushes only the **visible 360
  bytes** (pages 0–4 at column `BUFFER_X`) — the same region the keycap actually
  shows — so it is ~2.9× less SPI. Prefer `kdisp_send_window()` for any per-key
  redraw that only touches the visible window (the standard case).
- **To preview a keycap faithfully, use `PolyKybdHost/tools/oled_preview.py`** (its
  `gfx_font` loader + `oled_to_rgb`) — it parses the generated headers correctly and
  renders the real 72×40 OLED look. A hand-rolled renderer cost two wrong "flag
  offset" guesses this session before the real cause (the baseline-align above) was
  found. `gfx_font.load_all_fonts(base/fonts)` includes `flag_fonts.h`, so it can
  render pack/flag glyphs too. **Caveat:** the preview models glyph `xOffset/yOffset`
  but NOT the `kdisp` baseline-align shift, so it won't reproduce that bug — reason
  about `fonts[0]` separately.
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
- ⚠️ **A hint/overlay string is drawn OVER the legend at the SAME origin, so
  full-size extra art ERASES it — a secondary mark belongs MOVE'd into a corner, and
  that corner is the BOTTOM-right.** `update_displays()` draws the legend at
  `(BUFFER_X, 23)`, sets `text = NULL`, then draws `keycode_to_disp_overlay()`'s
  string at the *same* origin with `KDISP_CY_DEFAULT` — whose 3px courtyard clears the
  legend underneath before the glyph even lands. That is correct for a held-modifier
  shortcut hint (the whole keycap *means* Ctrl+C while Ctrl is down) and wrong for a
  mark that must coexist with the legend. The bottom anchor is not taste:
  `render_key()` draws the **shift preview in the UPPER right** (baseline 23, x from
  `*_HOFFSET VAR_SHIFT`), so a top-anchored corner mark lands on it. Measured badge-
  ink-on-legend-ink over all 15 modifier combinations: a letter is 0 either way
  (en-US sets `LETTER_*_OFFSET VAR_SHIFT` to `HIDE_KEY`), but a **digit/symbol key
  always has a preview** and went from 21 px of overlap at 2 marks (top-anchored) to
  21 px at 4 marks only (bottom-anchored). ⚠️ **Measure that as the INTERSECTION of
  the two ink sets** — a "how many legend pixels survived" count reads **0 damage**
  for a real collision, because overlapping lit-on-lit loses no pixels and still reads
  as merged (that metric hid the digit collision for a round). The
  `keycap-layout-preview` skill wraps the whole measure-don't-eyeball loop.
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

### Status OLED (128×64 split72 / 128×32 split42, SSD1306 over **I2C**)
The status OLED is the QMK `ssd1306` driver (`OLED_DRIVER = ssd1306`, no
`OLED_TRANSPORT` → QMK defaults to **I2C**) on `I2CD0` (GP0/GP1) at **400 kHz**
(`config.h`). ⚠️ It is a **different bus** from the per-keycap displays (those are
the SPI SSD1306s driven by `disp_array.c`) — don't conflate them. Each half drives
its **own** status OLED locally. Rendering lives in `oled_helper.c` (`oled_task_user`
dispatch) + `<variant>/status_oled.c` (`oled_update_buffer*` composers): everything
is composed into the 1024-byte kdisp scratch buffer (`get_scratch_buffer()`,
`128×8`, cleared by `kdisp_set_buffer(0)`) then blitted to the QMK framebuffer with
`oled_write_raw`.

**The "updates in multiple passes" flicker (2026-07):** QMK's driver splits the
frame into **16 blocks** and `oled_render()` flushes only `OLED_UPDATE_PROCESS_LIMIT`
(default **1**) block per call. `oled_render()` runs every main-loop iteration, so a
static screen normally paints fast — but two things made a full repaint dribble out
band-by-band:
- Each screen composer used to call **`oled_clear()`** before `oled_write_raw`, which
  marks **all 16 blocks dirty every 66 ms tick** even when only a digit moved.
  `oled_write_raw` already diffs byte-for-byte and dirties only changed blocks, so the
  `oled_clear()` was **dropped** from `oled_status_screen()` and `oled_fw_update_screen()`
  — a static screen now costs nothing on the bus and an incremental change touches 1–2
  blocks. (The scratch is a full-frame black background, so no stale pixels result.)
- When the main loop is **saturated** (a firmware/font-pack flash streaming HID chunks
  + driving the deferred sector erase, or a boot-time busy window), the 1-block-per-call
  flush can't finish a full-screen transition before the loop starves it — the classic
  symptom was the **status→"Firmware Update" screen transition tearing**, bottom rows
  still showing the old status. Fix: both `oled_status_screen()` and
  `oled_fw_update_screen()` end with **`oled_render_dirty(true)`** to push all changed
  blocks in **one** synchronous pass. It is a **no-op when nothing changed** (early-returns
  on `!oled_dirty`), so it only pays the ~26 ms full-frame I2C cost on an actual full
  swap, never per idle tick. ⚠️ Don't reintroduce a per-frame `oled_clear()` — it defeats
  the diffing and makes `oled_render_dirty(true)` re-push the whole frame every tick.
- The other `oled_clear()` (`poly_keymap.c` `oled_init_user`) is harmless: QMK calls
  `oled_init_user` at the **top** of `oled_init`, before `oled_initialized = true`, so
  the `oled_off/render/on` around it are early-return no-ops (it only touches RAM).
- The logos + DOOM status paths use diff-based `oled_write_raw` (no `oled_clear`) and
  hardware scroll; they can still dribble on a busy transition but are non-critical, so
  they were left as-is.

**Boot noise (deferred `DISPLAY_ON`)** — the SSD1306 powers up with random GDDRAM, and
stock `oled_init()` sent `DISPLAY_ON` before any content was flushed, so boot flashed
RAM noise before the splash. Patched in QMK core (`drivers/oled/oled_driver.c`, tracked
in `UPSTREAM_PATCHES.md`): the panel stays off through init, an all-black GDDRAM is
flushed, **then** `DISPLAY_ON` — boot shows black → splash.

**Speed levers not yet pulled** (were unnecessary once the diffing + one-shot flush
landed; revisit only if a full swap still looks slow on hardware): raise
`OLED_UPDATE_PROCESS_LIMIT`, or bump I2C to Fast-Mode+ 1 MHz (`I2C1_CLOCK_SPEED`,
above SSD1306 spec — A/B on real hardware).

**Settings → "More" shows TELEMETRY instead of the status screen** (`oled_helper.c`
`oled_telemetry_screen()`, dispatched from `oled_task_user` on the synced
`poly_sync_t.settings_more`). Four lines on the 64 px panel, two on the 32 px one:
`FW <version>` · `P<protocol> HW <device_ver>` · `<USB|LNK> up <h:mm:ss>` ·
`Lnk <err>% <frames>`. Shared across both variants and landscape on split42 too,
matching the flash / confirm / apply screens there.

- **The identity fields are the ones `GET_ID` reports** (`FW_VERSION`,
  `PROTOCOL_VERSION`, `DEVICE_VER` — the same macros `hid_com.c` builds its string
  from), so the panel and the host's view of the board cannot disagree.
- ⚠️ **The link line reads `Lnk n/a` on the non-USB half, NOT `0.0%`.** The counters
  live in `bridge_helper.c` and only the master calls `send_to_bridge()`, so the
  slave's are zero because it never initiates — rendering that as a perfect link
  would be a flattering lie on exactly the panel someone reads to judge the wire.
  `poly_get_link_stats()` / `poly_link_err_permille()` expose them; the percentage is
  computed by the same expression as the periodic console line, so the two can never
  diverge. This puts the split-link health somewhere you can actually see it — until
  now it existed only in a line emitted every 200 frames on a console nobody has open.
- ⚠️ **Both link fields are COMPACTED, and the default fixture is what hides why.**
  `ls_attempts` climbs for as long as the board is up (millions within hours), so
  spelled out in full the line measures **135 px against a 127 px budget** — while the
  `1234tx` of a fresh boot fits comfortably. The count is abbreviated `k`/`M` and the
  rate drops its decimal at/above 10 %, which puts the widest reachable form at
  122 px. Checked with `tools/status_oled_preview.py --telemetry --diag` at the worst
  case (`--link 1000,4294967295 --uptime "999d 23h"`), not at the default.
- `tools/status_oled_preview.py --telemetry` renders it (both halves, `--diag` for the
  clipping check) — the same mirror-the-C treatment `build_fw_confirm_panel` gets.
  - ⚠️ **Mirror the C's GUARDS, not just its formatting — a preview's INPUT DOMAIN can
    be wider than the device's reachable state space.** `oled_telemetry_screen()` tests
    `ls.attempts == 0U` and prints `Lnk idle` **before** it computes a rate (and
    `poly_link_err_permille()` returns 0 at zero attempts anyway), so a percentage over
    zero frames is a reading no keyboard can display. The preview mirrored only the
    `link is None` case, so `--link 0,0` rendered `Lnk 0.0% 0` and `--link 250,0`
    rendered `Lnk 25.0% 0` — the preview depicting the impossible, which is the
    direction it exists to catch. This is the **sibling** of the "A PREVIEW THAT MIRRORS
    THE IMPLEMENTATION AGREES BY CONSTRUCTION" note below, not the same thing: there the
    preview and the C are wrong identically; here the preview can render a state the C
    cannot reach.
  - **Fix the RENDERER, not just the argparse validator** — these panel builders are a
    LIBRARY surface, not only a CLI. `status-oled-layout`'s `measure_bands.py` does
    `import status_oled_preview as P` and calls `P.build_panel(...)` directly, so a
    guard living in `link_arg()` is simply absent for an importing caller.
    `build_telemetry_panel()` has no such caller *yet* — it is reached only from the
    tool's own `main()` — which is exactly why the guard has to go in the renderer
    now rather than after one appears. The validator is the second half (it refuses a
    non-zero rate over zero frames, an input describing no device), not the first.
    Check it by rendering: `--link 0,0` and `--link idle` must produce
    **byte-identical** PNGs.
    - ⚠️ This bullet previously asserted that the skill imports `build_telemetry_panel()`
      itself. It does not — one `grep` settles it — and a note about verifying claims is
      the worst place to leave an unverified one. Caught by Greptile on #262, which is
      the cross-file consistency check an LLM reviewer is genuinely good at and a linter
      cannot do at all.

**split42: PORTRAIT status OLED (2026-07).** The split42 panel is 128×32 physical
but **mounted rotated 90°**, so the user reads it as **32 wide × 128 tall**. The poly
pipeline blits a **raw page-format buffer** via `oled_write_raw`, which **bypasses
QMK's `OLED_ROTATION`** — setting `OLED_ROTATION_90` does nothing here. So
`split42/status_oled.c` `oled_update_buffer()` composes the whole screen in a
**logical 32×128 portrait space** and **software-rotates** each lit pixel into the
128×32 scratch page buffer via a `pset()` mapping `(lx,ly) → (px=ly, py=31−lx)`
(page offset `(py>>3)*128 + px`; `kdisp_set_buffer(0)` clears the full 1024 B first).
It carries **self-contained portrait primitives** (`pdraw_glyph/_text/_text_center/
_glyph_half/_text_center_half/_bitmap`) that reuse `kdisp_gfx_glyph_font()` for the
lookup but plot through `pset` (the shared `kdisp_write_gfx_*` draw landscape into the
128-wide buffer, unusable for portrait). Orientation is the **single compile switch
`POLY42_STATUS_ROT_CW`** — flip it if the panel reads mirrored/upside-down (nothing
else changes; everything composes in logical space). The flash/update + boot-logo
screens are still landscape (deferred). Preview + clip check:
`tools/status_oled42_preview.py` (`--diag`) mirrors the C coordinate-for-coordinate.

**Layout work: MEASURE the pixel bands, don't eyeball the render** (2026-07-29).
`--diag` only catches pixels off the *panel*; it says nothing about rows colliding
or slack pooling at the bottom. Both previews are importable, so wrap their draw
helpers to tag which pixels each call produced, then reduce to contiguous lit-row
bands and the gaps between them — the **`status-oled-layout` skill** wraps this
whole loop (instrument → bands → place → render → build). What it caught that the
eye did not: the layout name's descenders **overlapping the row below by 2px**
(`Qwerty Stag!` descends to baseline+4 across x7..90 — a *wide* tail, not a narrow
one, so the row below cannot dodge it), and the RGB panel's colour/S+V rows
touching at a **0px** gap, while 4 rows sat unused under the bottom row.
- **Space each panel independently; do NOT share one set of row baselines.** The
  two panels have opposite shapes — the layout panel's descenders are on row B,
  the RGB panel's on row C — so a shared set is over-constrained: a brute force
  over all (rowB, rowC) pairs maxed out at a **1px** minimum gap with a lopsided
  7px elsewhere, vs **3/2/2** and **3/3/3** when split. The halves sit ~20cm apart,
  so the 1–3px row offset between them does not read as misalignment.
- **Pin the bottom row so its last pixel lands on the final screen row** (63 on
  split72), and give the side marker that same baseline — it then sits level with
  the last content row instead of floating. Derive each row's extent from its own
  content: text is `base-10..base` (`+4` with descenders), the 13px globe is
  `base-12..base`, a full brightness gauge is `base-12..base` and 98px wide, so the
  meter can only ever hold a row alone.
- **Check the worst case, not the default fixture**: longest layout name
  (`Qwerty Stag!`, 95px), a fully-lit gauge, and a 3-digit WPM. And when moving a
  readout between panels, confirm the value is actually available there —
  `get_current_wpm()` reads correctly on both halves only because `config.h` sets
  **`SPLIT_WPM_ENABLE`** (the master syncs it); without that it renders 0 on the
  non-master half.

- ⚠️ **Read glyph `xOffset`/`yOffset` through `int8_t`** in the portrait draw
  helpers: `pgm_read_byte()` returns `uint8_t` and **zero-extends** the Adafruit-GFX
  signed offsets, so `int yo = pgm_read_byte(&g->yOffset)` turns a text glyph's
  `yOffset −8` into `248` and the glyph plots off-screen (silently clipped by
  `pset`). Every text glyph has a negative `yOffset` (above baseline) and the icons
  −15/−16, so this blanks **all** text + icons while bitmaps/globe/bars still draw.
  Cast: `int xo = (int8_t)pgm_read_byte(&g->xOffset)` — the pattern `disp_array.c`
  uses. ⚠️ The Python preview parses signed decimals directly, so it does **not**
  reproduce this bug — it validates the *layout*, not the compiled C sign-handling
  (this shipped once, PR #149, caught in review).
- ⚠️ **Font-header DOUBLE-DEFINITION trap** (cost a full link cycle): `util_font.h`
  (`NotoSans_Regular_Mid_19px7b`) and `nano_font.h`
  (`NotoSans_Regular_Nano_10px7b`) **define** the font *data* (non-`static`) and are
  **already compiled into `poly_keymap.c`**. `#include`ing them in `status_oled.c` too
  gives a `multiple definition of …` **link** error (compiles fine). **Declare them
  `extern const GFXfont X;`** instead — the pattern `oled_helper.c` already uses.
  (`NotoSans_Medium_Base_8pt.h` is only included here, so that
  `#include` is safe.)
- **32 px width budget:** at 32 px only ~5 chars fit, and the layout name is the
  tightest thing on the panel. ⚠️ **Do NOT half-scale a bigger font to get there** —
  a 2×2-OR downsample ORs pixel pairs together, which thickens every stem back to
  ~2 px and closes the counters that grid-fitting just opened (`Qwrty` ran its `w`
  and `r` together); the decimation ("thin") downsample instead breaks strokes.
  Render a real small face at native size: the layout name uses the dedicated
  **`_Nano_` 10 px** (`nano_font.h`), the largest that fits — its widest short name
  `Wkmn` is 30 px, versus 33 px (1 px past the panel) at the `_Tiny_` 11 px size.
  `LAYOUT_NAME_BASE` in `split42/status_oled.c` places it by cap height. split42 uses **short**
  layout names via `layout_name_short()` in `status_oled.c` (`Qwrty/Stag!/ColDH/Neo/
  Wkmn/Unkn`); split72 keeps the full names in the shared `oled_helper.c` array — keep
  the two in sync when layouts change.

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

### Idle anti-burn-in styles (`poly_keymap.c`)
When the keyboard idles, the keycap legends would otherwise burn the **same**
pixels in. **Four** styles (EEPROM `poly_eeconf_t.idle_style`, HID cmd 28, enum
`poly_idle_style` in `state.h`): `IDLE_STYLE_PULSE` (0), `IDLE_STYLE_JITTER` (1),
`IDLE_STYLE_IDDQD` (2, the DOOM attract-demo screensaver), `IDLE_STYLE_EDEN` (3,
the looping "Eden" comet-field screensaver). The first two are described in detail
below; IDDQD/EDEN are full-screen animations that own the keycaps via their own
tick (`doom_tick()` / `startup_anim_tick()`), so `update_displays()` early-returns
while they run (see "Eden startup animation & idle screensaver" below for EDEN):

⚠️ **The DEFAULT is board-dependent since 2026-08-31: `POLY_DEFAULT_IDLE_STYLE`
(`state.h`) is EDEN on split72 and PULSE everywhere else.** Three things about that
change generalise well beyond this setting:
- ⚠️ **EDEN on split42 would have been an anti-burn-in setting that does NOTHING —
  and the enum's own comment claimed it "behaves like PULSE".** It does not.
  `anim/startup_anim.c` is `#if defined(KEYBOARD_polykybd_split72)` with no-op
  stubs, *and* every other idle painter stands down for EDEN by design:
  `kdisp_idle()` returns immediately, the engage branch holds contrast at
  `EDEN_IDLE_BRIGHTNESS` instead of computing a pulse, and `update_displays()`
  early-returns while `DISP_IDLE`. So the legends **freeze**, dim and unmoving,
  until the TURN_OFF suspend — the one outcome this whole feature exists to
  prevent. The default macro is gated on the **same** board macro the animation
  compiles itself on, so a default whose renderer is not in the image is not
  expressible. **Before defaulting anything to a feature with stubs, check what the
  stub path actually leaves running** — "degrades gracefully" was written down and
  was wrong.
- ⚠️ **An explicit PULSE on a pre-2026-08-31 board is NOT RECOVERABLE, because the
  old format never recorded it.** `IDLE_STYLE_PULSE` is 0 and QMK's wear levelling
  hands back a cleared byte as **zero** (the trap that made `latin_assign` read as
  "every key hosts 'a'"), so "chose pulse" and "never chose" are the *same byte*. No
  migration scheme can separate them, which is what makes the one-time move of the
  whole pre-sentinel population to the board default the honest reading rather than
  a compromise. Say so plainly in release notes: **existing keyboards that never
  picked JITTER/IDDQD/EDEN will come up in the new default once.**
- **The fix for next time is the `idle_style_fmt` sentinel** (`0x3C`, tail byte,
  same shape as `latin_ext_fmt` / `keymap_layers_fmt`). `load_user_eeconf()`
  substitutes the board default while it is unstamped; `save_user_settings()` stamps
  it **after** the block it guards, so an interrupted write cannot claim a choice
  that was not stored. From the first save onwards the byte is taken verbatim — so a
  *future* default change can no longer overwrite a real choice, which is exactly
  the property this one lacked. `eeconfig_init_user()` writes both, so a fresh
  EEPROM is born stamped and never migrates. Cost: `EECONFIG_USER_DATA_SIZE`
  156 → 157, well inside the 256-byte reservation, so **no keymap relocation and no
  user reset**.
- **Verify the gate in the compiled object, not the preprocessor.** `g_idle_style`
  lands in `.data` on split72 (initialiser `03` = EDEN) and in `.bss` on split42
  (zero = PULSE) — one check per board, against the image that actually ships.
  - ⚠️ **`objdump -s -j .data.<sym>` is NOT that check — it prints only the
    file-format header and looks like an empty section.** GCC merges the symbol into
    plain `.data` here, so no per-symbol section exists. ⚠️ `nm` is misleading too:
    it reported `g_idle_style` as type **`t`** (text) for a symbol whose address is
    inside `.data`'s VMA range. **Classify by ADDRESS against the section VMAs, then
    read the byte** — which also gives you the value, not just the section:
    ```bash
    arm-none-eabi-nm -S <elf> | grep -w g_idle_style        # -> ADDR SIZE TYPE NAME
    arm-none-eabi-objdump -h <elf> | awk '$2==".data"{print $4}'   # .data VMA
    arm-none-eabi-objcopy -O binary --only-section=.data <elf> /tmp/data.bin
    od -An -tu1 -j $(( ADDR - VMA )) -N1 /tmp/data.bin
    ```
    `.data` membership alone already proves it is not PULSE — a zero-initialised
    static would be in `.bss`.
- ⚠️ **A `static inline` helper is often emitted OUT-OF-LINE, so "grep the caller for
  the flag address" reads as "the fix didn't take".** Verifying `kdisp_plot_ink`, all
  five composite primitives showed **0** references to `s_gfx_erase`/`s_gfx_scanline`
  — and were correct: GCC emitted the helper as a real local function and they `bl`
  it. Grep for the **call**, not the data address:
  `objdump -d <elf> | grep -c "bl.*<kdisp_plot_ink>"`. Same family as the note above:
  when checking a change in the image, find the thing that MOVED, not the thing you
  wrote.

- **`IDLE_STYLE_PULSE` (0, legacy; the default on boards without Eden):** `kdisp_idle()` only modulates each
  keycap's SSD1306 contrast register (a per-key out-of-phase "breathing"). The
  buffer is never re-rendered, so the lit pixels never move — the burn-in risk.
- **`IDLE_STYLE_JITTER` (1):** keeps the pulse, but **each key independently**
  relocates its own legend to a fresh random spot the instant that key's
  out-of-phase pulse dims it to black — so the lit pixels migrate per key, not in
  lockstep. Mechanics (all in `kdisp_idle()`):
  - `kdisp_idle()` already computes a **per-key brightness** (`to_brightness((contrast
    + per-key phase) % 50)`) and walks every key on this half with the shift register
    selecting each in turn. On the **lit→dark edge** (`idle_brightness==0` and the
    `s_idle_was_dark[r][c]` latch was clear) in JITTER style it **switches that key's
    panel OFF first, then** calls **`render_idle_key(kc, led_state, seed)`** to redraw
    *that one key* straight into the currently selected (now-dark) display. Writing the
    new frame *after* the off-switch is what makes the move invisible — the glyph
    reappears already at its new spot on the next bright cycle (~once per ~15 s per key);
    writing before the off-switch flashed it at the old contrast first (a visible jump
    just before the key dimmed out). `s_idle_was_dark` gates it to once per dark episode
    (a 1-bit-per-key latch, this-half-only). `render_idle_key()` **returns false without
    touching the buffer** when the keycode has no plain-text legend (a language flag,
    emoji, region tab, MRU control — full-bleed images that can't be jittered), so those
    keys keep their current frame and just pulse instead of being blanked (the
    language-layer flags no longer disappear on the first idle cycle).
  - **No shared offset, so nothing extra crosses the UART.** Each half runs
    `kdisp_idle()` on its own keys with the synced pulse `contrast`; only the **style
    bit** is synced (`poly_sync_t.idle_style`, set from `housekeeping_task_user()` on
    the master, adopted by the slave's `copy_local_state`) so the slave jitters iff
    the master's style says so. The legend is **re-derived from the keycode** on every
    relocation — nothing is stored in the OLED's own memory (the panel only holds the
    last frame we send).
  - `update_displays()` **early-returns while `DISP_IDLE` is set** (it would otherwise
    fight `kdisp_idle()` and redraw the awake chrome) — the keycaps already hold the
    last centred awake render when idle begins, and `kdisp_idle()` owns all idle
    visuals from there. `render_idle_key()` draws **only the resting normal legend** —
    no shift/AltGr preview, no overlay image, no tab/MRU chrome. The relocated keycode
    is resolved through **`display_keycode_at()`** — the shared helper (also used by the
    awake `update_displays`) that honours the active momentary stack **and the default
    layer** (`def_layer`, folded in so a Colemak/Neo base shows its own legends, not
    `_BL`) with a one-level transparent fallback — so a jittered key matches what was on
    screen rather than snapping to the base layer.
  - **The travel range is derived per glyph from its own on-screen slack** — there is
    deliberately **no global `±N` offset envelope**. `render_idle_key()` measures the
    legend with `kdisp_gfx_text_bbox()` (full x+y box, mirroring the draw's cursor
    rules and per-glyph yAdvance shift; `kdisp_gfx_text_bounds()` is now a wrapper over
    it) and `roll_idle_offset()` rolls a **uniform random position within that glyph's
    free space** inside the visible window `[BUFFER_X, BUFFER_X+SCREEN_WIDTH-1] × [0,
    SCREEN_HEIGHT-1]` (= `[28,99]×[0,39]`). So a slim `i` roams its full free width
    while a wide `w` (or a full-width CJK legend) moves only as far as it can without
    clipping — each uses all *and only* the room it has, for any script. A fixed cap
    would be counter-productive: it would throttle the slim glyph and edge-bias the
    wide one (most rolls clamping to the same boundary). A glyph with no slack in an
    axis simply doesn't move in it — no clipping, no special-casing. `SET_PIXEL_CLIPPED`
    in `disp_array.c` remains the memory-safety backstop, but is not relied on for
    visibility.
  - The per-key latch is cleared by **`reset_idle_jitter()`** on every wake/suspend
    path (`display_wakeup`, `poly_suspend`, `suspend_wakeup_init_kb`, cmd 15
    stop-idle), so a fresh idle session starts from the centred awake legend and
    relocates every key cleanly. (This **replaces** the earlier global-offset jitter,
    where the master picked one `idle_dx/idle_dy` per ~15 s cycle, synced it, and all
    keys shifted together — the per-key version is the nicer effect *and* drops the
    synced offset.)
  A "Matrix-style" idle animation was considered but shelved — it defeats the
  "glance at the dimmed legend and resume typing" hint the pulse preserves; jitter
  was chosen as the default-preserving, legibility-preserving fix. ⚠️ **That
  reasoning no longer describes the shipped default on split72**, which is EDEN (see
  the board-default note at the top of this section) — a deliberate trade of the
  glance affordance for a screensaver that actually repaints the panel. It still
  describes why JITTER, not an animation, was the fix *within* the pulse family.

### Eden startup animation & idle screensaver (`anim/startup_anim.*`, `poly_keymap.c`)
A **fully procedural** (no framebuffer) per-keycap comet-field animation that
converges into the "EDEN" letters. It has **two lifetimes**, sharing one engine:
- **One-shot intro** — `startup_anim_start()` (`s_loop == false`): runs to black
  then ends. Fired by the **`KC_EDEN`** keycode and the host **HID cmd 31**
  (REPLAY_ANIM). ⚠️ **cmd 31 is NOT protocol-gated / bumps NO `PROTOCOL_VERSION`**
  — it's dispatched independently in `hid_com.c` case 31, like the fontpack cmds.
  There is deliberately **no boot auto-play yet** (see the TODO in
  `anim/startup_anim.h`: play the intro after the boot splash with a fade-in).
- **Looping screensaver** — `IDLE_STYLE_EDEN` (3): `startup_anim_start_loop(contrast)`
  holds the opening comet field open forever at the idle brightness, no letters/
  converge/fade. `eden_idle_tick()` in `poly_keymap.c` drives it; it is a **no-op
  while awake** (only runs when `idle_style == EDEN` and idle). While the animation
  owns the keycaps (`startup_anim_active()`), `update_displays()` early-returns.
- ⚠️ **The looping idle frame is TIME-SLICED — never render it as one blocking unit.**
  A full frame is ~36 keycaps × (procedural 72×40 background + comet trails + the
  drifting legend + a 360 B SPI push), tens of ms during which the main loop cannot
  scan the matrix. Rendered whole, a short tap that starts *and* ends inside a frame
  is simply never seen — the "Eden doesn't wake on the first keypress" report
  (2026-07-29) — and on the slave half it also stalls that half's own scan and the
  master's matrix pull. `startup_anim_tick()` therefore renders keycaps until
  `EDEN_IDLE_SLICE_MS` (3) is spent, returns, and **resumes at the same keycap** on
  the next pass; `el` and the spark set are latched once per frame (`s_frame_el` /
  `sa_build_sparks`) so the slices compose into one coherent frame, and
  `EDEN_IDLE_FRAME_MS` (10) still gates the gap between frames measured from the
  **end** of the last one. `startup_anim_stop()` drops a half-rendered frame so its
  leftover slices can't paint comets over freshly-woken legends. The idle log
  reports `frame Nms, worst slice Nms` at frame END (first frame of a session
  immediately, then ~5 s) — **the worst slice is the responsiveness number**; tune
  `EDEN_IDLE_SLICE_MS` against it, not against the frame time.
  - **`EDEN_IDLE_FRAME_MS` is NOT a latency dial** — it was 55 ms only because it
    was once the sole thing handing the main loop back between unsliced frames. With
    slicing it just cost frame rate (22% of a measured ~250 ms period), so it is now
    10 ms: one guaranteed clean main-loop pass per frame as a backstop, nothing more.
    Don't raise it to "help responsiveness" (that's `EDEN_IDLE_SLICE_MS`) and don't
    take it to 0.
  - ⚠️ **Measured on hardware (2026-07-31), so don't re-litigate it by arithmetic:**
    a frame is **~150 ms** of CPU for ~36 keycaps (~4.3 ms each, of which only
    ~0.3 ms is the 360 B SPI push — so ~93% is compute in the 2,880-px inner loop /
    `sa_plot_sparks` / the legend draw). An A/B probe alternating the 4 KB `SA_NOISE`
    tile between XIP flash and SRAM *every frame* measured **154 ms vs 145 ms — ~6%**,
    refuting the theory that XIP stalls dominate. **The tile stays in flash**; moving
    it is not worth 4 KB of the ~5.8 KB free SRAM (the `.heap` remainder, and there is
    no allocator in the image to consume it). If you want the frame cost down, stub
    out one stage at a time and read the `frame Nms` line — estimating from cycle
    counts was off by 2.5× and sent this chase down a dead end.
  The boot intro (`sa_render_frame`) is deliberately left unsliced/unthrottled: it is
  brief, swallows every key anyway, and owns the CPU.
- **The idle path's background is 2×2-coarsened on the ROTATED thumbs too** (the boot
  intro keeps them full-res, so its look is byte-identical): in local space it is the
  same block approximation the flat keys already use and it cuts a thumb's `sa_bg`
  calls 4×. Both paths also early-out on `bgv == 0` before the noise lookup — exactly
  equivalent (0 can never exceed an unsigned threshold) and most pixels are 0 at this
  faint density.
- **The idle legend** (the resting key label drawn over the comet haze) is rendered
  **LIT + scanline** (`kdisp_set_gfx_scanline(true)` around the text draw), not
  erased — the scanline halves the lit pixels so the legend reads as a dim overlay
  while still drifting via `roll_idle_offset()`. (ERASE mode was tried but looked
  worse with the drifting glyphs.)
- **split72-only.** `anim/startup_anim.c` gates on
  `#if defined(KEYBOARD_polykybd_split72)` (else no-op stubs) because it needs the
  generated per-board geometry header `anim/startup_anim_geom.h` (key OLED
  positions/rotations + splash-letter targets), produced by
  `PolyKybdHost/tools/startup_anim_demo.py --emit-geom … --kle …`. **The recipe to
  add split42 is `anim/SPLIT42_EDEN.md`** (author a split42 KLE + splash plan,
  regenerate the geom header, drop the stub).
- **Boot-intro-done persistence** rides the suspend-only dirty-flag EEPROM model:
  `mark_boot_intro_done()` sets `g_boot_dirty` (NOT a direct write); `save_all_dirty()`
  flushes it — do not add a direct EEPROM write here.

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

### Keycap legend size (`base/legend_plan.c` + `poly_keymap.c`, HID cmd 34, protocol v13+)

*(The planner — `glyph_size_remap()` / `plan_main_legend()` — is pure in
`base/legend_plan.c` since #237, behind has-glyph/bbox callbacks with the firmware
binding kept as wrappers in `poly_keymap.c`; `make test:polykybd_legend_plan` pins
the tier bases, the all-or-nothing fallback and the origin clamps.)

Three sizes for a key's **MAIN** legend — `GLYPH_SIZE_S` (0, the 27 px face the board
has always drawn, and the default), `M` (1, 33 px em) and `L` (2, 39 px em). State and
plumbing mirror the glyph-script override exactly (`poly_eeconf_t.glyph_size` tail byte,
`poly_sync_t.glyph_size` master-authoritative, housekeeping diff + `request_disp_refresh`,
`KC_GLYPH_SIZE` on the settings layer). What is worth knowing is the parts that are NOT
like the glyph script:

- ⚠️ **The range is CLOSED — see the cmd-34 note in the protocol list above.** Do not
  "make it open like the script".
- **No migration sentinel, and for once that is sound rather than lucky.** The default
  is 0 and QMK's wear levelling normalises an unwritten byte to **ZERO** — the exact
  fact that made `latin_assign` read as "every key hosts 'a'" (see the Intl-remap traps)
  works in our favour here. `load_user_eeconf()` still bounds-guards it.
- **The bigger faces are RELOCATED, not a second lookup path.** `g_all_fonts` is scanned
  front-to-back and the resident `latin` font is always in front, so a second face at
  native codepoints could never be reached. `fonts.yaml`'s `latinbig` category emits each
  tier at a fixed offset into supplementary PUA plane 15 (**fontconvert `-o`**, the
  range-mode sibling of the `-F` the glyph scripts use): M at `0xF0000 + cp`, L at
  `0xF3000 + cp`. `glyph_size_base[]` in `base/legend_plan.c` must stay identical to the
  `offset:` values in the yaml (a `_Static_assert` pins the size indices; the unit
  suite pins the base values). ⚠️ `-o` was documented as "add" but implemented as
  "subtract" (an exact alias of `-n`) until 2026-08-20; nothing used it, so the fix was
  inert — but an older fontconvert will silently emit the WRONG range here.
- **`glyph_size_remap()` is ALL-OR-NOTHING.** If the `latinbig` bundle is absent, or any
  glyph of the legend is missing at that size (a CJK/Arabic/Indic keycap — this is a
  latin-only feature), the WHOLE legend falls back to the small face. A partial hit would
  mix two fonts in one legend, which by the documented baseline-align rule also means two
  baselines.
  - ⚠️ **A legend can carry LEADING CURSOR OPS, and refusing them silently halved the
    French number row.** A base legend is a mini display list like a hint string, and 73
    of them across the 160 layouts open with a zero-argument cursor nudge — `é è ç à` on
    AZERTY are spelled `\f\f <letter>` (a 4 px lift hand-tuned for the small face), cs-CZ
    uses four. The first version bailed on any codepoint `< 0x20`, so **`& " ' ( - _` grew
    with the setting while `é è ç à` stayed small** — a real gap, invisible from the code
    and obvious the moment the row was rendered (2026-08-21). `glyph_size_remap()` now
    **drops** the five ops that occur (`0x05 0x06 0x08 0x0B 0x0C`) and still bails on
    every other one: `HINT_MOVE`/`HINT_FRAME` consume the two codepoints after them, which
    would then be relocated as if they were glyphs, and `HINT_HALF`/`HINT_THIN` rescale the
    next glyph. Measured, so it can be re-checked: every op present is one of those five
    and every one **leads** the legend (`0x0C` ×150, `0x0B` ×8, `0x06` ×9, `0x08` ×1,
    `0x05` ×1; **not one after a glyph**).
  - ⚠️ **DROPPED rather than carried, because `kdisp_gfx_text_bbox()` and the DRAW
    disagree about these ops** — a pre-existing inconsistency this was the first code to
    depend on. `\f` is `y = y > 1 ? y - 2 : 0` applied to the cursor; the draw runs it from
    the real baseline (23/25/28) where it genuinely lifts 2 px, while bbox runs it from
    `y = 0` **relative** to the baseline, where the ternary saturates and it does nothing.
    So carrying the op moves the glyph by an amount `plan_main_legend()`'s clamp cannot
    see. Carrying it was tried first and clipped 6–8 px off the accents of `é è à` at M/L
    — and the reasoning that predicted it would be safe ("the clamp measures the same
    ops") was wrong for exactly this reason. Dropped, the measured bbox **is** what gets
    drawn, and nothing is lost: the nudge was tuned for the small face's fixed baseline,
    which is the thing the planner replaces.
  - **Measured after the fix**: 1467 of 1500 latin number-row keys reach the bigger face
    (was 1338); the 33 that don't are genuinely non-latin (Thai, Bopomofo, Armenian,
    Cherokee, Vietnamese PUA composites). Clipped pixels **drop** at M/L rather than
    rising — cs-CZ's nine number keys clip at small and are clean at M/L, because the
    clamp fixes what the hand nudge could not.
- ⚠️ **THE SIZES ARE MEASURED, AND THE PANEL IS THE BINDING CONSTRAINT.** The keycap is
  40 px tall and the tallest latin glyph (Ḉ, `_LatinExtAdd_`) already inks **33** of them
  at the base size, so a uniform scale factor clips the accent stacks long before the
  plain letters run out of room — there is no single factor that gives two clean tiers.
  Each `latinbig` entry instead takes the largest pixel size whose TALLEST glyph still
  inks ≤ 40 px, capped at the tier target, so four entries deliberately grow less than
  the rest (`_SupAndExtA_` L 35, `_LatinExtB_` L 37, `_LatinExtAdd_` M 31 / L 33,
  `_Cyrillic_` M 35). `fonts/measure_glyph_sizes.py` is how those were chosen; re-run it
  after ANY change there, and `PolyKybdHost/tools/glyph_size_preview.py --check` to
  confirm zero clipped pixels.
  - ⚠️ **Before adding a SCRIPT to the bigger tiers, MEASURE it — the obvious proxy is
    not predictive, and it was wrong about every script it was applied to.** The
    tempting estimate is `40 px panel ÷ what the script inks today`; it assumes the
    glyph would fill the panel at the bigger tier, which depends on the face's own
    ink-to-em ratio and is only knowable by rendering. `measure_glyph_sizes.py
    --category <names>` does that — it reads each entry's real source, ranges and
    options out of `fonts.yaml` through `generate_fonts.py`'s own `resolve()` /
    `build_argv()`, so it cannot drift from what would actually be emitted. Measured
    per entry (2026-08-21), largest fitting ppem's ink ÷ as-shipped ink: **latin
    ×1.18–1.50** (the shipped feature), Cherokee ×1.52, **Japanese ×1.29–1.38**,
    Telugu ×1.26, Bengali/Ethiopic ×1.23, Armenian/Georgian/Bopomofo/Tamil/Thai/
    Canadian ×1.21–1.22, **Hebrew ×1.20**, Devanagari ×1.06, and **Hangul ×0.92 — it
    would get SMALLER.** The estimate had called Hebrew and Hangul ×1.60 apiece and
    written Japanese off as hopeless; all three were wrong, and Korean — the layout
    that prompted the question — is the one script that provably cannot benefit.
  - ⚠️ **`render_height` (fontconvert `-r`) is NOT an ink ceiling.** `latin` carries
    `render_height: 44` and grows fine, because a tier overrides it with a pixel size.
    An earlier cut of the tool verdicted off the presence of that flag and declared
    latin unable to grow. Read the measured ink, never the flag. It does mean a script
    can already be drawn LARGER than any tier ppem would give it, which is exactly why
    Hangul (`render_height: 51`) shrinks.
  - ⚠️ **Measure per ENTRY, not per category, and a range's tallest glyph may not be a
    legend.** Latin needed four of its twelve entries capped below the tier target; a
    per-category maximum hides that and condemns the whole category on one glyph.
    Hebrew's range maximum is a standalone nikud mark inking 43 px that never appears
    on a keycap, so the category number reads far worse than the letters do.
  - **Coverage as shipped is Latin, Cyrillic and Greek.** Those scale completely
    (`ru-RU` and `el-GR` measure 49/49 keys). The other **41 layouts come out MIXED** —
    their digits and punctuation are latin and grow while the letters do not (`ko-KR`
    is 23 grown / 26 unchanged). That is stated on the public `using/legend-size` page
    rather than hidden. Don't "fix" it by gating the setting off per layout: that only
    takes the feature away from the keys it does reach.
- **`yadvance: 40` on every `latinbig` entry** makes `kdisp_write_gfx_char`'s baseline
  align a no-op, so the y `plan_main_legend()` computes IS the baseline. Side effect
  worth having: at M/L every latin sub-font shares one baseline, where at S `a`
  (`_Base_`, yAdv 37) and `ä` (`_SupAndExtA_`, 44) sit 7 px apart.
- **Placement = nominal baseline THEN clamp against the legend's own bbox.** The nominal
  keeps ordinary letters on a shared baseline; the clamp is what stops a tall accent or a
  deep descender clipping. Two knock-ons that only bite at the bigger sizes, both in
  `render_key()`: the overlap **stagger must not lift a big base** (already clamped to the
  panel, so a 6 px lift pushes it off the top), and the **AltGr preview** — kept off the
  legend by its VERTICAL offset at S — is pushed clear HORIZONTALLY instead, because a big
  legend fills that height.
- The shift/AltGr previews stay small **by design**: a keycap has room for one big thing.

### Brightness keys — one icon family (`keycode_helper.c`, `base/fonts/gfx_icons.h`)

The eight brightness keycodes now draw **one resident IconsFont glyph each**, all
built on the sun the status OLED already uses for brightness: `KC_DMIN` / `KC_D1Q` /
`KC_DHLF` / `KC_D3Q` / `KC_DMAX` are a sun whose **rays grow with the level** beside a
staircase that states the level outright; `KC_DDIM` / `KC_DBRI` are a small/large sun
with `−`/`+` and no staircase (they name no level); `KC_DAUTO` spells **AUTO** or
**MANUAL** under the sun.

- ⚠️ **What it replaced was actively misleading, not merely inconsistent.** The five
  presets were **moon phases**, and the mapping ran BACKWARDS from the obvious
  reading: `PRIVATE_DISP_BRIGHT` was **U+1F311 🌑 NEW MOON**, the all-black disc,
  because it depicted the unlit *screen* rather than the brightness. Nothing else on
  the board used that convention. `KC_DDIM`/`KC_DBRI` meanwhile borrowed the plain
  page arrows `ICON_LEFT`/`ICON_RIGHT`, which say nothing about light at all.
- **The staircase has FOUR steps because the presets ARE quarters** (`FULL_BRIGHT`
  × 1/4, 1/2, 3/4, 1/1), so each lights exactly its own number of them and `KC_DMIN`
  — brightness **2 of 50**, below the first quarter — lights none. Five steps put 50%
  and 75% on 2 and 3 of 5, i.e. a meter misreporting the value it exists to state.
  An unlit step keeps a **1px foot**, the status OLED's own rule
  (`split72/status_oled.c` `draw_brightness_bars`), so the full scale stays visible.
- ⚠️ **`KC_DMIN` keeps a FILLED sun with zero rays — do not "improve" it to a hollow
  one.** It sets brightness 2, the dimmest **lit** level (`DISP_OFF` is 0,
  `MIN_BRIGHT` is 1), so a hollow sun would claim an off state the key cannot reach.
- ⚠️ **`KC_DAUTO` spells the mode out instead of wearing `ICON_SWITCH_ON/OFF`.** A
  toggle beside a sun reads as *"the light is on/off"*, which is the one thing this
  key does not control — reported in the field as exactly that confusion.
- **One glyph per legend is the point, not an accident.** `kdisp_write_gfx_char`
  baseline-aligns by `font->yAdvance - fonts[0]->yAdvance`, so a legend composed from
  an icon plus base-font text sits on two baselines (the `à»ñ` note in the Intl-remap
  traps). Baking each cell as a single IconsFont glyph — `IconsFont` **is** `fonts[0]`,
  so its adjustment is 0 — makes the whole cell one unit at one baseline.
  - ⚠️ **Therefore NO leading pad space**, unlike the `U"  " ICON_LEFT` legends beside
    them. Each glyph carries its own `xOffset` measured from `BUFFER_X`; a space would
    advance the cursor and shift the whole cell right.
- **Cost: +1747 B of flash, 0 B of RAM** (`.data` 318292 → 320052, `.bss` unchanged).
  Nine glyphs at 50–58 px wide; the monolithic `POLYKYBD_DOOM=yes` flavour still links.
- ⚠️ **The now-unused `PRIVATE_DISP_*` moon macros stay, and so does the resident
  `_Brightness_` font (1208 B) — it is NOT dead.** It covers `0x1F311..0x1F318`, which
  the **emoji layer** also lists (`emoji/emoji_data.h`), and resident wins the
  front-to-back lookup — so dropping it would silently re-render those emoji from the
  pack at a different size. Removing a resident font also shifts every pack font's
  gidx and forces a full-pack reship, for 1.2 KB against a 2 MB partition at ~38%.
- **Verify by rendering, never by reading the header.** `PolyKybdHost/tools/gfx_font.py`
  parses the committed headers and walks the same front-to-back `ALL_FONTS` lookup the
  firmware does, so a sheet drawn through it checks the shipped bytes *and* the
  codepoint routing. Count the pixels it drops outside the 72×40 window — that is the
  clipping check, and it must be 0.

### The utility layer's remaining text keys (`keycode_helper.c`, `poly_keymap.c`)

Three `_UL` keys still spelled themselves out in four letters while every neighbour
drew an icon, and one pair of keys was replaced by a single state-reflecting key.

- **Mute is the speaker we already had, with a cancellation X beside it** —
  `PRIVATE_MUTE` (U+1F568) + U+1F5D9, placed by the ordinary cursor advance.
  U+1F507 (the emoji cancelled speaker) was shipped first and reverted: at 40×39 the
  slashed circle fills the whole cell and reads as busy rather than as "muted".
  U+1F5D9 is the crispest of the three X glyphs already in the pack (U+2717 is a
  script ballot X, U+2718 a heavy one) and comes from the same `Window` font the
  legend-size icons use.
- **Why the old glyph failed, which is the part worth keeping:** The old `PRIVATE_MUTE` (U+1F568) is
  a speaker with **no wave arcs**, i.e. it differs from `PRIVATE_VOL_DOWN` (U+1F569,
  one arc) and `PRIVATE_VOL_UP` (U+1F56A) only by an *absence* — nothing on it says
  "muted", which is exactly how it was reported. ⚠️ **Do not "finish the family" by
  moving the volume keys to U+1F508/U+1F50A**: those NotoEmoji glyphs are filled and
  render visibly heavier beside the NotoSansSymbols2 line art (rendered and compared).
  - A slash **composited over** the speaker cannot work, which is why the X sits
    BESIDE it: a legend display list has no erase op, so a lit slash over a solid
    glyph merges into it, and a dark-gap version would need a baked glyph — which the
    **full** C1 band has no room for (see the icon-slot note above). The speaker is
    only 19 px wide, so there is room for a separate mark at no cost.
- **Scroll Lock keeps the word and gains a STATE badge**: `U"Scr"` + `ARROWS_DOWNSTOP`
  (U+2B73, the glyph the **status OLED** already lights for this state) at half size
  inside a rounded box that goes **solid when the lock is engaged** — the same shape
  Caps Lock and Num Lock use, which is why it reads at a glance. `led_t.scroll_lock`
  rides `poly_layer_t.led_state`, which is **synced**, so the slave half shows it too.
  - **The status OLED no longer shows scroll lock** (`split72/status_oled.c`). It used
    to draw the same glyph while engaged, but with no *off* state and by **replacing
    the L/R side marker** — so the half lost its side marker exactly while the lock was
    on. The keycap badge supersedes it, which is where Caps and Num are read from
    anyway. split42's panel never had one.
  - ⚠️ **The badge is DRAWN, not baked**, and that is forced: the resident C1 band is
    full (32/32), so there is nowhere to put the OFF/ON glyph pair Caps and Num each
    get. **`HINT_BADGE` (`\x13`, args `w, h, style`)** draws either state — style 1 a
    2px outline, style 2 the solid — and **`HINT_ERASE` (`\x14`)** punches the arrow
    back out of the solid one; that knock-out is what makes the engaged state read as
    *inverted* rather than as a blob.
  - ⚠️ **The corner radius is MEASURED off the baked glyphs, and `HINT_FRAME` is the
    wrong shape for this.** `ICON_CAPSLOCK_*` insets its corners **2, 1, 0 px** — a
    radius-**2** arc — while `HINT_FRAME` draws at radius 4 (4, 2, 1, 1, 0), which
    reads visibly rounder beside it. That is why `HINT_BADGE` fixes the radius at
    `KDISP_BADGE_RADIUS` instead of taking it as an argument, and why `\x12` keeps its
    own radius for the run-dialog hint: **do not merge the two ops.**
  - ⚠️ **A style argument can never be 0** — these are `U"…"` strings, so a 0
    codepoint terminates them. Hence outline = 1, solid = 2.
  - ⚠️ **`kdisp_draw_round_rect()` CANNOT draw the released state, and two attempts to
    make it shipped wrong.** Its Bresenham arc renders a radius-2 corner as insets
    **1,0** where the scanline formula gives **2,1,0** — so the two disagree about what
    "r = 2" *looks like*, and the outlined badge came out squarer than the solid one
    even though both asked for the same radius. Stroking the 2px border as two nested
    Bresenham rects is worse: the outer arc's pixel and the inner rect's first pixel
    sit two apart, leaving a **1px hole in every corner**.
  - `kdisp_draw_badge_rect(x, y, w, h, r, border)` draws both states from ONE scanline
    fill (`border` 0 = solid, else a ring that thick), so the engaged badge is exactly
    the released one with its middle removed — they cannot drift apart. Per-row inset
    is `r - floor(sqrt(r² - d²))`; `r ≤ 4` in every caller, so the integer-sqrt loop is
    a few iterations, no float and no table.
  - ⚠️ **The HOLE keeps a 1px corner nick, which a true concentric offset does not
    give.** Offsetting inward by `border` implies an inner radius of `r - border`, and
    at `r == border` that is a perfectly square inner corner — one pixel short of the
    baked `ICON_CAPSLOCK_OFF`, whose hole still insets 1 on its first row. Reported
    from hardware as "it misses a single pixel on the inside corner", so the radius is
    floored at 1 whenever the outer corner is rounded at all. Only the released state
    has a hole, which is why the engaged one was right throughout.
  - **Verify a drawn badge against the baked glyph as ASCII, not as a render.** The
    radius error was invisible at 1× and obvious the moment both were dumped as
    character grids and the corner insets compared row by row.
  - ⚠️ **`HINT_ERASE` restores the PREVIOUS `s_gfx_erase`, not `false`.** It is a
    static plotter mode, so leaving it on blanks every keycap drawn after this one in
    the same pass — and a caller may already be mid-erase (the inverted-keycap
    pattern), which a hardcoded `false` would clobber. ⚠️ It used to cover **only the
    text paths** — `\x0F`/`\x11`/`\x15`/`\x13`/`\x12` composite through their own
    primitives, which plotted unconditionally — so `HINT_ERASE` before a HALF or a
    BADGE silently drew it lit. Fixed by the same choke-point move as the jitter
    offset: every ink primitive now plots through **`kdisp_plot_ink()`**. See the
    plotter-mode note below.
  - The arrow **alone** was rendered first and is too sparse to identify, and the
    Caps/Num badge glyphs could not be borrowed because they carry a literal `A` / `1`.
- **Pause spells the word out**, at half the **L legend tier** (`0xF3000`) — 14 px
  caps, 56 px wide, the largest that still fits the 72 px panel. `HINT_SMALL` halves
  whatever face the glyph comes from, so the size is chosen by picking WHICH face: the
  resident 27 px base halves to 10 px caps (41 px), M to 12 px (49 px), L to 14 px
  (56 px); the full 27 px face would need 106 px. Two solid U+275A bars were tried
  first and read as ambiguous.
  - ⚠️ **This is the one legend on the layer that needs the `latinbig` bundle** rather
    than `symbol`/`emoji`. A missing glyph makes `kdisp_write_gfx_char_half` draw
    **nothing** — unlike the full-size writer, which substitutes `'!'` — so with no
    font pack the keycap is blank, as its whole row already is (every neighbour is a
    pack glyph too). Drop back to the base face if that stops being acceptable.

**`HINT_SMALL` (`\x10`) is what makes small TEXT possible on a keycap, and it is not
`HINT_HALF`.** The three standalone UI faces (`_Small_` 15 px, `_Mid_` 19 px, `_Nano_`
10 px) are **not in `g_all_fonts`**, so no codepoint can reach them — the resident
latin face has exactly one size, and `latinbig` only goes *bigger*. So a smaller face
has to be synthesised at draw time.

- `HINT_HALF` (`\x0F`) could not do it: it takes the **literal top-left of the ink**
  and **does not advance the cursor**, because it exists to composite one icon into a
  hint. Spelling a word with it needs a `HINT_MOVE` per letter, with the top-left
  computed per glyph — 20 codepoints for "Pause", and each MOVE is an absolute buffer
  position that `kdisp_gfx_text_bbox()` cannot measure.
- `HINT_SMALL` instead latches a mode for the **rest of the string**, and
  `kdisp_write_gfx_char_half()` keeps `kdisp_write_gfx_char`'s baseline and advance
  semantics — only the glyph's own offsets and extents are halved, never the baseline.
  So `U"  \x05\x05" HINT_SMALL U"Pause"` centres itself with ordinary full-size
  spaces, exactly like the `U"  " ICON_*` legends beside it.
- ⚠️ **Halve offsets with FLOOR, not C truncation.** `xOffset`/`yOffset` are negative
  (above the baseline) and `/2` rounds toward zero, which puts lowercase 1 px off the
  run's baseline. `half_floor()` is written out rather than `>> 1` because a right
  shift of a negative value is only arithmetic by implementation guarantee.
- There is deliberately **no "back to full size" op** — the one use is a legend that is
  entirely small text, and a toggle is a second thing to get wrong. `\x18` (reset) does
  not clear it either; it resets the cursor only.

**`HINT_MID` (`\x16`) is the other direction, and the only size BETWEEN the two.**
`HINT_SMALL` synthesises a smaller face by halving; `HINT_MID` reaches the real
standalone **`_Mid_` 19px** one (~14px caps against the keycap face's ~20px), for
the rest of the string. It exists because the settings labels at half the 27px face
were reported as too small to read at a glance (2026-08-26).

- **It is a SINGLE-font array on purpose** — `kdisp_write_gfx_char` baseline-aligns
  by `font->yAdvance - fonts[0]->yAdvance`, so making the face its own `fonts[0]`
  makes that adjustment 0, the same reason the language flags draw through
  `{ &flag_font }`.
- **It falls back to the caller's pool PER GLYPH.** The mid face is ASCII-only
  (0x20..0x7E), so anything outside it — an icon — renders at its normal size
  instead of `'!'`. That is what makes a **word-over-icon** legend possible at all
  (the layout picks: a name over its on/off switch). The bbox mirrors this, and the
  baseline reference must follow the **per-glyph** choice, not the latch: `fonts[0]`
  is the mid face only for the glyphs the mid face supplied.
- It is what let a bespoke `else if (keycode == KC_EDEN)` branch be **deleted** from
  `update_displays()` rather than replaced — the size that branch existed to reach
  is now expressible in an ordinary legend.

⚠️ **TWO full-size TEXT lines CANNOT fit a 40px keycap, and the descender budget is
exactly ONE — which is what decides between the two MID stacks.** `\v` advances a
fixed 15px while the keycap face inks ~20px above the baseline, so two plain
`\r\v` text lines overlap outright; at the mid face they fit, but only just.
Nineteen legends shipped overlapped this way for a long time (Store/EE, Word/sel,
Line/join — which also lost **55px** off the panel — App/sw and the twelve OS
auto/pin cells), all now half-scale.

- **`MID_TWO_LINE(top, bottom)`** — lift 10px / push 6px — spends the descender at
  the **BOTTOM**. Values like `Teng`, `Amiga` and `Jittr` fit; the top may have
  **neither a descender nor an ascender**, which is why the labels are `IDLE:` /
  `SCRIPT:` / `RESET`. All caps is how that is guaranteed, not the rule itself.
- **`MID_TWO_WORD(top, bottom)`** / **`MID_WORD_OVER_ICON(word, icon)`** — lift 8px /
  push 8px — spend it at the **TOP**. An ascender fits (`Mods`, `Cmds`, `Colemk`);
  the bottom must not descend.
- ⚠️ **They cannot be merged.** Swapping the two spacings breaks four legends in
  each direction — measured by sweeping every (lift, push) pair, not reasoned.
  Re-measure rather than eyeball whenever a word changes.

⚠️ **`kdisp_gfx_text_bbox()` did not know the display-list ops at all, and that was a
real bug the moment a MAIN legend started using them.** Every op byte *and each of its
argument codepoints* fell into `default:`, matched no font, and was substituted with
`'!'` — so a legend carrying one `HINT_MOVE` measured **three** bogus glyphs. That box
feeds `plan_main_legend()`'s shift-preview layout and `roll_idle_offset()`'s jitter
travel, so it was luck rather than design that nothing visibly broke (none of the
affected keys has a shift preview, and none is drawn at idle). The ops are mirrored
now: `\x10` switches the measurement to half-scale, `\x16` to the mid face,
`\x0F`/`\x11` consume one argument, `\x0E`/`\x12` consume two, `\x13` three.
Since #238 the interpreter (and the glyph resolver) live in pure
**`base/font_lookup.c`** — `kdisp_gfx_text_bbox_in()` takes the HINT_MID pool as a
parameter, `disp_array.c` keeps `kdisp_gfx_text_bbox()` as the wrapper binding the
resident mid face, and `make test:polykybd_font_bbox` (34 tests) pins the whole
op-argument table, the SMALL/MID semantics and the baseline-shift rule.
- ⚠️ **`\x0E` (MOVE) skipped NO arguments until 2026-08-26, and that sentence above
  was FALSE for it** — it fell through to `\x14`'s bare `break`, so a MOVE's two
  coordinate bytes were dispatched through the same switch on the next iterations.
  A coordinate is an arbitrary byte: **13 of the 31 `HINT_POS_*` / `HINT_SZ_*` /
  `MTB_*` macros carry one that is also an op** — `HINT_SZ_STOPSQ` is (15,15), i.e.
  `\x0F \x0F`, two HALFs; `HINT_SZ_SCRBOX` is (19,19), two BADGEs; `HINT_POS_SCRBOX`'s
  y is `\x06`. Ten of those predate the ops added in 2026-08 and cost only a
  mis-measured glyph, but the newer `\x16` latches a different FONT for the rest of
  the run and `\x15` eats the next two codepoints — the Ctrl mod-badge hint measured
  x1..30 where it is really x0..47. Skipping the arguments (what `\x12` two lines
  below always did) closes the class for the existing ops **and any op added later**.
  ⚠️ **So check a new op byte against those macros' argument bytes** — or rather,
  don't have to, now that MOVE consumes its own.
- ⚠️ **`kdisp_gfx_text_bbox()` still ignores the MOVE itself** — it works relative to
  the draw origin and MOVE names an ABSOLUTE buffer position, which is not knowable
  without one, so a MOVE'd legend's box only covers the part laid out relatively (and
  the composite ops contribute no extent at all — measuring them at an unresolvable
  cursor would be worse than skipping them). **Prefer the ordinary cursor advance over
  a MOVE in a main legend** — that is why `ICON_MUTE` places its X with `\f\f` and the
  glyph's own `xAdvance` rather than a MOVE.
  - ✅ **There IS now a form that resolves it: `kdisp_gfx_text_bbox_abs()`**, which
    takes the draw origin and returns the ABSOLUTE buffer box — MOVE resolved, and
    `\x0F`/`\x11`/`\x15`/`\x13`/`\x12` measured at their real extents. The relative
    form is unchanged to the byte (both are one walk in `bbox_walk()`, parameterised
    by origin), so its 34 existing tests still pin it. **Use the absolute one whenever
    you need to know where ALL of a legend lands** — which is exactly what the idle
    jitter needs, and what it did not have (below).
- ✅ **TWO bbox-vs-draw asymmetries were FIXED 2026-08-29 — both made the measured
  box describe glyphs the draw would not produce.** Worth knowing they existed,
  because the shape recurs: this function and the draw resolved glyphs by two
  different routes, so they could disagree without either looking wrong.
  1. **The `'!'` substitution had no `small` guard.** The measure path did
     `if (!f) { f = pool[0]; …; ch = U'!'; }` unconditionally, while the SMALL draw
     (`kdisp_write_gfx_char_half`) does `if (glyph == NULL …) return 0;` — no ink and
     **no advance**. So a `HINT_SMALL` run containing an uncovered codepoint measured a
     half-`'!'` *and* spent an advance the draw never spends, putting every following
     glyph at the wrong x.
  2. **The scan did not skip 0x0 GAP records.** It was a bare
     `if (ch >= first && ch <= last) { f = pool[i]; break; }`, where every draw path
     goes through `kdisp_gfx_glyph_font`, which skips a `{0,0,0,0,0,0}` padding record
     so a later font wins. A codepoint inside a padded span — Pashto letters under
     `_PerArab_`'s wider range — measured the empty gap while the draw resolved a real
     glyph from the next font.
  **The fix is one line of intent: resolve through `kdisp_gfx_glyph_font`, the same
  lookup the draw uses**, and skip the codepoint instead of substituting when a SMALL
  run finds nothing. Don't reintroduce a private range scan here — that is what made
  (2) possible, and a second resolver can always drift from the first.
  - **It mattered because `plan_main_legend()` positions the main legend from this box
    and clamps it to the panel**, and `roll_idle_offset()` derives a glyph's idle travel
    from it — so a wrong box is a mis-placed or clipped legend, not just a wrong number.
    Reachability was narrow (a legend needs a missing glyph *and* a size op, or a gapped
    codepoint), which is why nothing had reported it.
  - ⚠️ **The old C suite passed over BOTH.** `GapRecordFallsThroughToTheNextFont` covers
    the *resolver*, not the bbox, and there was no missing-glyph-in-a-SMALL-run case at
    all — 34 tests, neither asymmetry visible. The two added with the fix
    (`SmallSkipsAMissingGlyphInsteadOfSubstitutingBang`, `FontBboxGapTest`) were
    confirmed to FAIL against the pre-fix file and pass after, with the other 34
    unmoved. **A suite that measures a resolver is not measuring its callers.**
  - ⚠️ **The host's Python mirror moves with this.** `PolyKybdHost`'s
    `oled_preview.Renderer` reproduces this function; it already skipped gaps (so (2)
    was never wrong there) but deliberately pinned the `'!'` substitution as C parity.
    ✅ **That pin has been INVERTED to match (PolyKybdHost#209, 2026-09-01)** —
    `Renderer.bbox()` now skips an unresolvable glyph in a `HINT_SMALL` run too, and
    its test is ported from `SmallSkipsAMissingGlyphInsteadOfSubstitutingBang`.
    ⚠️ **The general point outlives this instance: a cross-repo parity pin is a
    LIABILITY the moment one side moves, because nothing fails when it goes stale.**
    Neither suite would have gone red — the host would simply have been wrong in the
    opposite direction, silently. Measured after the fix: of the host's 197 static
    legends, 12 use `HINT_SMALL` and **none** carries a glyph the pool cannot resolve,
    so nothing rendered differently either way, which is exactly why only a written
    note could have caught it. **When you pin parity, name the change on the other
    side that would invalidate the pin.**
**The legend-size key is now ONE key that states its own tier.** `KC_GLYPH_SIZE_UP` on
`_UL` draws `ICON_FONT_BIGGER` plus the current tier as a digit in the top-right;
holding **Shift** swaps the icon to `ICON_FONT_SMALLER` and reverses the step, so the
`KC_GLYPH_SIZE_DOWN` keycode survives but is bound nowhere.

- ⚠️ **The legend lives in `to_static_text()` (`poly_keymap.c`), NOT in
  `keycode_to_static_text()`.** Both halves of it are **synced** state — the tier from
  `poly_sync_t.glyph_size` and the modifier from `poly_layer_t.mods` — and
  `keycode_to_static_text()` only receives `led_t`, so on the **slave** it would draw
  the master's tier with its own (always-clear) mods. Any legend that depends on a
  synced field belongs on this side of that seam.
- **The action reads the LIVE `get_mods()`, the legend reads the synced copy** — and
  that asymmetry is deliberate. The action runs on the master at the instant of the
  release and must follow the finger; the legend must render identically on a half that
  only ever sees the housekeeping snapshot.
- **Placement is measured** (`HINT_POS_SIZENUM` = buffer (85,25)): the digit inks rows
  3–23 and columns 86–98 of the 72×40 window, clearing both the 43 px icon (which ends
  at column 70) and the panel edge at 99. ⚠️ What makes room for it is that this legend
  carries **no leading pad space**, unlike the `U"  " ICON_*` legends beside it — with
  the usual two spaces the icon ends at column 61 and the digit will not fit.
- The legend contains a `HINT_MOVE`, so `glyph_size_remap()` bails and the key itself
  always draws at the small face. That is correct — it is a mixed icon cell, not a
  latin legend — but it means the size key does not demonstrate the setting it changes.

### Dynamic macros (`poly_macro.c`, HID cmds 36/37/38, protocol v15+)

A macro is text (or a short key sequence) stored on the keyboard, typed back on one
keypress, with a **label the keycap spells out** along its bottom edge. What is worth
knowing is the parts that are NOT what you would write from scratch:

- **Storage is QMK's own dynamic-macro buffer** — a run of NUL-terminated bodies at
  `DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR`, macro N found by counting N terminators. We did
  not invent a format; `dynamic_keymap_macro_get/set_buffer` already manage it.
- **The LABELS are a separate fixed-stride array**, carved off the TOP of the same
  region by shrinking `DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE` (config.h). Deliberately not
  inside the NUL-delimited buffer: a body is addressed by counting separators — fine
  once per keypress, wrong for something `render_key()` reads for every macro keycap
  on every refresh. Shrinking the QMK constant is what keeps them apart, since every
  upstream path bounds itself on it and so cannot reach the labels.
- ⚠️ **Playback is OURS and must stay a state machine.**
  `dynamic_keymap_macro_send()` runs the whole macro inline and spells its delay
  `while (ms--) wait_ms(1)`. On a single-controller board that is merely rude; here
  the same loop scans the matrix, drives the split UART, services USB HID and pushes
  72 SPI displays, so a macro with a half-second delay would freeze the board and drop
  the link. `poly_macro_tick()` runs at most ONE step per housekeeping pass and treats
  a delay as a deadline. **No time-slicing is needed** (unlike Eden): steps have to be
  SPACED anyway for the host to see distinct events, so the pacing IS the yield.
- **The wire format is QMK's send-string encoding, NOT Vial's extension of it** —
  `0x01 0x01/02/03 <kc>` tap/down/up, `0x01 0x04 <ascii digits>` delay. Staying on the
  base encoding means the buffer is still playable by `dynamic_keymap_macro_send()`, a
  real cross-check rather than a theoretical one. Cost: 8-bit keycodes, so no mod-taps
  or layer keys; every modifier is 0xE0..0xE7, so chords are fine.
  - ⚠️ **The byte ENDING a delay is NOT consumed** — send_string re-reads it as the
    next step. Consuming it silently swallows the character after every delay, which
    presents as "the macro drops a letter sometimes".
- **The decoding is pure in `base/macro_decode.c`** (a byte-reader callback; no
  quantum.h, no EEPROM, no timer), the same seam as `base/fw_up_verdict.c`: the
  arithmetic is the part with a bug history and it was only unreachable because it
  shared a function with the I/O. `make test:polykybd_macro_decode` — 23 tests,
  mutation-tested against 7 deliberate breaks, each caught by the intended test.
- ⚠️ **`clear_keyboard()` on abort.** A DOWN step leaves a modifier registered, and
  any key press aborts playback — without the clear the host auto-repeats a key
  nothing will ever release. Same rule as the FW-2 prompt and `doom_begin()`.
- **Swallowed in `process_record_user()`**, not left to the release edge — an `OSL()`
  layer re-dispatches a release-edge action up to three times, which for a macro means
  playing it two or three times over (§ "A release-edge action fires up to THREE
  times").
- **Labels live in a RAM cache on BOTH halves** (192 B). Partly speed, mostly
  necessity: the host writes macros to the master, so the slave's own EEPROM never
  sees one. The master pushes each label over the split link, ONE per housekeeping
  pass, clearing its dirty bit only on a real ACK — so the mask is its own retry queue
  and nothing has to detect "the link is up". ⚠️ Never inline in the HID handler:
  sixteen bridges of up to ten retries each is seconds of dead main loop on exactly
  the link that needed the retries.
  - It **multiplexes onto `USER_SYNC_DYNAMIC_KEYMAP_DATA`** with a private op byte
    (`POLY_KEYMAP_OP_MACRO_LABEL`) rather than spending one of the 32 transaction
    slots — that handler was already op-dispatched, the same trick the MRU snapshots
    and the doom mirror use on `USER_SYNC_OVERLAY_MAP_DATA`.
- **The keycap draws the index above and the label below**, mirroring
  `render_lang_flag_key`. The INDEX rather than a generic macro glyph: a generic glyph
  is identical on all sixteen keys, so it says "this is a macro" and nothing else,
  while the index says which one and needs no font pack.
  - ⚠️ **Truncate by MEASURED WIDTH, never by character count.** Measured against the
    shipped `_Nano_` face: `WWWWWWWW` is exactly 72 px (8 chars) and `iiiiiiiiiiii`
    is 34 px (12 chars) — an estimate is wrong in both directions.
    `PolyKybdHost/tools/macro_label_preview.py --check` renders the keycap the way
    `render_macro_key()` composes it and counts pixels outside the 72×40 window (320
    cells, 0 clipped). Its measurement lives in the Qt-free
    `polyhost/services/macro_label.py` because the host editor shows the same
    truncation while the user types, and an approximation would disagree with the key.
- **Nothing binds `QK_MACRO_*` in the default keymap.** A macro key is assigned from
  the host's layout editor, so a user who never opens it pays nothing — and `via.c` is
  the only core dispatcher for that range, which we do not compile, so the keycodes
  are ours outright.
- ⚠️ **A PREVIEW THAT MIRRORS THE IMPLEMENTATION AGREES BY CONSTRUCTION — it cannot
  catch a placement bug, and this is the limit of the repo's "verify by rendering"
  rule.** `draw_macro_mark()` first drew a chosen icon at its native size or skipped
  it. A pack emoji inks **26–39 px** while a captioned keycap leaves about **29 rows**
  above the label, so measured over the icons the host picker offers, **four in five
  drew nothing at all**. `macro_label_preview.py` models the same placement, so it
  showed the same nothing — the field report was *"after selecting the icon I cannot
  see it in the preview and also not on the keyboard"* (2026-08-27), and neither half
  could contradict the other. Rendering only proves the C and the Python agree; the
  check that would have caught this is **measuring the glyph against the space it has
  to fit**, which is a different question and needs the real font metrics. The fix
  halves an overflowing icon through `kdisp_draw_glyph_half_at` (2×2-OR, which keeps
  the thin strokes plain decimation loses; half of even the tallest pack glyph is
  ~20 px) and falls back to the index when it fits at no size — the same fallback a
  missing glyph already took, so a keycap can never end up unmarked.
- ⚠️ **A guard whose precondition NOBODY ESTABLISHES is not a guard, and the comment
  claiming it holds is what hides that.** `poly_macro_start()` refuses to play a
  buffer whose last byte is not NUL (`poly_macro_buffer_intact`), and the case-37
  comment asserted this covered a half-streamed upload because "the host leaves the
  last byte clear until the final chunk". It does not: `join_buffer()` zero-fills to
  capacity, so the byte reads 0 **before** a write, **during** it and **after** it —
  the guard could never fire. An interrupted upload therefore left a *playable*
  splice, and the splice is made of the old macro:
  ```text
  before: "password123\0"   write: "hi\0" (interrupted)
  after:  "hi\0sword123\0"  -> macro 0 = "hi", macro 1 = "sword123"
  ```
  i.e. a fragment of a former macro becomes something a keypress types. Closed at
  **both** ends and the two are not redundant: the host raises a non-zero marker in
  the last byte *before* streaming and clears it with the final window
  (`write_macro_buffer`), and `poly_macro_write()` invalidates that byte on any
  window that does not carry it — the firmware must not depend on the host to arm its
  own integrity guard, and the host half is the one a mocked test can exercise.
  ⚠️ Consequence: a deliberate **prefix** write now leaves the buffer unplayable until
  something writes the tail. That is correct, and it is why the rig's prefix-write
  test restores the terminating NUL.
- **Cost: 208 B of RAM** (the 192 B label cache + the playback state), 0 B of EEPROM
  beyond the reclaim above. ⚠️ Verified against the **monolithic `POLYKYBD_DOOM=yes`**
  flavour, which PR CI does not build and which is the first thing to fail on any RAM
  growth: `.heap` 3828 → **3620 B** free. Re-measure there, not on the pack build,
  before adding another static.

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

### Unit tests (`make test:<name>`)

QMK has a googletest harness; a **standalone** test (one that links a subsystem
against mocks, rather than booting a whole fake keyboard) is the right shape for
module code. `quantum/wear_leveling/tests/` is the model to copy — it mocks its
backing store the way a driver test should mock its bus.

```bash
git submodule update --init --depth 1 --no-recommend-shallow lib/googletest  # needs add_repo qmk/googletest first
export QMK_HOME=$PWD && export PATH="/root/.qmk_venv/bin:$PATH"
make test:polymod_ltr559          # ~1 s, 19 tests — the LTR-559 driver vs a mock I2C bus
make test:fw_up_verdict           # ~1 s, 27 tests — the flash-staging COMMIT decision layer
```

⚠️ **`make test:<name>` with a name that is not in `TEST_LIST` exits 0 and prints
NOTHING** — no "unknown target", no test output, just a clean prompt. It is the same
silent-green failure the CI workflow's zero-suites guard exists to catch, one level
down and with nothing guarding it: `make test:os_hints` "passed" twice before the
missing output was noticed (2026-08-18); the suite is `polykybd_os_hints`. **Judge the
run by the `[  PASSED  ] N tests.` line, never by the exit code** — a real run always
prints one. `grep -rn "TEST_LIST +=" --include=testlist.mk .` is the authoritative
list of registered names — they live in `base/tests/testlist.mk`,
`hints/tests/testlist.mk` and each `modules/polykybd/polymod_*/tests/`; neither a
count nor an enumeration is kept here, because this sentence once listed four
suites while eleven existed.

✅ **These run in CI — via `polykybd-unit-test.yml`, NOT upstream's `unit_test.yml`.**
That distinction is the whole point: upstream's workflow filters on `builddefs/ quantum/
platforms/ tmk_core/ tests/`, and a PolyKybd change touches none of them, so for as long
as these suites existed **CI never ran a single one of them** — 46 tests, hand-run only,
on a PR board that otherwise looks comprehensively green. Do **not** "fix" that by adding
our paths to `unit_test.yml`: it is stock upstream and would conflict at the next
catch-up merge.
- **The suite names are DERIVED, not hardcoded.** The workflow greps every
  `*polykybd*/testlist.mk` that `builddefs/testlist.mk` includes and reads their
  `TEST_LIST +=` lines, so **a third suite needs no workflow edit** — register it in the
  two builddefs files (see below) and CI picks it up. Same reasoning as
  `sync_is_link_fault()` refusing to enumerate its siblings: a list that must be kept in
  sync is a list that goes stale silently.
- ⚠️ **It fails when it discovers ZERO suites**, and again if the loop runs zero. A test
  job that quietly runs nothing and reports green is strictly worse than no job — it is
  the exact failure this workflow was added to remove, so it must not be able to
  recreate it one level up. The loop also continues past a failing suite, so one run
  names every broken suite rather than just the first.
- ⚠️ **`ghcr.io/qmk/qmk_cli` runs steps under POSIX `sh` (dash), not bash** — the log
  header says `shell: sh -e {0}`. Write every `run:` in that container POSIX-clean, and
  **test it with `dash`, not your login shell**; `bash script.sh` passing proves nothing.
  Two traps, and the second is the dangerous one:
  - `done <<< "$list"` is a bash **herestring**: dash won't parse it at all —
    `Syntax error: redirection unexpected`, exit 2. Loud, so it is the good case, but
    it is what made this workflow's first run red.
  - `printf … | while read` is the tempting POSIX fix and is **worse**: it parses, but
    POSIX runs the loop body in a **subshell**, so counters and accumulators are
    discarded at the `done`. A failing suite would leave the failure list empty and the
    job would report **green** — which is why the count-zero guard above is not
    redundant paranoia; it is the only thing that catches this.
  - `for t in $LIST` (unquoted, from an `env:`) keeps state in the current shell and
    works in every POSIX shell. Safe here because suite names are makefile identifiers
    — the harness rejects even a `-`, so they can never contain whitespace.

- **`fw_up_verdict` is the pattern for testing DECISION logic** (as opposed to
  `polymod_ltr559`, which is the pattern for a driver vs a mock bus). It covers the
  COMMIT-failure classification, the ack vocabulary, the STATUS-snapshot CRC guard and
  the font-pack status byte. **Getting it testable required decoupling first, and that
  decoupling was worth doing on its own terms** — the two smells it exposed:
  - `split_sync.h` conflated a **protocol contract** (4 ack bytes + `sync_succeeded`)
    with the sync **payload structs**, which need `config.h`/`state.h`/`mru.h`. So
    asking "is this ack a success?" pulled in the whole keyboard config, and
    `sync_succeeded()` — the helper guarding every `send_to_bridge` call site, and
    itself the subject of a field bug — had no test for years. The vocabulary now
    lives in dependency-free **`base/sync_ack.h`**, re-exported by `split_sync.h` so
    all consumers are unchanged.
  - `fw_up_slave_refused_commit()` mixed RPC transport, CRC validation, the decision
    and `uprintf` in one function; the decision was the only part with a bug history
    and the only part unreachable. It is now pure in **`base/fw_up_verdict.c`**, with
    the I/O and the four diagnostic lines left in `split_fw_up.c`. This mirrors what
    the host repo does deliberately (`polyhost/core/decisions.py`,
    `decide_stale_bundles`, `classify_commit_reply`) — the firmware just never had
    the seams.
  - ⚠️ `base/fw_up_verdict.c` is listed in the **shared** `POLY_SRC` in
    `keyboards/polykybd/rules.mk`, NOT with the other `base/*.c` in the per-variant
    `rules.mk` — its consumer `split_fw_up.c` is in that shared list, so a variant
    that forgot the line would just fail to link.
- **A host fixture can never catch THIS end emitting the wrong byte.** PolyKybdHost has
  had `classify_commit_reply` under test for a while, but those tests encode the
  firmware's reply bytes as fixtures — so they only catch the *host* misreading a
  status, which is the opposite direction from the bug that actually shipped. Both ends
  of the font-pack COMMIT contract are now pinned: `fontpack_commit_status()` is a pure
  `static inline` in `hid_fontpack.h` (which is why that header now includes
  stdint/stdbool instead of `quantum.h` — nothing in it needed quantum.h).

Wiring a new one needs **two** registrations plus one non-obvious source list:

- `builddefs/testlist.mk` — `include <path>/tests/testlist.mk` (which does
  `TEST_LIST += <name>`). ⚠️ Test names **cannot contain `-`**; the makefile
  hard-errors.
- `builddefs/build_test.mk` — `include <path>/tests/rules.mk`, alongside the
  `quantum/*/tests/rules.mk` lines. This defines `<name>_SRC/_INC/_DEFS`.
  - ⚠️ **A test under `keyboards/` must NOT name that file `rules.mk` — CI reads it as
    a KEYBOARD.** `qmk ci-validate-keyboard-targets` globs `keyboards/**/rules.mk` and
    flags every hit whose path lacks a directory named `keymaps`, `common` or `lib` and
    which has no `keyboard.json` beneath it (`lib/python/qmk/cli/ci/
    validate_keyboard_targets.py`, 17 lines — read it, it is the whole rule). There is
    no exemption for tests, because upstream keeps none under `keyboards/`. The failure
    is `keyboards/polykybd/base/tests::Legacy target detected` and it is a **separate
    lint-job step from `qmk lint`**, so `qmk lint --strict` passing tells you nothing
    about it. `keyboards/polykybd/base/tests/` therefore uses **`test_rules.mk`**; the
    `build_test.mk` include names the file explicitly, so the name is free.
    (`testlist.mk` is unaffected — only `rules.mk` is globbed.) Cost a CI round
    2026-08-17.
  - ⚠️ **So run the WHOLE lint job locally, not just `qmk lint`.** The recipe in the CI
    section above already lists all of it; the two `ci-validate-*` commands are the
    ones easy to skip, and they are ~1 s each:
    ```bash
    qmk ci-validate-keyboard-targets && qmk ci-validate-aliases   # both must exit 0
    ```
- ⚠️ **A standalone test must put the timer in its own `_SRC`.**
  `platforms/common.mk` adds `platforms/timer.c` + `platforms/test/timer.c` to `SRC`,
  which only the **full-keyboard** harness consumes — so a standalone test links with
  `undefined reference to timer_read32 / timer_elapsed32` until you list both files
  yourself.
- ⚠️ **`set_time()` / `advance_time()` have no header.** They are defined only in
  `platforms/test/timer.c`; every test that drives the clock forward-declares them
  (see `quantum/sequencer/tests/sequencer_tests.cpp`).
- **Mutation-test the suite before trusting it — the `mutation-test-suite` skill is
  the recipe.** Break the thing on purpose (swap a
  byte order, delete a bound) and confirm the *expected* test fails — the same
  discipline as "verify against the rendered glyph shape, not a transform∘inverse
  round-trip". A suite that passes against a deliberately broken driver is measuring
  nothing. `fw_up_verdict` was validated this way against 7 mutations (dropped
  `!status_ok` guard, `recorded != SYNC_ACK` as the refusal test, probe outranking an
  explicit refusal, `sync_succeeded` as a blacklist, a 1-bit-spaced ack value, a slave
  refusal reported as retryable, and a CRC check that always passes) — each caught by
  the intended test.
  - ⚠️ **Strip ANSI escapes before grepping gtest output, or the mutation harness
    FAILS OPEN.** gtest prints `\e[0;32m[  FAILED  ]`, so a regex anchored on a leading
    `[` matches nothing and **every** mutation reads as "still green" — i.e. the
    harness reports the exact result that means "your tests are worthless", for all of
    them, which is itself the tell that the detector and not the suite is broken. Pipe
    through `sed 's/\x1b\[[0-9;]*m//g'` first. Cost a full round (2026-08-17); a
    single manual mutation is the 30 s way to confirm the harness works before
    trusting a sweep.
  - ⚠️ **A second fail-open path, same shape: a mutation that never APPLIED.** A
    failed `sed`/`perl` (a C `||` collides with `sed`'s `s|…|…|` delimiter) leaves
    the source untouched, the suite passes because nothing was broken, and the
    empty "caught by:" reads identically to "not caught". Assert the edit landed
    (`git diff --quiet` on the mutated file) before believing the run. Hit while
    dogfooding the skill, 2026-08-18.
  - ⚠️ **…and that `git diff --quiet` guard is itself fail-open unless it
    compares the PRE-MUTATION baseline.** Diffing against **HEAD** only works
    while the tree is clean: mutate code you have not committed yet — the normal
    case, since you mutation-test a suite right after writing it — and the guard
    sees your own feature diff, reports "changed", and passes for every
    mutation whether or not any applied. So the check meant to catch a
    non-applied mutation is exactly the one that stops working when you need it.
    Copy the file first and compare to the copy:
    ```bash
    cp path/to/src.c /tmp/base.c                      # pre-mutation baseline
    ...apply mutation...
    diff -q /tmp/base.c path/to/src.c >/dev/null; rc=$?
    case $rc in
      0) echo "MUTATION DID NOT APPLY - result meaningless" >&2; exit 1 ;;
      1) ;;                                           # applied, carry on
      *) echo "diff failed ($rc) - baseline unreadable?" >&2; exit 1 ;;
    esac
    ...run suite, restore with: cp /tmp/base.c path/to/src.c
    ```
    ⚠️ **The guard has to EXIT, not just print** — a third instance of the same
    family, caught in review of this very note (CodeRabbit, #221). A bare
    `diff … && echo "DID NOT APPLY"` returns 0 and the loop carries on to report
    the mutation as "not caught", with the warning buried in a screen of gtest
    output. And `diff` has **three** exit codes — `0` same, `1` differs, `2`
    could not read a file — so `else`-ing on "not 0" silently treats a missing
    baseline as a successful mutation. Hence the `case`.
    ⚠️ That restore **overwrites whatever is in the file**, and by this note's
    own premise the tree is uncommitted — so there is no git copy to recover
    from. Do not edit the source between mutating it and restoring it.
    Hit on the host repo 2026-08-20 (Python, same shape — the family is not
    C-specific). The run happened to be sound because every mutation *did* apply
    and turned the suite red, but that was luck: the guard could not have told
    me otherwise. **A fail-open guard that is only correct on a clean tree is a
    fail-open guard.**

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
    `.claude/skills/status-oled-layout/measure_bands.py 72` after any size change.
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

## Investigations in progress

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

> ## ✅ RESOLVED (2026-07-17): the split42 split-link saga — two compounding root
> causes, neither of which was the pointing feature or the boot delay.
> **Authoritative record: `keyboards/polykybd/split42/SPLIT42_LINK_STATUS.md`**
> (the per-boot test log, rows 1–24 + resolution summary). The narrative below this
> banner is the HISTORICAL investigation trail — its conclusions about
> `SPLIT_POINTING_ENABLE` being required, the `wait_ms(400)` being load-bearing,
> the "dead PIO1 RX-IRQ", and the heartbeat/dummy-transaction refutations are all
> **SUPERSEDED** (they were taken through an unrecognized hardware coin-flip).
> The two real causes:
> 1. **Hardware:** the split42-left v1.0 board leaves the flipped-orientation
>    link-USB-C data pads copper-orphaned behind U26 (ESD array). With U26's
>    bridge broken and both halves being left boards (no right boards were ever
>    fabbed), only **1 of 4 plug-orientation combinations** links — the source of
>    every works↔dead flip on identical firmware. Bench fix: reflow/populate U26
>    or bodge `USB2.8→6` + `USB2.5→7`; next-rev items in the hardware repo's
>    `SPLIT42_REDESIGN_NOTES.md`.
> 2. **Firmware:** with the orientation controlled, split42 needs exactly an
>    **8-byte pad at the pointing member's position in `split_shared_memory_t`**,
>    in front of the RPC buffers — now shipped explicitly as
>    `POLY_SPLIT_SHMEM_RPC_GUARD` (`transport.h`, tracked in
>    `UPSTREAM_PATCHES.md`). The pointing subsystem and the 400 ms delay were
>    both removed from split42. **Open follow-up:** find the latent writer the pad
>    guards against (canary plan in the status doc, row 24) — until then do NOT
>    remove the guard.

**Bisect result (2026-07-14): split42 needs `SPLIT_POINTING_ENABLE`'s periodic split
transaction — NOT the trackpad, NOT its I2C.** Two-stage bisect:
- RGB + pointing device + LTR-559 (`d74e7e11`) → **works**; drop only the pointing
  device (`b25f2045`) → **breaks**. ⇒ the pointing device is the required piece (RGB +
  LTR-559 were both on in the broken build, so they're cleared; kept on anyway,
  harmless when unpopulated).
- Enabling the pointing device had **two** effects: (1) an extra periodic master→slave
  split transaction (`SPLIT_POINTING_ENABLE` → the master pulls `GET_POINTING_CHECKSUM`/
  `GET_POINTING_DATA` from the slave every cycle, `quantum/split_common/transactions.c`
  `pointing_handlers_master/_slave`), and (2) a per-cycle slave I2C read that could stall
  up to `CIRQUE_PINNACLE_TIMEOUT` (20 ms) since GP0/GP1 aren't broken out. Swapping the
  Cirque driver for QMK's **no-op `custom` driver** (weak hooks do zero I2C) while keeping
  `SPLIT_POINTING_ENABLE` (`5de77192`) → **still works**. ⇒ the fix is **effect (1), the
  split transaction**, not the I2C stall and not the trackpad hardware.

**⚠️ SYMPTOM CORRECTED (2026-07-14, after careful hardware observation):** the earlier
"slave hangs mid-render / core1 hang" framing was WRONG (that was a misread of an
un-refreshed splash). The real symptom is a **split-link establishment failure at boot**:
with pointing disabled, the two halves (same image on both) can't talk — the master
retries split transactions, exhausts `SPLIT_MAX_CONNECTION_ERRORS` (200), **times out**,
then runs **solo** (the display stays on the boot splash until a **keypress** forces a
refresh to the default layer). It follows the **master role** (swap USB → the behavior
moves to the new master), not a physical half. Enabling the pointing feature makes the
link come up; it is deterministic (not a flaky race). Consequently the heartbeat test
result is NOT evidence about traffic — a housekeeping heartbeat can't rescue a link that
never *establishes*. The core1 / render-hang lines above are superseded for this bug.

**Resting config:** split42 keeps `SPLIT_POINTING_ENABLE` + `POINTING_DEVICE_DRIVER =
custom` (no-op) — same fix as the real trackpad but with no dead I2C on the un-broken-out
bus. **ROOT CAUSE STILL OPEN:** *why* the shared PolyKybd firmware depends on that
periodic slave-pull transaction. split72 always had it (real trackpad), which hid the
dependency. Leading theory: split42's only other regular master→slave traffic is QMK's
built-in matrix pull + the poly custom syncs (which fire on *diffs*), so on an idle
freshly-booted split42 the slave may go too long without being serviced by the transport
in the way the poly split state machine expects; the pointing transaction restores a
guaranteed every-cycle pull. **Heartbeat test (2026-07-14, `01cb83d0`) — REFUTES the frequent-pull theory.** Disabled
the pointing device entirely and instead drove an **every-cycle** master→slave pull over
the existing `USER_SYNC_SLAVE_DATA` channel (reused, so no new transaction / no shmem
change — pure traffic) from `housekeeping_task_user()`. Result: split42 **still breaks**.
So an every-cycle slave pull is **not** what split42 needs — the dependency is **not the
traffic/frequency**.

⇒ **The dependency is structural to *enabling the pointing feature itself*, or a
memory-layout coincidence.** Enabling `POINTING_DEVICE_ENABLE`+`SPLIT_POINTING_ENABLE`
does several things a reused-transaction heartbeat does NOT: (a) adds 3 transaction IDs
(`GET_POINTING_CHECKSUM`/`GET_POINTING_DATA`/`PUT_POINTING_CPI`) → shifts the USER
transaction-id numbering and bumps `NUM_TOTAL_TRANSACTIONS` (the poly table is near the
32 cap); (b) adds a `pointing` member to `split_shared_memory_t` → changes shmem
size/offsets; (c) links `pointing_device.c` + runs `pointing_device_init/_task` → shifts
image/RAM layout. Any of (a)–(c) could be the real cause, **including the possibility
that "enable pointing" merely perturbs memory layout and masks a latent bug** (a
stack/buffer/uninitialised-use error) — the same *class* of coincidence the I2C-timing
red herring was. **Resting fix stays `SPLIT_POINTING_ENABLE` + no-op `custom` driver
(`5de77192`)** — that is the last confirmed-working config; the heartbeat commit
`01cb83d0` is an experiment, to be reverted to `5de77192` if the investigation doesn't
supersede it. NEXT: get the exact failure symptom (slave-dead vs no-USB vs display vs
boot-hang), then discriminate (a)/(b) from (c) by adding a **dummy split transaction +
shmem member with no task** (tests transaction-count/shmem-layout alone) and by an
**`-Wl,-Map` layout/`.bss` diff** of the working vs broken image (tests the
layout-coincidence hypothesis). Do NOT ship split42 off `01cb83d0`.

**Transport-level findings (2026-07-14, cont.) — it's a split-link *establishment*
failure, and the transaction COUNT is ruled out.**
- **Master HID console (broken build):** `Split link: … crc_err=0 transport_fail=100.0%`,
  climbing to >1.2M frames all failing. So the QMK **serial transport is dead** — every
  frame times out at the transport layer; this is **NOT** payload/CRC corruption
  (`crc_err=0`), and the handshake token can't mismatch (`tid ^ NUM_TOTAL_TRANSACTIONS`,
  same image both sides). The master exhausts `SPLIT_MAX_CONNECTION_ERRORS` (200), gives
  up, and runs solo; the "stuck splash" is just the un-refreshed screen until a keypress
  forces `update_displays`.
- **Corollary:** a dead transport can't be fixed by the pointing *transactions* riding it,
  so enabling pointing must fix the transport via a **side effect**.
- **Transaction count RULED OUT (`e260bcd4`):** registered **3 dummy split transactions**
  (no pointing) so `NUM_TOTAL_TRANSACTIONS` matched the working build — **still 100%
  transport_fail**. So it is NOT the count / handshake token / transaction-table size.
- **Memory layout ruled out earlier:** `.bss`/`.data`/stacks are within ~100 B and the
  stacks sit at identical addresses between working and broken (`5de77192` vs `01cb83d0`).
- **Narrowed to two candidates**, both present only when `SPLIT_POINTING_ENABLE` is set:
  **(b)** the `split_shared_memory_t` `pointing` member — it sits **immediately before the
  RPC buffers** (`transport.h`: `pointing` at line ~210, then `rpc_info`/`rpc_m2s_buffer`/
  `rpc_s2m_buffer`), so it **shifts the RPC buffers' offset** the poly `USER_SYNC_*`
  transactions transfer through; vs **(c)** merely linking `pointing_device.c` + running
  its init/task (a layout/init side effect). Discriminator flashed but not yet read back:
  **`0e04469d`** = `POINTING_DEVICE_ENABLE` with the no-op `custom` driver but **without**
  `SPLIT_POINTING_ENABLE` (pointing code linked/run, but no shmem member, no transactions).
  *Link revives → (c) code-linkage (coincidental); still dead → (b) the shmem `pointing`
  member specifically.*
- **Working config shipped for the repo:** PR **#144** (branch
  `claude/split42-working-all-subsystems`, cut at **`d74e7e11`** = RGB + pointing[Cirque] +
  LTR-559, confirmed working) captures the working split42 while this root-cause work
  continues on `claude/split42-literal-split72-copy`.

**⚠️ The DEFAULT branch (`PolyKybd`) regressed split42 again — TWO unreverted
experiment commits (found + FIXED 2026-07-15, confirmed on hardware).** After the
above work, the split42 on the `PolyKybd` tip was itself broken (a fresh build from
the default branch didn't come up). The working commit **`5de77192`** (FW 0.9.51,
`SPLIT_POINTING_ENABLE` + no-op `custom` driver) *is an ancestor* of the tip, so a
`git diff 5de77192..PolyKybd` restricted to split-relevant code isolated it — the
entire split **transport** (`serial_vendor.c`, `serial_protocol.c`, `split_sync.c`,
`bridge_helper.c`, `base/`) is **byte-identical** working↔tip, so it was neither a
transport nor a hardware regression. Two breaks, both from root-cause EXPERIMENT
commits committed straight onto `PolyKybd` and never reverted:
- **Break #1 (config):** `SPLIT_POINTING_ENABLE` was removed from
  `split42/config.h` by `01cb83d0` (heartbeat experiment) and only
  `POINTING_DEVICE_ENABLE` was re-added by `0e04469d` — leaving the exact broken
  **(c)** state the bisect condemned (pointing code linked, **no split transaction
  registered**). The whole `01cb83d0…0e04469d` experiment series (heartbeat, boot
  traces, `SERIAL_DEBUG`, 3 dummy transactions, pointing-code-only) landed on the
  default branch; only the final one's config state survived, and it was the broken
  one. **Lesson: revert experiment commits, or run them on a throwaway branch — do
  NOT commit a discriminator series onto the release branch and walk away.**
- **Break #2 (boot timing):** the progressive boot-splash rework (PR #138 + merges
  #143/#144: `show_splash_screen()` → `splash_progress()`, `boot_diag.c`) **removed
  the pre-init `wait_ms(400)` delay** the old blocking splash had (where it was
  purely a **logo dwell**, now served by the progressive reveal), deferring the
  dwell to the *end* of `post_init` and adding ~7 per-`post_init` keycap renders +
  a boot banner. Fine on split72 (real trackpad → robust link) but split42's marginal
  link (the dead PIO1 RX-IRQ) does not come up without that pre-init delay. This is
  the only live shared-code delta working↔tip (the other two diffs are inert: a
  `POLY_DUMMY_TXN` macro gated off, and a `POLY_KB_NAME` GET_ID string).
  - ⚠️ **The delay is EMPIRICALLY load-bearing but its MECHANISM is unknown — do NOT
    invent one.** A clean single-variable A/B on hardware (2026-07-15 eve) settled it:
    the working restore with **only** the `wait_ms(400)` removed (identical config,
    `SPLIT_POINTING_ENABLE` on) **fails** — the slave runs just its own local scan
    (keypress display inversion works) while the rest of the split link never
    establishes. So the delay genuinely fixes something on the link; *why* a boot
    delay affects it is not understood. An earlier version of this note (and the
    in-code comment) claimed a "settle window so the slave comes up before the master
    hammers transactions" — that was a **fabricated mechanism**, corrected here. The
    value 400 is inherited from the old splash (known-good, not a measured minimum).
  - **RULED OUT — it is NOT the pointing driver / per-cycle I2C (2026-07-15 eve).**
    Follow-up A/B: split42 built with the **real `cirque_pinnacle_i2c` driver** (like
    split72, so each pointing cycle does a real I2C read — GP0/GP1 aren't broken out so
    it just times out ~20 ms) **and no delay** → **still broken**, same symptom (no
    split link, slave only doing its autonomous local invert). So split42 needs the
    delay **regardless of pointing driver** (no-op `custom` and real Cirque both fail
    without it), and split72 needs no delay — the split72↔split42 difference that
    requires the delay is therefore **not** the pointing driver and **not** the
    per-cycle I2C activity. ⚠️ This also means the earlier bisect's "the fix is the
    split transaction, not the I2C stall" was tested *with the delay present*, which
    masked this — the I2C stall is now cleanly ruled out on its own. Leading remaining
    hypothesis (UNPROVEN): split72 simply **boots slower** (36 vs 21 keycap OLEDs/side,
    real RGB, bigger status OLED), and with the dead PIO1 RX-IRQ making establishment a
    *polling race*, a slower master boot wins the race — the 400 ms stands in for that.
    The proper fix is the IRQ-independent pre-poll (`claude/split42-link-diag-minimal`),
    not more delay tuning.
- **Fix (branch `claude/split42-fresh-rebuild-4m3vip`, restarted from the tip; one
  restore commit — PR #141 on it was closed unmerged, so the stale rebuild history
  was discarded per the restart-from-default procedure):**
  (1) restore the Cirque/pointing block + `SPLIT_POINTING_ENABLE` in
  `split42/config.h`; (2) restore the pre-init `wait_ms(400)` delay in
  `show_splash_screen()`, **split42-only** (`#if defined(KEYBOARD_polykybd_split42)`)
  so the progressive reveal is kept and split72 is untouched. Confirmed working on
  hardware 2026-07-15. The delay is a **stopgap** (empirically needed, mechanism
  unknown — see the ⚠️ above) — drop it once the IRQ-independent link fix lands (the
  deeper "why does the marginal link need the pointing transaction *and* a boot
  delay" root cause is still open; see the `claude/split42-link-diag-minimal`
  pre-poll work, where `poll_miss` — pointing OFF ≈500, pointing ON ≈2 — is the
  definitive probe of the dead RX-IRQ).

### Bug: second half of keyboard becomes unresponsive (slave stops sending key events)

**Symptom**: Intermittently, the right/slave half stops recognising keystrokes. Only keys on the master (USB) side still work. Reconnecting (replugging) or reflashing restores it. Happens "once in a while", not on every boot.

**Root cause identified (2026-04-29)**: Two separate EEPROM-write paths can block the slave's UART long enough to miss a split transaction response window, causing the master to declare the slave unresponsive. Both were introduced in commit `98ed47612d` ("eeprom refactoring — still needs testing", 2026-04-24). On the RP2040, EEPROM is wear-leveled flash; most writes are fast journal appends, but when the journal fills the firmware does a page consolidation (~50 ms blocking erase) — which is when the symptom occurs.

**Path 1 — blocked inside sync transaction handler (rare: only on default-layer change)**:
`eeconfig_update_default_layer()` was called directly inside `user_sync_layer_data_handler()`, a split UART transaction callback. Blocking there guaranteed a UART timeout whenever `def_layer` changed.

**Path 2 — blocked in housekeeping on the slave side (normal typing, brightness keys)**:
`mark_settings_dirty()` was called on the slave from `user_sync_poly_data_handler()` whenever the master synced a contrast or lang change (i.e. after any brightness key press). Five seconds later `brightness_save_if_pending()` fired on the slave in `housekeeping_task_user()`, writing EEPROM. The slave has no need to persist these values — the master is the authoritative owner and syncs them on every boot. This is the likely cause of occurrences during normal typing days with no layout switch.

**Fix applied (2026-04-29)**:
- `split_sync.c` `user_sync_layer_data_handler()`: replaced blocking `eeconfig_update_default_layer()` with `defer_default_layer_save()` — moves the flash write out of the UART transaction callback into housekeeping.
- `state.c` / `state.h`: added `defer_default_layer_save(layer_state_t)` and `default_layer_save_if_pending()`.
- `keymap.c` `housekeeping_task_user()`: added `default_layer_save_if_pending()` call on both sides.
- `brightness_save_if_pending()` was already deferred (5 s debounce, housekeeping) — no change needed there.

**Superseded (2026-06, PR #63 "unify emoji & language layers")**: persistence moved to a
**suspend-only dirty-flag model**. `defer_default_layer_save()` now just sets `g_def_layer_dirty`
(+ pending value) and the actual write is folded into the centralized `save_all_dirty()` in
`state.c`, which flushes every dirty block (settings / latin / default layer / MRU) at the real
flush points only: USB suspend (`suspend_power_down_kb`), the host shutdown signal
(`shutdown_user`), the firmware-update / `mcu_reset` paths in housekeeping, and the manual store
key (`KC_STORE_EE` → `request_eeprom_save` → `save_all_if_requested`). Consequently
`default_layer_save_if_pending()` was **removed** and is no longer called from
`housekeeping_task_user()` — do NOT re-add a per-housekeeping default-layer drain (that was the
old model and reintroduces the frequent in-housekeeping EEPROM write this very bug was about).
Base-layer changes apply immediately and persist on the next suspend/reset/store.

**How to confirm the fix worked**: reproduce by switching the default layer while typing on both halves. If the slave stays responsive, the sync-handler path is fixed.

**If the bug reappears after this fix**, the remaining risk is the RP2040 wear-leveling consolidation (~50 ms page erase) coinciding with a split UART transaction window, triggered by `brightness_save_if_pending()` firing in housekeeping 5 s after a brightness key press. This is a statistical coincidence, not a guaranteed block. Mitigations to try in order:

1. Also defer `save_user_latin()` in `user_sync_latin_ex_data_handler()` — still a direct EEPROM write inside a sync handler (triggered on language changes).
2. `eeprom_update_block()` in `dynamic_keymap_set_buffer_poly()` — also inside a sync handler, only during keymap remapping, lowest priority.
3. **Proper fix: offload EEPROM writes to core 1.** The keyboard already uses core 1 for RLE decompression via `multicore_exec.c` and the FIFO dispatch. Instead of calling `save_user_settings()` / `save_user_latin()` / `eeconfig_update_default_layer()` directly on core 0, post the write as a job to core 1 via the FIFO. Core 1 does the blocking flash operation while core 0 (QMK main loop, UART, USB) keeps running uninterrupted — eliminating the framing-corruption risk entirely. Main caveat: core 1 is currently single-purpose (RLE decompression), so the two job types must not collide; check that core 1 is idle before posting, or add a small job queue. EEPROM writes and RLE decompression are unlikely to overlap in practice since both are rare and burst-style.

**Relevant files**:
- `keyboards/polykybd/split_sync.c` — all `user_sync_*_data_handler` functions
- `keyboards/polykybd/state.c` / `state.h` — deferred-write helpers
- `keyboards/polykybd/poly_keymap.c` — `housekeeping_task_user()`

---

### Bug: key displays turn on when keyboard is suspended/sleeping

**Symptom**: Per-keycap OLED displays briefly light up (or stay lit) when the keyboard should be in suspend/sleep state.

**Root cause identified (2026-04-29)**: `poly_suspend()` in `keymap.c` clears `STATUS_DISP_ON` and `DISP_IDLE` but did not clear `IDLE_TRANSITION`. If the keyboard was in the fade-out phase (IDLE_TRANSITION set in local_state but not yet propagated to global_state) when USB suspend was triggered, `sync_and_refresh_displays()` — called immediately after `poly_suspend()` from `suspend_power_down_kb()` — detects `back_from_idle_transition = true` (IDLE_TRANSITION in local but not global) and restores `contrast = ee.brightness` from EEPROM, overwriting the `DISP_OFF` value `poly_suspend()` had just set. This triggers `contrast_changed = true` → `set_displays(ee.brightness, false)` → `kdisp_enable(true)` on both master and slave — keycap displays turn on for one suspend cycle before the next iteration corrects it.

**Fix applied (2026-04-29)**: Added `IDLE_TRANSITION` to the flags cleared in `poly_suspend()`:
```c
local_state->flags &= ~((uint8_t)STATUS_DISP_ON) & ~((uint8_t)DISP_IDLE) & ~((uint8_t)IDLE_TRANSITION);
```

**If the bug reappears**: Check whether a split transport failure (the other bug above) is preventing the suspend state from reaching the slave — if the slave never receives `STATUS_DISP_ON=0` it will keep its displays on indefinitely. The two bugs can look identical from the outside.

**Relevant files**:
- `keyboards/polykybd/poly_keymap.c` — `poly_suspend()`, `suspend_power_down_kb()`, `sync_and_refresh_displays()`
- `keyboards/polykybd/base/com.h` — flag bit definitions (`STATUS_DISP_ON`, `IDLE_TRANSITION`, `DISP_IDLE`)

---

### Bug: core1 hangs whenever overlay/ROI data is processed (post-merge regression)

**Symptom**: After merging upstream QMK master into the `PolyKybd` branch (May 2026), the master half hangs whenever the host sends overlay/ROI data over HID. Simple HID commands (GET_ID, brightness, language) still work. Core0 pushes a `CORE1_CMD_*` to the FIFO successfully; core1 starts processing then stops mid-work; core0 blocks in its busy wait for `core1_decomp_count` to catch up, which never happens; that wait loop starves the USB main loop on master, freezing master entirely. Slave keeps running because slave is autonomous (its own scan loop) — slave keypress inversion still works while master is frozen.

**Status (2026-05-15): WORKAROUND APPLIED, ROOT CAUSE NOT YET IDENTIFIED.**

**Workaround (currently in tree)**: `__asm volatile("cpsid i" ::: "memory");` at the top of `core1_entry` in `multicore_exec.c`. Sets PRIMASK=1 — masks all configurable-priority exceptions on core1. Empirically eliminates the hang completely; ROI and DECOMPRESS commands process across many keys/mods with steady `tick` growth and matching counts. Safe because core1 in this codebase has no IRQ-driven work — `multicore_fifo_pop_blocking` polls FIFO_ST, doesn't need an IRQ to wake.

**What we know decisively**:
- `cpsid i` is the actual cure — replacing it with a pure memory clobber (`__asm volatile("" ::: "memory")`) does NOT fix the hang. So PRIMASK=1 is doing the work, not compiler ordering.
- An explicit clear of `NVIC->ICER[0] = 0xFFFFFFFF`, `NVIC->ICPR[0] = 0xFFFFFFFF`, `SysTick->CTRL = 0`, and `ICSR PENDSV/SysTick CLR bits` at core1 entry — without setting PRIMASK — does NOT fix the hang. So the offending exception is NOT one we can prevent by disabling/clearing the standard sources.
- The hang point is **deterministic per build** but **shifts with the workload per inner-loop iteration** (heavier loop body → earlier stop). E.g. tick=101 with full inner loop, tick=143 with writes stubbed, tick=493 in `rle_decompress`. That points to a wall-clock-time-driven event, not iteration count.
- Our state captures (live, sampled inside the inner loop on every iteration) consistently show `ISER=0`, `SysTick CTRL=0`, `ICSR.PENDSVSET=0`, `ICSR.PENDSTSET=0`, `VECTACTIVE=0`. The exception fires and clears between samples — invisible in pre/post snapshots.
- ICSR bit 22 (`ISRPENDING`) is set and ISPR shows many IRQs pending at entry (`0x818a61` = TIMER_IRQ_0, USBCTRL_IRQ, XIP_IRQ, PIO1_IRQ_0, DMA_IRQ_0, SIO_IRQ_PROC0, SIO_IRQ_PROC1, I2C0_IRQ). With ISER=0, none should fire.
- An override of `_unhandled_exception` (the ChibiOS weak fallthrough used by all unhandled vector entries) never fires (`core1_fault_signal=0`). So whatever fires has a *strong* handler installed elsewhere.

**The contradiction**: PRIMASK=1 masks configurable-priority exceptions (NVIC IRQs, SysTick, PendSV). It does NOT mask NMI or HardFault. cpsid eliminates the hang ⇒ the exception is maskable ⇒ SysTick / PendSV / NVIC IRQ. But we've ruled out all three at sample time. The exception must fire so briefly between our inner-loop captures that pending/active bits aren't observable, and the handler that runs must come from a *strong* override (not falling through to our `_unhandled_exception` shim).

**Top remaining suspect — ChibiOS context switch via NMI**. ChibiOS `ARMv6-M-RP2` port with `CH_CFG_SMP_MODE=TRUE` (set in `platforms/chibios/boards/GENERIC_RP_RP2040/configs/chconf.h`) and `CORTEX_ALTERNATE_SWITCH=FALSE` (default) uses **NMI as the context-switch vector** (strong `NMI_Handler` in `lib/chibios/os/common/ports/ARMv6-M-RP2/chcore.c`) and a strong `Vector80` (SIO_IRQ_PROC1) FIFO drain in the same file whose `CH_IRQ_EPILOGUE` triggers NMI via `__port_exit_from_isr` writing `ICSR.NMIPENDSET`. But: NMI is unmaskable by PRIMASK, so this *can't* be what cpsid is preventing — unless the chain is "Vector80 fires (IRQ 16, maskable) → handler triggers NMI". In that case, masking Vector80 (the IRQ) prevents NMI from being triggered. PRIMASK=1 would do that. Catch: `NVIC->ISER` bit 16 is consistently 0 in our captures, meaning Vector80 shouldn't fire. Either our capture has a timing gap that misses a transient ISER bit being set, or some other path triggers it.

**What's currently in tree (post-cleanup, 2026-05-15; updated 2026-05-16)**:
- `keyboards/polykybd/multicore_exec.c` — `__asm volatile("cpsid i" ::: "memory");` at the top of `core1_entry` (with an explanatory comment pointing at this doc). Also: `core0_decomp_count` changed from plain `static uint32_t` to `static volatile uint32_t` (real correctness fix — the compiler could otherwise hoist the load out of the wait loops).
- `keyboards/polykybd/base/multicore/core1.c` — `CORE1_STACK_SIZE` set to 384 (originally 256, briefly bumped to 1024 during the investigation, then sized based on measurement: peak observed ~164 bytes via the `CORE1_STACK_HWM` probe — see `keyboards/polykybd/readme.md` "For developers" → "Diagnostics"). The same file ships an `#ifdef CORE1_STACK_HWM` painting/walking probe that is off by default.
- All diagnostic instrumentation has been removed from `multicore_exec.c`, `base/overlay.c`, and `base/rle.c` apart from the gated HWM probe.
- **Reverted as not-actually-a-race (2026-05-16)**: an earlier cleanup added a `core0_decomp_count != core1_decomp_count` wait to `core1_roi_start()` framed as a "race fix". On re-analysis it was redundant — `CORE1_CMD_RESET_BIT_IDX` only mutates `core1_bit_index`, FIFO ordering guarantees any in-flight DECOMPRESS/ROI_UPDATE finishes atomically before RESET runs, and every caller immediately follows `core1_roi_start()` with `core1_update_roi()` which has its own wait + buffer-write + dmb + push. Now reduced to the bare `multicore_fifo_push_blocking(CORE1_CMD_RESET_BIT_IDX)`.

**Local divergence from SDK that may matter**:
- `keyboards/polykybd/base/multicore/core1.c` reimplements `multicore_launch_core1_*` locally and does NOT call `irq_init_priorities()` (the post-merge SDK version of `core1_wrapper` does, see `lib/pico-sdk/src/rp2_common/pico_multicore/multicore.c:89`). Unverified whether this matters — `irq_init_priorities` only sets `NVIC->IPR` priorities and doesn't enable IRQs, but the priorities affect handler interaction.
- The local `core1_wrapper` has `runtime_run_per_core_initializers()` commented out (function doesn't exist in post-merge SDK anyway).

**Vector address lookup completed (2026-05-15)**. With `cpsid i` reinstated (firmware working), captured the handler address at each vector slot from the live `VTOR=0x10000100` and resolved against the `.elf` symbol table:

| Vector slot | Captured addr | Symbol |
|---|---|---|
| NMI | `0x10011649` | `NMI_Handler` (ChibiOS RP2 port, `lib/chibios/os/common/ports/ARMv6-M-RP2/chcore.c:85`) — strong override; the context-switch handler when `CORTEX_ALTERNATE_SWITCH=FALSE` |
| HardFault, SVC, PendSV, SysTick (and all weak vectors 0x20–0x78) | `0x100002c7` | shared body in `lib/chibios/os/common/startup/ARMCMx/compilers/GCC/vectors.S` that does `bl _unhandled_exception` |
| SIO_IRQ_PROC1 (Vector80) | `0x10011721` | `Vector80` (ChibiOS RP2 port, same file:167) — strong override; drains FIFO_RD, calls `CH_IRQ_EPILOGUE` which can trigger NMI via `__port_exit_from_isr` writing `ICSR.NMIPENDSET` |
| `_unhandled_exception` | `0x10001858` | OUR strong override in `multicore_exec.c` that increments `core1_fault_signal` and infinite-loops |

`core1_fault_signal` stays at 0 across ALL tests. That proves no exception going through the weak `bl _unhandled_exception` shared body ever fires on either core — eliminating HardFault, SVC, PendSV, SysTick, BusFault, MemManage, UsageFault, and Vector20–78. The cure must therefore be masking one of the two strong overrides: **`NMI_Handler`** (unmaskable by PRIMASK — ruled out) or **`Vector80`** (NVIC IRQ 16, maskable by PRIMASK).

**That makes Vector80 the only candidate consistent with `cpsid i` being the fix.** The mystery: every live capture of `NVIC->ISER` reads bit 16 as 0, which says Vector80 should not be deliverable to core1. Either there's a transient enable between our inner-loop samples (e.g. something in the Vector80 handler chain re-enables itself, or a fast handler that runs and finishes between two captures), or RP2040 silicon delivers SIO_IRQ_PROC1 via a path that bypasses ISER (check the FIFO/SIO interrupt model in the RP2040 datasheet — there are NVIC `FORCE` registers and `IPSR` semantics worth re-examining). The chain we suspect: Vector80 fires → its `CH_IRQ_EPILOGUE` writes `ICSR_NMIPENDSET` (see `chcoreasm.S:142–147` `__port_exit_from_isr` for `CORTEX_ALTERNATE_SWITCH=FALSE`) → NMI fires on return → ChibiOS NMI handler runs context-switch logic on a core with no thread state → hang.

**Web search hint (pico-sdk issue #284, "Unable to disable FIFO_IRQ_PROC0")**: there's a known quirk where the SIO FIFO interrupt on RP2040 behaves abnormally — "FIFO_IRQ_PROC (15) keeps firing continuously, and disabling it from the NVIC seems to ignore it", plus "writing `1<<15` to NVIC ISER causes a hard fault". This is exactly the behaviour pattern that fits our observations (`ISER` bit 16 reads 0 in every sample, yet PRIMASK=1 is the only thing that stops the IRQ from being taken). It looks like an SDK / silicon oddity around the SIO FIFO IRQs, not something specific to PolyKybd. That makes `cpsid i` the right shape of fix — there isn't a cleaner per-IRQ disable available.

**If revisiting**:
1. Consider whether the local copy of `multicore_launch_core1_*` in `base/multicore/core1.c` should be replaced with the pico-sdk one (or at least updated to call `irq_init_priorities()`). Unclear it matters given the `cpsid i` mask, but it's a known divergence from the SDK.
2. Try setting `CORTEX_ALTERNATE_SWITCH=TRUE` in the polykybd chconf — that moves ChibiOS's context-switch handler from NMI to PendSV. Wouldn't change whether Vector80 fires on core1, but would make the trap go through PendSV (a maskable exception) instead of NMI, making the failure mode more predictable.
3. If the FIFO IRQ behaviour is investigated further, search for the RP2040 silicon errata / pico-sdk discussions around how `SIO_IRQ_PROC0` / `SIO_IRQ_PROC1` are enabled — they may need to be cleared/disabled via a peripheral-side register rather than NVIC alone.

**Separate but related issue surfaced during this debugging**: when the slave half is flashed with the same firmware as master, master → slave UART split-sync repeatedly fails ("Bridge sync retry … Failed to sync … for transaction UserCompressed / UserRoi"). Flashing slave with a *known-working* firmware (older) cleans up these retries. Deferred — this is a different code path (split_sync.c / split UART transport) from the core1 hang. Worth investigating but out of scope for the core1 fix.

---

### Bug: key display brightness drops to 0 on boot / wake (post-PR-#63 regression)

**Symptom**: Keycap OLED brightness intermittently comes up as 0 on keyboard start and after wake from suspend, without the user having set it to 0.

**Root cause (2026-06-10 — FIXED)**: PR #63's suspend-only persistence flushes `save_user_settings()` at exactly the moments `l_state.contrast` holds a *transient* value, persisting it as the user brightness:
- `suspend_power_down_kb()` calls `poly_suspend()` (sets `contrast = DISP_OFF`) **before** `save_all_dirty()` — a dirty flag set any time since boot persisted brightness 0.
- The slave was hit on *every* suspend: the master syncs `contrast = 0` before the flush, and `user_sync_poly_data_handler()` marked settings dirty on any contrast diff — including the suspend sync itself — then copied 0 into local state.
- The idle paths (`TURN_OFF_TIME` → `poly_suspend()`, fade transition, 0–49 pulsing) also leave transients in `contrast` that a later flush persisted.

**Fix**: `state.c` keeps a `g_user_brightness` snapshot that is updated **only** at deliberate set-points — `inc/dec_brightness()`, the new `set_user_brightness()` (used by the `KC_D*` preset keys and HID cmd 13), `note_user_brightness()` at boot-time EEPROM load, and on the slave when adopting an *awake* master's synced contrast (`contrast > DISP_OFF` and `DISP_IDLE|IDLE_TRANSITION` clear). `save_user_settings()` persists `~g_user_brightness` instead of `~l_state.contrast`. All idle/suspend *restore* paths (`back_from_idle_transition`, fade target, `display_wakeup()`, `suspend_wakeup_init_kb()`, HID stop-idle) now read `get_user_brightness()` instead of re-loading EEPROM — which also means an unflushed brightness change survives an idle/wake cycle (EEPROM was stale there under the suspend-only flush model). The suspend-only flush model itself is unchanged.

**Relevant files**:
- `keyboards/polykybd/state.c` / `state.h` — `g_user_brightness`, `set/note/get_user_brightness()`
- `keyboards/polykybd/split_sync.c` — `user_sync_poly_data_handler` awake-guard
- `keyboards/polykybd/hid_com.c` — cmd 13 (set brightness), cmd 15 (stop idle)
- `keyboards/polykybd/poly_keymap.c` — preset keys, idle/wake restore paths, boot seeding (shared by split72 + split42)

**Follow-up (2026-06-23): host-auto state now persists across reboots.** The
`g_user_brightness` model above keeps the *manual* brightness clean, but it is
**only** updated at deliberate set-points — host-auto/daylight (VOLATILE) pushes
never touch it. So once `g_user_brightness` held a low value (e.g. an old
pre-v5 host that pushed daylight values as plain *persisted* sets wrote a
night-time `2`, or the `KC_DMIN` preset), auto mode *masked* it at runtime but
every reboot re-exposed it: the keyboard boots in **manual** mode (auto is
RAM-only) at the stale `~g_user_brightness` until the host re-engages — "both
halves came up at 2 after a firmware reboot" (field, 2026-06-23). Fix: the
**host-auto mode + last auto value are now persisted** in the freed
`poly_eeconf_t.auto_brightness` byte (`pack_auto_brightness`/`load_auto_brightness`
in `state.c`, bit7 = mode engaged, **bit6 = a real host value is known**, bits0-5 =
value). The known bit is essential: engaging auto *before* the host pushes a value
must NOT persist the default `g_last_auto_brightness` as if real — else the next
boot snaps to it (the FULL_BRIGHT jump `get_active_brightness` guards at runtime).
On load, auto-on-but-not-known comes up in auto mode but falls back to the manual
brightness until the host pushes. `set_brightness_auto_mode` /
`set_auto_brightness_value` set `g_brightness_dirty` so the state flushes at the
next suspend/store; `keyboard_post_init_user` calls `load_auto_brightness()` so a
reboot while host-auto was engaged comes up at the **last auto value** (with
`g_auto_value_known` set) instead of the stale manual one — `set_displays()` now
uses `local_state->contrast` (the restored active brightness), not `ee.brightness`.
The stale `g_user_brightness` stays in EEPROM but is no longer shown while auto is
on. Old EEPROMs read the byte as 0 (auto off) — clean migration. ⚠️ This is the
**one** place an auto-derived value is persisted; it is kept SEPARATE from
`g_user_brightness` (the manual value), so the brightness-0 separation above is
intact. Also: the slave's `user_sync_poly_data_handler` adopt no longer
`mark_settings_dirty()` — it tracks the master's awake contrast in RAM (for
idle/wake restore) but never persists it (the master is authoritative and syncs
brightness every boot), so the slave can't independently bank a stale auto value.

---

### Bug: slave does not show overlay icons after MRU program switch until modifier change

**Symptom**: After the host switches to a new program using the MRU overlay path, the slave half's keycap OLEDs do not display overlay icons. Keys on the master half show correctly. A layer or modifier change (which triggers a full display refresh) makes them appear.

**Root cause (2026-05-17 — FIXED)**: Two missing `request_disp_refresh()` calls, plus `DISPLAY_OVERLAYS` not being included in `OVERLAY_SYNCED_STATE_FLAGS`.

**Fix 1 — slave mapping handler** (`split_sync.c` `user_sync_overlay_map_data_handler`): when the master bridges an overlay mapping chunk to the slave, the slave called `set_10bit_overlay_mapping()` (setting usage bits and pool→display mappings) but never called `request_disp_refresh()`. Added the call so the slave redraws after each mapping chunk lands.

**Fix 2 — master mapping handler** (`hid_com.c` case 21): symmetric gap — the master also called `set_10bit_overlay_mapping()` without a following `request_disp_refresh()`. Added it.

**Fix 3 — ESC (and any key in a later mapping chunk) not appearing** (`base/com.h`): The MRU host sends overlay mappings in chunks of 24 pairs per HID report. For programs with many overlays (e.g. an IDE with all A–Z + numbers), ESC (display_flat_idx=37) falls in the second chunk. Fix 1's per-chunk refresh fires after chunk 1 lands — at that point ESC's usage bit is still 0 — so ESC shows fallback text. Chunk 2 fires another refresh and should correct it, but this creates a transient window. The reliable fix: add `DISPLAY_OVERLAYS` to `OVERLAY_SYNCED_STATE_FLAGS` so that `enable_overlays()` (called by the host after **all** mapping chunks are confirmed ACK'd) force-syncs state to the slave via case 11. The slave detects `state_diff`, calls `request_disp_refresh()`, and renders with all chunks already in place — guaranteed final correct refresh.

**Relevant files**:
- `keyboards/polykybd/split_sync.c` — `user_sync_overlay_map_data_handler`
- `keyboards/polykybd/hid_com.c` — case 21
- `keyboards/polykybd/base/com.h` — `OVERLAY_SYNCED_STATE_FLAGS`

---

### Bug: one keycap's overlay missing on the SLAVE half after an app switch, fixed by switching away and back

**Symptom (field, 2026-08-01)**: intermittently one keycap on the link-side half
falls back to its plain legend while the rest of the app's overlay set renders
(observed on **Esc**, Explorer). Switching to another app and back fixes it.

**Root cause**: every phase of an app switch bridges to the slave with the return
value **DISCARDED** — prepare (cmd 11), image uploads (`fill_overlay.c`), mapping
chunks (cmd 21), enable (cmd 11). `send_to_bridge()` returns the slave's ACK byte
*or* `SYNC_CRC32_ERR` once its retries are exhausted, so a give-up was
indistinguishable from success: the master applied the change to its own tables and
moved on, halves diverged, **no log line anywhere**. ⚠️ This is the *discarding*
sibling of the documented "never bool-test `send_to_bridge()`" rule — classify
every ack with `sync_succeeded()`, including the fire-and-forget bulk sends.

A **mapping chunk** is the one that bites: it is **one-shot** — nothing re-fires it,
unlike the periodic state syncs where the diff *is* the retry queue — and the
slave's render gate is the usage bit that `set_10bit_overlay_mapping()` sets. So a
lost chunk blanks exactly the positions it carried. Esc is display index 37, which
at 24 pairs/report lands in **chunk 2** (the same "later chunk" position as the
2026-05-17 ESC bug).

**⚠️ The differential that identifies WHICH bridge dropped** — `resolve_upload_side()`
means the master keeps **no copy of an other-side overlay image** (`is_on_current_side()`
is false → the local `memcpy` is skipped, the bytes go only over the wire), while the
host's MRU cache records that image as resident and will **not** re-send it:

| lost bridge | symptom | recovers on next app switch? |
|---|---|---|
| prepare (reset) | previous app's icons | yes |
| **image** | blank/stale keycap | **NO** — MRU hit, never re-sent; sticks until the cache resets |
| **mapping chunk** | **missing icon, others fine** | **yes** — full mapping re-sent every switch |
| enable | *all* icons missing on that half | yes |

Self-healing therefore points at the **mapping**, and rules the image path out.

**Fix (2026-08-01)**: all four acks classified with `sync_succeeded()` + a named
warning. The mapping is additionally **repaired**: the master holds the authoritative
`overlay_map[]` + `use_overlay[]` (it applies every chunk locally either way), so a
loss arms a repair that rebuilds the slave's view from the master's own tables.
⚠️ The repair **drains from `housekeeping_task_user()`**, 2 reports/tick from a saved
cursor — **never inline in the HID handler**: a full mapping is up to 34 reports and
each bridge can burn 10 retries × the bridge timeout, i.e. *seconds* of dead main loop
on exactly the bad link that triggered the repair. The 10-bit packer is the inverse of
`set_10bit_overlay_mapping()`'s decode — verify any change to it by round-tripping
through that decoder, not by eye.

The two **image** bridges are checked and logged but deliberately **not repaired** —
per the table above the master cannot: it never had the bytes. Closing that would need
a master-side shadow copy (RAM it does not have) or a host-visible failure signal (a
protocol change). Do it only if the logs show it actually happens.

**Relevant files**: `keyboards/polykybd/hid_com.c` (cases 11, 21),
`keyboards/polykybd/fill_overlay.c` (`arm_overlay_map_repair`,
`overlay_map_repair_tick`, `resolve_upload_side`), `keyboards/polykybd/poly_keymap.c`
(housekeeping drain)

---

### Bug: slave half stuck in the idle pulsing frame — keypress/shift won't wake it, only a brightness key does

**Symptom (field, 2026-06-18)**: After the displays went into the idle *pulsing*
animation, the **slave** half froze on one pulse frame ("some keycaps off, others
very dim") and **did not update at all** — neither a keypress nor Shift brought it
back. The master woke normally and kept logging key events. Pressing a manual
brightness key restored the slave.

**Root cause**: `send_to_bridge()` returns the slave's reply ack **byte**, or
`SYNC_CRC32_ERR` once it exhausts its retries. **All three returns are non-zero**
(`SYNC_ACK 0xCA`, `SYNC_ACK_SIG 0x4D`, `SYNC_CRC32_ERR 0x35`), but three callers in
`poly_keymap.c` `sync_and_refresh_displays()` tested it as a bool —
`if(!send_to_bridge(...))`. `!0x35 == false`, so the failure branch
(`state_diff/layer_diff = false`, "failed to send") was **dead code**: on a
give-up the master fell through, ran `copy_global_state()`/`copy_global_layer()`,
**advanced `global` to `local`**, and so produced no diff next pass → the lost
sync was **never re-fired**. (The accompanying comment block — "the diff IS the
retry queue; global only advances on a successful sync" — described the *intended*
behaviour that the `!` test silently defeated.)

Why it only bit the *pulsing→awake* transition: the pulsing contrast changes every
housekeeping pass, so a dropped frame is replaced by the next fresh diff and is
invisible. **Wake-from-idle is single-shot** (`display_wakeup()` clears
`DISP_IDLE` + restores `contrast` once). If that lone sync's give-up was
mis-classified as success, the master stopped re-sending and the slave — which
only pulses because the master *tells* it to, the idle math is `is_usb_host_side()`
only — stayed on its last received pulse frame indefinitely. A brightness key
mutates `contrast` again → a brand-new diff → fresh send → recovery (matching "the
manual brightness control brought it back"). Also explains why no
`USER_SYNC_POLY_DATA failed to send` line ever appeared in the logs.

**Fix (2026-06-18)**: added `static inline bool sync_succeeded(uint8_t ack)`
(`split_sync.h`, by the `SYNC_*` defines) returning `ack == SYNC_ACK || ack ==
SYNC_ACK_SIG`, and routed all `sync_and_refresh_displays()` send sites through it
(POLY / LAYER / LASTKEY, plus the already-correct MRU send for uniformity). A
genuine give-up now keeps the diff so the send re-fires next pass, as the comments
always claimed. ⚠️ Never bool-test `send_to_bridge()` directly — every return value
is non-zero; classify it with `sync_succeeded()`.

**Relevant files**:
- `keyboards/polykybd/poly_keymap.c` — `sync_and_refresh_displays()` send sites; `display_wakeup()`, `housekeeping_task_user()` (the single-shot wake)
- `keyboards/polykybd/split_sync.h` — `sync_succeeded()` helper + `SYNC_*` values
- `keyboards/polykybd/bridge_helper.c` — `send_to_bridge()` (returns the ack byte / `SYNC_CRC32_ERR`)

---

### Bug: idle mode sometimes never starts; host "start idle" (cmd 15) is a no-op right after boot

**Symptom**: (1) Once in a while the keycaps never enter the idle
fade/pulse/turn-off animation at all — the displays just stay at full brightness
until suspend. (2) The host-side "start idle" HID command (cmd 15, payload ≠ 0)
does nothing when sent within the first ~2 minutes after the keyboard powers on —
the keyboard keeps waiting the full idle timeout instead of idling immediately.

**Root cause (2026-07-07 — FIXED)**: both trace to `base/update.c`'s activity
timestamp `last_update` being a **signed `int32_t` that overloaded a `uint32_t`
timestamp with sentinels** (`-1` = "idle tracking off"), and the housekeeping loop
gating idle on `if(get_last_update() >= 0)`.
- **(1) The 24.86-day sign-bit window.** `update_performed()` stores
  `timer_read32()` (a `uint32_t` ms counter) into the signed `last_update`. Once
  uptime passes ~24.86 days (`timer_read32() ≥ 2³¹`), that value reads back
  **negative**, so `if(update >= 0)` is false and the **entire idle/turn-off block
  in `housekeeping_task_user()` is skipped** — idle silently stops working for the
  ~25-day window until the 49.7-day `uint32` wrap. Intermittent, uptime-dependent →
  "sometimes it doesn't idle".
- **(2) Backdating underflow near boot.** `hid_com.c` case 15's "start idle" set
  `last_update = timer_read32() - FADE_OUT_TIME` to make idle begin one fade-out
  interval "ago". In the first `FADE_OUT_TIME` (120 s) of uptime `timer_read32() <
  120000`, so the signed subtraction went **negative and was clamped to 0** — which
  reads as "just became active", not "idle now", so the fade never triggered. (The
  code even logged `Starting idle in N msec` and then didn't.)

**Fix**: separate the "idle tracking enabled" state from the timestamp.
`base/update.c` now stores `last_update` as a real **`uint32_t`** plus a distinct
`bool idle_tracking` flag; `get_time_since_last_update()` uses `timer_elapsed32()`
(correct modular `uint32` arithmetic at any uptime, including across the wrap).
Housekeeping gates on **`is_idle_tracking()`** instead of the sign of the
timestamp, so idle works for the full 49.7-day timer range. The host "start idle"
path calls the new **`backdate_last_update(FADE_OUT_TIME)`** — modular
`timer_read32() - ms`, correct even when `now < ms`, so idle begins on the next
pass regardless of uptime. The old `set_last_update(-1)` "idle off" calls are now
the clearer **`disable_idle_tracking()`** (suspend / host display-off cmd 24 /
turn-off-reached); `set_last_update(int32_t)` is kept as a thin compat shim (`<0`
disables, `≥0` sets+enables). No wire-protocol change (cmd 15 payload identical),
so no `PROTOCOL_VERSION`/`__protocol__` bump.

**Relevant files**:
- `keyboards/polykybd/base/update.c` / `update.h` — `uint32_t last_update` +
  `idle_tracking`; `is_idle_tracking()`, `disable_idle_tracking()`,
  `backdate_last_update()`
- `keyboards/polykybd/poly_keymap.c` — `housekeeping_task_user()` idle gate
  (`is_idle_tracking()`), the turn-off + `suspend_power_down_kb()` disable calls
- `keyboards/polykybd/hid_com.c` — cmd 15 start branch (`backdate_last_update`),
  cmd 24 display-off (`disable_idle_tracking`)

---

### Bug: keyboard hangs on the boot splash after a firmware apply (slave not rebooted)

**Symptom (field, 2026-06-22)**: After a successful HID firmware flash + apply, the
master rebooted onto the new firmware but **hung on the boot splash** ("SPLIT 72");
no USB enumerated for minutes (`No Interface` in the host log) until the **slave
half was replugged**. Afterwards the split link showed a high steady error rate
(`err=36%`) because master ran new firmware while the slave still ran the old one.

**Root cause**: `CMD_FW_UP_APPLY` (`hid_fw_up.c`) tells the slave to install its
staged image and reboot in lockstep via `send_to_bridge(USER_SYNC_FW_UP_APPLY, …)`,
then arms the master's own reboot **regardless of the slave's ack**. That bridge was
sent with only **5 retries**, so one unlucky drop on this single critical
transaction left the slave on old firmware; the rebooted master then waits for a
slave handshake at split init that never comes → hang. (The master booting alone
into mismatched firmware is exactly why the apply bridges to the slave at all.)

**Fix (2026-06-22)**: bump the slave-apply bridge to **20 retries** and **re-fire the
whole round once** if it still hasn't acked. Safe: the slave apply handler is
idempotent (validates the staged image + arms a *deferred* reboot), and
`send_to_bridge` is **synchronous** (returns only after the slave handled the
message), so by the time the master proceeds to reboot the slave has already armed
its own. Worst case adds ~1 s, only on a bad link.

**Recovery if it recurs**: re-run the flash + **Apply** (re-bridges the install to
the slave, which already has the image staged), or flash the slave directly via
BOOTSEL/UF2. The high `err%` clears once both halves run matching firmware.

**Relevant files**:
- `keyboards/polykybd/hid_fw_up.c` — `CMD_FW_UP_APPLY` (slave bridge retries)
- `keyboards/polykybd/split_fw_up.c` — `user_sync_fw_up_apply_handler` (deferred, ACK-first)

---

### Split-link integrity: wire noise, the app-level CRC32, retries, and the health counter

> **RESOLVED (2026-06-16): migrated the split UART to full-duplex two-wire — the
> ongoing corruption is gone.** `config.h` now sets `SERIAL_USART_FULL_DUPLEX` +
> `SERIAL_USART_TX_PIN GP5` / `SERIAL_USART_RX_PIN GP4` + `SERIAL_USART_PIN_SWAP`.
> GP4 was always wired (a second conductor) but unused — there was no PIO
> full-duplex when the board was brought up; the vendor PIO driver supports it
> now. The cable is **straight** (GP5↔GP5, GP4↔GP4); `SERIAL_USART_PIN_SWAP`
> gives the crossover by swapping TX/RX **only on the master half's init path**
> (`serial_vendor.c`: `serial_transport_driver_master_init` swaps,
> `..._slave_init` does not), so **one identical image** produces the logical
> crossover at runtime by role — no per-side build, no EEPROM handedness. Works
> for the normal single image (USB half = master) and the HIL rig (roles forced
> per image via `POLYKYBD_HIL`, same `is_keyboard_master()`).
>
> **Measured result** via the health counter below: half-duplex was corrupting the
> small frequent syncs (`Failed to sync … UserLayer/UserPoly` lines — i.e. exactly
> the layer-drop + RGB-flash symptoms). On full-duplex, across **858 tx including
> deliberate heavy overlay/RGB load, `crc_err`/`giveup` stayed frozen at the
> boot-only burst (39/13) with `transport_fail=0`** — i.e. **zero** steady-state
> errors; `err%` only decays as the boot burst dilutes (14.3 → 4.5 % and falling).
> The boot burst is unmonitored (it precedes HID-console attach — the counter
> caught what the live log couldn't) and harmless (persistent state, re-delivered
> by the diff re-fire once the link settles). Why it works: full-duplex removes the
> single-wire **bus-turnaround/line-float** hazard and drives push-pull both ways
> (no pull-up), and gives the reply direction its own clean line.
>
> **Consequently the transport-level CRC patch and the upstream QMK PR are SHELVED**
> — they would have fixed *ongoing* payload corruption, which no longer occurs. The
> app-level CRC32 + `PERIODIC_SYNC_RETRIES=3` stay as the cheap backstop that
> absorbs the boot burst. Reopen only if `crc_err`/`giveup` start climbing in
> *steady state* (watch the counter). The analysis below is retained as the record
> of why the link behaves as it does — note the "half-duplex/single-wire/230400/
> 12 mA" descriptions are now historical (pre-2026-06-16).

**The split UART has no payload integrity check of its own — the per-transaction
CRC32 in `split_sync.c` is the only thing that catches a bit flipped by wire
noise in flight.** This is the single most important fact about the link, and
the reason the CRC32 was added (intermittent sync corruption that looked random).

**The link, pre-migration (HISTORICAL — the half-duplex setup in use until the
2026-06-16 full-duplex switch in the RESOLVED note above)**: `SERIAL_DRIVER = vendor` → the RP2040 **PIO
half-duplex, single-wire** driver (`serial_vendor.c`) on **`SERIAL_USART_TX_PIN
GP5`** (no RX pin, no `SERIAL_USART_FULL_DUPLEX` → one shared wire). Baud is
**230400** (`SELECT_SOFT_SERIAL_SPEED 1` in both variants' `halconf.h` →
`serial_usart.h` maps that to 230400; 8× PIO oversampling). TX is driven at
**12 mA** (`GPIO_DRIVE_STRENGTH_12MA`, `serial_vendor.c`) — fast, strong edges
that ring/reflect on a longer split cable.

**What QMK's transport guarantees (almost nothing)** — traced in
`platforms/chibios/drivers/serial_protocol.c`:
- A **1-byte handshake token**: master sends the transaction id, slave echoes
  `tid ^ NUM_TOTAL_TRANSACTIONS`. Proves *a* transaction of that id is starting —
  says nothing about the data bytes.
- A **20 ms** receive timeout (`SERIAL_USART_TIMEOUT`).
- The actual `initiator2target` / `target2initiator` **payload buffers travel
  raw** — no CRC, no checksum, not even parity. A flipped bit inside the 64-byte
  buffer is delivered to the slave callback and the transaction reports
  **success**. Without the app-level CRC32 the slave applies garbage state
  (contrast/flags/layer/overlay bytes) silently. ⚠️ Do **not** remove the CRC32
  thinking the transport covers it — it does not.
- Note the **reply** (`poly_sync_reply_t`, 1 ACK byte) has **no CRC** either; a
  corrupted reply can turn a real `SYNC_ACK` into a non-ACK → master retries (safe,
  idempotent) or, ~1/256, into a false ACK. Low impact, but it's why a tiny
  fraction of `crc_err` counts can be reply corruption rather than payload.
  - **That missing CRC is why the ack BYTE VALUES are Hamming-spaced**, and the
    vocabulary lives in dependency-free **`base/sync_ack.h`** (re-exported by
    `split_sync.h`, so consumers are unchanged) with tests enforcing it:
    `SyncAckTest.AckValuesStayHammingSpaced` requires min pairwise distance **4**
    across all six values, `EveryAckValueIsDistinct` forbids a duplicate, and
    `NoAckValueIsAStuckLineReading` forbids `0x00`/`0xFF` (what a stuck or floating
    line reads as). The set is built as **complement pairs**, each balanced at
    popcount 4: `SYNC_ACK 0xCA ↔ SYNC_CRC32_ERR 0x35`, `SYNC_ACK_SIG 0x4D ↔
    SYNC_NACK_REFUSED 0xB2`, `SYNC_BUSY 0x1B ↔ SYNC_GIVEUP 0xE4`.
    ⚠️ **Adding a seventh value must keep distance 4** or the single-bit tolerance
    degrades for the *whole* set. A mutually-distance-4 code containing these six
    extends to **16**, so 10 remain (8 excluding `0x00`/`0xFF`) — take the complement
    of an unused one to keep the pattern. `sync_succeeded()` is a deliberate
    **whitelist** so a new failure value is a failure at all 14 existing call sites
    with no edits; `SyncSucceededIsFailClosedAcrossEveryByte` sweeps all 256 bytes to
    pin that, because a blacklist implementation passes every other test.
  - ✅ **`SYNC_CRC32_ERR` is DE-OVERLOADED — it now means exactly one thing: "the
    frame I received did not check out".** It used to mean four: that, plus "still
    erasing", "no answer at all", and "I refuse". Each now has its own value —
    `SYNC_BUSY` (the `flash_stage_begin` re-poll while the deferred erase runs),
    `SYNC_GIVEUP` (`send_to_bridge` exhausted its retries, or we never asked), and
    `SYNC_NACK_REFUSED` (processed and declined: an unknown bundle id, a rejected
    chunk write, an apply with no valid staged image, an unknown reset action).
    The audit that keeps it true: **every remaining `= SYNC_CRC32_ERR` sits directly
    on a `crc32 != …->crc32` (or magic) check** —
    `grep -rn -B3 "ack = SYNC_CRC32_ERR" --include=*.c keyboards/polykybd/` should
    show no exceptions.
    - **Relabelling was behaviourally inert, which is why it was safe**: every
      consumer tests `== SYNC_ACK` / `sync_succeeded()`, i.e. ACK-or-not, so no
      decision changed — only what the logs and the COMMIT classifier can tell apart.
    - ⚠️ **The one place needing a compat guard is `hid_fw_up.c`'s erase-progress
      counter**, which matches on the begin re-poll value. It accepts **both**
      `SYNC_BUSY` and the legacy `SYNC_CRC32_ERR`, because the two halves can
      transiently run different firmware (a fw apply reboots the master first — the
      2026-06-22 boot-splash hang). Mismatched halves are safe in both directions
      *because* the functional decision is ACK-or-not.

**How CRC32 + retries + noise interact** (the model that drives the retry-count
choice). With `p` = probability a single frame is corrupted (and caught by CRC32),
`N` independent attempts fail this housekeeping pass with prob ≈ `p^N`:
- **CRC32 detects** corruption → slave returns `SYNC_CRC32_ERR` (not `SYNC_ACK`).
- **Retries recover** → `send_to_bridge` re-sends; all handlers are idempotent.
- Retries trade latency/CPU for resilience; **they do not reduce `p`.** A high
  `SPLIT_MAX_CONNECTION_ERRORS` (200, raised for the fw-update erase) is itself a
  tell that `p` is non-trivial.

**Periodic syncs use `PERIODIC_SYNC_RETRIES` (=3)** in `poly_keymap.c`
`sync_and_refresh_displays()` (poly/MRU/layer/last-key). Was briefly cut to **1**
(to avoid the ~400 ms main-loop stall that 10 retries × ~40 ms timeout costs once
`SPLIT_MAX_CONNECTION_ERRORS=200` stops failures fast-failing). **1 was too few**:
the diff re-fire only guarantees eventual delivery of state that *persists* (it
re-sends the current snapshot; global advances only on success), so a *transient*
that reverts to == global before the next successful sync is dropped, and even a
persistent transition leaves the slave visibly stale for a pass+. Field symptoms
at retries=1: layer updates occasionally not propagating (briefly-held momentary
layer lost), and the RGB matrix flashing on the slave for a fraction of a second
(stale disp/RGB until the deferred sync lands). 3 rides through a single glitch
within the same pass while bounding the worst-case stall to ~3 × 40 ms (the active
fw-update path skips this code).

**Measuring `p` — the split-link health counter** (`bridge_helper.c`,
master-side, added 2026-06-16). Every `send_to_bridge` frame is counted and
classified: `ok` / `crc_err` (slave NACK or corrupted reply — payload integrity
miss) / `transport_fail` (timeout/handshake) / `giveup` (retries exhausted).
`send_to_bridge` emits a compact summary every `LINK_STATS_LOG_EVERY` = 200
frames (count-based, no timer — the cadence follows real traffic, so it's dense
during overlay bursts and silent when idle; gated on `debug_enable`):

```text
Split link: 12345 tx crc_err=4 nack=17 transport_fail=1 giveup=0 err=0.0%
```

`err%` is the all-time detected-error rate over all frames — a direct read on the
wire. **Use it to validate any link change** (baud/cable/drive/termination) by
watching the number move, instead of by feel. `giveup` should stay ~0 with
retries=3; if it climbs, attack `p` at the source.

⚠️ **`giveup` counts only calls that ended on a LINK fault, and `nack` is EXCLUDED
from `err%`** — both decided by the one shared predicate `sync_is_link_fault(got_reply,
ack)` (`base/sync_ack.h`), so the two numbers can never disagree about what a bad wire
is. A link fault is exactly *nobody answered* or *the slave says what reached it was
corrupt* (`SYNC_CRC32_ERR`); every other byte means the wire delivered a frame and the
slave answered with a verdict of its own.
- ⚠️ **It is deliberately NOT an enumeration of the non-fault values.** Listing the
  siblings (`ack == SYNC_BUSY || ack == SYNC_NACK_REFUSED || …`) is the guard shape that
  goes stale — a seventh ack value would be misclassified until someone remembered to
  add it. `SyncAckTest.AnUnknownReplyValueIsNotMistakenForALinkFault` sweeps all 256
  bytes to pin that, and it fails against the enumerating implementation.
- **Why `giveup` needed this:** the `flash_stage_begin` re-poll runs with
  `max_retries=1`, so **every** poll of a deferred erase exhausted its retries with a
  perfectly good `SYNC_BUSY` answer and counted as a give-up. Measured on hardware
  (2026-08-18) a healthy font-pack sync read `nack=11 transport_fail=1 giveup=12` — one
  real fault, twelve reported give-ups. `giveup` is read as "the link is failing", so
  that is the same category error that had `err%` reading 6.0% on that link instead of
  0.5%.

⚠️ **`nack` is EXCLUDED from `err%` on purpose** — it counts valid non-ACK answers
(`SYNC_BUSY`, `SYNC_NACK_REFUSED`), where the wire worked and the slave simply said
something other than yes. Only `crc_err` (a corrupted frame) and `transport_fail`
(no answer) are link faults. Before the split, every non-ACK incremented
`crc_err` — and since `SYNC_BUSY` now arrives on **every erase re-poll of a flash**,
a single font-pack update would otherwise have added hundreds of phantom "errors"
to the one number used to judge cable/baud changes.

⚠️ **`send_to_bridge()` returns what the slave SAID; it returns `SYNC_GIVEUP` only
when the slave never answered.** It used to return a *constant* on give-up,
discarding `reply.ack` — and that worked only by **coincidence**, because the
constant was `SYNC_CRC32_ERR`, which happened to equal what the slave sent in every
case that mattered. Distinguishing the failure values exposed the discard, and with
it **`fw_up_slave_refused_commit()`'s "a refusal is self-describing, so don't spend
a STATUS RPC" short-circuit, which was dead code** — a refusal arrived as the
give-up constant, never as `SYNC_NACK_REFUSED`, so every refusal paid for a probe
(found in review, 2026-08-17). **Generalise: a sentinel that happens to equal a
real value hides the fact that the real value is being thrown away.**
- ⚠️ **The near-miss is the more instructive half, and it was initially reported
  here as a second dead-code case — wrongly.** `hid_fw_up.c`'s erase-progress
  counter kept firing throughout, just not for the reason it reads as: its guard
  accepts `SYNC_BUSY` **or** `SYNC_CRC32_ERR`, a compat arm added for transiently
  mismatched halves, and that arm also matched the give-up constant. A defensive
  clause written for one hazard quietly covered the discard, so the counter fired
  on a value the slave never sent. Verified on hardware 2026-08-18: it logs
  `begin-pending` at poll 17 and 33 of a 117-sector erase — as it did before.
  **Check a dead-code claim against the guard's OTHER arms before making it**; the
  git history of the condition settles it in one `git show`.

**Reducing `p` at the source (the real root fix), in order of leverage**:
1. **Lower the baud** — biggest, cheapest software lever. 230400 → 115200
   (`SELECT_SOFT_SERIAL_SPEED 2`) roughly doubles the per-bit sampling margin.
   Cost: overlay transfers (the bulk of UART bytes) ~2× slower; tiny state/layer
   syncs imperceptibly. A/B-test it against the health counter before keeping it.
2. **Driver edge rate** — the 12 mA TX drive in `serial_vendor.c` is strong; a
   slower edge helps signal integrity but lives in QMK core (would be a tracked
   local divergence, not a config knob).
3. **Hardware** — single-wire half-duplex over a TRRS-style cable is the classic
   culprit: ~100 Ω series resistor near the driver (damp reflections), a ground
   conductor twisted with the data line, shorter/shielded cable, solid common
   ground, good connector contact; rule out RGB/SPI/I²C coupling.
4. **Full-duplex two-wire** ✅ **DONE (2026-06-16)** — see the RESOLVED note at the
   top of this section. Removed the single-wire bus-turnaround hazard and drove the
   steady-state error rate to zero, so options 1–3 above were never needed.

**Relevant files**:
- `keyboards/polykybd/split_sync.c` — per-transaction CRC32 (the only payload check)
- `keyboards/polykybd/bridge_helper.c` / `.h` — `send_to_bridge` retries + the link health counters / `LINK_STATS_LOG_EVERY` summary
- `keyboards/polykybd/poly_keymap.c` — `PERIODIC_SYNC_RETRIES`, `sync_and_refresh_displays()`
- `keyboards/polykybd/config.h` — `SPLIT_MAX_CONNECTION_ERRORS`; the full-duplex defines (`SERIAL_USART_FULL_DUPLEX`, `SERIAL_USART_TX_PIN GP5`, `SERIAL_USART_RX_PIN GP4`, `SERIAL_USART_PIN_SWAP`)
- `<variant>/halconf.h` — `SELECT_SOFT_SERIAL_SPEED`. ⚠️ **Currently `0`, i.e. 460800 baud** in both variants — *not* the 230400 quoted in the historical half-duplex paragraph above, which describes the pre-2026-06-16 setup and is the figure a reader otherwise carries forward. The mapping lives in `platforms/chibios/drivers/serial_usart.h` (0→460800, 1→230400, 2→115200, …) and nothing in `keyboards/polykybd/` overrides `SERIAL_USART_SPEED` directly. It matters for any wire-time estimate: at 460800 8N1 one byte is **21.7 µs**, so the per-scan split transactions (slave matrix + pointing, unconditional in `transactions_master()`) are a fixed cost that does **not** shrink when the CPU clock rises — measured at ~473 µs, about half of an idle main-loop iteration (see the 200 MHz measurement in PR #187).
- `platforms/chibios/drivers/serial_protocol.c`, `drivers/vendor/RP/RP2040/serial_vendor.c` — QMK transport (no payload integrity)

---

### Bug: HIL "get current language" (cmd 7) times out once early in the run — boot-time busy window stalling the main loop

> **⚠️ CORRECTION (2026-06-27): the "flaky rig link" premise this note was written
> on is WRONG.** The rig runs the **same clean full-duplex two-wire split link as a
> shipping keyboard** (identical `config.h` defines; the crossover is done by role
> at runtime via `SERIAL_USART_PIN_SWAP`, not a different cable). There is **no
> "flaky / slow-ACK rig link"** — that phrasing below is superseded. The real
> differentiator is **timing/readiness, not link quality**: the rig fires its first
> HID queries within ~2 s of the master booting, inside the master's boot-time busy
> window (initial 72-keycap OLED render + the one-shot split sync to the
> just-booted slave), and the slave — independently flashed and rebooted on the rig
> (`usb_disconnect()` image) — can still be coming up then. A human user never pokes
> the keyboard that early. The forced-resync analysis below is also partly stale:
> the one-shot gate (`fc6ee693`, `is_transport_connected()`-gated, cleared even on a
> drop) already removed the per-pass spin. Treat the boot-window timing as the cause;
> the exact internal mechanism for the multi-second silence is unconfirmed (no trace).
> The rig-side mitigations live in `polykybd-ctnd` (sustained settle #37, packed-list
> headroom #38).

**Symptom (HIL rig, 2026-06-24)**: The `get current language` test (cmd `0x07`)
times out (`GET_LANG response: None`) and **fails the run**, while the *same*
command answers fine everywhere else in the *same* run — 3× during the runner's
settle phase and again in the later language round-trip read-back. It reproduced
**identically across two consecutive runs** (always test #4, right after the three
GET_IDs), so it is not pure randomness.

**Root cause (diagnosis, not yet fixed)**: the boot-time **forced layer-resync**
in `poly_keymap.c` `sync_and_refresh_displays()`. `g_force_layer_resync` starts
`true` and the master re-sends `USER_SYNC_LAYER_DATA` **every housekeeping pass
until the slave ACKs**, at `PERIODIC_SYNC_RETRIES` (3) per attempt
(`send_to_bridge`). On the rig the slave (the `*_hil_right` image) is **slow to ACK
at boot** (it is independently flashed + rebooted and still coming up — NOT a link
problem; see the correction banner above), so the resync **spins and blocks the master
main loop** for ~3 × the bridge timeout per pass, right in the early window where
the host is issuing its first HID queries — deterministically landing on cmd 7
(test #4). Once the slave finally ACKs, `g_force_layer_resync` clears and the loop
is responsive again (the later `GET_ID stress` shows 0 retries / 3–7 ms latency).

**Why it is almost certainly rig-only (and why it did not block the merge)**: on
real hardware the split link is the reliable **full-duplex two-wire** setup (see
the split-link RESOLVED note above — zero steady-state errors). There the slave
ACKs the **first** attempt, so `g_force_layer_resync` clears on pass 1 with
negligible stall and no HID command is delayed. The flake only manifests on the
rig because of *when* it queries (mid-boot) and the slave's boot latency — not link
quality. PR #85 merged with this HIL test red for exactly this reason.

**What the forced resync is and why it exists** (don't remove it blindly): each
half loads its **own** default layer from EEPROM, and the master only pushes
`USER_SYNC_LAYER_DATA` on a *diff*. So when the active default layer equals the
master's last-synced `global` (e.g. `_L0`/Qwerty = all-zero `global` after a fresh
boot or a fw-apply reboot), a slave that came up with a **stale** default layer
would never be corrected until the next manual layer change. The one-shot resync
forces a single push to fix that. It is gated by `g_force_layer_resync` (set at
boot, cleared on the first successful push); on failure the flag stays set so the
push re-fires — which is exactly the spin that stalls the rig.

**If hardening is wanted** (so it can't spin/stall even when the slave is slow to
come up at boot, without losing the fresh-boot correction): make the forced push a true
one-shot — attempt it **once** (ideally gated on the split transport being
connected so the single try has a real chance) and clear the flag regardless of
ACK, rather than re-firing every pass; or back off its retry cadence instead of
hammering each housekeeping pass. A genuine slave-stale case would then still be
corrected by the next real layer diff. Not done — left optional since real hardware
is unaffected.

**Relevant files**:
- `keyboards/polykybd/poly_keymap.c` — `g_force_layer_resync`, the forced-push branch in `sync_and_refresh_displays()` (`if ( layer_diff || g_force_layer_resync )`)
- `keyboards/polykybd/bridge_helper.c` — `send_to_bridge()` (per-attempt blocking cost = `PERIODIC_SYNC_RETRIES` × bridge timeout)
- `polykybd-ctnd` `station/hil_tests.py` — the `get current language` test (no miss-tolerance, unlike `test_get_id_stress`)
