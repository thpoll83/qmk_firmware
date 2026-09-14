---
name: cross-repo-pr-sweep
description: Find PolyKybd work that is committed and pushed but has NO pull request — the failure mode no local git command can detect, because the branch is clean, in sync and ahead, and only the absence of a PR is wrong. Use before declaring a cross-repo feature done, when the user asks "is everything saved / did I miss a repo / is anything left to open", at the end of a session that touched more than one repo, or when a feature spans firmware + host + rig + docs + hardware and you cannot remember which repos got PRs. NOT for checking a PR's review state (that is triage-pr-review) or its CI (that is diagnose-hil-failure).
---

# Sweep for pushed work with no PR

A PolyKybd feature routinely spans 4–6 repos, and **every "is everything saved?" check
passes on the repo you forgot**: the branch is committed, pushed, in sync with its
upstream, and `git status` is clean. Only the *absence of a PR* is wrong, and no local
command looks for that. On 2026-08-22 the legend-size work had four PRs open and reviewed
while AdafruitGFX sat on two pushed commits with none — one of them the `fontconvert -o`
sign fix the whole feature depended on. It was found only because the user asked.

## ⚠️ "Commits ahead" cannot answer this — do not build the sweep on it

A squash- or merge-merged branch reads as **ahead of the default forever**. Measured
across the five repos: **525** `claude/*` branches, **95** read as "ahead", and **3** of
those had an open PR. A ~95% false-positive rate is a check nobody reads twice.

Git tells you *what exists locally*. **Only GitHub can answer "does a PR exist"**, and
that is the whole job.

## The sweep

### 1. Enumerate what each repo has pushed

```bash
for e in qmk_firmware:PolyKybd PolyKybdHost:main polykybd-docs:main \
         polykybd-ctnd:main Adafruit-GFX-Library:master PolyKybd:master; do
    r=/home/user/${e%%:*}; d=origin/${e##*:}
    [ -d "$r/.git" ] || { echo "!! ${e%%:*}: not attached to this session"; continue; }
    git -C "$r" fetch -q --no-recurse-submodules origin \
        || { echo "!! ${e%%:*}: fetch FAILED — not inspected"; continue; }
    b=$(git -C "$r" rev-parse --abbrev-ref HEAD)
    n=$(git -C "$r" rev-list --count "$d"..HEAD 2>/dev/null)
    u=$(git -C "$r" rev-parse --abbrev-ref '@{upstream}' 2>/dev/null || echo "NOT PUSHED")
    echo "${e%%:*}  branch=$b  ahead=$n  upstream=$u"
done
```

⚠️ **Five ways the obvious version of this loop fails OPEN** — it prints nothing, which
reads identically to "all clean". All five were hit writing it:

- **`~` is `/root`, not `/home/user`** in this container, so a `~/repo` path matches
  nothing and the loop skips every repo in silence. Use absolute paths.
- **`origin/HEAD` is UNSET in every clone**, so auto-detecting the default yields nothing
  and an `|| origin/main` fallback silently reports 0 for the firmware (`PolyKybd`) and
  AdafruitGFX (`master`). Hence the explicit `repo:default` table.
- **Without the fetch, a stale ref cries wolf** the other way — right after a merge an
  un-fetched repo still shows the merged branch as ahead.
- **A FAILED fetch reads as inspected.** It cannot fake a clean result (a stale ref can
  only report the same or more), but the repo is compared against unknown-age data —
  hence `|| continue`.
- ⚠️ **It only sees the branch that is CHECKED OUT.** Work pushed to another branch is
  invisible, so standing on a merged branch prints a clean board for a repo with work
  elsewhere. Measured 2026-09-01: all six reported clean while a 13-commit branch sat in
  one of them.

Treat the loop's output as "here is what to ask GitHub about", never as a verdict.

### 2. Ask GitHub whether a PR exists — this is the actual check

For each repo that printed a branch with commits, one call:

```
mcp__github__list_pull_requests(
    owner="thpoll83", repo="<repo>",
    head="thpoll83:<branch>",     # owner-qualified — a bare branch name matches nothing
    state="all",                  # "open" alone hides a merged PR and reports a false gap
    fields=["number","title","state","html_url"])
```

- **`[]` means no PR has ever existed for that head** → this is a real gap, report it.
- **A result with `state: closed`, `merged: true`** → the branch's work landed; anything
  pushed *after* that merge is orphaned (see below).
- ⚠️ **Verify the empty result means what you think** before trusting a run of them: make
  the same call against a branch you know has a PR and confirm it returns one. An empty
  list from a malformed `head` is indistinguishable from a genuine gap.

### 3. Check for orphaned commits on an already-merged branch

A push to a branch whose PR has merged **succeeds silently** — git reports an ordinary
fast-forward, GitHub shows nothing, and the work is in no PR and not in the default
branch. That is the other half of this failure, and the window is "the PR merged while
you were still working", not "you deliberately reused an old branch".

```bash
git -C <repo> merge-base --is-ancestor <sha> origin/<default> && echo "in default" || echo "ORPHANED"
git -C <repo> log --oneline --merges -5 origin/<default>     # did my PR's merge already land?
```

Recovery: `git checkout -B <branch> origin/<default>`, cherry-pick the orphan,
`git push --force-with-lease`, open a **new** PR. Expect an add/add conflict if another
PR touched the same region meanwhile.

### 4. Report

Name every repo in one of three states, so the user can see the whole board at once:

```
qmk_firmware    branch=claude/foo   3 commits   PR #291 (merged)
PolyKybdHost    branch=claude/foo   2 commits   PR #212 (open)
polykybd-ctnd   branch=claude/foo   1 commit    ⚠️ NO PR
PolyKybd        branch=master       clean
```

Do not open the missing PRs unprompted — opening a PR is outward-facing. Report the gap
and ask.

## Notes

- ⚠️ **Do NOT "improve" this by sweeping every remote `claude/**` branch.** That is the
  95%-false-positive measurement above; it produces a wall of noise that trains people to
  skip the check.
- **A repo not attached to the session cannot be swept.** Say so explicitly rather than
  omitting it — an unmentioned repo reads as a clean one, which is the exact failure this
  skill exists to prevent.
