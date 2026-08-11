---
name: mirror-master-with-upstream
description: Fast-forward the fork's master branch to match upstream/master exactly, then push it to origin. Use this to keep the fork's master as a clean mirror before or after a merge-upstream-into-branch run.
---

# Mirror master with upstream

Keeps `thpoll83/qmk_firmware:master` as a byte-for-byte mirror of `qmk/qmk_firmware:master`.
This branch should never carry custom commits — all PolyKybd work lives on `PolyKybd`.

Repo root: `/home/thpoll/Repos/qmk_firmware` on the user's machine,
**`/home/user/qmk_firmware`** in a Claude Code web/remote container.

## Step 0 — container prerequisites (skip on the user's own machine)

A fresh remote-container clone has **neither the `upstream` remote nor a local
`master`**, and its history is **shallow** — which makes every "how far behind"
answer below wrong until fixed. Establish all three first:

```bash
git rev-parse --is-shallow-repository                     # true → unshallow before trusting any count
git fetch origin --unshallow --no-recurse-submodules      # ~1-2 min; omit if already false
git remote add upstream https://github.com/qmk/qmk_firmware   # if missing
git fetch upstream master --no-recurse-submodules
git fetch origin  master --no-recurse-submodules          # creates origin/master; there may be no LOCAL master
```

`--no-recurse-submodules` throughout: the `qmk/*` submodule repos are not
proxy-authorized, so a plain fetch ends in `Could not access submodule
'lib/chibios'` noise that hides the real result (the fetch itself still worked).

## Procedure

1. **Verify master has no local-only commits** — if it does, stop and warn the user rather than silently overwriting their work:
   ```bash
   git fetch upstream
   git log --oneline upstream/master..origin/master     # use origin/master if no local master exists
   ```
   If any commits are listed, report them and abort. Ask the user whether those commits were intended for `PolyKybd` instead.

2. **Check how far behind master is**:
   ```bash
   git log --oneline master..upstream/master | wc -l
   git log --oneline master..upstream/master | head -10
   ```
   Report the count and the most recent 10 upstream commit messages. If the count is 0, report "master is already up to date" and stop.

3. **Fast-forward master.** Prefer the **checkout-free** form — it is the only one
   that works when there is no local `master` (the container case), and it is the
   better choice even when there is one, because checking out master would swap the
   whole working tree mid-session and invalidate any `.build/` output:
   ```bash
   git merge-base --is-ancestor origin/master upstream/master && echo "fast-forward OK"
   git branch -f master upstream/master     # refuses if master is checked out — then use the classic form
   ```
   The `--is-ancestor` test **is** the `--ff-only` safety check: it fails exactly
   when master has diverged. If it fails, abort and report the divergence — do not
   force-reset without explicit user instruction.

   Classic form (user's machine, master not otherwise busy):
   ```bash
   git checkout master && git merge --ff-only upstream/master
   ```

4. **Push to origin** — name both ends, so it works whatever branch is checked out:
   ```bash
   git push origin master:master
   ```

5. **Return to the previous branch** so the user's working context is unchanged:
   ```bash
   git checkout -
   ```
   Not needed after the checkout-free form in step 3 — nothing moved.

6. **Report**:
   - How many commits were pulled in.
   - New HEAD SHA and the upstream commit message it corresponds to.
   - Reminder: run `/merge-upstream-into-branch` next if you also want `PolyKybd` updated.

## Notes / pitfalls

- **Never use `git reset --hard`** to move master — if `--ff-only` fails it means something unexpected happened; surface it rather than destroying state.
- This skill only touches `master`. It does not affect `PolyKybd` or any feature branch.
- If the user has not added the upstream remote yet, add it first:
  ```bash
  git remote add upstream https://github.com/qmk/qmk_firmware.git
  ```
  Then re-run from step 1.
- ⚠️ **On a shallow clone every count in steps 1–2 is meaningless**, and it fails
  silently rather than loudly — `git merge-base` returns an *empty string*, not an
  error, so a script using it degrades to comparing against `HEAD` and reports tens
  of thousands of "unrelated" commits. Do step 0's `--is-shallow-repository` check
  before believing anything (2026-08-11).
- **`master` is a mirror, so it is fine for it to be AHEAD of what PolyKybd merged.**
  Mirroring to the upstream tip while PolyKybd merges a stable tag is the normal
  case — but it makes `UPSTREAM_PATCHES.md`'s `git diff master..HEAD` check
  approximate by exactly that gap. See the note at the top of that file.
- `git fetch upstream` (step 1) already fetches `upstream/master` — no separate fetch needed in later steps.
