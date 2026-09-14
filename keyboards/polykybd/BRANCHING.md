# Branching — the cross-repo sweep and its failure modes

Extracted from `CLAUDE.md` 2026-09-14. The prose is unchanged; only heading levels
and relative links were adjusted to suit a standalone file.

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

