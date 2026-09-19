# Firmware releases — mechanics

Extracted from `CLAUDE.md` 2026-09-14. The prose is unchanged; only heading levels
and relative links were adjusted to suit a standalone file.

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
  - ⚠️ **The delta may ALSO carry files the build and the rig never read** — the
    `!` entries of `qmk-test.yml`'s own `paths:` filter (`**.md`, `scripts/**`,
    `.claude/**`, the other workflows). Without that, a single docs or skills
    merge landing between a rig run and a release cost the release a fresh
    `tier: fwapply` round-trip: `PolyKybd-fw-v0.27.1` published with **zero
    assets** on 2026-09-19 because the retro merge sat in the way, and the
    refusal read *"4 commits separate the covered run from the release commit"*.
    - **The filter is READ OUT of the workflow, never copied here.** It already
      answers exactly this question and its own comment states the test —
      *whether the build or the rig ever READS it* — so an edit there moves the
      gate with it. A second list would be a second thing to keep in step.
    - ⚠️ **It fails CLOSED.** An unparseable or absent `paths:` block makes every
      file count as reaching the image, degrading the gate to its old strictness
      rather than waving a release through. `--selftest` pins that, and pins
      that a rename OUT of a build path (a `.c` becoming a `.md`) is judged on
      **both** names — the new one being harmless proves nothing, since the move
      removed a build input.
    - The commit-count bound is no longer the proof and was raised to 25
      (`MAX_DELTA_COMMITS`); the proof is the file list, which is refused
      outright if it reaches GitHub's 300-file cap and may have been truncated.
    - ⚠️ **The delta must not be able to write the policy it is judged by.**
      `release.yml` checks out the commit being RELEASED, so a filter read off
      disk is one the delta may have authored: a single commit could drop the
      `.github/workflows/qmk-test.yml` re-include, add `!keyboards/**`, and edit
      the firmware, and all three files would read as harmless. Two independent
      answers, both in place (Greptile P1 on #300, reproduced before fixing):
      the filter is fetched from the **covered commit** over the API rather than
      from the checkout, and a delta touching `qmk-test.yml`,
      `require_fwapply_run.py` or `release.yml` is refused outright whatever any
      filter says. The line is "decides whether a release is SAFE": `release.yml`
      controls whether the gate runs and on which sha, `bump-version.yml` only
      picks a number. ⚠️ **This does not defend against the gate being deleted
      from `release.yml`** — nothing running inside that workflow could, and the
      protection for it is the branch ruleset and review, not this script. It
      stops a delta that weakened the release path being auto-cleared on the way
      past.
      Editing either therefore costs the next release a fresh rig run — the
      correct price for changing what decides whether a release is safe.
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
  - **`bump:none` skips the bump entirely**, for a PR that cannot change the firmware
    image — docs, skills, `scripts/`. It is tested FIRST and beats every other label:
    a PR carrying both is contradictory, and the safe reading of a contradiction is
    not to move the version, since a version that did not move can be bumped by the
    next merge while one that moved is already published history.
  - ⚠️ **It is a convenience, not the fix for the release gate.** The gate now
    accepts non-build files on its own (above), so forgetting `bump:none` costs a
    patch number and nothing else. That ordering matters: a fix that depended on
    remembering a label would fail exactly the way the missing-`bump:minor`
    incidents below already have.
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

