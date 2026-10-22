# SPDX-License-Identifier: GPL-2.0-only
"""Probe: does an overlay burst landing as the board leaves Eden reset the master?

A user's master reset on 2026-10-22 with no crash record on either half. The
console and the host log line up like this:

    13:33:30.272  Wake by keypress           (out of the Eden screensaver)
    13:33:32.21   host: app switch burst      3x cmd 42 icon fills, 1x cmd 41 PRC,
                                              3x cmd 33 mappings
    13:33:32.329  Stop idle.                  (host cmd 15, payload 0)
    ~13:33:33.1   USB gone; boot banner at 13:33:37

So the suspect is a race between that burst and core1's Eden loop being torn
down. This probe repeats it as often as the time budget allows:

1. idle style Eden (cmd 28), then start idle (cmd 15 payload 1) and wait for the
   firmware's "Transition to idle [style=eden]";
2. dwell in Eden for a random 0.2..6 s;
3. one of three orders, picked at random each round:
   - "field":  burst, then stop idle 0..100 ms later (what the log shows);
   - "wake-first": stop idle, then the burst 0..500 ms later;
   - "late-stop": burst, then stop idle 1..2 s later;
4. check the master answers and the console printed no boot banner.

The rig cannot press a key (cmd 14 needs debug_enable, which takes a physical
key), so the wake is always the host's cmd 15. The rig holds no icon library,
so the cmd 42 fills are refused at their first pair: they exercise the relay and
the reply, not the icon draw. The PRC record for KC_P is bridged to the slave.

A reset shows up as a boot banner (and, from fw 1.9.11, a "reset:" line naming
the cause) in the console, or as a master that stops answering.

Run it with::

    tier: debug   probe: eden_wake_burst
"""
import random
import time

from station.console_log import TAP
from station.hil_tests import (
    CMD_FILL_POOL_FROM_ICON,
    CMD_IDLE_STATE,
    CMD_IDLE_STYLE,
    CMD_OVERLAY_FLAGS_OFF,
    CMD_OVERLAY_FLAGS_ON,
    DISPLAY_OVERLAYS_BIT,
    ICON_FILL_CLEANUP_BITS,
    IDLE_ENGAGE_TIMEOUT_S,
    IDLE_STYLE_EDEN,
    KC_A,
    KC_P,
    MAPPING_FLAG_RESET,
    MAPPING_FLAG_SHOW,
    MIRROR_OVERLAYS_BIT,
    OVERLAY_MAPPING_RESET_BITS,
    POLY_CHANNEL,
    PRC_CHECKER,
    PRC_CHECKER_BOX,
    PRC_SQUARE,
    PRC_SQUARE_BOX,
    _icon_fill_report,
    _master_alive,
    _overlay_map_w_report,
    _prc_record,
    _prc_report,
)

NAME = "probe: overlay burst while leaving Eden idle"
MIN_PROTOCOL = 21          # cmd 33 flags (v21), cmd 42 (v20), cmd 41 (v19)
NEEDS_CONSOLE = True       # the idle transition and a reboot are read off the console

BUDGET_S = 25 * 60         # the debug job allows 45 min, flashing included
MAX_ROUNDS = 120
SEED = 20261022
BANNER = "== PolyKybd"


def _burst(raw):
    """The app-switch burst from the field log, in its order."""
    # "Prepare for MRU send (mirror + reset mapping/usage)": cmd 11.
    raw.send(bytes([POLY_CHANNEL, CMD_OVERLAY_FLAGS_ON, OVERLAY_MAPPING_RESET_BITS]),
             timeout_ms=500, attempts=1)
    # Three icon-fill reports (cmd 42 replies; on the rig it refuses at pair 0).
    for base in (0, 27, 54):
        pairs = [(base + i, i) for i in range(27)]
        raw.send(_icon_fill_report(pairs, 9), timeout_ms=500, attempts=1)
    mapping, _ = _overlay_map_w_report(9)
    first = (bytes([mapping[0], mapping[1], mapping[2] | MAPPING_FLAG_RESET])
             + mapping[3:])
    last = (bytes([mapping[0], mapping[1], mapping[2] | MAPPING_FLAG_SHOW])
            + mapping[3:])
    raw.write_reports([
        _prc_report(_prc_record(KC_A, 0, *PRC_SQUARE_BOX, PRC_SQUARE),
                    _prc_record(KC_P, 0, *PRC_CHECKER_BOX, PRC_CHECKER)),
        first, mapping, last,
    ])


def _stop_idle(raw):
    return raw.send(bytes([POLY_CHANNEL, CMD_IDLE_STATE, 0]), timeout_ms=1000, attempts=2)


def probe(raw, log):
    rng = random.Random(SEED)
    cur = raw.send(bytes([POLY_CHANNEL, CMD_IDLE_STYLE, 0xFF]))
    if cur is None or len(cur) < 4:
        log(f"idle-style query failed: {cur!r}")
        return False
    original = cur[3]
    log(f"idle style was {original}; switching to Eden ({IDLE_STYLE_EDEN})")
    raw.send(bytes([POLY_CHANNEL, CMD_IDLE_STYLE, IDLE_STYLE_EDEN]))

    counts = {"field": 0, "wake-first": 0, "late-stop": 0}
    no_eden = 0
    t_end = time.monotonic() + BUDGET_S
    passed = True
    rounds = 0
    try:
        while rounds < MAX_ROUNDS and time.monotonic() < t_end:
            rounds += 1
            mark = TAP.mark()
            raw.send(bytes([POLY_CHANNEL, CMD_IDLE_STATE, 1]))
            line = TAP.wait_for("Transition to idle", mark, timeout=IDLE_ENGAGE_TIMEOUT_S)
            if line is None or "style=eden" not in line:
                no_eden += 1
                log(f"round {rounds}: Eden did not engage ({line!r}); skipping the round")
                _stop_idle(raw)
                continue

            dwell = rng.uniform(0.2, 6.0)
            order = rng.choice(("field", "wake-first", "late-stop"))
            counts[order] += 1
            time.sleep(dwell)
            if order == "field":
                gap = rng.uniform(0.0, 0.1)
                _burst(raw)
                time.sleep(gap)
                _stop_idle(raw)
            elif order == "wake-first":
                gap = rng.uniform(0.0, 0.5)
                _stop_idle(raw)
                time.sleep(gap)
                _burst(raw)
            else:
                gap = rng.uniform(1.0, 2.0)
                _burst(raw)
                time.sleep(gap)
                _stop_idle(raw)

            time.sleep(1.5)   # the field reset came ~0.8 s after the stop
            alive = _master_alive(raw, log)
            rebooted = TAP.find_all(BANNER, mark)
            log(f"round {rounds}: dwell {dwell:.2f}s, {order} gap {gap * 1000:.0f}ms -> "
                + ("ok" if alive and not rebooted else "RESET"))
            if rebooted or not alive:
                passed = False
                for ln in TAP.since(mark):
                    if any(k in ln for k in ("reset:", "crash:", BANNER, "Stop idle",
                                             "Transition", "Eden")):
                        log(f"  console: {ln.strip()}")
                if not alive:
                    log("  master did not answer GET_ID after the round")
                break
    finally:
        _stop_idle(raw)
        raw.send(bytes([POLY_CHANNEL, CMD_IDLE_STYLE, original]))
        raw.send(bytes([POLY_CHANNEL, CMD_OVERLAY_FLAGS_ON, ICON_FILL_CLEANUP_BITS]))
        raw.send(bytes([POLY_CHANNEL, CMD_OVERLAY_FLAGS_OFF,
                        DISPLAY_OVERLAYS_BIT | MIRROR_OVERLAYS_BIT]))
        log(f"restored idle style {original}, reset mapping/usage/pool, cleared overlay flags")

    log(f"{rounds} round(s): " + ", ".join(f"{k} {v}" for k, v in counts.items())
        + f", {no_eden} without Eden")
    if passed and rounds - no_eden == 0:
        log("FAIL: Eden never engaged, so no round tested anything")
        return False
    return passed
