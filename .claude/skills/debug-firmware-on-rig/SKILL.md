---
name: debug-firmware-on-rig
description: >
  Chase a PolyKybd firmware bug on real hardware WITHOUT asking the user to flash
  a `.bin` and paste a console log back. Writes a one-off probe under
  `keyboards/polykybd/tools/hil_probes/`, dispatches `qmk-test.yml` with
  `tier: debug`, and reads the firmware's own `[qmk] …` console lines out of the
  job log. Use when a bug needs the keyboard to answer a question the graded HIL
  suite does not ask — "does it still NACK after X", "what does the console say
  during Y", "did the apply path actually run" — or when the user offers a
  hardware round and a probe would be faster. Also the right tool for exercising
  the firmware-APPLY path, which is self-recovering only on the rig. NOT for a
  red HIL check (that is diagnose-hil-failure), NOT for timing numbers (that is
  measure-firmware-perf), and NOT for a change the user must feel on the keycaps
  (that is deliver-test-firmware).
---

# Debug firmware on the HIL rig

The graded suite answers fixed questions. A **probe** answers a one-off one: *flash
this branch, drive it like so, and show me what the firmware printed.*

Before this existed that round trip needed a person — build a `.bin`, hand it over,
wait for it to be flashed, wait for a console log to come back. That was the slow
half of every hardware bug, the HID-apply brick (#258) included.

## When a probe is the right tool

| Question | Reach for |
|---|---|
| "Does the board still answer / NACK / persist X?" | **a probe** |
| "What does the firmware print while Y happens?" | **a probe** |
| "Does the HID *apply* path survive this change?" | `tier: fwapply`, or a probe |
| A red `HIL test (split72)` check | `diagnose-hil-failure` |
| Loop timing / overlay cost / HID latency | `measure-firmware-perf` |
| "Is the legend right?" — anything a human must LOOK at | `deliver-test-firmware` |
| A question worth asking forever | promote it into `station/hil_tests.py` |

**The rig cannot see the keycaps.** It has no camera, so a probe can only assert
what HID and the console say. If the answer is a picture, it is a user round.

## The loop

### 1. Write the probe

`keyboards/polykybd/tools/hil_probes/<name>.py`, on the **same branch as the
firmware change it investigates** — probe and firmware are then one commit:

```python
NAME = "what this is looking for"     # optional; defaults to the filename
MIN_PROTOCOL = 15                     # optional; same gate keys as a HIL test
NEEDS_CONSOLE = True                  # optional; SKIPs if the console is down

def probe(raw, log):
    reply = raw.send(bytes([0x50, 0x06]))   # 'P', GET_ID
    log(f"GET_ID -> {reply!r}")
    return reply is not None
```

`raw` is the rig's `RawHID`, `log` writes into the run log, `True` passes. The
flash, the readiness gates and the console tap are already done when it runs.
`keyboards/polykybd/tools/hil_probes/README.md` is the **reference** — the probe
format, the `TAP.mark()`/`TAP.since()` console idiom, and the pitfalls. Read it
before writing anything non-trivial; this skill is only the driver around it.

### 2. Push the branch

```bash
git add keyboards/polykybd/tools/hil_probes/<name>.py && \
  git commit -m "hil: probe for <question>" && git push -u origin <branch>
```

A push to a `claude/**` branch starts **nothing** — `qmk-test.yml` listens for
pushes on `PolyKybd` alone — so the dispatch is the only thing that occupies the
rig. That is why you iterate on a branch with **no open PR**.

### 3. Dispatch

```
mcp__github__actions_run_trigger
  method: run_workflow
  owner: thpoll83   repo: qmk_firmware
  workflow_id: qmk-test.yml
  ref: <your branch>            # NOT PolyKybd — this is what builds your firmware
  inputs: { tier: debug, probe: <name> }
```

`ref:` is the whole trick: dispatch **entry** must exist on the default branch,
but the workflow and firmware that run come from the ref. So a probe iterates
entirely unmerged.

### 4. Read the log

Find the run (it appears within seconds), then read the probe job:

```
mcp__github__actions_list  method: list_workflow_runs
    resource_id: qmk-test.yml
    workflow_runs_filter: { event: workflow_dispatch, branch: <your branch> }
    per_page: 3
mcp__github__actions_list  method: list_workflow_jobs   resource_id: <run id>
mcp__github__get_job_logs  job_id: <numeric id of "Debug probe (split72)">
                           tail_lines: 300
```

⚠️ **The `branch` filter narrows; the `head_sha` DECIDES. Confirm it is the commit
you just pushed — never just take the newest dispatch.** Measured on this repo: a
`tier: fwapply` recovery run and a `tier: debug` run sit adjacent in that list,
80 minutes apart, **both `workflow_dispatch` and both on `PolyKybd`** — so the
branch filter alone does not separate them, and neither does recency once someone
else dispatches too. Reading the wrong run attributes another commit's console
output to the firmware you are investigating, which is worse than reading nothing.
The sha check is also what catches the `ref:` mistake above: dispatch `PolyKybd`
by accident and the run exists, goes green, and tested none of your change — only
its `head_sha` says so.

⚠️ **`actions_list` saves a large JSON to a file and hands you the path** — parse
that, don't retry with a smaller `per_page`; it is the per-run payload that is
large. And an **in-progress run has no `conclusion` key at all**, so read it as
`r.get("conclusion", "<running>")`.

⚠️ **Ask for ≥300 `tail_lines`.** Every job ends in ~40 lines of git post-job
cleanup, and a failing tool often dumps diagnostics *after* its own error.

### 5. Edit, push, dispatch again

The rig runs one job at a time. Measured on the run below: **5m21s** end to end —
**4m03s of cloud build** (four `qmk compile`s plus the container pull) and **1m13s**
on the rig, of which the flash-and-probe step itself is **35s**. So the iteration
cost is almost entirely the build, and a full pipeline is ~8.5 min by comparison.

## What a healthy run looks like

Measured end to end on 2026-09-01, run `33559310727` (`tier: debug`,
`probe: identity`, head `283eac13`):

- **`Debug probe (split72)` ✅, and every other job `skipped` — including
  `HIL test (split72)`.** That skip IS the tier working: `debug` is the only value
  that carries an `if:` on `hil-test`, because a debug loop pays the rig cost on
  every iteration. Do not read it as a broken run.
- The firmware's console flows into the log as `[qmk] …` — the boot banner
  (`== PolyKybd Split72 0.17.1 P15 HW0x0320 | left master ==`), the `clk:` line,
  the apply log, the intl/idle/keymap diagnostics.
- The runner reports its own gates: `[runner] master settled — 15 consecutive
  GET_LANG replies <= 250 ms after 19 probe(s)`, then `[runner] device caps:
  protocol P15, fw 0.17.1`.
- The probe's own lines, then `[test] PASS: probe: <name>`.

⚠️ **`console lines captured while probing: 0` is normal for a fast probe.**
Abundant `[qmk]` lines appear *before* the probe window; the tap only sees what
the firmware prints inside it. QMK drops console output nobody drains, so a
gap spanning a flash is expected, not a symptom.

## Things that cost a cycle

- ⚠️ **`raw.send()` RETRIES by re-writing the request** — safe only because the
  commands are idempotent. `GET_ID` is the exception: it consumes the one-shot
  fresh-boot marker, so a dropped reply makes the retry return a perfectly correct
  answer to a question already answered. Pass `attempts=1` when the read observes
  a one-shot side effect.
- ⚠️ **Most firmware diagnostics are gated on `debug_enable`, which is FALSE** —
  `Bridge sync retry` and `Failed to sync …` never reach the rig; the `Split link:`
  summary is deliberately ungated. Check the gate in `bridge_helper.c` before
  designing a probe around a console line.
- ⚠️ **Dispatch on the BRANCH, not `PolyKybd`.** Dispatching on the default branch
  builds the default branch — a green run that tested none of your change.
- ⚠️ **Do NOT add a label trigger.** Dispatch-only IS the security control: it needs
  write access, so a fork PR can never reach the rig, which is a self-hosted runner
  for a public repo (HIL-2). The containment check in `station/probe.py` is
  operational, not that control, and its own docstring says so.
- ✅ **A brick is self-recovering here** — the rig asserts BOOTSEL over GPIO and
  BOOTSEL/UF2 bypasses `fw_staging` entirely. That is what makes the rig the right,
  and the only, place to exercise the firmware-apply path.

## Closing out

A probe is **disposable**. When the bug closes, either delete it or promote the
question into `polykybd-ctnd` `station/hil_tests.py` if it is worth asking forever
— and note the merge order there: the rig force-syncs its checkout to ctnd `main`
before every run, so a test living only in an unmerged ctnd PR does not exist on
the rig and the suite goes green having never run it.
