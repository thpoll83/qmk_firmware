# SPDX-License-Identifier: GPL-2.0-only
"""The orange "can't type" cue on both halves, seen by the camera (2026-11-27).

Reported from hardware: during a firmware apply the halves breathe out of step,
and one half ends its orange noticeably later than the other. Also: on a device
reset only the slave lit orange (fixed on this branch, checked in C).

A. Staging, for comparison: the cyan cue's phase offset (master minus slave).
B. The FW-2 prompt: stage the rig's unsigned image, hold the ACCEPT/REJECT prompt
   for PROMPT_S while the camera records, then cancel it. Phase offset and each
   half's LED step interval.
C. Device reset (HID cmd 43) with the lights off: per-half LED luma every 50 ms
   from 1 s before the command to RESET_S after it.

Reports only; passes when it got through.
"""
import glob
import os
import threading
import time

from station.console_log import TAP

NAME = "probe: orange cue timing on both halves (camera)"
NEEDS_CONSOLE = True

POLY = 0x50
CMD_REBOOT = 0x2B
SET, GET, CH, EFFECT = 0x07, 0x08, 3, 2
PROMPT_S = 10.0
RESET_S = 12.0


def _bin():
    bins = sorted(glob.glob(os.path.join("firmware", "*.bin")))
    pick = [b for b in bins if "split72" in b and "left" in b] or bins
    return bins, (pick[0] if pick else None)


def _phase(camera, s, rec, log, what):
    res, master, slave = camera.analyse_traces(rec, s)
    log(f"  {what}: offset {res.offset_ms:+.1f} ms (master minus slave), "
        f"correlation {res.correlation:.2f}, {res.frames} frames at {res.fps:.0f} fps, "
        f"swing master {res.swing_left:.0f} slave {res.swing_right:.0f}, "
        f"LED step master {res.step_left_ms:.0f} ms slave {res.step_right_ms:.0f} ms")
    return master, slave


def _console(log, mark, keys):
    for line in TAP.since(mark):
        if any(k in line for k in keys):
            log(f"    console: {line}")


def _stage(log, bin_path, camera, s, hold_prompt):
    """stage_and_verify, with the camera on the cue. With hold_prompt the COMMIT
    cancel waits PROMPT_S while a second recording runs on the prompt."""
    from station import fw_update
    staging = camera.BackgroundRecording(s, seconds=6.0)
    prompt = {"rec": None}
    orig = fw_update._FwLink.xfer

    def xfer(self, payload, *a, **k):
        if hold_prompt and bytes(payload[:3]) == bytes([POLY, 0x42, ord("x")]):
            log(f"  prompt is up; recording {PROMPT_S:.0f} s before the cancel")
            bg = camera.BackgroundRecording(s, seconds=PROMPT_S)
            bg.start()
            try:
                prompt["rec"] = bg.result(timeout=PROMPT_S + 40)
            except Exception as exc:
                log(f"  prompt recording failed: {exc}")
        return orig(self, payload, *a, **k)

    fw_update._FwLink.xfer = xfer
    try:
        ok = fw_update.stage_and_verify(bin_path, log, on_begin=staging.start)
    finally:
        fw_update._FwLink.xfer = orig
    try:
        rec = staging.result(timeout=60)
    except Exception as exc:
        log(f"  staging recording failed: {exc}")
        rec = None
    return ok, rec, prompt["rec"]


def probe(raw, log):
    from station import camera
    s = camera.snapshot_settings(camera.SETTINGS)
    if not s.quads:
        log("  no outlines saved in Adjust view: cannot tell the halves apart; stopping")
        return False
    bins, bin_path = _bin()
    log(f"  images: {bins}; staging {bin_path}")
    if not bin_path:
        return False

    log("  -- A + B: staging, then the FW-2 prompt --")
    mark = TAP.mark()
    ok, rec_stage, rec_prompt = _stage(log, bin_path, camera, s, hold_prompt=True)
    log(f"  stage_and_verify -> {ok}")
    if rec_stage is not None:
        _phase(camera, s, rec_stage, log, "staging (cyan)")
    if rec_prompt is not None:
        _phase(camera, s, rec_prompt, log, "prompt (orange)")
    else:
        log("  no prompt recording (the image may be signed, or the prompt never came)")
    _console(log, mark, ("COMMIT", "confirm", "CONFIRM", "FW-2", "prompt"))
    time.sleep(4)   # past FLASH_RGB_HOLD_MS

    log("  -- C: device reset (cmd 43), lights off --")
    r = raw.send(bytes([SET, CH, EFFECT, 0]))
    log(f"  effect off -> {r[:3]!r}" if r else "  effect off -> no reply")
    time.sleep(0.5)
    bg = camera.BackgroundRecording(s, seconds=RESET_S + 1.0)
    bg.start()
    time.sleep(1.0)
    mark = TAP.mark()
    t_cmd = time.time()
    threading.Thread(target=lambda: raw.send(bytes([POLY, CMD_REBOOT]), attempts=1),
                     daemon=True).start()
    try:
        rec = bg.result(timeout=RESET_S + 40)
    except Exception as exc:
        log(f"  reset recording failed: {exc}")
        return True
    boards = {side: camera.load_board(side) for side in ("left", "right")}
    ts = rec.timestamps_ms
    log(f"  {len(ts)} frames; cmd 43 sent about 1.0 s into the recording "
        f"(t={t_cmd:.2f}). left = master (USB), right = slave; led = mean luma at "
        f"the LEDs, disp = p95 in the status display box")
    nxt = 0.0
    for t, luma in zip(ts, rec.lumas):
        t -= ts[0]
        if t < nxt:
            continue
        nxt += 50.0
        lv = {sd: camera.half_levels(luma, rec.width, rec.height, s, sd, boards[sd])
              for sd in boards}
        log(f"  [{t / 1000 - 1.0:+6.2f} s] left led {lv['left'][0]:5.1f} disp "
            f"{lv['left'][1]:3d} | right led {lv['right'][0]:5.1f} disp {lv['right'][1]:3d}")
    _console(log, mark, ("reboot", "Reboot", "slave ack", "boot"))
    return True
