#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Refuse to publish a release that no firmware-APPLY run has ever covered.

The HID-apply brick (qmk#258) shipped in a release because nothing applied a
firmware image on hardware: the rig flashes by UF2 over GPIO BOOTSEL, which
bypasses ``fw_staging`` entirely. ``qmk-test.yml``'s fwapply tier now runs on
every merge to ``PolyKybd``, so in the normal case the commit being released is
already covered and this script is a formality. It exists for the case that is
not normal — a hand-made tag, a re-published release, a merge whose rig run was
red and got forgotten — where the failure is silent and expensive.

⚠️ **The release tag does NOT point at a commit any workflow ran on.** Release
tags land on the auto-bump ``chore: … [skip ci]`` commit (``bump-version.yml``),
and ``[skip ci]`` suppresses the run — so a gate that demanded a run on
``github.sha`` would fail every single release. The bump touches exactly one
file, so this walks back through ancestors for a covered commit and then proves
the delta to the release commit is *only* the version bump. Accepting an
ancestor without that proof would be worse than no gate at all: it would report
coverage that belongs to different firmware.

Exits 0 (with the run it accepted) or 1 (with what to do about it).
"""

import json
import os
import re
import sys
import urllib.error
import base64
import urllib.request

API = "https://api.github.com"
WORKFLOW = "qmk-test.yml"
WORKFLOW_PATH = os.path.join(".github", "workflows", WORKFLOW)
# The same path as GitHub spells it in a compare/contents response, which is
# always POSIX regardless of the runner.
WORKFLOW_PATH_POSIX = ".github/workflows/" + WORKFLOW
# ⚠️ The files that DECIDE this gate's verdict. A delta touching one of them is
# NEVER cleared by the path filter, whatever that filter says, because the
# filter it would be judged by is one the delta itself could have written.
# Found in review of #300: release.yml checks out the RELEASE sha, so without
# this one commit could drop the `.github/workflows/qmk-test.yml` re-include,
# add `!keyboards/**`, and edit the firmware in the same breath — and all three
# files would read as harmless. Reproduced before fixing.
#
# The consequence is intended: changing the policy, or this script, costs the
# next release a fresh rig run. That is the correct price for editing the thing
# that decides whether a release is safe.
#
# The line is drawn at "decides whether a release is SAFE", which is why
# release.yml is here and bump-version.yml is not: one controls whether this
# gate runs and on which sha, the other only picks a version number.
SELF_PATHS = (
    WORKFLOW_PATH_POSIX,                                  # the policy
    "keyboards/polykybd/tools/require_fwapply_run.py",    # the gate
    ".github/workflows/release.yml",                      # what invokes it
)
JOB_ID = "fwapply-test"
# Fallback only. The real name is DERIVED from the workflow (see job_name): a
# hardcoded string here would silently stop matching the day someone renames the
# job, and a gate that matches nothing reports "never covered" for firmware that
# was — the enumerating-guard failure this repo keeps getting caught by.
JOB_NAME = "Firmware apply round-trip (split72)"
# The bump commit edits only this, and only its version line.
VERSION_FILE = "keyboards/polykybd/config.h"
# BOTH macros, because bump-version.yml writes either one depending on the merged
# PR's label: `bump:protocol` increments PROTOCOL_VERSION and leaves the semver
# alone, so FW_VERSION is re-substituted with the same value and produces no diff
# line at all. Accepting only FW_VERSION would refuse a perfectly well-covered
# protocol release — and it would do so at publish time, which is the worst
# moment to discover it. This does not widen the gate: any change outside these
# lines, or outside this file, still fails.
VERSION_MACRO = "FW_VERSION"
VERSION_MACROS = ("FW_VERSION", "PROTOCOL_VERSION")
# `+/-` stripped by the caller; leading whitespace allowed, nothing else before
# the directive and nothing but the macro name after it.
VERSION_DEFINE_RE = re.compile(
    r"^\s*#\s*define\s+(?:" + "|".join(VERSION_MACROS) + r")\b")
MAX_ANCESTORS = 12
# GitHub's compare API returns at most this many files, silently truncated. A
# truncated list cannot prove "nothing else changed", so it is refused rather
# than trusted — the whole gate rests on that proof.
COMPARE_FILE_CAP = 300
# The delta from a covered ancestor to the release commit is the auto-bump: one
# commit. Anything more is not the bump, and bounding it means the file list is
# small enough that the cap above cannot have hidden anything.
# A bound on the walk, NOT the proof. The proof that "nothing else changed" is
# the file list itself: COMPARE_FILE_CAP above refuses a list that may have been
# truncated, and every file in it must then be individually cleared by
# delta_is_provably_harmless(). This number only stops an unbounded compare, so
# it can be generous now that a delta may legitimately carry several docs merges
# as well as the bump. (It was 2 when the ONLY acceptable delta was one bump
# commit; that is no longer the shape being bounded.)
MAX_DELTA_COMMITS = 25


def job_name(workflow_text=None):
    """The display name GitHub reports for the apply job.

    Read out of the checked-out workflow rather than hardcoded, so renaming the
    job cannot quietly turn this gate into a no-op. Falls back to the constant
    when the file is unreadable (the script is also runnable outside a checkout).

    A deliberate line scan rather than a YAML parse: PyYAML is not guaranteed on
    a runner, and the shape being read is two adjacent lines at known indents.
    """
    if workflow_text is None:
        try:
            with open(WORKFLOW_PATH) as fh:
                workflow_text = fh.read()
        except OSError:
            return JOB_NAME
    in_job = False
    for line in workflow_text.splitlines():
        if re.match(rf"^  {re.escape(JOB_ID)}:\s*$", line):
            in_job = True
            continue
        if in_job:
            m = re.match(r"^    name:\s*(.+?)\s*$", line)
            if m:
                return m.group(1).strip("'\"")
            # A new job started (two-space indent, non-comment) before any name.
            if re.match(r"^  \S", line):
                break
    return JOB_NAME


def api(path, token):
    req = urllib.request.Request(
        f"{API}{path}",
        headers={"Authorization": f"Bearer {token}",
                 "Accept": "application/vnd.github+json",
                 "User-Agent": "polykybd-release-gate"})
    with urllib.request.urlopen(req, timeout=30) as fh:
        return json.load(fh)


def _glob_to_re(pattern):
    """GitHub Actions path-glob semantics, as a regex.

    `**` crosses directory separators, `*` and `?` do not. Translated by hand
    because fnmatch's `*` crosses `/`, which would make `!**.md` and `!*.md`
    mean the same thing and quietly widen every exclusion.
    """
    out, i = [], 0
    while i < len(pattern):
        if pattern.startswith("**", i):
            out.append(".*")
            i += 2
        elif pattern[i] == "*":
            out.append("[^/]*")
            i += 1
        elif pattern[i] == "?":
            out.append("[^/]")
            i += 1
        else:
            out.append(re.escape(pattern[i]))
            i += 1
    return re.compile("^" + "".join(out) + "$")


def build_path_filter(workflow_text=None):
    """The ordered `paths:` list from qmk-test.yml, or None if unreadable.

    ⚠️ ONE source of truth, and it is the workflow's, not a copy kept here. That
    filter already answers exactly the question this gate needs — "can a change
    to this file reach a firmware image?" — and its own comment states the test:
    *whether the build or the rig ever READS it*. A second list here would be a
    second thing to keep in step, which is the guard shape this repo keeps
    getting caught by; reading the workflow means an edit there moves both.

    ⚠️ Returns None when the block cannot be found or is empty, and every caller
    must treat that as "assume everything reaches the image". A parse that fails
    OPEN would hand a release the one proof this gate exists to demand.
    """
    if workflow_text is None:
        try:
            with open(WORKFLOW_PATH, encoding="utf-8") as fh:
                workflow_text = fh.read()
        except OSError:
            return None
    lines = workflow_text.splitlines()
    for idx, line in enumerate(lines):
        if not re.match(r"^(\s*)paths:\s*$", line):
            continue
        indent = len(line) - len(line.lstrip())
        patterns = []
        for follow in lines[idx + 1:]:
            if not follow.strip() or follow.lstrip().startswith("#"):
                continue
            if len(follow) - len(follow.lstrip()) <= indent:
                break
            m = re.match(r"^\s*-\s*['\"]?([^'\"#]+?)['\"]?\s*(?:#.*)?$", follow)
            if not m:
                break
            patterns.append(m.group(1))
        return patterns or None
    return None


def build_path_filter_at(repo, ref, token):
    """The filter as it stood at a commit a green rig run already covered.

    ⚠️ Read from the COVERED commit, never from the working checkout. release.yml
    checks out the commit being RELEASED, so a filter read from disk is one the
    delta under judgement may have written — the delta would be setting its own
    policy. The covered commit's copy is the one a rig run actually vouched for.

    Fails CLOSED (None) on any error: a missing file, a non-base64 body, undecodable
    bytes or a filter that will not parse.
    """
    try:
        blob = api(f"/repos/{repo}/contents/{WORKFLOW_PATH_POSIX}?ref={ref}", token)
    except urllib.error.HTTPError:
        return None
    if not isinstance(blob, dict) or blob.get("encoding") != "base64":
        return None
    try:
        text = base64.b64decode(blob.get("content") or "").decode("utf-8")
    except (ValueError, UnicodeDecodeError):
        return None
    return build_path_filter(text)


def path_reaches_the_image(path, patterns):
    """Replay the workflow's filter for one file. LAST match wins.

    ⚠️ Fails CLOSED: with no usable filter, every path is treated as reaching the
    image, which reduces this gate to its old behaviour rather than disabling it.
    A path matching nothing at all is also 'reaches' — being unlisted is not
    evidence of safety.
    """
    if not patterns:
        return True
    verdict = True
    matched = False
    for pattern in patterns:
        negated = pattern.startswith("!")
        body = pattern[1:] if negated else pattern
        if _glob_to_re(body).match(path):
            verdict = not negated
            matched = True
    return verdict if matched else True


def delta_is_provably_harmless(files, patterns):
    """True when every file in the delta provably cannot change the image.

    Two ways a file can clear: it is the auto-bump (the version macros in
    VERSION_FILE, nothing else), or its path is one the firmware build and the
    rig never read — the `!` entries in the workflow's filter, which is how a
    docs-only or skills-only merge between a rig run and a release stops costing
    a fresh round-trip.

    ⚠️ A RENAME is judged on both names. Moving a .c to a .md removes a build
    input, so the new path being harmless proves nothing on its own.
    """
    for f in files:
        name = f.get("filename")
        if not name:
            return False
        previous = f.get("previous_filename")
        # ⚠️ Before the filter is consulted at all: a change to the gate's own
        # inputs cannot be judged by them.
        if name in SELF_PATHS or (previous and previous in SELF_PATHS):
            return False
        if previous and path_reaches_the_image(previous, patterns):
            return False
        if not path_reaches_the_image(name, patterns):
            continue
        if not only_a_version_bump([f]):
            return False
    return True


def only_a_version_bump(files):
    """True when this diff is exactly the auto-bump and nothing else.

    Pure so it can be tested without the network. ``files`` is the GitHub
    compare API's ``files`` list; an empty list means the commits are identical,
    which is the ideal case (the release sha itself was covered).
    """
    for f in files:
        if f.get("filename") != VERSION_FILE:
            return False
        # Every changed line in the patch must be the version macro. A patch is
        # absent for a pure rename/mode change — refuse rather than guess.
        patch = f.get("patch")
        if not patch:
            return False
        for line in patch.splitlines():
            if line[:1] in ("+", "-") and not line.startswith(("+++", "---")):
                # ⚠️ An explicit `#define <MACRO>` definition, not merely a line
                # MENTIONING the token. config.h has other lines that reference
                # these macros, so a substring test would accept a real code edit
                # sitting next to one — precisely the change this gate exists to
                # refuse (caught in review of #264).
                if not VERSION_DEFINE_RE.match(line[1:]):
                    return False
    return True


def compare_is_conclusive(compare):
    """True when a compare response can PROVE what changed.

    Pure, so it is testable without the network. Two ways the API can leave the
    question open, both of which must refuse rather than assume:

    * the ``files`` list is capped at ``COMPARE_FILE_CAP`` and silently truncated
      — a partial list cannot establish "and nothing else";
    * the delta is more commits than an auto-bump can be, which means whatever is
      being compared is not the bump at all.

    Returns ``(ok, reason)``; ``reason`` is empty when ok.
    """
    files = compare.get("files")
    if files is None:
        return False, "the compare response carried no file list"
    if len(files) >= COMPARE_FILE_CAP:
        return False, (f"the compare lists {len(files)} files, at or past GitHub's "
                       f"{COMPARE_FILE_CAP}-file cap — the list may be truncated, so "
                       f"it cannot prove nothing else changed")
    ahead = compare.get("ahead_by")
    if ahead is None:
        return False, "the compare response carried no ahead_by"
    if ahead > MAX_DELTA_COMMITS:
        return False, (f"{ahead} commits separate the covered run from the release "
                       f"commit, past the {MAX_DELTA_COMMITS}-commit bound on what "
                       f"this gate will examine")
    return True, ""


def covered_by(runs_jobs, name=None):
    """Return the id of a run whose apply job SUCCEEDED, else None.

    ``runs_jobs`` is an iterable of ``(run_id, jobs)``. A run whose apply job is
    absent does not count — that is the ordinary case for a run predating the
    tier, and reporting it as coverage is exactly the failure this guards.
    """
    name = name or JOB_NAME
    for run_id, jobs in runs_jobs:
        for job in jobs:
            if job.get("name") == name and job.get("conclusion") == "success":
                return run_id
    return None


def main():
    token = os.environ.get("GH_TOKEN") or os.environ.get("GITHUB_TOKEN")
    repo = os.environ.get("REPO")
    sha = os.environ.get("SHA")
    if not (token and repo and sha):
        print("::error::require_fwapply_run needs GH_TOKEN, REPO and SHA", file=sys.stderr)
        return 1

    try:
        commits = api(f"/repos/{repo}/commits?sha={sha}&per_page={MAX_ANCESTORS}", token)
    except urllib.error.HTTPError as exc:
        print(f"::error::cannot list commits for {sha[:8]}: {exc}", file=sys.stderr)
        return 1

    want_job = job_name()
    print(f"looking for a green '{want_job}' run covering {sha[:8]}")
    checked = []
    for commit in commits:
        candidate = commit["sha"]
        try:
            runs = api(f"/repos/{repo}/actions/workflows/{WORKFLOW}/runs"
                       f"?head_sha={candidate}&per_page=20", token)["workflow_runs"]
        except urllib.error.HTTPError as exc:
            print(f"::warning::cannot list runs for {candidate[:8]}: {exc}")
            continue
        if not runs:
            checked.append(f"{candidate[:8]} (no run)")
            continue
        pairs = []
        for run in runs:
            try:
                jobs = api(f"/repos/{repo}/actions/runs/{run['id']}/jobs"
                           f"?per_page=50", token)["jobs"]
            except urllib.error.HTTPError:
                continue
            pairs.append((run["id"], jobs))
        run_id = covered_by(pairs, want_job)
        if run_id is None:
            checked.append(f"{candidate[:8]} (no green apply job)")
            continue

        if candidate == sha:
            print(f"::notice::firmware apply verified on this exact commit "
                  f"({candidate[:8]}) by run {run_id}")
            return 0
        try:
            compare = api(f"/repos/{repo}/compare/{candidate}...{sha}", token)
        except urllib.error.HTTPError as exc:
            print(f"::error::cannot compare {candidate[:8]}...{sha[:8]}: {exc}", file=sys.stderr)
            return 1
        conclusive, why = compare_is_conclusive(compare)
        if not conclusive:
            print(f"::error::cannot prove {candidate[:8]}...{sha[:8]} is only the "
                  f"version bump: {why}. Refusing rather than assuming.",
                  file=sys.stderr)
            return 1
        files = compare.get("files", [])
        if only_a_version_bump(files):
            print(f"::notice::firmware apply verified on {candidate[:8]} by run {run_id}; "
                  f"the only delta to {sha[:8]} is the {VERSION_MACRO} bump")
            return 0
        patterns = build_path_filter_at(repo, candidate, token)
        if patterns is None:
            print(f"::warning::could not read the path filter out of {WORKFLOW_PATH} "
                  f"at the covered commit {candidate[:8]}; treating every changed "
                  f"file as able to reach the image")
        if delta_is_provably_harmless(files, patterns):
            print(f"::notice::firmware apply verified on {candidate[:8]} by run {run_id}; "
                  f"the delta to {sha[:8]} is the {VERSION_MACRO} bump plus files the "
                  f"build and the rig never read")
            return 0
        blocking = sorted(f.get("filename", "?") for f in files
                          if path_reaches_the_image(f.get("filename", ""), patterns))
        changed = ", ".join(blocking[:8]) or "(none)"
        print(f"::error::the newest apply-verified commit is {candidate[:8]}, but "
              f"{sha[:8]} differs from it by more than the version bump ({changed}). "
              f"That run does not cover this firmware.", file=sys.stderr)
        return 1

    print(f"::error::no green '{want_job}' run covers {sha[:8]} or its "
          f"{len(checked)} most recent ancestors — refusing to publish an image "
          f"whose HID firmware-update path has never been exercised on hardware. "
          f"Checked: {'; '.join(checked)}. "
          f"Fix: run the Build and HIL Test workflow on this commit with "
          f"tier=fwapply (Actions -> Build and HIL Test -> Run workflow), wait for "
          f"it to go green, then re-run this job.", file=sys.stderr)
    return 1


def selftest():
    """Exercise the two decision functions without the network.

    This repo has no Python test harness (its suites are googletest), and the
    logic below is the kind that decides a release — the same reason
    ``fw_up_verdict.c`` was split out of its I/O so it could be tested at all.
    A self-test keeps it honest at the cost of one 0.1 s CI step.
    """
    def patch(*lines):
        return {"filename": VERSION_FILE, "patch": "\n".join(lines)}

    cases = [
        # (name, files, expected)
        ("identical commits", [], True),
        ("pure version bump",
         [patch("@@", "-#define FW_VERSION \"0.16.23\"", "+#define FW_VERSION \"0.16.24\"")],
         True),
        # The +++/--- header lines name the file, not a change; they must not be
        # mistaken for content and must not fail the version check.
        ("bump with a diff header",
         [patch("--- a/x", "+++ b/x", "-#define FW_VERSION \"a\"", "+#define FW_VERSION \"b\"")],
         True),
        # bump:protocol changes ONLY this line — FW_VERSION is rewritten to the
        # same value and never appears in the diff. Accepting just FW_VERSION
        # would refuse a well-covered protocol release at publish time.
        # ⚠️ A line that merely MENTIONS the macro is not the bump. config.h
        # has such lines, so a substring test would wave through a real code
        # edit that happens to sit beside one.
        ("a real edit that only references the macro",
         [patch("@@", "-  build_id(FW_VERSION, 1);", "+  build_id(FW_VERSION, 2);")],
         False),
        ("a #define of something else that references the macro",
         [patch("@@", "-#define BANNER \"fw \" FW_VERSION", "+#define BANNER \"v\" FW_VERSION")],
         False),
        # ⚠️ Word boundary: a DIFFERENT macro that merely starts with the same
        # name is not the bump. Without \\b in the regex this passes.
        ("a different macro sharing the prefix",
         [patch("@@", "-#define FW_VERSION_MAJOR 0", "+#define FW_VERSION_MAJOR 1")],
         False),
        # ⚠️ The filename check is what rejects this — the LINE is a perfectly
        # well-formed version define, so the regex alone cannot. Before this
        # fixture existed, deleting the filename check escaped the whole sweep.
        ("a real version define in a DIFFERENT file",
         [{"filename": "keyboards/polykybd/base/fw_staging.c",
           "patch": "@@\n-#define FW_VERSION \"a\"\n+#define FW_VERSION \"b\""}],
         False),
        ("indented / spaced directive is still the bump",
         [patch("@@", "-  #  define FW_VERSION \"a\"", "+  #  define FW_VERSION \"b\"")],
         True),
        ("protocol-only bump (bump:protocol)",
         [patch("@@", "-#define PROTOCOL_VERSION 15", "+#define PROTOCOL_VERSION 16")],
         True),
        ("both version macros bumped",
         [patch("@@", "-#define FW_VERSION \"a\"", "+#define FW_VERSION \"b\"",
                "-#define PROTOCOL_VERSION 15", "+#define PROTOCOL_VERSION 16")],
         True),
        ("protocol bump PLUS a real edit in the same file",
         [patch("@@", "-#define PROTOCOL_VERSION 15", "+#define PROTOCOL_VERSION 16",
                "-#define SOMETHING 1", "+#define SOMETHING 2")],
         False),
        ("version bump PLUS a real edit in the same file",
         [patch("@@", "-#define FW_VERSION \"a\"", "+#define FW_VERSION \"b\"",
                "-#define SOMETHING 1", "+#define SOMETHING 2")],
         False),
        ("a second file changed",
         [patch("@@", "-#define FW_VERSION \"a\"", "+#define FW_VERSION \"b\""),
          {"filename": "keyboards/polykybd/poly_keymap.c", "patch": "@@\n+x"}],
         False),
        ("only another file changed",
         [{"filename": "keyboards/polykybd/base/fw_staging.c", "patch": "@@\n+x"}], False),
        # ⚠️ A DIFFERENT file whose changed lines all mention the macro. Not
        # hypothetical - hid_com.c builds the GET_ID string out of FW_VERSION -
        # and without this case the filename check can be DELETED and every
        # other fixture still passes, because the per-line check happens to
        # reject them for the other reason. Found by mutation-testing this
        # selftest, which is the whole argument for doing that.
        ("another file whose lines all mention the macro",
         [{"filename": "keyboards/polykybd/hid_com.c",
           "patch": "@@\n-  x(FW_VERSION);\n+  y(FW_VERSION);"}], False),
        # No patch = a rename or mode change; there is nothing to inspect, so it
        # must be refused rather than assumed benign.
        ("version file with no patch body", [{"filename": VERSION_FILE}], False),
    ]
    ok = True
    for name, files, want in cases:
        got = only_a_version_bump(files)
        if got != want:
            print(f"selftest FAIL: only_a_version_bump({name}) = {got}, want {want}")
            ok = False

    bump = [patch("@@", "-#define FW_VERSION \"a\"", "+#define FW_VERSION \"b\"")]
    compare_cases = [
        ("an ordinary one-commit bump", {"files": bump, "ahead_by": 1}, True),
        ("identical commits", {"files": [], "ahead_by": 0}, True),
        # ⚠️ GitHub truncates `files` at 300. A truncated list cannot prove
        # "and nothing else changed", which is the only thing this gate asserts.
        ("a file list at GitHub's cap (may be truncated)",
         {"files": [{"filename": VERSION_FILE, "patch": "@@"}] * COMPARE_FILE_CAP,
          "ahead_by": 1}, False),
        ("more commits than an auto-bump can be",
         {"files": bump, "ahead_by": 40}, False),
        ("no file list at all", {"ahead_by": 1}, False),
        ("no ahead_by at all", {"files": bump}, False),
    ]
    for name, compare, want in compare_cases:
        got, _ = compare_is_conclusive(compare)
        if got != want:
            print(f"selftest FAIL: compare_is_conclusive({name}) = {got}, want {want}")
            ok = False

    # ⚠️ The crafted range that shipped in the first cut of the firmware guard:
    # last - first + 1 wraps a uint32 to ZERO. Mirrored here because the same
    # arithmetic trap applies to anything reasoning about these ranges.
    def span_ok(first, last, capacity):
        span = (last - first) & 0xFFFFFFFF
        return capacity != 0 and span <= capacity - 1
    span_cases = [("normal", (0x41, 0x5A, 8751), True),
                  ("uint32 wrap first=0 last=0xFFFFFFFF", (0, 0xFFFFFFFF, 8751), False),
                  ("exactly fits", (0, 8750, 8751), True),
                  ("one past", (0, 8751, 8751), False),
                  ("zero capacity", (0, 0, 0), False)]
    for name, (a, b, cap), want in span_cases:
        if span_ok(a, b, cap) != want:
            print(f"selftest FAIL: span_ok({name}) = {not want}, want {want}")
            ok = False

    green = {"name": JOB_NAME, "conclusion": "success"}
    job_cases = [
        ("a green apply job", [(1, [green])], 1),
        ("apply job failed", [(1, [{"name": JOB_NAME, "conclusion": "failure"}])], None),
        ("apply job absent (run predates the tier)",
         [(1, [{"name": "HIL test (split72)", "conclusion": "success"}])], None),
        ("green in a later run of the same commit",
         [(1, [{"name": JOB_NAME, "conclusion": "failure"}]), (2, [green])], 2),
        ("no runs at all", [], None),
        ("apply job still running", [(1, [{"name": JOB_NAME, "conclusion": None}])], None),
    ]
    for name, pairs, want in job_cases:
        got = covered_by(pairs)
        if got != want:
            print(f"selftest FAIL: covered_by({name}) = {got}, want {want}")
            ok = False

    name_cases = [
        ("reads the job's own name",
         "jobs:\n  fwapply-test:\n    name: Firmware apply round-trip (split72)\n",
         "Firmware apply round-trip (split72)"),
        ("quoted name",
         "  fwapply-test:\n    name: 'Apply (split72)'\n", "Apply (split72)"),
        # ⚠️ Must NOT bleed into the NEXT job's name when ours declares none —
        # that would silently match a different job and report coverage that
        # does not exist.
        ("no name on our job -> the constant, not the next job's",
         "  fwapply-test:\n    needs: [build]\n  other-job:\n    name: Something Else\n",
         JOB_NAME),
        ("job absent entirely -> the constant",
         "  hil-test:\n    name: HIL test (split72)\n", JOB_NAME),
    ]
    for name, text, want in name_cases:
        got = job_name(text)
        if got != want:
            print(f"selftest FAIL: job_name({name}) = {got!r}, want {want!r}")
            ok = False

    # The REAL workflow must resolve, or the gate silently falls back to the
    # constant and stops tracking a rename — the whole point of deriving it.
    if os.path.exists(WORKFLOW_PATH):
        got = job_name()
        if got == JOB_NAME and f"  {JOB_ID}:" in open(WORKFLOW_PATH).read():
            # Equal to the fallback is fine ONLY if that is genuinely the name.
            pass
        if f"  {JOB_ID}:" not in open(WORKFLOW_PATH).read():
            print(f"selftest FAIL: {WORKFLOW_PATH} has no '{JOB_ID}' job — "
                  f"the gate would match nothing")
            ok = False

    # ---- the path filter, replayed -----------------------------------------
    # A small stand-in for the workflow, so these cases do not move when the
    # real filter is edited. The real one is asserted separately below.
    sample = ("on:\n  push:\n    paths:\n"
              "      - '**'\n"
              "      - '!**.md'\n"
              "      - '!scripts/**'\n"
              "      - '!.claude/**'\n"
              "      - '!.github/workflows/**'\n"
              "      - '.github/workflows/qmk-test.yml'\n"
              "  workflow_dispatch:\n")
    pats = build_path_filter(sample)
    if pats != ["**", "!**.md", "!scripts/**", "!.claude/**",
                "!.github/workflows/**", ".github/workflows/qmk-test.yml"]:
        print(f"selftest FAIL: build_path_filter parsed {pats!r}")
        ok = False

    reach_cases = [
        # (path, reaches the image?)
        ("keyboards/polykybd/poly_keymap.c", True),
        ("keyboards/polykybd/config.h", True),
        ("CLAUDE.md", False),
        ("keyboards/polykybd/CI_CHECKS.md", False),
        (".claude/skills/audit-derived-verdict/SKILL.md", False),
        ("scripts/publish_release.py", False),
        (".github/workflows/release.yml", False),
        # ⚠️ Re-included by the LAST pattern. Order is load-bearing in the
        # workflow and must stay load-bearing here.
        (".github/workflows/qmk-test.yml", True),
        # ⚠️ `*` must NOT cross a separator, or `!**.md` and a careless `!*.md`
        # would mean the same thing and widen every exclusion.
        ("keyboards/polykybd/lang/lang_lut.c", True),
    ]
    for path, want in reach_cases:
        got = path_reaches_the_image(path, pats)
        if got != want:
            print(f"selftest FAIL: path_reaches_the_image({path}) = {got}, want {want}")
            ok = False

    # ⚠️ Single `*` must NOT cross a separator, and nothing above notices: the
    # live filter uses only `**`, so the distinction is unobservable through it.
    # It still has to be pinned, because a later `!*.md` written to mean "the
    # top-level ones" would otherwise exclude every nested .md as well — and an
    # over-wide exclusion fails OPEN, waving a real source change through.
    star = ["**", "!*.md"]
    if path_reaches_the_image("README.md", star):
        print("selftest FAIL: '!*.md' did not exclude a top-level .md")
        ok = False
    if not path_reaches_the_image("keyboards/polykybd/CI_CHECKS.md", star):
        print("selftest FAIL: '!*.md' crossed a separator and excluded a nested .md")
        ok = False

    # ⚠️ FAIL CLOSED. An unreadable or empty filter must make everything look
    # like it reaches the image, so the gate degrades to its old strictness
    # instead of waving a release through. This is the one case here whose
    # failure mode is a published, unverified firmware image.
    for empty in (None, []):
        if not path_reaches_the_image("CLAUDE.md", empty):
            print(f"selftest FAIL: filter {empty!r} did not fail closed")
            ok = False
        if delta_is_provably_harmless([{"filename": "CLAUDE.md"}], empty):
            print(f"selftest FAIL: delta_is_provably_harmless fails open on {empty!r}")
            ok = False
    if build_path_filter("on:\n  push:\n    branches: [PolyKybd]\n") is not None:
        print("selftest FAIL: a workflow with no paths: block must parse as None")
        ok = False

    # ---- the combined delta rule -------------------------------------------
    bump = patch("@@", '-#define FW_VERSION "0.27.0"', '+#define FW_VERSION "0.27.1"')
    delta_cases = [
        ("bump alone", [bump], True),
        # The exact shape that refused PolyKybd-fw-v0.27.1: a docs+skill merge
        # sitting between the covered run and the release commit.
        ("bump plus docs and a skill",
         [bump,
          {"filename": "CLAUDE.md"},
          {"filename": "keyboards/polykybd/CI_CHECKS.md"},
          {"filename": "keyboards/polykybd/SPLIT_SYNC.md"},
          {"filename": ".claude/skills/audit-derived-verdict/SKILL.md"}],
         True),
        ("docs only, no bump", [{"filename": "README.md"}], True),
        ("one real source file among the docs",
         [bump, {"filename": "CLAUDE.md"},
          {"filename": "keyboards/polykybd/poly_keymap.c", "patch": "@@\n+x"}],
         False),
        # ⚠️ A rename OUT of a build path removes a build input. The new name
        # being harmless proves nothing; without the previous_filename check
        # this passes.
        ("a .c renamed to a .md",
         [{"filename": "docs/old_driver.md",
           "previous_filename": "keyboards/polykybd/base/fw_staging.c"}],
         False),
        ("a .md renamed to another .md",
         [{"filename": "docs/b.md", "previous_filename": "docs/a.md"}], True),
        # A config.h edit is never waved through by the path rule — config.h
        # reaches the image, so it still has to BE the bump.
        ("a real config.h edit beside harmless docs",
         [{"filename": "CLAUDE.md"},
          patch("@@", "-#define SOMETHING 1", "+#define SOMETHING 2")],
         False),
        ("a file entry with no filename", [{"patch": "@@\n+x"}], False),
        # ⚠️ The delta must not be able to write the policy it is judged by.
        # release.yml checks out the RELEASED commit, so one commit could drop
        # the qmk-test.yml re-include, add `!keyboards/**`, and edit the
        # firmware — every file then reading as harmless. Reproduced on #300
        # before the fix; these pin both halves of it.
        ("the path filter itself changed",
         [{"filename": WORKFLOW_PATH_POSIX, "patch": "@@\n+      - '!keyboards/**'"}],
         False),
        ("this gate's own source changed",
         [{"filename": "keyboards/polykybd/tools/require_fwapply_run.py",
           "patch": "@@\n+    return True"}],
         False),
        # ⚠️ release.yml invokes this gate and supplies its SHA. The filter calls
        # it harmless (it is not a build input), so only SELF_PATHS refuses it.
        # This does NOT defend against the gate being deleted from that workflow
        # — nothing running inside it could — it stops a delta that weakened the
        # release path from being auto-cleared on the way past.
        ("the release workflow changed",
         [{"filename": ".github/workflows/release.yml",
           "patch": "@@\n-          python3 keyboards/polykybd/tools/require_fwapply_run.py"}],
         False),
        ("the filter renamed out of the way",
         [{"filename": "docs/old-workflow.md",
           "previous_filename": WORKFLOW_PATH_POSIX}],
         False),
    ]

    # ⚠️ And with a filter that says those files are harmless — the exact shape
    # the attack produces. The self-path rule must win over the filter, not
    # merely agree with it.
    hostile = ["**", "!**.md", "!.github/workflows/**", "!keyboards/**"]
    attack = [
        {"filename": WORKFLOW_PATH_POSIX, "patch": "@@\n+      - '!keyboards/**'"},
        {"filename": "keyboards/polykybd/base/fw_staging.c", "patch": "@@\n+bad"},
    ]
    if delta_is_provably_harmless(attack, hostile):
        print("selftest FAIL: a delta that rewrites the filter judged itself harmless")
        ok = False
    # ⚠️ The rename must be refused by the SELF_PATHS check on the PREVIOUS name
    # and by nothing else, so the filter here has to call that old path harmless
    # too. With the ordinary filter this case passes for the wrong reason — the
    # old path reaches the image on its own — and a rule that only looked at the
    # new name survived the mutation sweep because of it.
    renamed_away = [{"filename": "docs/gone.md",
                     "previous_filename": WORKFLOW_PATH_POSIX}]
    if path_reaches_the_image(WORKFLOW_PATH_POSIX, hostile):
        print("selftest FAIL: the hostile fixture no longer isolates the rename rule")
        ok = False
    if delta_is_provably_harmless(renamed_away, hostile):
        print("selftest FAIL: the filter was renamed out of the way and cleared")
        ok = False
    for name, files, want in delta_cases:
        got = delta_is_provably_harmless(files, pats)
        if got != want:
            print(f"selftest FAIL: delta_is_provably_harmless({name}) = {got}, want {want}")
            ok = False

    # The REAL workflow must still yield a usable filter, for the same reason
    # job_name() is asserted against it: a silent fallback here costs a rig
    # round-trip on every release that follows a docs merge.
    if os.path.exists(WORKFLOW_PATH):
        real = build_path_filter()
        if not real:
            print(f"selftest FAIL: no paths: filter found in {WORKFLOW_PATH}")
            ok = False
        elif path_reaches_the_image("CLAUDE.md", real):
            print(f"selftest FAIL: {WORKFLOW_PATH} no longer excludes Markdown")
            ok = False

    print("selftest: OK" if ok else "selftest: FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    if "--selftest" in sys.argv:
        sys.exit(selftest())
    sys.exit(main())
