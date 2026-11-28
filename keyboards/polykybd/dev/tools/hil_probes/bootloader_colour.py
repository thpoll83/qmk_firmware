# SPDX-License-Identifier: GPL-2.0-only
"""The LED colour of both halves when the master enters the bootloader (camera, 2026-11-28).

From the code: the master paints (0, 5, 3) in display_bootloader_message() and
then jumps to BOOTSEL, so its LEDs keep that last frame. The slave paints the same
(0, 5, 3) when BOOTLOADER_DISPLAY arrives, then rgb.c paints (24, 6, 0) orange on
every following frame. On the hardware the two halves look alike to the eye.

One recording, lights off first. Two reference colours through VIA (solid orange
and solid teal), lights off, then HID cmd 23. Per half, the mean Y, U and V at the
LED positions every 100 ms, with a rough RGB conversion. Then both halves are reset
through RUN, so the rig gets back to the firmware.

Reports only; passes when it got through.
"""
import threading
import time

from station.console_log import TAP

NAME = "probe: LED colour on both halves after cmd 23 (camera)"
NEEDS_CONSOLE = True

POLY, CMD_BOOTLOADER = 0x50, 0x17
SET, CH = 0x07, 3
BRIGHTNESS, EFFECT, COLOR = 1, 2, 4
SOLID = 1
RECORD_S = 22.0
SIDES = ("left", "right")


def _yuv_at(points, w, h):
    """[(y index, u index, v index)] for each LED pixel inside the frame."""
    out, ysz, csz = [], w * h, (w // 2) * (h // 2)
    for _ref, x, y in points:
        xi, yi = int(x), int(y)
        if 0 <= xi < w and 0 <= yi < h:
            c = (yi // 2) * (w // 2) + xi // 2
            out.append((yi * w + xi, ysz + c, ysz + csz + c))
    return out


def _rgb(y, u, v):
    u, v = u - 128.0, v - 128.0
    return (y + 1.402 * v, y - 0.344 * u - 0.714 * v, y + 1.772 * u)


def probe(raw, log):
    from station import camera
    from station.flash import FlashController
    s = camera.snapshot_settings(camera.SETTINGS)
    if not s.quads:
        log("  no outlines saved in Adjust view; stopping")
        return False
    boards = {sd: camera.load_board(sd) for sd in SIDES}
    idx = {sd: _yuv_at(camera.led_pixels(s, sd, boards[sd], s.width, s.height),
                       s.width, s.height) for sd in SIDES}
    log(f"  {s.width}x{s.height}; LED pixels left {len(idx['left'])} right {len(idx['right'])}")

    stats = []   # per frame: {side: (y, u, v)}
    orig = camera.split_yuv420

    def split(data, w, h):
        frame = w * h * 3 // 2
        for i in range(0, len(data) - frame + 1, frame):
            row = {}
            for sd in SIDES:
                n = len(idx[sd]) or 1
                row[sd] = tuple(sum(data[i + p[k]] for p in idx[sd]) / n for k in range(3))
            stats.append(row)
        return orig(data, w, h)

    def via(vid, *vals):
        r = raw.send(bytes([SET, CH, vid, *vals]))
        return r[:5].hex() if r else None

    marks = []
    t_ready = {}

    def script():
        def at(t, what, fn):
            dt = t_ready["t"] + t - time.monotonic()
            if dt > 0:
                time.sleep(dt)
            res = fn()
            marks.append(((time.monotonic() - t_ready["t"]) * 1000, what, res))

        at(0.2, "lights off", lambda: via(EFFECT, 0))
        at(2.5, "solid on", lambda: (via(EFFECT, SOLID), via(BRIGHTNESS, 96)))
        at(2.7, "ref orange (hue 11)", lambda: via(COLOR, 11, 255))
        at(5.5, "ref teal (hue 111)", lambda: via(COLOR, 111, 255))
        at(8.5, "lights off", lambda: via(EFFECT, 0))
        at(10.5, "cmd 23", lambda: raw.send(bytes([POLY, CMD_BOOTLOADER]), attempts=1))

    camera.split_yuv420 = split
    try:
        mark = TAP.mark()
        bg = camera.BackgroundRecording(s, seconds=RECORD_S)
        bg.start()
        if not bg.wait_ready(15):
            log("  camera never delivered a frame")
            return False
        t_ready["t"] = time.monotonic()
        th = threading.Thread(target=script, daemon=True)
        th.start()
        try:
            rec = bg.result(timeout=RECORD_S + 60)
        except Exception as exc:
            log(f"  recording failed: {exc}")
            rec = None
        th.join(30)
    finally:
        camera.split_yuv420 = orig

    for t, what, res in marks:
        log(f"  mark {t / 1000:6.2f} s: {what} -> {res!r}")
    if rec is not None and stats:
        ts = rec.timestamps_ms
        nxt = 0.0
        for t, row in zip(ts, stats):
            t -= ts[0]
            if t < nxt:
                continue
            nxt += 100.0
            parts = []
            for sd in SIDES:
                y, u, v = row[sd]
                r, g, b = _rgb(y, u, v)
                parts.append(f"{sd} Y{y:5.1f} U{u:5.1f} V{v:5.1f} rgb({r:4.0f},{g:4.0f},{b:4.0f})")
            log(f"  [{t / 1000:5.2f} s] " + " | ".join(parts))
    for line in TAP.since(mark):
        if "ootloader" in line or "BOOTLOADER" in line:
            log(f"    console: {line}")

    fc = FlashController()
    for sd in SIDES:
        try:
            fc.reset(sd, log)
        except Exception as exc:
            log(f"  reset {sd} failed: {exc}")
    time.sleep(5)
    return True
