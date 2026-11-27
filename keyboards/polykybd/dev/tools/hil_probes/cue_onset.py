# SPDX-License-Identifier: GPL-2.0-only
"""When each half's cyan staging cue starts after FW_UP_BEGIN (camera, 2026-11-27).

Reported from hardware: while flashing over HID, the slave's cyan cue starts
noticeably earlier than the master's. The camera records from 1.5 s before BEGIN;
per half, the mean luma at the LEDs every 50 ms, and the first time each half
rises above its level before BEGIN. Lights off first, so the cue is the only
light. Then the staging runs to the end and the prompt is cancelled as usual.

Reports only; passes when it got through.
"""
import os
import threading
import time

from station.console_log import TAP

NAME = "probe: cyan staging cue onset on both halves (camera)"
NEEDS_CONSOLE = True

SET, CH, EFFECT = 0x07, 3, 2
LEAD_S = 1.5
RECORD_S = 8.0
RISE = 10.0


def _bin():
    from station.uf2 import uf2_file_to_bin
    uf2 = os.path.join("firmware", os.environ.get("HIL_LEFT", "polykybd_split72_hil_left.uf2"))
    data = uf2_file_to_bin(uf2).rstrip(b"\xff")
    data += b"\x00" * (-len(data) % 4)
    path = "/tmp/cue_onset_probe.bin"
    with open(path, "wb") as fh:
        fh.write(data)
    return path


def probe(raw, log):
    from station import camera, fw_update
    s = camera.snapshot_settings(camera.SETTINGS)
    if not s.quads:
        log("  no outlines saved in Adjust view; stopping")
        return False
    boards = {sd: camera.load_board(sd) for sd in ("left", "right")}
    bin_path = _bin()
    r = raw.send(bytes([SET, CH, EFFECT, 0]))
    log(f"  effect off -> {r[:3]!r}" if r else "  effect off -> no reply")
    time.sleep(1.0)

    for run in range(2):
        log(f"  -- run {run + 1} --")
        bg = camera.BackgroundRecording(s, seconds=RECORD_S)
        bg.start()
        time.sleep(LEAD_S + 1.0)   # rpicam-vid needs about 1 s to start
        mark = TAP.mark()
        t_begin = {}
        result = {}

        def stage():
            t_begin["t"] = time.monotonic()
            result["ok"] = fw_update.stage_and_verify(bin_path, log)

        th = threading.Thread(target=stage, daemon=True)
        th.start()
        try:
            rec = bg.result(timeout=RECORD_S + 40)
        except Exception as exc:
            log(f"  recording failed: {exc}")
            th.join(300)
            continue
        ts = rec.timestamps_ms
        base, first, rows, nxt = {}, {}, [], 0.0
        for t, luma in zip(ts, rec.lumas):
            t -= ts[0]
            if t < nxt:
                continue
            nxt += 50.0
            lv = {sd: camera.half_levels(luma, rec.width, rec.height, s, sd, boards[sd])[0]
                  for sd in boards}
            rows.append((t, lv))
            for sd, v in lv.items():
                if t < 1000.0:
                    base[sd] = max(base.get(sd, 0.0), v)
                elif sd not in first and v > base.get(sd, 0.0) + RISE:
                    first[sd] = t
        for t, lv in rows:
            log(f"  [{t / 1000:5.2f} s] left (master) {lv['left']:5.1f}  "
                f"right (slave) {lv['right']:5.1f}")
        log(f"  baseline {base}; first rise: "
            + ", ".join(f"{sd} {first[sd] / 1000:.2f} s" if sd in first else f"{sd} never"
                        for sd in ("left", "right")))
        if "left" in first and "right" in first:
            log(f"  ** slave minus master onset: {first['right'] - first['left']:+.0f} ms")
        th.join(300)
        log(f"  staging -> {result.get('ok')}")
        for line in TAP.since(mark):
            if "FW_UP_BEGIN" in line or "erase" in line.lower():
                log(f"    console: {line}")
        time.sleep(4.0)   # past FLASH_RGB_HOLD_MS
    return True
