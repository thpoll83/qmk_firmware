# HIL probes — the firmware debug loop

A **probe** is a one-off script the HIL rig runs against a freshly-flashed
keyboard. It exists to answer the question the graded suite cannot: *"flash this
build, drive it like so, and show me what the firmware printed."*

Before this, that question needed a person — build a `.bin`, hand it over, wait
for it to be flashed, wait for a console log to be pasted back. That round trip
was the slow half of every hardware bug, the HID-apply brick (#258) included.

## The loop

1. Write a probe here, on the same branch as the firmware change it investigates.
2. Push the branch.
3. Dispatch `qmk-test.yml` on that branch with `tier: debug`, `probe: <name>`.
4. Read the job log. The rig flashes both halves, runs the probe, and pipes the
   firmware's own console into the log as `[qmk] …` lines.
5. Edit, push, dispatch again.

Nobody has to touch the hardware, and a **brick is self-recovering**: the rig
asserts BOOTSEL over GPIO, and BOOTSEL/UF2 bypasses `fw_staging` entirely. That
is what makes the rig the right — and the only — place to exercise the
firmware-apply path.

## Writing one

```python
NAME = "what this is looking for"     # optional; defaults to the filename
MIN_PROTOCOL = 15                     # optional; same gate keys as a HIL test
NEEDS_CONSOLE = True                  # optional; SKIPs if the console is down

def probe(raw, log):
    reply = raw.send(bytes([0x50, 0x06]))   # 'P', GET_ID
    log(f"GET_ID -> {reply!r}")
    return reply is not None
```

`raw` is the rig's `RawHID`; `log` writes into the run log. Return `True` to
pass. The probe runs with the flash, the readiness gates and the console tap
already done — see `polykybd-ctnd/CLAUDE.md` § *Debug loop* for the runner side
and `station/probe.py` for the loader.

To read what the firmware printed *while the probe ran*, use the shared tap:

```python
from station.console_log import TAP

def probe(raw, log):
    mark = TAP.mark()
    raw.send(bytes([0x50, 0x06]))
    for line in TAP.since(mark):
        log(f"console: {line}")
    return True
```

⚠️ **Iterate on a branch with no open PR.** A push to a `claude/**` branch starts
nothing on its own (`qmk-test.yml` listens for pushes only on `PolyKybd` and
`PolyKybd/**`), so the dispatch is the only thing that occupies the rig. Open a
PR and every probe edit *also* runs the full build + HIL pipeline behind your
dispatch — the rig runs one job at a time, so that is double the wait per turn.
Open the PR when the investigation is done.

## Things that will cost you a cycle if you forget them

- ⚠️ **`raw.send()` RETRIES by re-writing the request.** That is safe only
  because the commands are idempotent — `GET_ID` is the exception, since it
  consumes the firmware's one-shot fresh-boot marker. Pass `attempts=1` when the
  read observes a one-shot side effect.
- ⚠️ **The console cannot see a flash window.** QMK drops output nobody drains,
  and during a flash nothing does. A gap in the `[qmk]` lines spanning an update
  is expected, not a symptom.
- ⚠️ **Most firmware diagnostics are gated on `debug_enable`, which is false.**
  `Bridge sync retry` and `Failed to sync …` never appear on the rig; the
  `Split link:` summary is deliberately ungated. Check the gate in the firmware
  before designing a probe around a console line.
- **A probe is disposable.** Delete it when the bug is closed, or promote it to a
  real HIL test (`station/hil_tests.py`) if the question is worth asking forever.

## Why this is dispatch-only

Triggering a `workflow_dispatch` requires write access to this repo, so a fork PR
can never reach the probe job. That matters: the rig is a self-hosted runner for
a **public** repo (HIL-2 in `polykybd-ctnd/docs/SECURITY_AUDIT.md`). Do **not**
add a label trigger — a label is applied to a PR whose head may be a fork.
