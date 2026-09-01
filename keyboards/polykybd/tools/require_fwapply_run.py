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
import urllib.request

API = "https://api.github.com"
WORKFLOW = "qmk-test.yml"
WORKFLOW_PATH = os.path.join(".github", "workflows", WORKFLOW)
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
MAX_ANCESTORS = 12


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
                if not any(macro in line for macro in VERSION_MACROS):
                    return False
    return True


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
            files = api(f"/repos/{repo}/compare/{candidate}...{sha}", token).get("files", [])
        except urllib.error.HTTPError as exc:
            print(f"::error::cannot compare {candidate[:8]}...{sha[:8]}: {exc}", file=sys.stderr)
            return 1
        if only_a_version_bump(files):
            print(f"::notice::firmware apply verified on {candidate[:8]} by run {run_id}; "
                  f"the only delta to {sha[:8]} is the {VERSION_MACRO} bump")
            return 0
        changed = ", ".join(sorted(f.get("filename", "?") for f in files)[:8]) or "(none)"
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

    print("selftest: OK" if ok else "selftest: FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    if "--selftest" in sys.argv:
        sys.exit(selftest())
    sys.exit(main())
