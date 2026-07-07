# release-notes branch

This orphan branch holds **crafted GitHub Release notes**, one file per release
tag. It is read by `.github/workflows/release.yml` at release time.

## How it works
When a release tag is created, `release.yml` fetches `<TAG>.md` from the root of
this branch (`gh api …/contents/<TAG>.md?ref=release-notes`):
- **first line** `# <title>` → the release **title**
- **rest of the file** → the release **body**

If the file is absent, CI falls back to `--generate-notes` and a generic title,
so this branch is entirely optional.

## Conventions
- Firmware tags: `PolyKybd-fw-vX.Y.Z.md`  ·  Host tags: `vX.Y.Z.md`
- **One file per tag — never overwritten or deleted.** Each release adds a new
  file; the branch is an accumulating changelog archive.
- Files are normally written by the `polykybd-github-release` Claude Code skill
  after the notes are reviewed, then the matching tag is pushed to trigger CI.
