# SPDX-License-Identifier: GPL-2.0-only
"""Why does the split link not come up? (2026-11-26)

Seen on the rig: only the left (master) half lights its RGB, the right half's
status display works (so it is powered and runs), the master's GET_LANG replies
take up to 468 ms for 30 s after a reset, and FW_UP_BEGIN is answered with '~'
(slave erase not confirmed) until the 90 s deadline.

The debug tier flashes both halves over BOOTSEL first, so this also shows whether
a fresh flash brings the link back. Then:

1. USB state of both halves and the rig's USB device list;
2. the firmware's "Split link:" lines since boot, and again after 15 s;
3. GET_LANG latency (10 probes);
4. reset the right half alone, then the same readings;
5. right half into BOOTSEL: does its RP2040 bootloader enumerate on USB
   (its MCU and USB work, so the fault is the split cable)? Then back to run.

Reports only; passes when it got through all steps.
"""
import subprocess
import time

from station.console_log import TAP

NAME = "probe: split link down (right half dark)"
NEEDS_CONSOLE = True

POLY_CHANNEL = 0x50
CMD_GET_LANG = 7


def _split_lines(mark: int) -> list:
    return [l for l in TAP.since(mark) if "Split link" in l or "slave" in l.lower() or "==" in l]


def _latency(raw, log, label: str) -> None:
    times = []
    for _ in range(10):
        t0 = time.monotonic()
        try:
            r = raw.send(bytes([POLY_CHANNEL, CMD_GET_LANG]), timeout_ms=2500, attempts=1)
        except Exception:
            r = None
        times.append((time.monotonic() - t0) * 1000.0 if r else None)
        time.sleep(0.2)
    shown = ", ".join("lost" if t is None else f"{t:.0f}" for t in times)
    log(f"  GET_LANG latency ms ({label}): {shown}")


def _usb(log, label: str) -> None:
    out = subprocess.run(["lsusb"], capture_output=True, text=True).stdout
    keep = [l for l in out.splitlines() if "2e8a" in l.lower() or "feed" in l.lower() or "qmk" in l.lower()
            or "polykybd" in l.lower()]
    log(f"  USB devices ({label}): " + (" | ".join(keep) if keep else "(no RP2040/QMK device)"))


def probe(raw, log):
    from station.flash import FlashController
    fc = FlashController()
    for side in ("left", "right"):
        log(f"  USB port power {side}: {fc.query_usb_state(side)}")
    _usb(log, "after flash")

    log("  console since boot (split/slave/banner lines):")
    for l in _split_lines(0)[-20:]:
        log(f"    {l}")

    mark = TAP.mark()
    time.sleep(15)
    log("  console over the next 15 s:")
    for l in _split_lines(mark)[-20:]:
        log(f"    {l}")
    _latency(raw, log, "after flash")

    log("  -- reset the right half alone --")
    mark = TAP.mark()
    fc.reset("right", log=log)
    time.sleep(10)
    for l in _split_lines(mark)[-20:]:
        log(f"    {l}")
    _latency(raw, log, "after right reset")
    _usb(log, "after right reset")

    log("  -- right half into BOOTSEL, then back --")
    fc.set_bootsel("right", True, log=log)
    fc.reset("right", log=log)
    time.sleep(3)
    _usb(log, "right in BOOTSEL (expect 2e8a:0003)")
    fc.set_bootsel("right", False, log=log)
    fc.reset("right", log=log)
    time.sleep(10)
    mark2 = TAP.mark()
    time.sleep(10)
    for l in _split_lines(mark2)[-20:]:
        log(f"    {l}")
    _latency(raw, log, "after BOOTSEL round")
    _usb(log, "end")
    return True
