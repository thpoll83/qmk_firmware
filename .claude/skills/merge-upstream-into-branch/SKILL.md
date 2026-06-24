---
name: merge-upstream-into-branch
description: Merge upstream/master into the current branch (typically PolyKybd), resolve any conflicts, and push. Use after mirror-master-with-upstream to bring PolyKybd customisations up to date with the latest QMK changes.
---

# Merge upstream into branch

Integrates `qmk/qmk_firmware:master` into the current branch (normally `PolyKybd`).
Conflicts are expected — the files most likely to conflict are listed in the pitfalls section below.

Repo root: `/home/thpoll/Repos/qmk_firmware`

## Procedure

1. **Sanity checks**:
   ```bash
   git status --porcelain
   git branch --show-current
   ```
   - If the working tree is dirty, stop and ask the user to stash or commit first.
   - Confirm the current branch is the intended target (usually `PolyKybd`). If the user is on a short-lived feature branch, warn them — they may want to merge into `PolyKybd` first.

2. **Fetch upstream** (skip if `mirror-master-with-upstream` was just run):
   ```bash
   git fetch upstream
   ```

3. **Report the gap** — show the user what they're pulling in before touching anything:
   ```bash
   git log --oneline HEAD..upstream/master | wc -l
   git log --oneline HEAD..upstream/master | head -20
   ```
   Also show which of the fork's touched files appear in upstream's commit range — these are the likely conflict sites:
   ```bash
   git diff --name-only HEAD...upstream/master -- tmk_core/ quantum/ drivers/ | head -30
   ```

4. **Merge**:
   ```bash
   git merge upstream/master --no-edit -m "Merge upstream/master into $(git branch --show-current)"
   ```
   If the merge exits cleanly (no conflicts), skip to step 6.

5. **If there are conflicts**:
   ```bash
   git diff --name-only --diff-filter=U
   ```
   For each conflicted file:
   - Show the conflict markers with `git diff <file>`.
   - Resolve conservatively: keep the PolyKybd customisation and apply the intent of the upstream change around it. Do not silently drop upstream changes.
   - Common conflict files and their resolution strategy are in the pitfalls section below.
   - After resolving all files:
     ```bash
     git add <resolved-files>
     git merge --continue
     ```

6. **Update submodules** — the upstream merge advances submodule pointers (ChibiOS, pico-sdk, etc.) but does not check out the new commits. Always do this before building:
   ```bash
   git submodule status
   git submodule update
   ```
   Confirm all `+` prefixes are gone after the update. If a submodule commit is not present locally, `git submodule update` will fetch it from the remote.

7. **Verify the build is not broken** (optional but recommended for large upstream pulls):
   Activate the QMK virtualenv and do a quick compile check:
   ```bash
   source ~/qmk_env/bin/activate
   qmk compile -kb polykybd/split72 -km default 2>&1 | tail -20
   ```
   If it fails, diagnose before pushing. **Do not push a firmware that was built before step 6 — the old submodule checkouts (especially pico-sdk) can cause subtle runtime bugs like broken HID communication even when the build succeeds.**

8. **Push**:
   ```bash
   git push origin $(git branch --show-current)
   ```

9. **Report**:
   - Number of upstream commits merged.
   - Any files that had conflicts and how they were resolved.
   - New HEAD SHA.
   - Whether a build check was run and whether it passed.

## Known conflict-prone files

These files are modified by PolyKybd and are also touched periodically by upstream QMK:

| File | PolyKybd change | Upstream change pattern |
|------|-----------------|------------------------|
| `tmk_core/protocol/usb_descriptor.h` | Added custom HID interface constants | QMK occasionally bumps descriptor sizes or descriptor-count macros |
| `quantum/quantum.h` / `quantum.c` | — | QMK refactors API surface here; new includes can cause redefinition |
| `quantum/split_common/split_util.c` | — | Split transport protocol changes |
| `drivers/usb/usb_main.c` | — | USB stack changes |

**Resolution strategy for `usb_descriptor.h`**: keep the PolyKybd `#define` additions, apply any upstream value changes to the surrounding constants. If upstream renamed a macro the PolyKybd code uses, update the PolyKybd reference.

## Notes / pitfalls

- **Merge, don't rebase `PolyKybd`**. Rebasing 196+ custom commits onto a 1034-commit upstream delta would rewrite all SHA history, requiring a force-push and invalidating any open PRs. Merge commits are the right call for a long-lived integration branch.
- **Run `mirror-master-with-upstream` first** so `origin/master` is current — it's a good checkpoint even if this skill doesn't depend on it.
- **Do not merge upstream directly into a short-lived feature branch** — merge into `PolyKybd` first, then rebase the feature branch onto the updated `PolyKybd`.
- If upstream has **removed a file** that PolyKybd still uses (shows as a `CONFLICT (modify/delete)`), keep the file and add a comment explaining it was removed upstream but retained for PolyKybd compatibility.
- **Codegen files** (`lang/lang_lut.c`, `base/fonts/generated/*.h`) are never touched by upstream — any conflict there is surprising and should be flagged to the user.
- **Submodule update is not optional**: upstream QMK routinely bumps ChibiOS, pico-sdk, and chibios-contrib. The new pico-sdk may contain USB driver fixes. Building against the old submodule checkouts produces a binary that compiles cleanly but can have runtime failures (broken HID, USB enumeration issues). Always run `git submodule update` between merging and building.
