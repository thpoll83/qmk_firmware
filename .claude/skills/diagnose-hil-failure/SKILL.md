---
name: diagnose-hil-failure
description: >
  Triage a red "HIL test (split72)" check on a PolyKybd `thpoll83/qmk_firmware`
  PR and drive it to green or a correct root-cause diagnosis. Use when a HIL
  check fails, a `❌ HIL test failed — rig diagnostics` comment appears, the user
  asks to "look at the HIL failure / rig test / why is CI red", or when deciding
  whether to re-run the HIL job. Classifies the failure into stale-rig-code /
  no-enumeration / boot-burst flake / stale-rig-test / real-firmware-bug, or a
  rig-network failure that dies in workflow setup with no log at all, then
  acts (re-run, fix the ctnd test, or flag the rig). NOT for building firmware
  (that's the build job) or for non-HIL CI.
---

# Diagnose a HIL (hardware-in-the-loop) failure

The HIL job flashes both keyboard halves on the RPi4 rig (`polykybd-ctnd`) and
runs `station/hil_tests.py` over Raw HID. Most red HIL checks are **not** a bug in
the PR under test — they're a stale rig, a boot-timing flake, or a rig test that
drifted from the firmware. Classify before acting; a re-run only helps for some
classes, and re-running against a stale rig just reproduces the same red.

The rig, its self-update model, and the flake history are documented in
`polykybd-ctnd/CLAUDE.md` (esp. the self-update ⚠️ note) and the "Investigations"
HIL notes in `qmk_firmware/CLAUDE.md` — read those for background.

## 1. Get the diagnostics

The failing run posts a sanitized `test_runner` log as a PR comment (HTML marker
`<!-- hil-diagnostics -->`) and to the job Step Summary. Read the whole
`test_runner output` block — the per-test `[test] PASS/FAIL` lines and the
`::error::` annotations at the end are the signal. If you only have a `check_run`
event, get the run/jobs via the GitHub Actions tools:

```
mcp__github__actions_list  method=list_workflow_runs  resource_id=qmk-test.yml
                           workflow_runs_filter={branch: "<pr-branch>"}
mcp__github__actions_list  method=list_workflow_jobs  resource_id=<run_id>  filter=all
```

## 1.5 Did the suite RUN at all? — the no-log case

⚠️ **Before step 2: if the log ends at `Prepare all required actions`, there is no
settle line to read and NONE of the classes below apply.** The job died in workflow
SETUP — before checkout, before the station sync, before a single `[test]` line.
Signature (seen 3× on 2026-08-27, twice on a PR and again on the `PolyKybd` merge
push):

```
Download action repository 'actions/download-artifact@v4'
##[warning]Failed to download action 'https://codeload.github.com/actions/download-artifact/tar.gz/...'.
           Error: The request was canceled due to the configured HttpClient.Timeout of 100 seconds elapsing.
##[error]Failed to download archive ... after 3 attempts.
```

That is the **rig's own network** — not the firmware, not the rig's test code, and
not something a re-run fixes once it has reproduced. Three things make it
diagnosable:

- **A runner being ONLINE does not mean its network is fine.** It picked the job up,
  streamed logs and reported status, so `api.github.com` and the Actions service are
  reachable while `codeload.github.com` is not. That is a *selective* failure, not
  "the rig is offline" — don't go hunting for a dead runner.
- **Rule out a GitHub incident before blaming the rig**, one call:
  `https://www.githubstatus.com/api/v2/summary.json`. On 2026-08-27 Actions, Git
  Operations and API Requests were all operational, which is what pinned it to the Pi.
- ⚠️ **This is NOT the `codeload` note in `qmk_firmware/CLAUDE.md`.** That one is the
  *dev container's* submodule fetches getting a **403** from the injected git proxy,
  fixed with `add_repo`. This is the *rig runner* getting a **timeout** fetching a
  GitHub Action. Same hostname, different machine, different symptom — the container
  note's fix does not apply, and conflating them sends you to `add_repo` for nothing.

**Action:** it qualifies for the one re-run (it died before any test body ran), but a
second identical failure is real — stop re-running, say so, and keep the PR watched.
There is **no fix to port**: nothing in a firmware PR can reach the runner's
connectivity, and widening the PR to work around an Actions download is the wrong
shape. Recovery is the rig's network; after that the HIL job re-runs on its own
(it reuses the build artifact and force-syncs the station itself).

## 2. Check rig freshness FIRST — the settle line

Before anything else, read this line:

```
[runner] master settled — N consecutive GET_LANG replies <= 250 ms after N probe(s)
```

- **`need=3`** (`3 consecutive … after 3`) → the rig is running **STALE** station
  code (pre-`df6401d`). Already-merged rig fixes are NOT deployed — CI runs the
  *installed* `/opt/polykybd-ctnd`, updated only by the rig's self-update timer,
  which lags. **Do not chase the firmware.** Recovery: get the rig current (tap
  the **UPDATE** badge on the touch UI, or wait a ~5 min idle timer tick), then
  re-run. The durable fix is the "Sync station to current ctnd main" step in
  `qmk-test.yml` (if that PR isn't merged yet, that's why it's still lagging).
- **`need=15`** (`15 consecutive … after ≥15`) → the rig is current; the
  sustained settle rode past the boot burst. Proceed to classify the real failure.

## 3. Classify the failure

| Signature in the log | Class | Meaning / action |
|---|---|---|
| `raw HID interfaces present: 0` + every test fails `Raw HID interface not found` | **no-enumeration** | The master never enumerated its raw HID interface. NOT a display/font/keymap change (USB inits before rendering). Rig USB/flash/boot wedged or slow → **re-run**; if it persists across re-runs the rig needs physical attention (reseat/power-cycle a half), not a code fix. |
| **No `[test]` line at all**; log ends at `Prepare all required actions` | **rig network / setup** | The job never ran. See §1.5 — the runner cannot reach `codeload.github.com` to fetch an action. One re-run is allowed; a second identical failure is real and there is nothing in the PR to fix. |
| An early read (e.g. `GET_ID #2`, a layer/keymap read) times out (`None`), **everything after passes** | **boot-burst flake** | The async slave-connect render burst (~5 s stall) landed on an early test. It *wanders* onto a different adjacent test pair each run. `need=15` settle usually rides past it; a lone straggler → **re-run**. |
| One test fails on a **wrong value** (`status 0xNN != expected`, byte mismatch — a *response*, not a timeout), everything else passes | **stale rig test** | A rig-side mirror of a firmware constant/enum drifted. Compare the firmware source to the rig's mirror and fix the **ctnd** test, not the firmware. (2026-07: `POLY_OS_COUNT` was 6 on the rig but the firmware enum grew to 8 — `SET_OS(6)`=GNOME is valid, so the "invalid" probe wrongly ACKed.) |
| Broad failures with genuinely wrong data / NACKs where ACK expected across many commands | **real firmware bug** | Investigate the PR diff. This is the rare case the HIL suite exists to catch. |

Cross-check scope: a display/font/hint change **cannot** affect USB enumeration,
split master/slave detection, or unrelated command handlers — so "no-enumeration"
or an unrelated command's failure on such a PR points away from the diff.

## 4. Act

- **Re-run** (boot-burst flake, or after the rig is confirmed current):
  ```
  mcp__github__actions_run_trigger  method=rerun_failed_jobs  run_id=<run_id>
  ```
  Re-runs only the failed HIL job, reusing the build artifact. **Don't re-run
  against a stale rig** (step 2) — it reproduces the same red.
- **Stale rig test** → fix the mirror in `polykybd-ctnd/station/hil_tests.py`
  (match the firmware source), PR to ctnd `main`; the rig picks it up on its next
  self-update. Re-run once deployed.
- **Report** the class + root cause to the user (in chat, not a PR comment — be
  frugal). For a stale-rig or rig-test cause, say plainly it's not the PR's fault.

## Output

State: (a) rig fresh or stale (from the settle line), (b) the failure class, (c)
root cause + which repo owns the fix, (d) the action taken (re-run / ctnd fix /
flag rig). One or two sentences per point.

## Pitfalls

- **No settle line at all? You are in §1.5, not step 2.** Every class in the
  table presupposes the suite ran; a setup failure produces no log to classify.
- **Read the settle line before diagnosing.** `need=3` = stale rig; fixing the
  firmware/test is wasted effort until the rig is current.
- **Re-running a stale rig reproduces the same red.** Get the rig current first.
- **`0 interfaces` ≠ the busy-window flake.** Zero enumeration is a boot/USB/flash
  problem (or genuinely-slow enumeration), not a timed-out query — a longer
  readiness timeout won't conjure an absent interface.
- **A wrong-*value* failure (response present) is real** — either a firmware bug
  or a stale rig-test mirror; distinguish by comparing to the firmware source.
  Only *timeouts* (`None`) are the transient boot-burst class.
- **Don't blame a display/font/hint PR for USB/enumeration/unrelated-command
  failures** — those subsystems are independent of rendering.
- The HIL suite is **shared**: a stale-rig-test failure reddens *every* PR's HIL
  until the ctnd fix is merged and deployed, not just the PR that surfaced it.
