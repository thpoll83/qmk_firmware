---
name: measure-firmware-perf
description: Measure PolyKybd firmware performance on the HIL rig and interpret the result — main-loop iteration timing, overlay-burst cost split into bridge/render/rest, HID round-trip latency, and boot-to-responsive. Drives the opt-in perf CI job (or a manual workflow_dispatch), waits for it, downloads and decodes the perf-report artifact, checks the run was clean, compares against the committed baseline, and explains what the numbers mean. Use when asked "how fast is this", "did this change cost time", "get me the firmware timings", "measure the overlay/app-switch cost", "is this a regression", or when a timing question would otherwise be answered by flashing a build by hand and reading the console log. Also use to record a new baseline.
---

# Measure firmware performance on the rig

Replaces the old loop of "deploy a build by hand, poke the keyboard, paste the
`LoopProf:` block". The rig flashes a profiling firmware, drives defined workloads
inside **bounded profiler windows**, and emits a JSON + markdown report.

**You do not measure anything yourself** — CI and the rig do. This skill's job is
to *trigger*, *fetch*, *validate*, and *interpret*, because each of those has traps
that produce confident-sounding nonsense (see **Pitfalls**).

Repos: `qmk_firmware` (firmware + CI job), `polykybd-ctnd` (the harness + baselines).

## What gets measured

| Workload | Answers |
|---|---|
| **idle baseline** | Is the loop healthy at rest? (`iters_per_s`, worst iteration). The *control* — a burst number is meaningless without it. |
| **overlay burst, plain (cmd 10)** | Where app-switch time goes: `bridge` (master→slave UART) vs `render` (`update_displays`) vs `rest`. |
| **overlay burst, RLE/core1 (cmd 16)** | Same, for the compressed path. ⚠️ see Pitfalls. |
| **HID round-trip latency** | What a user feels as responsiveness (p50/p95/p99/max, misses). |
| **boot to first stable HID** | Cold-flash readiness. |

## Procedure

### 1. Trigger a run

Three ways; pick by context.

- **On an open PR** — add the `perf` label. This is the normal path for "did my
  change cost time". The label persists, so later pushes to that PR keep measuring.
- **On the default branch** — manual dispatch:
  ```
  mcp__github__actions_run_trigger  method=run_workflow
      owner=thpoll83 repo=qmk_firmware workflow_id=qmk-test.yml ref=PolyKybd
  ```
- **`[perf]` in a commit message** — push events only (see Pitfalls).

### 2. Wait for it, then find the run

```
mcp__github__actions_list  method=list_workflow_runs owner=thpoll83 repo=qmk_firmware
    resource_id=qmk-test.yml  workflow_runs_filter={"event":"workflow_dispatch"}
```
A full run is ~20–30 min (two docker builds, then the rig). Do not poll tightly;
prefer a scheduled check-in. Confirm the `Performance measurement (split72)` job
actually ran — if `build-perf` was skipped, the trigger did not take.

### 3. Download and decode the report

```
mcp__github__actions_list method=list_workflow_run_artifacts owner=… repo=… resource_id=<run_id>
mcp__github__actions_get  method=download_workflow_run_artifact owner=… repo=… resource_id=<artifact_id>
# then fetch the temporary URL it returns:
cd /tmp && rm -rf perfart && mkdir perfart && cd perfart \
  && curl -sSL -o perf.zip "<download_url>" && unzip -q perf.zip
python3 -m json.tool perf-report.json | head -60
```
The zip holds `perf-report.json` (machine-readable), `perf-report.md` (the posted
table) and `perf-run.log`.

### 4. Validate the run BEFORE reading any number

```bash
python3 -c "
import json; d=json.load(open('perf-report.json'))
t=d['timing']; print('gates:', t.get('ready_gate_ok'), t.get('settled'))
for k in ('overlay_plain','overlay_compressed'):
    s=d[k]; print(k, 'ovl_iters', s['ovl_iters'], 'reports', s['reports'])
print('console_tail:', d['console_tail'][:3])
"
```
Reject the run if any of these fail:
- **`ready_gate_ok` or `settled` is false** → it measured the master's boot-time
  busy window, not the workload. Re-run; never baseline it.
- **`ovl_iters != reports`** → the instrumentation is broken (one bulk overlay
  report must produce exactly one tagged iteration). No timing from that window is
  trustworthy.
- **Truncated `console_tail`** (lines ending mid-word) → the rig is running a
  station build older than the console-reassembly fix; the numbers are still valid
  but the rig is stale, so check what else it is missing.

### 5. Interpret

Compare against `polykybd-ctnd/perf/baselines/<board>.json` (the report does this
automatically when a baseline exists). Then read the **overlay attribution**, which
is the part that actually drives decisions:

- **`render` dominant** ⇒ render-bound. The fix is in the render path (coalesce
  redundant renders, chunk a render across passes). A keyboard-side resource
  pack / baked overlays would **not** help.
- **`bridge` dominant** ⇒ transfer-bound. Fewer/smaller relayed bytes would help;
  this is what makes a resource-pack approach worth building.
- **`rest` dominant** ⇒ look at the HID copy / RLE kick / core1 wait.

Also report `long_iters_ge_10ms` — iterations long enough to swallow a fast key
tap, i.e. the missed-keystroke risk.

### 6. Recording a baseline (only when asked, or when none exists)

Commit the run's **exact** `perf-report.json` to
`polykybd-ctnd/perf/baselines/<label>.json` on a fresh branch cut from `main`, with
a message saying *why* the numbers moved. There is deliberately no automatic
update — a self-rewriting baseline ratchets a slow regression in silently. See
`perf/baselines/README.md`.

## Output format

Lead with the decision-relevant number, not the table:

```
<workload> is <render|bridge|rest>-bound: <X> ms of <Y> ms (<Z>%).
<what that implies for the change/design under discussion>

vs baseline: <metric> <±N%> (or "no baseline recorded yet")
Run was clean (gates passed, ovl_iters == reports).
<caveats that stop the number meaning more than it does>
```

## Pitfalls

- **A regression never fails the check, by design.** These are wall-clock numbers
  on shared hardware; a flaky red check is one people learn to ignore. Only a
  *measurement* failure (wrong build flashed, device dead) exits non-zero. So a
  green job does **not** mean "no regression" — you must read the comparison.
- **`render = 0.0 ms` on the RLE/core1 path is an artefact, not a result.** Both
  paths redraw the same keycaps; the core1-deferred render lands *after* the window
  closes, because the harness ends a window when the master answers HID again — not
  when the render finished. **Never quote a plain-vs-RLE ratio from one report.**
- **The default workload is synthetic**: blank overlays to 8 keycodes, no reconnect
  in the loop. That understates bridge cost (which scales with relayed bytes) versus
  a real ~90-keycap cold app switch. State this whenever the number is used to argue
  about architecture.
- **"cmd 32 NACKed / not a POLYKYBD_LOOP_PROFILE build"** means the wrong images
  were flashed — the profiler is compiled out of normal builds and that NACK is the
  deliberate capability signal. It is *not* the same error as the device not
  answering at all; the harness distinguishes them.
- **`[perf]` in a commit message does nothing on a PR** — `github.event.head_commit`
  exists only on push events. And `workflow_dispatch` is only available once the
  workflow is on the default branch. On a PR, use the label.
- **The rig runs the *installed* `/opt/polykybd-ctnd`.** CI force-syncs it to
  `origin/main` first, so a harness fix must be merged to ctnd `main` before it
  affects a run.
- **GitHub MCP quirks**: `actions_get`/`actions_list` take `resource_id` (not
  `run_id`), and `list_workflow_runs`' `branch` filter is **not applied** — filter by
  `head_sha` yourself or you will read another branch's run.
