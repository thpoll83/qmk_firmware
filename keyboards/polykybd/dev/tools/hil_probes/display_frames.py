# SPDX-License-Identifier: GPL-2.0-only
"""Camera frames of both halves, written into the job log (2026-11-27).

Follow-up to right_idle: the right half's status display read dim from 3 s after
boot while the left followed the idle timeout. Brightness numbers cannot tell a
lower contrast from different content, so this writes the pictures themselves:
each as a greyscale PNG, base64, in log lines

    IMG <name> <part>/<parts> <base64>

(decode with a few lines of Python). Per moment: the whole recorded frame, and
each half straightened to its board (2 px per mm).

Moments: boot +3 s, boot +60 s, boot +150 s (left idle after 120 s), and after
DISPLAY_OFF. Reports only; passes when it got through.
"""
import base64
import time

NAME = "probe: camera frames of both halves into the log"

POLY_CHANNEL = 0x50
CMD_DISPLAY_OFF = 24
CHUNK = 3000


def _emit(log, name, png):
    data = base64.b64encode(png).decode()
    parts = [data[i:i + CHUNK] for i in range(0, len(data), CHUNK)]
    for i, part in enumerate(parts):
        log(f"IMG {name} {i + 1}/{len(parts)} {part}")


def _snap(camera, s, boards, log, moment):
    rec = camera.record(s, seconds=0.3, label={"text": f"probe display_frames · {moment}"})
    luma = rec.lumas[len(rec.lumas) // 2]
    _emit(log, f"{moment}_frame", camera.png_gray(rec.width, rec.height, luma))
    for side in ("left", "right"):
        ow, oh, img = camera.rectify(luma, rec.width, rec.height, s, side, boards[side],
                                     px_per_mm=2.0)
        _emit(log, f"{moment}_{side}", camera.png_gray(ow, oh, img))
    log(f"  {moment}: frame {rec.width}x{rec.height} and both halves written")


def probe(raw, log):
    from station import camera
    s = camera.snapshot_settings(camera.SETTINGS)
    if not s.quads:
        log("  no outlines saved in Adjust view; stopping")
        return False
    boards = {side: camera.load_board(side) for side in ("left", "right")}
    t0 = time.monotonic()
    for moment, at in (("boot03", 3), ("boot60", 60), ("boot150", 150)):
        time.sleep(max(0.0, at - (time.monotonic() - t0)))
        _snap(camera, s, boards, log, moment)
    r = raw.send(bytes([POLY_CHANNEL, CMD_DISPLAY_OFF]), attempts=1)
    log(f"  DISPLAY_OFF -> {r[:3]!r}" if r else "  DISPLAY_OFF -> no reply")
    time.sleep(5)
    _snap(camera, s, boards, log, "off")
    return True
