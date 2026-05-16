---
name: mirror-master-with-upstream
description: Fast-forward the fork's master branch to match upstream/master exactly, then push it to origin. Use this to keep the fork's master as a clean mirror before or after a merge-upstream-into-branch run.
---

# Mirror master with upstream

Keeps `thpoll83/qmk_firmware:master` as a byte-for-byte mirror of `qmk/qmk_firmware:master`.
This branch should never carry custom commits — all PolyKybd work lives on `PolyKeyboard`.

Repo root: `/home/thpoll/Repos/qmk_firmware`

## Procedure

1. **Verify master has no local-only commits** — if it does, stop and warn the user rather than silently overwriting their work:
   ```bash
   git fetch upstream
   git log --oneline upstream/master..master
   ```
   If any commits are listed, report them and abort. Ask the user whether those commits were intended for `PolyKeyboard` instead.

2. **Check how far behind master is**:
   ```bash
   git log --oneline master..upstream/master | wc -l
   git log --oneline master..upstream/master | head -10
   ```
   Report the count and the most recent 10 upstream commit messages. If the count is 0, report "master is already up to date" and stop.

3. **Switch to master and fast-forward**:
   ```bash
   git checkout master
   git merge --ff-only upstream/master
   ```
   If `--ff-only` fails (means master has diverged), abort and report the divergence — do not force-reset without explicit user instruction.

4. **Push to origin**:
   ```bash
   git push origin master
   ```

5. **Return to the previous branch** so the user's working context is unchanged:
   ```bash
   git checkout -
   ```

6. **Report**:
   - How many commits were pulled in.
   - New HEAD SHA and the upstream commit message it corresponds to.
   - Reminder: run `/merge-upstream-into-branch` next if you also want `PolyKeyboard` updated.

## Notes / pitfalls

- **Never use `git reset --hard`** to move master — if `--ff-only` fails it means something unexpected happened; surface it rather than destroying state.
- This skill only touches `master`. It does not affect `PolyKeyboard` or any feature branch.
- If the user has not added the upstream remote yet, add it first:
  ```bash
  git remote add upstream https://github.com/qmk/qmk_firmware.git
  ```
  Then re-run from step 1.
- `git fetch upstream` (step 1) already fetches `upstream/master` — no separate fetch needed in later steps.
