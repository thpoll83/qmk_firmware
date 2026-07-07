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
tool.** You cannot publish the release yourself. Prepare it fully and hand off.

**Firmware (`qmk_firmware`)** — the release is what triggers the build:
- `release.yml` runs on `release: published` **and** on a `PolyKybd-fw-v*` tag push. It
  builds `polykybd/split72:default` (the doom-pack shipping flavour) and uploads
  `polykybd_split72_default.bin`, `.uf2`, and `doom_pack_v1.plyx` as assets
  (idempotent, `--clobber`). **You do NOT attach binaries by hand — CI does.**
- **Recommended path:** the user creates the release in the GitHub UI — tag
  `PolyKybd-fw-vX.Y.Z` (target: `PolyKybd`), title = crafted title, body = approved
  notes, "Set as latest", **Publish**. Publishing kicks CI, which attaches the assets
  ~a minute later.
- **Alternative (tag push):** push an annotated tag `PolyKybd-fw-vX.Y.Z` on the bumped
  commit → CI *creates* the release with a **generic** title + `--generate-notes` +
  assets; the user then edits the title/body to the approved copy. Only if the user
  prefers it — it publishes generic notes first. (The `release: published` trigger
  exists precisely because release tags land on the `[skip ci]` bump commit, which
  suppresses the tag-push CI trigger — so *publishing* is the reliable build start.)

**Host (`PolyKybdHost`)**:
- `release.yml` runs **only** on a `v*` tag push and creates the release with
  `--generate-notes` + generic title `PolyKybdHost <tag>`. No build assets (pure Python).
- **Recommended:** the user creates the release in the UI — tag `vX.Y.Z` (target:
  `main`), crafted title + approved notes, latest, **Publish**. If instead the tag is
  pushed, CI makes a generic release and the user must replace the title + body with the
  approved copy.

**Deliver** the approved notes as a ready-to-paste Markdown block plus a one-line
metadata summary (`tag / title / target branch / latest`). Offer to also write them to
a file (e.g. `release-notes-<version>.md`).

## Pitfalls
- **You don't publish it.** No create-release tooling here — never claim the release is
  live; you prepare it, the user (or CI on tag) creates it.
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
