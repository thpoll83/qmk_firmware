#!/usr/bin/env python3
"""Publish the prepared PolyKybd release with one OS-independent command.

Run from anywhere inside either repo checkout:

    python scripts/publish_release.py            # publish
    python scripts/publish_release.py --dry-run  # show what it would do

What it does (no bash-isms, no extra pip installs — Python 3.7+ stdlib only):
  1. Auto-detects the repo (firmware qmk_firmware vs host PolyKybdHost) and the
     current version from the DEFAULT branch (config.h / polyhost/_version.py),
     so it is independent of whatever branch you have checked out.
  2. Reads the prepared release notes for that tag from the unprotected
     `release-notes` branch (`<TAG>.md`, first line `# <title>`, rest = body).
  3. Creates + publishes the GitHub Release (or updates it if it already exists).
     Firmware: publishing fires the `release: published` workflow, which builds
     and attaches the .bin/.uf2 — you do NOT attach anything by hand.

Auth: uses `GH_TOKEN` / `GITHUB_TOKEN` if set, else `gh auth token`. No token and
no `gh` -> it tells you how to fix it. `gh` is optional; a token alone is enough.

Tags:
  firmware  PolyKybd-fw-v<version>   (target branch: PolyKybd)
  host      v<version>               (target branch: main)
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
    return subprocess.run(cmd, capture_output=True, text=True)


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
    die("can't tell which repo this is (no keyboards/polykybd/config.h or polyhost/_version.py).")


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


def main():
    ap = argparse.ArgumentParser(description="Publish the prepared PolyKybd release.")
    ap.add_argument("--dry-run", action="store_true", help="show what would happen, change nothing")
    args = ap.parse_args()

    root = repo_root()
    kind, vpath, default_branch, tag_prefix = detect(root)

    # Fetch the refs we read from (best-effort; fall back to whatever is local).
    run(["git", "fetch", "origin", default_branch, "release-notes"])

    vtext = show(f"origin/{default_branch}:{vpath}")
    if vtext is None:
        with open(os.path.join(root, vpath), encoding="utf-8") as fh:
            vtext = fh.read()
    version = parse_version(kind, vtext)
    tag = tag_prefix + version

    notes = show(f"origin/release-notes:{tag}.md")
    if not notes or not notes.strip():
        die(
            f"no prepared notes for {tag} on the release-notes branch "
            f"(expected release-notes:{tag}.md).\n"
            f"  Draft + stage them first with the polykybd-github-release skill."
        )
    lines = notes.splitlines()
    title = re.sub(r"^#\s*", "", lines[0]).strip()
    body = "\n".join(lines[1:]).strip("\n")
    if not title:
        die(f"{tag}.md has an empty title line (first line must be '# <title>').")

    owner, repo = owner_repo(root)

    print(f"repo    : {owner}/{repo}  ({kind})")
    print(f"tag     : {tag}   target: {default_branch}")
    print(f"title   : {title}")
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
                      {"name": title, "body": body, "make_latest": "true", "draft": False})
        if st >= 300:
            die(f"updating existing release failed ({st}): {res.get('message')}")
        print(f"updated existing release {tag}")
        print(res.get("html_url"))
        print("note: an already-published release does not re-trigger the build; "
              "assets are only (re)built when the release is first published.")
        return

    st, res = api(token, "POST", f"/repos/{owner}/{repo}/releases", {
        "tag_name": tag,
        "target_commitish": default_branch,
        "name": title,
        "body": body,
        "make_latest": "true",
        "draft": False,
        "prerelease": False,
    })
    if st >= 300:
        die(f"creating release failed ({st}): {res.get('message')}")
    print(f"published release {tag}")
    print(res.get("html_url"))
    if kind == "firmware":
        print("firmware CI (release: published) will now build and attach the .bin/.uf2.")


if __name__ == "__main__":
    main()
