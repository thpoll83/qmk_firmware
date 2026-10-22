# SPDX-License-Identifier: GPL-2.0-only
"""Probe: does an overlay burst landing as the board leaves Eden reset the master?

A user's master reset on 2026-10-22 with no crash record on either half. The
console and the host log line up like this:

    13:33:30.272  Wake by keypress           (out of the Eden screensaver)
    13:33:32.21   host: app switch burst      cmd 11, 66 icon fills in 3x cmd 42,
                                              1x cmd 41 PRC, 3x cmd 33 mappings
    13:33:32.329  Stop idle.                  (host cmd 15, payload 0)
    ~13:33:33.1   USB gone; boot banner at 13:33:37

So the suspect is a race between that burst and core1's Eden loop being torn
down. This probe repeats it as often as the time budget allows. Each round:

1. idle style Eden (cmd 28), start idle (cmd 15 payload 1), and wait for the
   firmware's "Transition to idle [style=eden]";
2. dwell in Eden for a random 0.2..6 s;
3. one of four orders, picked at random:
   - "field":      key wake, 1.5..2.5 s, burst, 0..100 ms, stop idle (the log);
   - "key-fast":   key wake, 0..300 ms, burst, stop idle;
   - "wake-first": stop idle, 0..500 ms, burst (no key);
   - "late-stop":  burst, 1..2 s, stop idle (no key);
4. GET_ID: a '*' status is the fresh-boot marker, which only a boot sets.

Two things only this probe branch has, for troubleshooting:

* The HIL images take cmd 14 key injection without DB_TOGG (hid_com.c), so the
  wake is a real keypress through process_record_user() -> display_wakeup().
  The key is left shift, pressed and released, which types nothing.
* The real overlay icon library (eden_wake_burst_icons.plyi, PolyKybdHost
  res/fontpack/icons.plyi, content_version 3) is flashed into slot 8 first, so
  the cmd 42 fills draw real icons on both halves. Afterwards slot 8 gets an
  empty library (v0), as icon_fill_cost.py does.

Run it with::

    tier: debug   probe: eden_wake_burst
"""
import importlib.util
import os
import random
import struct
import time

from station.console_log import TAP
from station.hil_tests import (
    CMD_GET_ID,
    CMD_IDLE_STATE,
    CMD_IDLE_STYLE,
    CMD_OVERLAY_FLAGS_OFF,
    CMD_OVERLAY_FLAGS_ON,
    DISPLAY_OVERLAYS_BIT,
    FRESH_BOOT,
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
    _overlay_map_w_report,
    _prc_record,
    _prc_report,
    parse_fontpack_versions,
)

NAME = "probe: overlay burst while leaving Eden idle"
MIN_PROTOCOL = 21          # cmd 33 flags (v21), cmd 42 (v20), cmd 41 (v19)
NEEDS_CONSOLE = True       # the idle transition is read off the console

BUDGET_S = 25 * 60         # the debug job allows 45 min, flashing included
MAX_ROUNDS = 120
SEED = 20261022
CMD_KEY = 14
KC_LSFT = 0xE1
ICONS_SLOT = 8
FILL_WIDTH = 10            # ids up to 687 need 10 bits
FILLS = 66                 # the field burst: 66 fills in 3 reports
HERE = os.path.dirname(os.path.abspath(__file__))
LIBRARY = os.path.join(HERE, "eden_wake_burst_icons.plyi")


def _sibling(name):
    spec = importlib.util.spec_from_file_location(name, os.path.join(HERE, name + ".py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def _library_version(blob):
    return struct.unpack("<4sHHI", blob[:12])[3]


def _key_wake(raw):
    for released in (0, 1):
        raw.send(bytes([POLY_CHANNEL, CMD_KEY, KC_LSFT >> 8, KC_LSFT & 0xFF, released]),
                 timeout_ms=1000, attempts=1)


def _burst(raw, rng, n_icons):
    """The app-switch burst from the field log, in its order."""
    raw.send(bytes([POLY_CHANNEL, CMD_OVERLAY_FLAGS_ON, OVERLAY_MAPPING_RESET_BITS]),
             timeout_ms=500, attempts=1)
    pairs = list(zip(range(FILLS), rng.sample(range(n_icons), FILLS)))
    per = (61 * 8 // FILL_WIDTH) // 2
    verdicts = []
    for i in range(0, FILLS, per):
        reply = raw.send(_icon_fill_report(pairs[i:i + per], FILL_WIDTH), timeout_ms=1000, attempts=1)
        verdicts.append(chr(reply[2]) if reply and len(reply) >= 3 and reply[1] == 42 else "?")
    mapping, _ = _overlay_map_w_report(9)
    first = bytes([mapping[0], mapping[1], mapping[2] | MAPPING_FLAG_RESET]) + mapping[3:]
    last = bytes([mapping[0], mapping[1], mapping[2] | MAPPING_FLAG_SHOW]) + mapping[3:]
    raw.write_reports([
        _prc_report(_prc_record(KC_A, 0, *PRC_SQUARE_BOX, PRC_SQUARE),
                    _prc_record(KC_P, 0, *PRC_CHECKER_BOX, PRC_CHECKER)),
        first, mapping, last,
    ])
    return "".join(verdicts)


def _stop_idle(raw):
    return raw.send(bytes([POLY_CHANNEL, CMD_IDLE_STATE, 0]), timeout_ms=1000, attempts=2)


def _get_id_status(raw, tries=6):
    """GET_ID status byte ('.' or '*'), or None. attempts=1 per send: GET_ID
    consumes the fresh-boot marker, so a resend must not hide one."""
    for _ in range(tries):
        reply = raw.send(bytes([POLY_CHANNEL, CMD_GET_ID]), timeout_ms=1500, attempts=1)
        if reply and len(reply) >= 3 and reply[1] == CMD_GET_ID:
            return reply[2]
        time.sleep(0.5)
    return None


def _flash_library(raw, log, blob):
    from station.fw_update import _FwLink
    import station.hil_tests as H
    cost = _sibling("icon_fill_cost")
    link = _FwLink(raw._vid, raw._pid)
    if not link.open(timeout_s=10.0):
        log(f"  cannot open the raw HID interface: {link.last_error}")
        return False
    try:
        link.drain()
        return cost._flash(link, H, blob, log)
    finally:
        link.close()


def probe(raw, log):
    rng = random.Random(SEED)
    blob = open(LIBRARY, "rb").read()
    lib_version = _library_version(blob)
    n_icons = struct.unpack("<IIIIhH", blob[32:52])[3] + 1

    versions = parse_fontpack_versions(raw.send(bytes([POLY_CHANNEL, CMD_GET_ID]), attempts=1) or b"")
    held = (versions or {}).get(ICONS_SLOT)
    if held not in (0, lib_version):
        log(f"slot {ICONS_SLOT} holds version {held!r}: refusing to overwrite it "
            "(the cleanup writes an empty library)")
        return False
    flashed = held == lib_version
    if not flashed:
        log(f"flashing the real icon library: {n_icons} icons, {len(blob)} bytes, v{lib_version}")
        flashed = True    # before the flash: a lost COMMIT reply still leaves it there
        if not _flash_library(raw, log, blob):
            log("  icon library flash failed")
    time.sleep(1.0)       # the slave reloads its library in housekeeping
    probe_fill = raw.send(_icon_fill_report([(0, 1)], FILL_WIDTH), timeout_ms=1000, attempts=1)
    log(f"one-pair fill after the flash: {probe_fill[:4] if probe_fill else probe_fill!r} "
        "('.' = drawn on both halves)")

    cur = raw.send(bytes([POLY_CHANNEL, CMD_IDLE_STYLE, 0xFF]))
    original = cur[3] if cur and len(cur) >= 4 else 1
    log(f"idle style was {original}; switching to Eden ({IDLE_STYLE_EDEN})")
    raw.send(bytes([POLY_CHANNEL, CMD_IDLE_STYLE, IDLE_STYLE_EDEN]))
    _get_id_status(raw)   # consume any fresh-boot marker left from the flash

    orders = ("field", "key-fast", "wake-first", "late-stop")
    counts = {k: 0 for k in orders}
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
            order = rng.choice(orders)
            counts[order] += 1
            time.sleep(dwell)
            if order == "field":
                gap = rng.uniform(1.5, 2.5)
                _key_wake(raw)
                time.sleep(gap)
                fills = _burst(raw, rng, n_icons)
                time.sleep(rng.uniform(0.0, 0.1))
                _stop_idle(raw)
            elif order == "key-fast":
                gap = rng.uniform(0.0, 0.3)
                _key_wake(raw)
                time.sleep(gap)
                fills = _burst(raw, rng, n_icons)
                _stop_idle(raw)
            elif order == "wake-first":
                gap = rng.uniform(0.0, 0.5)
                _stop_idle(raw)
                time.sleep(gap)
                fills = _burst(raw, rng, n_icons)
            else:
                gap = rng.uniform(1.0, 2.0)
                fills = _burst(raw, rng, n_icons)
                time.sleep(gap)
                _stop_idle(raw)

            time.sleep(1.5)   # the field reset came ~0.8 s after the stop
            status = _get_id_status(raw)
            woke = bool(TAP.find_all("Wake by keypress", mark))
            ok = status == ord(".")
            log(f"round {rounds}: dwell {dwell:.2f}s, {order} gap {gap * 1000:.0f}ms, "
                f"fills {fills}, key wake seen {woke} -> "
                + ("ok" if ok else ("REBOOTED" if status == FRESH_BOOT else f"NO ANSWER ({status!r})")))
            if not ok:
                passed = False
                for ln in TAP.since(mark):
                    if any(k in ln for k in ("reset:", "crash:", "== PolyKybd", "Stop idle",
                                             "Wake", "Transition", "Eden", "boot:")):
                        log(f"  console: {ln.strip()}")
                break
    finally:
        _stop_idle(raw)
        raw.send(bytes([POLY_CHANNEL, CMD_IDLE_STYLE, original]))
        raw.send(bytes([POLY_CHANNEL, CMD_OVERLAY_FLAGS_ON, ICON_FILL_CLEANUP_BITS]))
        raw.send(bytes([POLY_CHANNEL, CMD_OVERLAY_FLAGS_OFF,
                        DISPLAY_OVERLAYS_BIT | MIRROR_OVERLAYS_BIT]))
        log(f"restored idle style {original}, reset mapping/usage/pool, cleared overlay flags")
        if flashed:
            empty = _sibling("icon_fill_cost")._library(0, 0)
            ok = _flash_library(raw, log, empty)
            log(f"slot {ICONS_SLOT} restored to an empty library (v0): " + ("ok" if ok else "FAILED"))

    log(f"{rounds} round(s): " + ", ".join(f"{k} {v}" for k, v in counts.items())
        + f", {no_eden} without Eden")
    if passed and rounds - no_eden == 0:
        log("FAIL: Eden never engaged, so no round tested anything")
        return False
    return passed
