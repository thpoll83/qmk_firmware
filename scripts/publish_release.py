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
     When NO commit declares it — the bump has not merged yet — there is
     nothing to pin to, and creating the release is refused rather than
     shipping a build that is not the version on the label
     (`--allow-version-mismatch` overrides; read its --help, since what that
     costs differs per repo). Creating is also refused when the tag ALREADY
     exists somewhere other than the pin, because a release never moves a tag.
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


# ⚠️ Every urlopen() here passes this. Without it urllib uses the socket
# default, which is None -- block forever -- so a connection GitHub accepts and
# then stops answering hangs the script instead of failing. That defeats
# release_exists()'s whole design, which is to return None on any trouble and
# let the caller take the stricter path, and it hangs --dry-run, the one command
# that must stay quick and safe. (Found by Greptile on PolyKybdHost#318, for
# release_exists; api() had it too and is fixed in the same pass.)
API_TIMEOUT = 30


def run(cmd):
    # Force UTF-8: git output (release notes) is UTF-8, but on Windows the
    # default is the locale codec (cp1252), which raises UnicodeDecodeError on
    # emoji/em-dashes and silently drops the output.
    try:
        return subprocess.run(cmd, capture_output=True, text=True,
                              encoding="utf-8", errors="replace")
    except OSError as e:
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


def version_tuple(v):
    """'0.9.20' -> (0, 9, 20), so 0.10.0 sorts after 0.9.20 where a string
    compare would not."""
    return tuple(int(x) for x in v.split("."))


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
        with urllib.request.urlopen(req, timeout=API_TIMEOUT) as resp:
            return resp.status, json.load(resp)
    except urllib.error.HTTPError as e:
        try:
            return e.code, json.load(e)
        except Exception:
            return e.code, {"message": e.read().decode(errors="replace")}


def release_exists(owner, repo, tag, token):
    """True, False, or None when it cannot be determined.

    A GET on a public repo's release needs no token, so this answers under
    --dry-run too. None means "do not know" (network trouble, a rate limit, a
    private repo with no token), and the caller must then assume the stricter
    case.

    ⚠️ Whether a TAG exists is a different question and not a usable
    substitute: a deleted release, a hand-pushed tag, or a release build that
    died before `gh release create` all leave a tag with no release behind it.
    """
    headers = {
        "Accept": "application/vnd.github+json",
        "X-GitHub-Api-Version": "2022-11-28",
        "User-Agent": "polykybd-publish-release",
    }
    if token:
        headers["Authorization"] = f"Bearer {token}"
    req = urllib.request.Request(
        f"https://api.github.com/repos/{owner}/{repo}/releases/tags/{tag}",
        method="GET", headers=headers)
    try:
        with urllib.request.urlopen(req, timeout=API_TIMEOUT) as resp:
            return resp.status == 200
    except urllib.error.HTTPError as e:
        # ⚠️ Only a 404 is "no release". Folding 401/403/5xx in with it is what
        # lets an auth or network failure read as success. (Found by Revix and
        # Greptile on wincompose#25.)
        return False if e.code == 404 else None
    except Exception:
        return None


def _get_json(owner, repo, path, token):
    """GET a public endpoint; (status, payload) or (None, None) if unreachable."""
    headers = {
        "Accept": "application/vnd.github+json",
        "X-GitHub-Api-Version": "2022-11-28",
        "User-Agent": "polykybd-publish-release",
    }
    if token:
        headers["Authorization"] = f"Bearer {token}"
    req = urllib.request.Request(
        f"https://api.github.com/repos/{owner}/{repo}{path}", method="GET", headers=headers)
    try:
        with urllib.request.urlopen(req, timeout=API_TIMEOUT) as resp:
            return resp.status, json.load(resp)
    except urllib.error.HTTPError as e:
        return e.code, None
    except Exception:
        return None, None


def tag_commit(owner, repo, tag, token):
    """Where an existing tag points: a sha, "" for a confirmed 404 (no such
    tag), or None when it cannot be determined.

    ⚠️ The three are NOT interchangeable, and collapsing them is how this check
    goes quiet: the caller SKIPS its guard when there is no tag, so a 5xx or a
    timeout reading as "no tag" lets a publish through at whatever commit the
    tag really points at. That is the same shape as the failed-lookup bug
    already fixed once in release_exists() -- only a 404 is an answer.
    (Found by Greptile on PolyKybdHost#318 and qmk_firmware#356.)

    ⚠️ Needed because `target_commitish` is documented as "Unused if the Git tag
    already exists" -- creating a release does not move a tag. So when the tag is
    already there, the pin computed by commit_for_version() is not what gets
    built, and reporting it would be a lie. Live example: wincompose PK-0.9.20's
    tag sits on the pre-bump commit while the pin resolves the bump commit.

    ⚠️ An ANNOTATED tag's ref points at the tag OBJECT, not the commit, so it is
    dereferenced one step.
    """
    st, ref = _get_json(owner, repo, f"/git/ref/tags/{tag}", token)
    if st == 404:
        return ""
    if st != 200 or not ref:
        return None
    obj = ref.get("object") or {}
    if obj.get("type") == "commit":
        return obj.get("sha") or None
    if obj.get("type") == "tag":
        st, tg = _get_json(owner, repo, f"/git/tags/{obj.get('sha')}", token)
        if st == 200 and tg:
            return (tg.get("object") or {}).get("sha") or None
        return None
    # A ref shape this does not understand is not an answer either.
    return None


def published_latest_version(owner, repo, tag_prefix, token):
    """Version of the release GitHub currently calls "latest", or None.

    ⚠️ This is the question that decides the Latest badge, and it is NOT the same
    as "the newest prepared notes file". Notes can be staged for a version that
    has not shipped, and comparing against them withholds the badge from the
    release being published -- leaving `releases/latest`, which the download
    links resolve, on an OLDER release while the update file names the new one.
    Every install is then offered the new version and handed the old one, which
    is the 0.9.18 failure over again. (Found by Greptile on wincompose#27.)

    404 means there is no published release yet, so whatever is being published
    is the newest; that is reported as the empty string rather than None, which
    means "could not tell".
    """
    st, rel = _get_json(owner, repo, "/releases/latest", token)
    if st == 404:
        return ""
    if st != 200 or not rel:
        return None
    name = rel.get("tag_name") or ""
    if not name.startswith(tag_prefix):
        return None
    rest = name[len(tag_prefix):]
    return rest if re.fullmatch(r"\d+\.\d+\.\d+", rest) else None


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
    ap.add_argument("--allow-version-mismatch", action="store_true",
                    help="publish a NEW release even though no commit declares its version and "
                         "the branch head's differs. What this costs depends on the repo: "
                         "wincompose's workflow asserts the built version against the tag and "
                         "refuses the assets, leaving a release with no downloads; the firmware "
                         "workflow names its assets FROM THE TAG without checking, so it ships a "
                         "binary whose filename and reported version disagree. Prefer merging "
                         "the bump; use this only to get notes up, and attach assets later with "
                         "a workflow_dispatch")
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

    owner, repo = owner_repo(root)
    token = get_token()
    version = tag[len(tag_prefix):]

    # Does this release take the "Latest" badge? /releases/latest is what the
    # download links resolve, so it must not be re-pointed at an OLDER release
    # (--tag exists to publish one). The comparison is against what is
    # PUBLISHED, not against the newest prepared notes: notes can be staged for
    # a version that has not shipped, and comparing against them withholds the
    # badge from the release being published -- see published_latest_version().
    published = published_latest_version(owner, repo, tag_prefix, token)
    if published is None:
        # Could not tell. Fall back to the notes comparison, which is the older
        # behaviour: it can withhold the badge but never steals it for an older
        # release, and says so either way.
        newest_tag = prepared[-1][1] if prepared else tag
        is_latest = (tag == newest_tag)
        latest_why = "could not read the published latest release; judged from the prepared notes"
    elif published == "":
        is_latest = True
        latest_why = "no published release yet"
    else:
        is_latest = version_tuple(version) >= version_tuple(published)
        latest_why = f"published latest is {tag_prefix}{published}"

    # Tag the commit that DECLARES this version, not the branch head -- see
    # commit_for_version(). Falls back to the head so a publish never becomes
    # impossible, but says so loudly, because the fallback is the old broken
    # behaviour and must not pass unnoticed.
    target = commit_for_version(kind, vpath, default_branch, version)
    pinned = target is not None
    shallow = False
    if not pinned:
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

    # Does publishing CREATE the release? That is what triggers a build, and so
    # what both guards below are conditioned on: re-applying notes to an
    # already-published release re-runs nothing.
    exists = release_exists(owner, repo, tag, token)
    # Unknown counts as creating: the stricter case, since guessing the other
    # way is what publishes a release whose assets are wrong.
    creating = exists is not True
    unknown_note = ""
    if exists is None:
        unknown_note = (f"\n  (could not reach the API to check whether {tag} already exists, so\n"
                        f"  this assumes it does not -- the stricter reading.)")

    # ⚠️ An EXISTING tag beats the pin, and nothing about the pin says so.
    # `target_commitish` is documented as "Unused if the Git tag already
    # exists" -- creating a release does not move a tag. So when the tag is
    # already there, the build comes from wherever it points and the pin is
    # only a claim. Live case: wincompose PK-0.9.20's tag sits on the pre-bump
    # commit, its release was deleted, and the pin resolves the bump commit --
    # so the script would have printed the right commit and published the wrong
    # one. This fires even when the version check below passes, which is the
    # whole point: there the pin found the correct commit and the mismatch
    # guard sees nothing wrong. (Found by Greptile on wincompose#27.)
    if creating:
        at = tag_commit(owner, repo, tag, token)
        if at is None:
            die(f"cannot tell whether the tag {tag} already exists or where it points, so\n"
                f"  whether the build would come from {target[:10]} is unknown -- and an\n"
                f"  existing tag is never moved by publishing. Refusing rather than guessing.\n"
                f"  Retry when the GitHub API is reachable." + unknown_note)
        if at and target != default_branch and at != target:
            die(f"the tag {tag} already exists, at {at[:10]}, and creating a release does\n"
                f"  not move it -- so the build would come from {at[:10]}, not from the\n"
                f"  {target[:10]} this would report. Either dispatch the release workflow on a\n"
                f"  ref that declares {version} to attach assets to the existing tag, or move\n"
                f"  the tag to {target[:10]} and publish." + unknown_note)
        if at and target == default_branch:
            print(f"note: the tag {tag} already exists, at {at[:10]}; creating a release does "
                  f"not move it,\n      so that commit is what gets built.")

    # What the default branch declares right now. Read only to describe the
    # drift or to refuse; the tag's own commit is what gets published.
    vtext = show(f"origin/{default_branch}:{vpath}")
    tree_ver = None
    if vtext:
        try:
            tree_ver = parse_version(kind, vtext)
        except SystemExit:
            pass

    if pinned:
        # Drift is normal and harmless here: every merge auto-bumps, so the tree
        # is usually ahead of the prepared tag by the time you publish, and the
        # later merges ship in the NEXT release rather than silently joining
        # this one. The pin is what makes it harmless -- do NOT turn this into a
        # refusal, or almost every legitimate firmware publish stops.
        if tree_ver and tree_ver != version:
            print(f"note: default branch has moved on to {tag_prefix}{tree_ver}; publishing "
                  f"prepared {tag} at its own commit, so the drift is harmless.")
    elif tree_ver and tree_ver != version:
        # ⚠️ Here the pin found NOTHING and the head declares something else, so
        # the branch head is what gets built and its version is what labels the
        # assets. That is how PK-0.9.19 shipped WinCompose-Setup-0.9.18.exe
        # (wincompose issue #21): published 50 minutes before its own bump
        # merged, so no commit declared the new version and the pin had nothing
        # to find. The release workflow asserts the built version against the
        # tag now, which turns the same mistake into a published release with NO
        # downloads at all -- this is the check that stops it before anything is
        # published.
        #
        # Enforced only when publishing would CREATE the release (see above).
        where = "behind" if version_tuple(tree_ver) < version_tuple(version) else "ahead of"
        msg = (f"{default_branch} is at {tree_ver}, {where} the prepared {tag}, and no commit\n"
               f"  on it declares {version}. The build takes its version from {vpath}, not\n"
               f"  from the tag, so what ships would be built from {tree_ver} while the\n"
               f"  release says {version}. Merge the bump that sets {vpath} to {version} on\n"
               f"  {default_branch}, then publish.")
        if shallow:
            # ⚠️ The search may simply not have SEEN the commit. Say so first:
            # told to "merge the bump" for a version that was bumped months ago,
            # the obvious next move is --allow-version-mismatch, which publishes
            # the very release this is trying to prevent.
            msg += ("\n  This clone is SHALLOW, so the search saw truncated history and the\n"
                    "  commit may well exist. Run `git fetch origin --unshallow` and retry\n"
                    "  BEFORE reaching for --allow-version-mismatch.")
        msg += unknown_note
        # ⚠️ What the override actually costs is NOT the same in every repo, so
        # do not promise one outcome. wincompose's release workflow asserts the
        # built exe's version against the tag and refuses the assets, leaving a
        # release with no downloads. The firmware workflow does neither: it
        # NAMES its assets from the tag (`VER="${TAG#PolyKybd-fw-v}"`), so the
        # override there publishes a .bin whose filename and internal
        # FW_VERSION disagree -- which is the mis-flash that sent a firmware
        # bisect to a wrong conclusion for several rounds (#258).
        # (Found by Greptile on qmk_firmware#356.)
        if creating and not args.allow_version_mismatch:
            die(msg + "\n  (--allow-version-mismatch publishes anyway; read its --help first.)")
        if creating:
            print("warning: " + msg)
            print("         --allow-version-mismatch given. Check what this repo's release")
            print("         workflow does with assets whose version is not the tag's: it")
            print("         either refuses them, leaving no downloads, or names them from")
            print("         the tag and ships a binary that reports something else.")
        else:
            print(f"note: {default_branch} is at {tag_prefix}{tree_ver}; re-applying notes to the "
                  f"existing {tag} (no build runs, so the difference does not matter).")
    lines = notes.splitlines()
    title = re.sub(r"^#\s*", "", lines[0]).strip()
    body = "\n".join(lines[1:]).strip("\n")
    if not title:
        die(f"{tag}.md has an empty title line (first line must be '# <title>').")

    print(f"repo    : {owner}/{repo}  ({kind})")
    print(f"tag     : {tag}   target: {target_desc}")
    print(f"title   : {title}")
    # Always printed, in both directions: which release keeps the Latest badge
    # decides where the download links point, and the REASON is what tells you
    # the decision came from the published releases rather than from a fallback.
    print(f"latest  : {'YES' if is_latest else 'NO'} — {latest_why}")
    print(f"body    : {len(body)} chars, {body.count(chr(10)) + 1} lines")
    print("-" * 60)
    print(body)
    print("-" * 60)

    if args.dry_run:
        print("dry-run: nothing published.")
        return

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
