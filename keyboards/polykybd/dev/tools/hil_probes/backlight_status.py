# SPDX-License-Identifier: GPL-2.0-only
"""Backlight and status displays of both halves, seen by the camera (2026-11-27).

Collects the numbers a graded camera check needs, and evidence for the status
display that goes dark on one half on the rig.

A. Backlight (VIA RGB matrix, protocol 39): white, solid effect, brightness 0 /
   64 / 128 / 255, then off. Per half: the mean luma at the LED positions. The
   halves' pictures at full brightness and off go into the log as PNG.
B. Status displays: lights off, 240 s with no HID traffic, a 0.3 s clip every
   10 s. Per half: the 95th percentile luma in the status display's box (from
   the first run: lit about 90, after DISPLAY_OFF about 2). A picture of both
   halves every 30 s and whenever a half changes between lit and dark. The master's console lines about idle, display and
   sleep are logged as they come.
C. DISPLAY_OFF (what the rig sends after a passing run), 30 s.

Writes pictures as `IMG <name> <part>/<parts> <base64>`. Restores the RGB state.
Reports only; passes when it got through.
"""
import base64
import time

from station.console_log import TAP

NAME = "probe: backlight and status displays of both halves (camera)"
NEEDS_CONSOLE = True

POLY_CHANNEL = 0x50
CMD_DISPLAY_OFF = 24
GET, SET = 0x08, 0x07
CH = 3
BRIGHT, EFFECT, SPEED, COLOR = 1, 2, 3, 4
CHUNK = 3000
STEP_S = 10.0


def _emit(log, name, png):
    data = base64.b64encode(png).decode()
    parts = [data[i:i + CHUNK] for i in range(0, len(data), CHUNK)]
    for i, part in enumerate(parts):
        log(f"IMG {name} {i + 1}/{len(parts)} {part}")


# The status display's box on each board, in mm (measured from the lit picture of
# the first run, with 3 mm margin).
DISPLAY_BOX = {"left": (151, 39, 183, 57), "right": (7, 32, 38, 50)}


def _box_p95(img, ow, oh, box, px_per_mm):
    x0, y0, x1, y1 = (int(v * px_per_mm) for v in box)
    vals = sorted(img[y * ow + x] for y in range(max(0, y0), min(oh, y1))
                  for x in range(max(0, x0), min(ow, x1)))
    return vals[int(len(vals) * 0.95)] if vals else 0


class _Halves:
    def __init__(self, camera, s):
        self.camera, self.s = camera, s
        self.boards = {side: camera.load_board(side) for side in ("left", "right")}
        self._far = {}

    def stats(self, luma, w, h, side):
        c = self.camera
        ow, oh, img = c.rectify(luma, w, h, self.s, side, self.boards[side], px_per_mm=1.0)
        leds = [(int(l["x"]), int(l["y"])) for l in self.boards[side]["leds"]]
        at = [img[y * ow + x] for x, y in leds if 0 <= x < ow and 0 <= y < oh]
        return (sum(at) / len(at) if at else 0.0), _box_p95(img, ow, oh, DISPLAY_BOX[side], 1.0)

    def clip(self, label, seconds=0.3):
        rec = self.camera.record(self.s, seconds=seconds, label={"text": label})
        luma = rec.lumas[len(rec.lumas) // 2]
        return rec, luma, {side: self.stats(luma, rec.width, rec.height, side)
                           for side in ("left", "right")}

    def pictures(self, log, name, rec, luma):
        for side in ("left", "right"):
            ow, oh, img = self.camera.rectify(luma, rec.width, rec.height, self.s, side,
                                              self.boards[side], px_per_mm=2.0)
            _emit(log, f"{name}_{side}", self.camera.png_gray(ow, oh, img))


def _rgb_get(raw, vid):
    r = raw.send(bytes([GET, CH, vid, 0, 0]))
    return (r[3], r[4]) if r and r[0] == GET and r[2] == vid else None


def _rgb_set(raw, vid, *v):
    r = raw.send(bytes([SET, CH, vid, *v]))
    return bool(r) and r[0] == SET


def _console(log, mark):
    for line in TAP.since(mark):
        low = line.lower()
        if any(k in low for k in ("idle", "display", "sleep", "wake", "suspend", "oled", "eden")):
            log(f"    console: {line}")


def probe(raw, log):
    from station import camera
    s = camera.snapshot_settings(camera.SETTINGS)
    if not s.quads:
        log("  no outlines saved in Adjust view: cannot tell the halves apart; stopping")
        return False
    halves = _Halves(camera, s)

    start = {vid: _rgb_get(raw, vid) for vid in (EFFECT, BRIGHT, SPEED, COLOR)}
    log(f"  RGB state at start: {start}")
    if any(v is None for v in start.values()):
        log("  no VIA RGB reply (firmware older than protocol 39?); stopping")
        return False

    log("  -- A: backlight --")
    try:
        _rgb_set(raw, COLOR, 0, 0)          # white
        _rgb_set(raw, EFFECT, 1)            # solid colour
        rows = []
        for b in (0, 64, 128, 255):
            _rgb_set(raw, BRIGHT, b)
            time.sleep(0.5)
            rec, luma, st = halves.clip(f"probe backlight · brightness {b}")
            rows.append((b, st))
            log(f"  brightness {b:3d} (read {_rgb_get(raw, BRIGHT)}): "
                + " | ".join(f"{sd} led {st[sd][0]:5.1f} disp {st[sd][1]:3d}" for sd in st))
            if b == 255:
                halves.pictures(log, "rgb_full", rec, luma)
        _rgb_set(raw, EFFECT, 0)            # off
        time.sleep(0.5)
        rec, luma, st = halves.clip("probe backlight · off")
        log("  off: " + " | ".join(f"{sd} led {st[sd][0]:5.1f} disp {st[sd][1]:3d}" for sd in st))
        halves.pictures(log, "rgb_off", rec, luma)
        full = rows[-1][1]
        for sd in ("left", "right"):
            log(f"  {sd}: LED luma full - off = {full[sd][0] - st[sd][0]:+.1f}")
    finally:
        _rgb_set(raw, COLOR, *start[COLOR])
        _rgb_set(raw, SPEED, start[SPEED][0])
        _rgb_set(raw, BRIGHT, start[BRIGHT][0])
        _rgb_set(raw, EFFECT, 0)            # B and C watch with the lights off
    log(f"  RGB restored except the effect (off for B): {[_rgb_get(raw, v) for v in (BRIGHT, COLOR)]}")

    up = raw.send(bytes([0x02, 0x01, 0, 0, 0, 0]))
    if up and up[0] == 0x02:
        log(f"  keyboard uptime at B: {int.from_bytes(up[2:6], 'big') / 1000:.1f} s")
    log("  -- B: status displays, 300 s without HID traffic --")
    lit, top = {}, {"left": 0, "right": 0}
    t0 = time.monotonic()
    while time.monotonic() - t0 < 300:
        mark = TAP.mark()
        try:
            rec, luma, st = halves.clip("probe status displays")
        except Exception as exc:
            log(f"  camera: {exc}")
            time.sleep(STEP_S)
            continue
        dt = time.monotonic() - t0
        log(f"  [+{dt:4.0f} s] " + " | ".join(f"{sd} disp {st[sd][1]:3d} led {st[sd][0]:5.1f}"
                                            for sd in st))
        changed = False
        for sd in st:
            top[sd] = max(top[sd], st[sd][1])
            now = st[sd][1] >= 30
            if sd in lit and lit[sd] != now:
                log(f"  ** {sd} status display went {'lit' if now else 'dark'} at +{dt:.0f} s")
                changed = True
            lit[sd] = now
        if changed or int(dt) // 30 != int(dt - STEP_S) // 30 or dt < STEP_S:
            halves.pictures(log, f"status_{int(dt):03d}", rec, luma)
        _console(log, mark)
        time.sleep(max(0.0, STEP_S - 1.5))

    log("  -- C: DISPLAY_OFF, 30 s --")
    mark = TAP.mark()
    r = raw.send(bytes([POLY_CHANNEL, CMD_DISPLAY_OFF]), attempts=1)
    log(f"  DISPLAY_OFF -> {r[:3]!r}" if r else "  DISPLAY_OFF -> no reply")
    for i in range(3):
        time.sleep(STEP_S - 1.5)
        rec, luma, st = halves.clip("probe status displays · after DISPLAY_OFF")
        log(f"  [off +{(i + 1) * 10} s] " + " | ".join(f"{sd} disp {st[sd][1]:3d}" for sd in st))
        if i == 0:
            halves.pictures(log, "display_off", rec, luma)
    _console(log, mark)
    _rgb_set(raw, EFFECT, start[EFFECT][0])
    log(f"  effect restored: {_rgb_get(raw, EFFECT)}")
    return True
