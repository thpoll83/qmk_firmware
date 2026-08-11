---
name: merge-upstream-into-branch
description: Merge upstream/master into the current branch (typically PolyKybd), resolve any conflicts, and push. Use after mirror-master-with-upstream to bring PolyKybd customisations up to date with the latest QMK changes.
---

# Merge upstream into branch

Integrates `qmk/qmk_firmware:master` into the current branch (normally `PolyKybd`).
Conflicts are expected — the files most likely to conflict are listed in the pitfalls section below.

Repo root: `/home/thpoll/Repos/qmk_firmware` on the user's machine,
**`/home/user/qmk_firmware`** in a Claude Code web/remote container.

## Step 0 — container prerequisites (skip on the user's own machine)

A fresh remote-container clone is **shallow** and has no `upstream` remote, which
makes every history answer below wrong until fixed:

```bash
git rev-parse --is-shallow-repository                    # true → fix before trusting anything
git fetch origin --unshallow --no-recurse-submodules     # ~1-2 min on this repo
git remote add upstream https://github.com/qmk/qmk_firmware   # if missing
git fetch upstream --tags --no-recurse-submodules
```

⚠️ **On a shallow clone `git merge-base` returns an EMPTY STRING, not an error.**
Anything comparing this branch to upstream then silently degrades — a script using
`$(git merge-base …)` reports tens of thousands of "unrelated" commits for a fork
whose merge base is one of its own branches. After unshallowing, the merge base
should resolve to the fork's own `master`; that is the check that it worked.

`--no-recurse-submodules` throughout: the `qmk/*` submodule repos are not
proxy-authorized, so a plain fetch ends in `Could not access submodule 'lib/chibios'`
noise that hides the real result.

## Which upstream point to merge — tag vs master

Merging a **stable tag** (`0.33.13`) rather than `upstream/master` gives a base
people can look up, and is what the fork does. Two consequences:

- `master` may be **ahead** of the merged tag. That is fine (it is a mirror), but it
  makes `UPSTREAM_PATCHES.md`'s `git diff master..HEAD` check approximate by exactly
  that gap — check `git diff --stat <tag>..master -- tmk_core quantum platforms builddefs lib`
  is empty, or diff against the tag you actually merged.
- A tag can carry a **new lint/CI rule whose fixes landed after it** (see step 7).

```bash
git tag --list --sort=-v:refname | grep -E '^[0-9]+\.[0-9]+\.[0-9]+$' | head -5
```

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

   ⚠️ **In a container that fetch 403s** — the `qmk/*` submodule repos are not in the
   session's authorized set. Call **`add_repo`** once per submodule repo
   (`qmk/ChibiOS`, `qmk/ChibiOS-Contrib`, `qmk/lufa`, `qmk/printf`, `qmk/pico-sdk`);
   each answers `read_available` **without attaching anything**, after which the
   ordinary command works:
   ```bash
   git submodule update --init --depth 1 --no-recommend-shallow lib/chibios   # …and the rest
   ```
   The old `codeload.github.com` tarball workaround is **dead** (403 as of 2026-08-11)
   and fails *quietly* inside a pipeline — `curl … | tar xz` prints only
   `gzip: stdin: not in gzip format` while the shell reports success.

6b. **Prove the UPSTREAM_PATCHES survived — diff the diffs, don't read them.** A
   clean merge is not evidence: git resolves those files silently whenever upstream
   didn't touch the same hunks, so "no conflicts" and "patch silently dropped" look
   identical from the merge output.
   ```bash
   # BEFORE the merge
   git diff <old-merge-base>..HEAD -- tmk_core quantum platforms builddefs drivers > /tmp/before.diff
   # AFTER
   git diff <tag>..HEAD           -- tmk_core quantum platforms builddefs drivers > /tmp/after.diff
   diff /tmp/before.diff /tmp/after.diff && echo "all patches intact, context unchanged"
   ```
   Identical output means every patch survived *and* upstream didn't restructure
   around it. Finish with a `grep` for one marker per patch (`raw_hid_pre_receive_kb`,
   `ifndef RAW_EPSIZE`, `POLY_SPLIT_SHMEM_RPC_GUARD`, `POLYKYBD_VREG_VSEL`,
   `oled_render_dirty(true)`). Full procedure in `keyboards/polykybd/UPSTREAM_PATCHES.md`.

7. **Verify the build is not broken** (optional but recommended for large upstream pulls):
   Activate the QMK virtualenv and do a quick compile check:
   ```bash
   source ~/qmk_env/bin/activate
   qmk compile -kb polykybd/split72 -km default 2>&1 | tail -20
   ```
   If it fails, diagnose before pushing. **Do not push a firmware that was built before step 6 — the old submodule checkouts (especially pico-sdk) can cause subtle runtime bugs like broken HID communication even when the build succeeds.**

   In the container: `export QMK_HOME=$PWD && export PATH="/root/.qmk_venv/bin:$PATH"`
   (there is no `~/qmk_env`). Build **both** variants, and prefer
   `keyboards/polykybd/doom/pack/build_pack.sh` on a DOOM-capable tree — it also
   builds the **monolith**, the RAM-tightest flavour, which PR CI never covers.

   ⚠️ **When an upstream merge breaks the build, suspect the vendored DOOM engine
   first.** QMK builds `-Werror`, so any warning upstream adds to
   `builddefs/common_rules.mk` becomes a hard failure in `doom/engine/`
   (third-party rp2040-doom). 0.33.13 added `-Wunused-but-set-variable`/`-parameter`
   — as collateral of *"GCC 16.1 compatibility fix" (#26216)*, so the commit subject
   gave no warning — and six `m_menu.c` callbacks failed. Fix in the **doom-scoped
   `-Wno-` demotion block in `keyboards/polykybd/rules.mk`**, never in vendored code.

7b. **Sweep for new lint/CI rules the tag brought.** A stable tag can add a rule whose
   fixes for upstream's *own* keyboards landed after it, so `lint` goes red on files
   this fork does not maintain. Reproduce the job locally (~1 min) rather than reading
   the CI log — `qmk info -l` fills the log tail with keyboard ASCII art, so any
   excerpt is a partial list:
   ```bash
   export QMK_HOME=$PWD
   git diff --name-only origin/PolyKybd...HEAD > /tmp/changed.txt
   for KB in $(qmk list-keyboards); do
     CH=$(grep -E "^keyboards/${KB}/" /tmp/changed.txt); [ -z "$CH" ] && continue
     [ "$(echo "$CH" | grep -cv /keymaps/)" -gt 0 ] || continue
     qmk lint --strict --keyboard "$KB" >/dev/null 2>&1 || echo "FAIL: $KB"
   done
   ```
   Collect **every** failure before fixing any. Where upstream has already fixed it
   past the tag, cherry-pick their commit (byte-identical ⇒ no future conflict);
   otherwise write the fix taking real facts from `git log --diff-filter=A` rather
   than inventing them. See the CI section of `CLAUDE.md` for the 0.33.13 worked case.

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
- ⚠️ **CodeRabbit will SKIP the PR entirely — a merge PR gets no bot review.** Its
  limit is 100 changed files (*"Review skipped — Too many files"*), and a catch-up
  merge is several hundred. The skip renders as an ordinary status comment, so the
  PR reads as reviewed. Treat **`Build firmware` + `HIL test` + a hardware flash** as
  the only real verification, and say so when reporting the PR as ready.
- **Check upstream's breaking-changes doc for removals that touch us**, and grep
  before assuming: 0.33.13 removed `isLeftHand` and `FORCE_NKRO`, both of which
  PolyKybd happened not to use — but that was worth *verifying*, not hoping, since a
  removed `FORCE_NKRO` silently changes behaviour rather than failing to build.
- **Run `mirror-master-with-upstream` first** so `origin/master` is current — it's a good checkpoint even if this skill doesn't depend on it.
- **Do not merge upstream directly into a short-lived feature branch** — merge into `PolyKybd` first, then rebase the feature branch onto the updated `PolyKybd`.
- If upstream has **removed a file** that PolyKybd still uses (shows as a `CONFLICT (modify/delete)`), keep the file and add a comment explaining it was removed upstream but retained for PolyKybd compatibility.
- **Codegen files** (`lang/lang_lut.c`, `base/fonts/generated/*.h`) are never touched by upstream — any conflict there is surprising and should be flagged to the user.
- **Submodule update is not optional**: upstream QMK routinely bumps ChibiOS, pico-sdk, and chibios-contrib. The new pico-sdk may contain USB driver fixes. Building against the old submodule checkouts produces a binary that compiles cleanly but can have runtime failures (broken HID, USB enumeration issues). Always run `git submodule update` between merging and building.
