"""A/B probe: does the Eden idle loop on core1 lose HID replies?

Run on #324's head (Eden on core0) and on #325's head (Eden on core1). Each cycle
queries the idle style (cmd 28, idempotent) with ONE attempt and a 1 s timeout, so a
lost reply is counted instead of being retried away, in three phases:

  before idle -> Eden idle (console confirms the transition) -> after idle stops.

Measurement only: it returns False solely on a freeze (no replies at all in a phase).
"""
import time

from station.console_log import TAP

NAME = "eden idle: lost HID replies before / during / after (A/B)"
NEEDS_CONSOLE = True

POLY = 0x50
CMD_IDLE_STATE = 15
CMD_IDLE_STYLE = 28
IDLE_STYLE_EDEN = 3
CYCLES = 3
N_BEFORE, N_DURING, N_AFTER = 300, 150, 300


def _burst(raw, log, n, label):
    lat, lost = [], 0
    for _ in range(n):
        t0 = time.monotonic()
        r = raw.send(bytes([POLY, CMD_IDLE_STYLE, 0xFF]), timeout_ms=1000, attempts=1)
        dt = (time.monotonic() - t0) * 1000.0
        if r is None:
            lost += 1
        else:
            lat.append(dt)
    lat.sort()
    if lat:
        med = lat[len(lat) // 2]
        p95 = lat[min(len(lat) - 1, int(len(lat) * 0.95))]
        log(f"  {label}: {n} sent, {lost} lost, latency median/p95/max = "
            f"{med:.0f}/{p95:.0f}/{lat[-1]:.0f} ms")
    else:
        log(f"  {label}: {n} sent, ALL lost")
    return lost, len(lat)


def probe(raw, log):
    cur = raw.send(bytes([POLY, CMD_IDLE_STYLE, 0xFF]))
    original = cur[3] if cur and len(cur) >= 4 else 1
    totals = {"before": 0, "during": 0, "after": 0}
    froze = False
    try:
        for c in range(1, CYCLES + 1):
            log(f"cycle {c}/{CYCLES}")
            lost, ok = _burst(raw, log, N_BEFORE, "before idle")
            totals["before"] += lost
            froze |= ok == 0
            raw.send(bytes([POLY, CMD_IDLE_STYLE, IDLE_STYLE_EDEN]))
            mark = TAP.mark()
            raw.send(bytes([POLY, CMD_IDLE_STATE, 1]))
            line = TAP.wait_for("Transition to idle", mark, timeout=25.0)
            log(f"  firmware: {line}")
            time.sleep(2.0)   # let the loop settle into steady frames
            lost, ok = _burst(raw, log, N_DURING, "during Eden idle")
            totals["during"] += lost
            froze |= ok == 0
            for l in TAP.since(mark):
                if "Eden idle" in l or "Status idle" in l or "core1" in l or "timed out" in l:
                    log(f"  console: {l}")
            raw.send(bytes([POLY, CMD_IDLE_STATE, 0]))
            time.sleep(0.5)
            lost, ok = _burst(raw, log, N_AFTER, "after idle")
            totals["after"] += lost
            froze |= ok == 0
    finally:
        raw.send(bytes([POLY, CMD_IDLE_STATE, 0]))
        raw.send(bytes([POLY, CMD_IDLE_STYLE, original]))
    log(f"TOTAL lost replies: before {totals['before']}/{CYCLES * N_BEFORE}, "
        f"during {totals['during']}/{CYCLES * N_DURING}, after {totals['after']}/{CYCLES * N_AFTER}")
    return not froze
