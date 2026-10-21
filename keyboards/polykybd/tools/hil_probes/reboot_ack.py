# SPDX-License-Identifier: GPL-2.0-only
"""Probe: what the host reads back for the reboot command (cmd 43).

The HIL boot loop failed twice on 2026-10-21 with the cmd 43 reply's status byte
damaged: 0xfe (round 20, run 39572236621 attempt 1) and 0xee (round 2, run
39608481800), where the firmware always sends '.' (0x2e). In passing runs 4 to
6 of 20 rounds get no reply at all, which the test tolerates. The test logs only
byte 2, so this probe records the whole reply of every round.

Each round sends cmd 43 once (attempts=1, the same as the boot loop), records the
reply and how long it took, then waits for the board to come back the way the
boot loop does. A reply is

* ``ok``: ``P 0x2b .`` and every later byte 0 (hid_com.c memsets the report),
* ``none``: no reply, or the interface went away during the read,
* ``bad``: anything else, printed in full.

Run it with::

    tier: debug   probe: reboot_ack
"""
import time

NAME = "probe: cmd 43 reboot reply, whole report"
MIN_PROTOCOL = 22
NEEDS_CONSOLE = False

POLY_CHANNEL = 0x50
CMD_REBOOT = 0x2B
ROUNDS = 100
READ_TIMEOUT_MS = 1000


def _classify(resp):
    if not resp:
        return "none"
    if resp[0] == POLY_CHANNEL and resp[1] == CMD_REBOOT and resp[2] == ord(".") \
            and not any(resp[3:]):
        return "ok"
    return "bad"


def probe(raw, log):
    from station import hil_tests as H

    # Baseline: an ordinary request/reply on this rig, same call shape.
    base = []
    for _ in range(20):
        t0 = time.monotonic()
        resp = raw.send(bytes([POLY_CHANNEL, 0x06]), timeout_ms=READ_TIMEOUT_MS, attempts=1)
        if resp:
            base.append((time.monotonic() - t0) * 1000.0)
    base.sort()
    if base:
        log(f"[probe] GET_ID baseline: {len(base)}/20 replies, {base[0]:.1f} .. {base[-1]:.1f} ms, "
            f"median {base[len(base) // 2]:.1f} ms")

    counts = {"ok": 0, "none": 0, "bad": 0}
    ok_ms = []
    states = {}
    for n in range(1, ROUNDS + 1):
        t0 = time.monotonic()
        error = ""
        try:
            resp = raw.send(bytes([POLY_CHANNEL, CMD_REBOOT]), timeout_ms=READ_TIMEOUT_MS,
                            attempts=1)
        except Exception as exc:  # noqa: BLE001 — the board may already be gone
            resp, error = None, f" ({type(exc).__name__}: {exc})"
        dt_ms = (time.monotonic() - t0) * 1000.0
        kind = _classify(resp)
        counts[kind] += 1
        if kind == "ok":
            ok_ms.append(dt_ms)
        else:
            shown = resp.hex(" ") if resp else "-"
            log(f"  round {n}: {kind} after {dt_ms:.1f} ms{error}: {shown}")
        state = H._wait_for_reboot(raw, H.BOOT_LOOP_RETURN_S)
        states[state] = states.get(state, 0) + 1
        if state == "no-reply":
            log(f"  round {n}: the board did not come back within {H.BOOT_LOOP_RETURN_S:.0f} s")
            break
        if state == "not-rebooted":
            log(f"  round {n}: the board answered without ever going away")
    ok_ms.sort()
    if ok_ms:
        log(f"[probe] ok replies: {len(ok_ms)}, {ok_ms[0]:.1f} .. {ok_ms[-1]:.1f} ms, "
            f"median {ok_ms[len(ok_ms) // 2]:.1f} ms")
    log(f"[probe] {sum(counts.values())} round(s): ok={counts['ok']} none={counts['none']} "
        f"bad={counts['bad']}; came back: {states}")
    return counts["bad"] == 0
