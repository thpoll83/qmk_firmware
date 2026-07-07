---
name: polykybd-github-release
description: Cut a GitHub Release for the PolyKybd firmware (qmk_firmware) or the host app (PolyKybdHost) — draft customer-facing release notes (newest version first, maintenance-only bumps skipped, concise, informative-over-funny), get the user's review, then publish with the correct tag, title, "latest" flag and (firmware) CI-built .bin/.uf2 assets. Use when asked to "cut/create/make/publish a release", "release the firmware/host", "release vX.Y.Z", "draft the release for …".
---

# PolyKybd GitHub release

Prepare and hand off a **GitHub Release** for one of the two shipping artifacts:

| Target | Repo | Default branch | Version source | Release tag |
|--------|------|----------------|----------------|-------------|
| **Firmware** | `thpoll83/qmk_firmware` | `PolyKybd` | `FW_VERSION` in `keyboards/polykybd/config.h` | `PolyKybd-fw-vX.Y.Z` |
| **Host** | `thpoll83/PolyKybdHost` | `main` | `polyhost/_version.py` (`__major/minor/patch__`) | `vX.Y.Z` |

This skill **builds on `polykybd-release-notes`** for gathering commits per version,
but applies a **release-specific voice** (below) and adds two things the plain
notes skill does not: a **mandatory review gate** before anything is created, and
getting the **release metadata / labels** right.

> **Hard rule: never create the tag, push, or create/publish the release until the
> user has reviewed and approved the notes + metadata (step 3).** You *prepare* the
> release; the user (or CI on a tag) creates it.

## 1. Pick the target & resolve the version range

Confirm which artifact (firmware vs host — ask if ambiguous), then resolve the range
exactly as `polykybd-release-notes` does:

- **Low end = last *published* GitHub release** — `mcp__github__list_releases` for the
  repo, entry `[0]`, strip the tag prefix (`PolyKybd-fw-v` for firmware, `v` for host).
  The list is newest-first. The in-tree version can be **ahead** of the last release
  (bumped-but-unreleased), so always anchor on the release, not the newest bump commit.
- **High end = current tree** — read the version source above.
- If they're equal there's nothing to release — say so.

State the resolved range back before drafting (e.g. "Last firmware release is
0.9.23, tree is 0.9.42 — release notes for 0.9.24 → 0.9.42").

Gather the commits per version with the `polykybd-release-notes` mechanics (bump
commits are the boundaries: `chore: bump firmware version to X.Y.Z [skip ci]` /
`chore: bump host version to X.Y.Z [skip ci]`; pull commit bodies + the relevant
`CLAUDE.md` "Investigations" write-ups for the real mechanism).

## 2. Draft the notes — release voice (differs from `polykybd-release-notes`)

Apply these four rules on top of the base skill's per-version gathering:

1. **Newest version first.** Reverse-chronological: lead with the most recent version,
   older ones below. (The plain notes skill defaults oldest-first; a *release* leads
   with what's new.)
2. **Skip maintenance-only bumps.** Omit versions whose changes have **no obvious
   owner-facing benefit**: pure docs, internal refactors, CI/test-only, dependency
   housekeeping, cleanups, version plumbing. Judge each interval by *"would a keyboard
   owner care?"* If a whole interval is maintenance, drop its header entirely (at most
   fold a single trailing "Plus maintenance releases 0.9.x–0.9.y 🧹" line — don't give
   each one a section). The version numbers still increment through them; you're
   trimming the *notes*, not renumbering.
3. **Concise.** Tighter than the plain notes — one headline + 1–3 tight bullets per
   version. Keep the load-bearing identifier (`cmd 30`, `protocol v9`, `GP5/GP4`,
   `SERIAL_USART_PIN_SWAP`) but cut the mechanism paragraph down to the essential fact.
4. **Informative over funny.** A short theme + a single on-brand emoji is fine and
   matches past releases ("OS Shortcuts", "Glyph Script control", "Full Duplex Split
   Sync") — but don't force jokes or a punny "theme in quotes". Lead each line with
   what it does *for the user*.

Still **honest and correct**: when a change bumps `PROTOCOL_VERSION`, say so and note
that host and firmware must be updated **together** (the connect gate is exact-match) —
that's the one piece of deep detail a user genuinely needs.

Per-version shape:

```
## 0.9.NN — <short theme> <emoji?>
<one-sentence, user-facing headline.>
- <tight bullet: what it does + the real identifier>
- <why you care / or "requires matching host+firmware — protocol vN" when a protocol bumped>
```

## 3. REVIEW GATE — get sign-off before creating anything

Present to the user, in chat:
- the **full drafted notes**, and
- the **proposed metadata**: tag, release title, target branch, latest-flag.

Ask them to approve or edit. **Do not** create/push a tag or create/publish the
release until they explicitly approve. Fold in their edits (and re-show if the change
is substantial). This is the "review the notes before crafting the release" step and
it is not optional.

## 4. Get the release metadata / labels right

Two "label" systems matter here; both are inferable from previous releases
(`mcp__github__list_releases` / `list_pull_requests`).

### (a) Release title (the visible label on the release)
Convention from past releases: **`<tag> <short feature theme>`**, occasionally led by a
headline number.
- Firmware: `PolyKybd v0.9.23 Fantasy scripts` · `PolyKybd-fw-v0.9.18 OS Shortcuts` ·
  `PolyKybd-fw-v0.8.21: 143 New languages, Emoji/Lang layer unification`
- Host: `v0.9.16 Glyph Script control` · `v0.8.47 Background service & snappier Windows tray`

Pick the theme from the **headline feature** of the newest/most significant version in
the range; keep it to ~2–6 words. Confirm it in the review gate. Note: CI's fallback
titles are generic (`PolyKybd Firmware <tag>` / `PolyKybdHost <tag>`) — **always replace
them with a crafted title.**

### (b) Latest vs prerelease
Every past PolyKybd release is a full **latest** release (`prerelease:false`,
`draft:false`). Mark the release as latest; only use prerelease/draft if the user
explicitly says it's a preview.

### (c) `bump:*` PR labels — what set the version number (verify, don't re-apply)
The version you're about to release was produced **automatically at PR-merge time**:
`bump-version.yml` reads the merged PR's label and bumps the version —
`bump:major` → `bump:minor` → `bump:protocol`, else **patch** (default). So "the right
label" is a PR-time action that decides which version the release carries.
- **Trust the actual `FW_VERSION`/`PROTOCOL_VERSION` (or `_version.py`) in the tree.**
  In this repo, protocol PRs frequently bump `PROTOCOL_VERSION` **in-source** and
  *deliberately omit* `bump:protocol` (the label would double-bump). Don't second-guess
  a version that's already correct in-tree.
- If a protocol-breaking change appears to have shipped **without** the version
  reflecting it, **flag it to the user before cutting the release** — a wrong version
  can't be retro-fixed by relabeling after merge.

## 5. Create / publish the release (hand-off)

⚠️ **This environment has no `gh` CLI and the GitHub MCP exposes no create-/edit-release
tool** — you can't call an API to publish. But both repos' `release.yml` are wired to
pull the **crafted notes from a `release-notes` branch**, so publishing is a two-push
job you *can* drive from here.

### The `release-notes` branch (how the crafted notes reach CI)
Both `release.yml` workflows, when a tag is created, look for
**`<TAG>.md` at the root of the unprotected `release-notes` branch** (via `gh api …
/contents/<TAG>.md?ref=release-notes`). If present, CI publishes with that file's
**first line `# <title>`** as the release title and the rest as the body; if absent it
falls back to `--generate-notes` + a generic title. So the file is optional and safe.

**Lifecycle: one file per tag, never overwritten or deleted.** `PolyKybd-fw-v0.9.42.md`
(firmware) / `v0.9.42.md` (host) is written once for that release; the next release adds
a *new* file. The branch accumulates them as a changelog archive — don't reuse or clear
old files.

### Automated path (recommended — fully hands-off)
1. Write the approved notes to a file whose first line is `# <crafted title>` and the
   rest is the approved body.
2. Put it on the `release-notes` branch as `<TAG>.md` and push (this branch is
   unprotected — a direct push works; create the branch if it doesn't exist yet, keeping
   any existing files):
   ```bash
   cd <repo>
   git fetch origin release-notes 2>/dev/null && git checkout release-notes \
     || git checkout --orphan release-notes && git rm -rf --cached . 2>/dev/null; true
   cp <approved-notes> PolyKybd-fw-v0.9.42.md      # or v0.9.42.md for the host
   git add PolyKybd-fw-v0.9.42.md && git commit -m "release notes: 0.9.42"
   git push -u origin release-notes
   git checkout -    # back to your working branch
   ```
3. Create + push the tag on the bumped commit so CI builds and publishes with the
   crafted notes + title:
   ```bash
   git tag PolyKybd-fw-v0.9.42 <bump-commit>   # host: v0.9.42
   git push origin PolyKybd-fw-v0.9.42
   ```
   - **Firmware:** the tag-push (or a later `release: published`) triggers `release.yml`
     → builds `polykybd/split72:default` (doom-pack flavour) → uploads
     `polykybd_split72_default.bin`, `.uf2`, `doom_pack_v1.plyx` → creates the release
     with the crafted notes. **Never attach binaries by hand — CI does.**
   - **Host:** the `v*` tag-push triggers `release.yml` → creates the release with the
     crafted notes (no build assets — pure Python).
   - If pushing the **tag** is blocked by a tag ruleset, fall back to the UI path below;
     the notes file on `release-notes` is still picked up whenever the release is created.

### Manual fallback (if you'd rather use the UI, or tag push is blocked)
The user creates the release in the GitHub UI — tag `PolyKybd-fw-vX.Y.Z` (target
`PolyKybd`) / `vX.Y.Z` (target `main`), title = crafted title, body = approved notes,
"Set as latest", **Publish**. For firmware, publishing still kicks CI to attach the
assets. (The `release: published` trigger exists precisely because release tags land on
the `[skip ci]` bump commit, which suppresses the tag-push CI trigger.)

**Always also deliver** the approved notes in chat as a ready-to-paste Markdown block +
a one-line metadata summary (`tag / title / target branch / latest`), so the user can
review/paste regardless of path.

## Pitfalls
- **Publishing goes through CI, not a release API.** There's no create-release tool
  here; the release is created by CI when the tag lands (with the crafted notes pulled
  from the `release-notes` branch). Don't claim it's live until the workflow has run —
  verify with `mcp__github__get_release_by_tag` / `list_releases`. If a tag ruleset
  blocks the push, hand off to the UI path.
- **Keep the notes file and the tag in sync** — the `release-notes/<TAG>.md` filename
  must exactly match the tag CI sees, or CI silently falls back to auto-generated notes.
- **In-tree version ≠ released version** — anchor "since the last release" on the GitHub
  release, not the newest bump commit.
- **Don't hand-attach firmware binaries** — CI builds and uploads them; a manually
  uploaded stale `.bin` gets clobbered or conflicts with the CI build.
- **Skipping maintenance versions is about the NOTES only** — the numbers still
  increment through them. Don't renumber or imply they don't exist; fold them into a
  single "plus maintenance releases" line if the user asks.
- **Protocol lockstep** — when the range includes a `PROTOCOL_VERSION` bump, the notes
  must tell users to update host + firmware together (exact-match connect gate).
- **No git tags in the firmware tree** — releases are the GitHub Releases API; history
  boundaries are the `bump firmware/host version` commits (see `polykybd-release-notes`).
- Keep the model identifier out of everything pushed (commit messages, release body).
