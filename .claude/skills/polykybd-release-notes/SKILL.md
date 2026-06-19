---
name: polykybd-release-notes
description: Generate PolyKybd firmware release notes for a version range, grouped per version with an entertaining-but-technical voice (emoji + real mechanisms, pins, command IDs). Use when asked to "write release notes", "changelog for vX → vY", "what changed since the last release", or to draft a GitHub release body. Auto-detects the range when no versions are given — last published GitHub release → current FW_VERSION.
---

# PolyKybd release notes

Produce release notes for the PolyKybd firmware (`keyboards/handwired/polykybd/`)
between two firmware versions, **grouped per version**, in the project's
house style: playful (emoji, a one-line theme per version) **and** technically
precise (name the actual mechanism — pins, command IDs, files, the root cause).

Releases are **GitHub Releases** tagged `PolyKybd-fw-vX.Y.Z` (there are **no git
tags** in the tree). Each version's boundary in history is a
`chore: bump firmware version to X.Y.Z [skip ci]` commit. `FW_VERSION` lives in
`keyboards/handwired/polykybd/config.h`.

## Resolve the range first

**If the user named both versions** (e.g. "0.8.21 → 0.8.26"), use them.

**If the user said "since the last release" / gave no version** — auto-detect:

1. **Last released version** = the newest published GitHub release. Prefer the
   GitHub API (most authoritative — a version can be bumped in-tree but not yet
   released):
   - `mcp__github__list_releases` for `thpoll83/qmk_firmware`, take entry `[0]`,
     strip the `PolyKybd-fw-v` prefix from `tag_name`. (List is newest-first.)
   - Fallback with no GitHub access: `git tag` is empty here, so use the
     **second-newest** bump commit as the last-released boundary.
2. **Most recent version** = current tree:
   `grep 'define FW_VERSION' keyboards/handwired/polykybd/config.h`
   (equivalently the newest `bump firmware version to …` commit).
3. If the most-recent version **equals** the last release, there's nothing
   unreleased — say so, and offer notes for the previous interval instead.

State the resolved range back to the user before writing ("Last release is
0.8.21, current tree is 0.8.26 — notes for 0.8.22 → 0.8.26").

## Gather the commits per version

Map each version to its bump commit, then list commits in each interval
(newest-first overall is fine; drop the bump commits themselves):

```bash
cd <qmk repo>
# boundary commit for a given version:
git log --oneline --grep="bump firmware version to 0.8.21" -1
# commits that landed in 0.8.22 (exclusive .. inclusive of its bump):
git log --no-merges --oneline <bump-0.8.21>..<bump-0.8.22> | grep -v "bump firmware"
```

Do this for each consecutive pair across the range. A version with **no**
`keyboards/handwired/polykybd/` changes between its bumps is a docs/housekeeping
release — say so honestly, don't invent content. For the **newest, not-yet-bumped**
work, use `<newest-bump>..HEAD`.

Pull commit bodies for the load-bearing changes (`git show --stat <sha>`, or read
the relevant `CLAUDE.md` "Investigations" section — it usually has the root cause
already written up) so each entry names the real mechanism, not just the subject.

## Write the notes

One section per version, newest or oldest first (oldest-first reads as a story;
match the user's preference, default oldest-first):

```
## 0.8.NN — *"<short punchy theme>"* <emoji>
**<one-sentence headline of the change.>**
- <technical bullet: the mechanism — file/pin/cmd id/root cause — in plain terms> <emoji>
- <impact bullet: why a user/dev cares; measured numbers if available>
```

Then a `### TL;DR by theme` table (theme | releases | one-liner) at the end.

**Voice rules**
- Every playful line must carry a real fact. "🤫 cmd 21 went silent" is good
  *because* it then explains the informationless ACK and the stale-reply drain.
- Use the true identifiers: `GP5`/`GP4`, `SERIAL_USART_PIN_SWAP`, `cmd 27`,
  `sync_succeeded()`, `PROTOCOL_VERSION`, etc.
- Quote measured results when the investigation recorded them (e.g. "858 tx,
  crc_err/giveup at zero").
- Keep it honest: a quiet release is "Housekeeping 🧹 — docs only", not spin.

## Deliver

Output the notes in chat as Markdown by default. Offer to (a) drop them into a
`CHANGELOG.md`/release-notes file, or (b) create the GitHub release with
`mcp__github__create_repository`-family tools (use the existing tag scheme
`PolyKybd-fw-vX.Y.Z`). **Don't** create the release or push unless asked.

## Pitfalls

- **No git tags** — never `git describe`/`git tag` to find releases; they don't
  exist. Releases are the GitHub Releases API; history boundaries are the bump
  commits.
- **In-tree version ≠ released version.** `FW_VERSION` can be ahead of the latest
  GitHub release (work bumped but not yet released). "Since the last release" means
  *released*, so anchor the low end on the GitHub release, not the previous bump.
- **A bump interval can be empty** of polykybd changes (e.g. 0.8.24 was docs-only).
  Don't fabricate firmware changes — label it housekeeping.
- **The split-sync / core1 / suspend bugs** all have detailed write-ups in
  `keyboards/handwired/polykybd/CLAUDE.md` ("Investigations in progress"); reuse
  that root-cause text rather than re-deriving it from the diff.
