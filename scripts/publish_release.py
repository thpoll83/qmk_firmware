#!/usr/bin/env python3
"""Publish the prepared PolyKybd release with one OS-independent command.

Run from anywhere inside either repo checkout:

    python scripts/publish_release.py            # publish
    python scripts/publish_release.py --dry-run  # show what it would do

What it does (no bash-isms, no extra pip installs — Python 3.7+ stdlib only):
  1. Auto-detects the repo (firmware qmk_firmware / host PolyKybdHost /
     wincompose) and the current version from the DEFAULT branch (config.h /
     polyhost/_version.py / wincompose.csproj), so it is independent of
     whatever branch you have checked out.
  2. Reads the prepared release notes for that tag from the unprotected
     `release-notes` branch (`<TAG>.md`, first line `# <title>`, rest = body).
  3. Creates + publishes the GitHub Release (or updates it if it already exists),
     tagging the commit that DECLARES the prepared version rather than the
     branch head — so a merge landing between preparing the notes and
     publishing them cannot ship a binary whose version differs from its label.
     Firmware and wincompose: publishing fires the `release: published`
     workflow, which builds and attaches the assets (.bin/.uf2 / the installer
     + portable zip + SHA256SUMS) — you do NOT attach anything by hand.

Auth: uses `GH_TOKEN` / `GITHUB_TOKEN` if set, else `gh auth token`. No token and
no `gh` -> it tells you how to fix it. `gh` is optional; a token alone is enough.

Tags (created at the commit declaring <version>, found on the branch named):
  firmware    PolyKybd-fw-v<version>   (branch: PolyKybd)
  host        v<version>               (branch: main)
  wincompose  PK-<version>             (branch: main)
"""
import argparse
import json
import os
import re
import subprocess
import sys
import urllib.error
import urllib.request


def run(cmd):
    # Force UTF-8: git output (release notes) is UTF-8, but on Windows the
    # default is the locale codec (cp1252), which raises UnicodeDecodeError on
    # emoji/em-dashes and silently drops the output.
    try:
        return subprocess.run(cmd, capture_output=True, text=True,
                              encoding="utf-8", errors="replace")
    except (FileNotFoundError, NotADirectoryError, PermissionError) as e:
        # The executable isn't installed / isn't runnable. Report it as an
        # ordinary non-zero result so callers can fall back, instead of letting
        # it escape as a traceback. get_token() probes `gh`, which plenty of
        # machines don't have — on Windows that surfaced as a raw
        # "[WinError 2] The system cannot find the file specified" traceback
        # instead of the "no GitHub token…" message that was already written
        # for exactly this case.
        return subprocess.CompletedProcess(cmd, 127, stdout="", stderr=str(e))


def die(msg):
    sys.exit("publish_release: " + msg)


def repo_root():
    r = run(["git", "rev-parse", "--show-toplevel"])
    if r.returncode:
        die("not inside a git repository.")
    return r.stdout.strip()


def detect(root):
    """Return (kind, version_path, default_branch, tag_prefix)."""
    if os.path.exists(os.path.join(root, "keyboards", "polykybd", "config.h")):
        return ("firmware", "keyboards/polykybd/config.h", "PolyKybd", "PolyKybd-fw-v")
    if os.path.exists(os.path.join(root, "polyhost", "_version.py")):
        return ("host", "polyhost/_version.py", "main", "v")
    # The fork tags PK-<version> (GitVersion.yml tag-prefix), and the shipped
    # version is the csproj AssemblyVersion that iscc reads off the built exe.
    if os.path.exists(os.path.join(root, "src", "wincompose", "wincompose.csproj")):
        return ("wincompose", "src/wincompose/wincompose.csproj", "main", "PK-")
    die("can't tell which repo this is (no keyboards/polykybd/config.h, "
        "polyhost/_version.py or src/wincompose/wincompose.csproj).")


def show(ref_path):
    """`git show <ref>:<path>` -> text, or None if absent."""
    r = run(["git", "show", ref_path])
    return r.stdout if r.returncode == 0 else None


def parse_version(kind, text):
    if kind == "firmware":
        m = re.search(r'#define\s+FW_VERSION\s+"(\d+\.\d+\.\d+)"', text)
        if not m:
            die("couldn't find FW_VERSION in config.h.")
        return m.group(1)
    if kind == "wincompose":
        m = re.search(r'<AssemblyVersion>(\d+)\.(\d+)\.(\d+)', text)
        if not m:
            die("couldn't find <AssemblyVersion> in wincompose.csproj.")
        return f"{m.group(1)}.{m.group(2)}.{m.group(3)}"
    maj = re.search(r'__major__\s*=\s*(\d+)', text)
    mnr = re.search(r'__minor__\s*=\s*(\d+)', text)
    pat = re.search(r'__patch__\s*=\s*(\d+)', text)
    if not (maj and mnr and pat):
        die("couldn't parse __major__/__minor__/__patch__ from _version.py.")
    return f"{maj.group(1)}.{mnr.group(1)}.{pat.group(1)}"


def owner_repo(root):
    r = run(["git", "remote", "get-url", "origin"])
    m = re.search(r"[:/]([^/]+)/([^/]+?)(?:\.git)?/?$", r.stdout.strip())
    if not m:
        die(f"couldn't parse owner/repo from origin remote: {r.stdout.strip()!r}")
    return m.group(1), m.group(2)


def get_token():
    for var in ("GH_TOKEN", "GITHUB_TOKEN"):
        if os.environ.get(var):
            return os.environ[var]
    r = run(["gh", "auth", "token"])
    if r.returncode == 0 and r.stdout.strip():
        return r.stdout.strip()
    return None


def api(token, method, path, payload=None):
    url = "https://api.github.com" + path
    data = json.dumps(payload).encode() if payload is not None else None
    req = urllib.request.Request(url, data=data, method=method, headers={
        "Authorization": f"Bearer {token}",
        "Accept": "application/vnd.github+json",
        "X-GitHub-Api-Version": "2022-11-28",
        "Content-Type": "application/json",
        "User-Agent": "polykybd-publish-release",
    })
    try:
        with urllib.request.urlopen(req) as resp:
            return resp.status, json.load(resp)
    except urllib.error.HTTPError as e:
        try:
            return e.code, json.load(e)
        except Exception:
            return e.code, {"message": e.read().decode(errors="replace")}


def commit_for_version(kind, vpath, default_branch, version):
    """OLDEST commit on the default branch whose <vpath> declares `version`.

    The release must be tagged at a commit that actually reports the version the
    notes describe, not at whatever the branch head happens to be. Tagging the
    head means any merge between preparing the notes and publishing them ships a
    binary whose version differs from its label -- and since `bump-version.yml`
    has no paths filter, *every* merge bumps, docs-only ones included. Left
    unfixed that is not merely cosmetic: on 2026-08-29 nine merges landed inside
    that window, one of them the FW-9 engine-pack signing fix, which would have
    shipped under notes that never mentioned it.

    Resolved by reading the version file at each commit rather than by matching a
    `chore: bump ... version to X` message: it checks the property we actually
    care about, and it works for wincompose, which has no bump commits at all.

    ⚠️ OLDEST, not newest, and the difference is not academic. A version stays
    declared from its bump commit until the next bump, so several commits report
    it -- and the later ones are the NEXT release's work, sitting in the window
    before its own bump lands. Taking the newest match resolved v0.15.14 to a
    commit from PR #231, which actually shipped in 0.15.15; the oldest match is
    the bump commit, which is what every historical release was in fact tagged
    at.

    ⚠️ The whole history is walked, with NO early exit. An earlier version broke
    out on the first non-match after a match, on the theory that a version
    occupies one contiguous run. That is true in time but FALSE in a path-limited
    `git log`, where a PR's own commits and its merge commit interleave with the
    mainline: the break stopped at the end of the first run and returned a commit
    from a later release. Measured on this repo, 9 of 154 versions mis-resolved --
    0.15.17 to a PR #234 feature commit that shipped in 0.16.0, and 0.15.18 to
    that PR's merge commit. A full walk is ~1.1 s, so the break bought nothing.

    ⚠️ `--first-parent` is load-bearing, for the same reason as the missing
    break. `git log` orders by commit DATE, so commits merged in from a branch
    interleave with the mainline and "last in the newest-first list" is not
    topologically oldest -- measured, 6 of 151 versions then resolved to a
    commit that is not even an ancestor of their bump. Restricting to the
    mainline removes the interleaving at its source: 150 of 151 land exactly on
    the `chore: bump ...` commit. The one exception (0.9.2) had its bump made on
    a side branch, so first-parent resolves it to the merge that brought it onto
    the mainline -- which still declares 0.9.2, so the label-matches-binary
    invariant holds, which is the property that actually matters here.

    ⚠️ The pathspec is `:(top)`-prefixed because a plain `-- <path>` is
    CWD-RELATIVE. Run from a subdirectory -- which this script explicitly
    supports -- it matches nothing and `git log` exits 0 with an EMPTY list, so
    the returncode guard never fires and the caller silently falls back to the
    branch head. (`show()` is unaffected: `<rev>:<path>` is always root-relative,
    so nothing else looks wrong.)
    """
    r = run(["git", "log", "--format=%H", "--first-parent",
             f"origin/{default_branch}", "--", f":(top){vpath}"])
    if r.returncode:
        return None
    found = None
    for sha in r.stdout.split():  # git log is newest-first
        text = show(f"{sha}:{vpath}")
        if not text:
            continue
        try:
            if parse_version(kind, text) == version:
                found = sha  # keep walking back; the last match is the oldest
        except SystemExit:
            # An older revision the current parser can't read: skip it rather
            # than abort the publish.
            continue
    return found


def prepared_tags(prefix):
    """All prepared <prefix><X.Y.Z>.md files on the release-notes branch,
    as (version_tuple, tag) sorted ascending."""
    r = run(["git", "ls-tree", "--name-only", "origin/release-notes"])
    out = []
    for name in r.stdout.splitlines():
        if not (name.startswith(prefix) and name.endswith(".md")):
            continue
        m = re.fullmatch(r"(\d+)\.(\d+)\.(\d+)", name[len(prefix):-3])
        if m:
            out.append(((int(m[1]), int(m[2]), int(m[3])), name[:-3]))
    out.sort()
    return out


def main():
    ap = argparse.ArgumentParser(description="Publish the prepared PolyKybd release.")
    ap.add_argument("--dry-run", action="store_true", help="show what would happen, change nothing")
    ap.add_argument("--tag", help="publish a specific prepared tag instead of the newest one")
    args = ap.parse_args()

    # Print UTF-8 (emoji in the notes) even on a cp1252 Windows console.
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except Exception:
            pass

    root = repo_root()
    kind, vpath, default_branch, tag_prefix = detect(root)

    # Fetch the refs we read from (best-effort; fall back to whatever is local).
    run(["git", "fetch", "origin", default_branch, "release-notes"])

    # The release-notes branch is the source of truth for *what is ready to
    # publish* — NOT the tree version, which drifts forward on every PR merge
    # (each merge auto-bumps the version, so the tree is usually ahead of the
    # prepared release by the time you publish). Pick the newest prepared tag.
    prepared = prepared_tags(tag_prefix)
    if args.tag:
        tag = args.tag
    else:
        if not prepared:
            die("no prepared release notes on the release-notes branch "
                f"(no {tag_prefix}<X.Y.Z>.md files).\n"
                "  Draft + stage them first with the polykybd-github-release skill.")
        tag = prepared[-1][1]
        if len(prepared) > 1:
            others = ", ".join(t for _, t in prepared[:-1])
            print(f"note: newest prepared tag is {tag}. Others on the branch: {others}")
            print(f"      (use --tag to publish a specific one.)")

    # One guard for both paths. prepared_tags() matches on FILENAME only, so an
    # empty or unreadable <TAG>.md reaches here on the auto-select path too --
    # where it used to raise IndexError on lines[0] (or AttributeError on a None
    # from show()) instead of saying what was wrong.
    notes = show(f"origin/release-notes:{tag}.md")
    if not notes or not notes.strip():
        die(f"no prepared notes for {tag} on the release-notes branch "
            f"(expected release-notes:{tag}.md, non-empty).")

    # Only the newest prepared tag becomes "Latest release". The notes branch is
    # an append-only archive and --tag exists to publish an older one, so an
    # unconditional make_latest would re-point /releases/latest -- and the tray
    # updater that reads it -- at older firmware.
    newest_tag = prepared[-1][1] if prepared else tag
    is_latest = (tag == newest_tag)

    # Tag the commit that DECLARES this version, not the branch head -- see
    # commit_for_version(). Falls back to the head so a publish never becomes
    # impossible, but says so loudly, because the fallback is the old broken
    # behaviour and must not pass unnoticed.
    version = tag[len(tag_prefix):]
    target = commit_for_version(kind, vpath, default_branch, version)
    if target is None:
        shallow = (run(["git", "rev-parse", "--is-shallow-repository"])
                   .stdout.strip() == "true")
        print(f"WARNING: no commit on {default_branch} declares version {version}; "
              f"falling back to the branch head.")
        if shallow:
            print("         This clone is SHALLOW, so the search saw truncated "
                  "history. Run `git fetch origin --unshallow` and retry.")
        else:
            print("         The published binary may then report a different "
                  "version than this tag's label. Check before announcing it.")
        target = default_branch
        target_desc = default_branch
    else:
        target_desc = f"{target[:10]} (declares {version})"

    # Informational: the tree drifting past the prepared tag is normal and, now
    # that the tag is pinned above, harmless -- later merges ship in the NEXT
    # release rather than silently joining this one.
    vtext = show(f"origin/{default_branch}:{vpath}")
    if vtext:
        try:
            tree_tag = tag_prefix + parse_version(kind, vtext)
            if tree_tag != tag:
                print(f"note: default branch has moved on to {tree_tag}; publishing "
                      f"prepared {tag} at its own commit, so the drift is harmless.")
        except SystemExit:
            pass
    lines = notes.splitlines()
    title = re.sub(r"^#\s*", "", lines[0]).strip()
    body = "\n".join(lines[1:]).strip("\n")
    if not title:
        die(f"{tag}.md has an empty title line (first line must be '# <title>').")

    owner, repo = owner_repo(root)

    print(f"repo    : {owner}/{repo}  ({kind})")
    print(f"tag     : {tag}   target: {target_desc}")
    print(f"title   : {title}")
    if not is_latest:
        print(f"latest  : NO — {newest_tag} is newer and keeps the Latest badge")
    print(f"body    : {len(body)} chars, {body.count(chr(10)) + 1} lines")
    print("-" * 60)
    print(body)
    print("-" * 60)

    if args.dry_run:
        print("dry-run: nothing published.")
        return

    token = get_token()
    if not token:
        die("no GitHub token. Set GH_TOKEN / GITHUB_TOKEN, or install gh and run `gh auth login`.")

    status, rel = api(token, "GET", f"/repos/{owner}/{repo}/releases/tags/{tag}")
    if status == 200:
        st, res = api(token, "PATCH", f"/repos/{owner}/{repo}/releases/{rel['id']}",
                      {"name": title, "body": body, "make_latest": str(is_latest).lower(), "draft": False})
        if st >= 300:
            die(f"updating existing release failed ({st}): {res.get('message')}")
        print(f"updated existing release {tag}")
        print(res.get("html_url"))
        print("note: an already-published release does not re-trigger the build; "
              "assets are only (re)built when the release is first published.")
        return

    st, res = api(token, "POST", f"/repos/{owner}/{repo}/releases", {
        "tag_name": tag,
        "target_commitish": target,
        "name": title,
        "body": body,
        "make_latest": str(is_latest).lower(),
        "draft": False,
        "prerelease": False,
    })
    if st >= 300:
        die(f"creating release failed ({st}): {res.get('message')}")
    print(f"published release {tag}")
    print(res.get("html_url"))
    if kind == "firmware":
        print("firmware CI (release: published) will now build and attach the .bin/.uf2.")
    elif kind == "wincompose":
        print("wincompose CI (release: published) will now build and attach the "
              "installer .exe, the portable .zip and SHA256SUMS.txt.")


if __name__ == "__main__":
    main()
