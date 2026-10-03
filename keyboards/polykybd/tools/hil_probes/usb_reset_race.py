# SPDX-License-Identifier: GPL-2.0-only
"""Reboot the master N times under USB interrupt-masking stress; count enumeration failures.

Question: when the USB interrupt is blocked across a host bus reset AND the
SETUP that follows it (a flash erase does that: BY25Q64ES tSE 35 ms typ), does
the RP2040 USB driver still enumerate? ChibiOS-Contrib's ISR handles SETUP
before BUS_RESET; the reset then rewinds EP0's state machine under the reply it
just armed, and the status stage is STALLed. The fix handles the reset first.

Needs a firmware built with ``-e POLYKYBD_USB_STRESS=yes`` (base/usb_stress.h),
which masks interrupts in 35/70/150/300 ms windows while the host enumerates and
prints a ``usbdiag:`` line with the ISR's counters. Each round:

1. reboot both halves (cmd 43), wait for the master to answer GET_ID again;
2. read the firmware's last ``usbdiag:`` line for that boot;
3. read the rig's kernel log for USB enumeration errors since the reboot;
4. read cmd 39, so a watchdog reset during the stress is not mistaken for USB.

PASS means: the stress demonstrably hit the race (``both`` > 0 in some round)
and the host saw no enumeration error and the board never needed its own USB
reconnect. A run that never hit the race is reported as INCONCLUSIVE (FAIL),
not as a pass.

Run it with::

    tier: debug   probe: usb_reset_race

⚠️ ``tier: debug`` builds the NORMAL HIL images. Cherry-pick 31158ea (sets
``POLYKYBD_USB_STRESS ?= yes`` for HIL builds) in a temporary commit first, see
UPSTREAM_PATCHES.md; without it the probe stops after round 1.
"""
import os
import re
import subprocess
import time

from station.console_log import TAP
from station.hil_tests import (
    _crash_record_body, _wait_for_reboot, classify_boot_round,
    CRASH_HID_FLAG_PRESENT,
)

NAME = "probe: USB bus-reset/SETUP race under masked-IRQ stress"
NEEDS_CONSOLE = True
MIN_PROTOCOL = 22            # cmd 43 (REBOOT)

POLY_CHANNEL = 0x50
CMD_REBOOT = 43
ROUNDS = int(os.environ.get("USB_RACE_ROUNDS", "15"))
RETURN_S = 90.0              # a host that gives up + the firmware's 15 s self-heal
DIAG_WAIT_S = 30.0
SLAVE_WAIT_S = 8.0
# EP0 STALLs a healthy boot arms on this rig: the Pi's Linux asks for the
# DEVICE_QUALIFIER three times and a full-speed device refuses each (2 STALLs per
# refusal). A property of the HOST, so re-measure it on another rig. Anything
# above it is a failed control transfer, which also catches a race that falls
# outside the 48-entry ISR event log.
EP0_STALL_BASELINE = 6

DIAG_RE = re.compile(r"usbdiag: (up=\d+.*)$")
EV_RE = re.compile(r"usbev: (i=\d+.*)$")
EV_KV = re.compile(r"(\w+)=([0-9a-fA-F>/]+)")
INTS_SETUP, INTS_RESET, INTS_BUFF = 0x10000, 0x1000, 0x10
EP0_STATE = {0: "STP_WAITING", 9: "IN_TX", 10: "IN_WAITING_TX0", 11: "IN_SENDING_STS",
             20: "OUT_WAITING_STS", 21: "OUT_RX", 6: "ERROR"}
KV_RE = re.compile(r"(\w+)=(\d+)")
# Kernel lines that mean an enumeration step failed or was retried.
KERNEL_BAD = re.compile(
    r"device descriptor read|unable to enumerate|not accepting address|"
    r"can't set config|Cannot enable|device not responding|error -(32|71|110|62)",
    re.IGNORECASE)
KERNEL_NEW = re.compile(r"new (full|low|high)-speed USB device", re.IGNORECASE)
KERNEL_TS = re.compile(r"^\[\s*(\d+\.\d+)\]\s*(.*)$")


# --- kernel log -------------------------------------------------------------

def _uptime() -> float:
    with open("/proc/uptime") as f:
        return float(f.read().split()[0])


def _kernel_lines():
    """[(monotonic_s, text)] from dmesg, else journalctl -k; (source, lines|None, why)."""
    attempts = (
        ("dmesg", ["dmesg"]),
        ("journalctl -k", ["journalctl", "-k", "-b", "-o", "short-monotonic", "--no-pager", "-q"]),
    )
    why = []
    for name, cmd in attempts:
        try:
            out = subprocess.run(cmd, capture_output=True, text=True, timeout=10)
        except Exception as e:  # noqa: BLE001 — missing binary, timeout
            why.append(f"{name}: {e}")
            continue
        if out.returncode != 0 or not out.stdout.strip():
            why.append(f"{name}: rc={out.returncode} {out.stderr.strip()[:120]}")
            continue
        lines = []
        for raw in out.stdout.splitlines():
            m = KERNEL_TS.match(raw.strip())
            if m:
                lines.append((float(m.group(1)), m.group(2)))
        if lines:
            return name, lines, ""
        why.append(f"{name}: no timestamped lines")
    return None, None, "; ".join(why)


def _kernel_since(t0: float):
    src, lines, why = _kernel_lines()
    if lines is None:
        return None, why
    return [text for ts, text in lines if ts >= t0 and "usb" in text.lower()], src


# --- firmware diag ----------------------------------------------------------

def _diag_lines(mark):
    out = []
    for line in TAP.since(mark):
        m = DIAG_RE.search(line)
        if m:
            out.append(dict((k, int(v)) for k, v in KV_RE.findall(m.group(1))))
    return out


def _events(mark):
    """The ISR event log lines of this boot, decoded, keyed by index (last copy wins)."""
    evs = {}
    for line in TAP.since(mark):
        m = EV_RE.search(line)
        if not m:
            continue
        kv = dict(EV_KV.findall(m.group(1)))
        try:
            b, a = kv["st"].split(">")
            rq0, rq1 = kv["rq"].split("/")
            evs[int(kv["i"])] = {
                "t": int(kv["t"]), "ints": int(kv["ints"], 16), "buf": int(kv["buf"], 16),
                "before": int(b), "after": int(a), "bmrt": int(rq0, 16), "breq": int(rq1, 16),
                "v": int(kv["v"], 16), "l": int(kv["l"]), "stalls": int(kv["stalls"]),
                "addr": int(kv["addr"]),
            }
        except (KeyError, ValueError):
            continue
    return [evs[k] for k in sorted(evs)]


def _ev_text(e, t0):
    flags = "+".join(n for bit, n in ((INTS_RESET, "RESET"), (INTS_SETUP, "SETUP"),
                                      (INTS_BUFF, "BUFF")) if e["ints"] & bit)
    rq = (f" req={e['bmrt']:02x}/{e['breq']:02x} v={e['v']:04x} l={e['l']}"
          if e["ints"] & INTS_SETUP else "")
    ep0 = f" ep0buf={e['buf'] & 3:#x}" if e["ints"] & INTS_BUFF else ""
    return (f"+{(e['t'] - t0) / 1000:8.1f} ms {flags:<17}{rq}{ep0} "
            f"ep0 {EP0_STATE.get(e['before'], e['before'])}->{EP0_STATE.get(e['after'], e['after'])} "
            f"stalls={e['stalls']} addr={e['addr']}")


def _classify(evs):
    """For each pass that saw RESET and SETUP together: what happened to EP0 next.

    'clean'   -- the next EP0 pass started from a state the SETUP put it in;
    'broken'  -- the next EP0 completion found EP0 in STP_WAITING (the reply's
                 state was rewound), or EP0 went to ERROR, or a stall followed.
    """
    out = []
    for i, e in enumerate(evs):
        if (e["ints"] & (INTS_RESET | INTS_SETUP)) != (INTS_RESET | INTS_SETUP):
            continue
        nxt = next((x for x in evs[i + 1:] if x["ints"] & (INTS_BUFF | INTS_SETUP | INTS_RESET)), None)
        verdict = "no-follow-up"
        if e["after"] == 6:
            # The same pass ended in ERROR (rig run 37124742288, round 5: a
            # RESET+SETUP+BUFF pass whose stale IN completion hit setup_error).
            verdict = "broken"
        elif nxt is not None:
            if nxt["after"] == 6 or nxt["stalls"] > e["stalls"] or \
                    (nxt["ints"] & INTS_BUFF and not nxt["ints"] & INTS_SETUP and nxt["before"] == 0):
                verdict = "broken"
            else:
                verdict = "clean"
        out.append((e, nxt, verdict))
    return out


def _final_diag(mark, log):
    """The last usbdiag line of this boot, once the stress has ended (or the wait expired)."""
    deadline = time.monotonic() + DIAG_WAIT_S
    last = None
    while time.monotonic() < deadline:
        lines = _diag_lines(mark)
        if lines:
            last = lines[-1]
            if last.get("stress_end", 0) and last["up"] >= last["stress_end"] + 4000:
                return last
        time.sleep(1.0)
    if last is None:
        log("    no usbdiag line seen -- is this a POLYKYBD_USB_STRESS build?")
    return last


# --- the probe --------------------------------------------------------------

def probe(raw, log):
    src, _, why = _kernel_lines()
    if src:
        log(f"kernel log source: {src}")
    else:
        log(f"kernel log NOT readable on this rig ({why}); judging on firmware counters only")

    totals = {"both": 0, "rounds_with_both": 0, "kernel_bad": 0, "reconnects": 0,
              "ep0_stalls": 0, "ep0_stalls_over_baseline": 0, "resets": 0,
              "extra_attach": 0, "crashes": 0,
              "race_clean": 0, "race_broken": 0, "race_unknown": 0}
    reset_first = None
    boot_times = []
    for n in range(1, ROUNDS + 1):
        t_kernel = _uptime() if src else 0.0
        mark = TAP.mark()
        try:
            raw.send(bytes([POLY_CHANNEL, CMD_REBOOT]), attempts=1)
        except Exception:  # noqa: BLE001 — may already be gone
            pass
        t0 = time.monotonic()
        state = _wait_for_reboot(raw, RETURN_S)
        dt = time.monotonic() - t0
        if state in ("no-reply", "not-rebooted"):
            log(f"  round {n}: master {state} after {dt:.1f} s -- stopping")
            totals["kernel_bad"] += 1      # count it as a failed enumeration
            break
        boot_times.append(dt)

        d = _final_diag(mark, log) or {}
        if not d and n == 1:
            # Not a USB result: the image has no stress. Fourteen more reboots
            # would only end in INCONCLUSIVE.
            log("FAIL: round 1 printed no usbdiag line -- the flashed image is not a "
                "POLYKYBD_USB_STRESS build (tier: debug builds the normal HIL images)")
            return False
        if reset_first is None and "reset_first" in d:
            reset_first = d["reset_first"]
        both = d.get("both", 0)
        totals["both"] += both
        totals["rounds_with_both"] += 1 if both else 0
        totals["reconnects"] += d.get("reconnects", 0)
        ep0_stalls = d.get("ep0_stalls", 0)
        totals["ep0_stalls"] += ep0_stalls
        totals["ep0_stalls_over_baseline"] += max(0, ep0_stalls - EP0_STALL_BASELINE)
        totals["resets"] += d.get("resets", 0)

        master = _crash_record_body(raw, 0)
        slave = None
        deadline = time.monotonic() + SLAVE_WAIT_S
        while True:
            slave = _crash_record_body(raw, 1)
            if (slave is not None and slave[0] & CRASH_HID_FLAG_PRESENT) or time.monotonic() >= deadline:
                break
            time.sleep(1.0)
        ok, detail = classify_boot_round(master, slave)
        if not ok:
            totals["crashes"] += 1
            log(f"    round {n}: crash record: {detail}")

        kern_txt = ""
        if src:
            klines, kwhy = _kernel_since(t_kernel)
            if klines is None:
                log(f"    round {n}: kernel log unreadable ({kwhy})")
                klines = []
            bad = [k for k in klines if KERNEL_BAD.search(k)]
            attaches = sum(1 for k in klines if KERNEL_NEW.search(k))
            totals["kernel_bad"] += len(bad)
            totals["extra_attach"] += max(0, attaches - 1)
            kern_txt = f" kernel: {attaches} attach, {len(bad)} error line(s)"
            for k in bad[:6]:
                log(f"      kernel: {k}")
        evs = _events(mark)
        races = _classify(evs)
        for e, nxt, verdict in races:
            totals["race_" + ("unknown" if verdict == "no-follow-up" else verdict)] += 1
        if evs and (n <= 2 or races):
            t0e = evs[0]["t"]
            log(f"    ISR event log ({len(evs)} of {d.get('ev')} passes):")
            for e in evs:
                mark_txt = ""
                for r, _nxt, verdict in races:
                    if r is e:
                        mark_txt = f"   <-- RESET+SETUP in one pass: {verdict}"
                log(f"      {_ev_text(e, t0e)}{mark_txt}")
        log(f"  round {n}: back in {dt:.1f} s ({state}); start_delay={d.get('start_delay')}ms usbdiag "
            f"resets={d.get('resets')} setups={d.get('setups')} both={both} "
            f"ep0_stalls={d.get('ep0_stalls')} first_reset={d.get('first_reset')}ms "
            f"last_reset={d.get('last_reset')}ms windows={d.get('windows')} "
            f"masked={d.get('masked_ms')}ms reconnects={d.get('reconnects')}{kern_txt}")

    log(f"driver: reset_first={reset_first} "
        f"({'fixed order: BUS_RESET before SETUP' if reset_first == 1 else 'Contrib order: SETUP before BUS_RESET' if reset_first == 0 else 'unknown'})")
    if boot_times:
        log(f"boot: {min(boot_times):.1f}..{max(boot_times):.1f} s over {len(boot_times)} round(s)")
    log("totals: " + " ".join(f"{k}={v}" for k, v in totals.items()))

    if totals["rounds_with_both"] == 0:
        log("INCONCLUSIVE: the stress never put a bus reset and a SETUP into one ISR pass")
        return False
    # A crash record (e.g. the late-boot watchdog firing under the stress) fails
    # the run too: the board did not survive the boot, whatever USB then did.
    failed = totals["kernel_bad"] or totals["reconnects"] or totals["extra_attach"] \
        or totals["race_broken"] or totals["crashes"] or totals["ep0_stalls_over_baseline"]
    log("RESULT: " + ("enumeration FAILED under the race" if failed
                      else "enumeration survived every race"))
    return not failed
