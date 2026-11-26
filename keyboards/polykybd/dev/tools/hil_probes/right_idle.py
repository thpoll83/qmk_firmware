# SPDX-License-Identifier: GPL-2.0-only
"""Why does the right half's status display go dark on its own? (2026-11-26)

On the rig only, the right half's status display goes dark after a short time
while the left stays lit. Idle is decided on the master and copied to the slave,
so both halves should go dark together.

The camera watches both halves. The rig has no keycap displays, so in each
half's straightened image the light comes from the RGB LEDs and the status
display: "led" is the mean luma at the LED positions, "disp" the 99th percentile
of the pixels more than 7 mm from any LED (the status display when lit).

1. 240 s with no HID traffic after the fresh flash (fresh boot);
2. DISPLAY_OFF (cmd 24), what the rig sends after a passing run; 60 s;
3. one GET_LANG (a host query, like the UI's); 60 s.

Every 10 s: a 0.3 s clip, one line per half. The master's console lines about
idle and wake are logged too. Reports only; passes when it got through.
"""
import time

from station.console_log import TAP

NAME = "probe: right half goes idle on its own"
NEEDS_CONSOLE = True

POLY_CHANNEL = 0x50
CMD_GET_LANG = 7
CMD_DISPLAY_OFF = 24
STEP_S = 10.0


def _half_stats(camera, s, rec, side, board):
    w, h = rec.width, rec.height
    luma = rec.lumas[len(rec.lumas) // 2]
    ow, oh, img = camera.rectify(luma, w, h, s, side, board, px_per_mm=1.0)
    leds = [(int(l["x"]), int(l["y"])) for l in board["leds"]]
    at = [img[y * ow + x] for x, y in leds if 0 <= x < ow and 0 <= y < oh]
    far = []
    for y in range(oh):
        for x in range(ow):
            if all((x - lx) ** 2 + (y - ly) ** 2 > 49 for lx, ly in leds):
                far.append(img[y * ow + x])
    far.sort()
    p99 = far[int(len(far) * 0.99)] if far else 0
    return (sum(at) / len(at) if at else 0.0), p99


def _watch(camera, s, boards, log, seconds, phase):
    t_end = time.monotonic() + seconds
    t0 = time.monotonic()
    while time.monotonic() < t_end:
        mark = TAP.mark()
        try:
            rec = camera.record(s, seconds=0.3, label={"text": f"probe right_idle · {phase}"})
            parts = []
            for side in ("left", "right"):
                led, disp = _half_stats(camera, s, rec, side, boards[side])
                parts.append(f"{side} led {led:5.1f} disp {disp:3d}")
            log(f"  [{phase} +{time.monotonic() - t0:4.0f} s] " + " | ".join(parts))
        except Exception as exc:
            log(f"  [{phase}] camera: {exc}")
        for line in TAP.since(mark):
            low = line.lower()
            if "idle" in low or "wake" in low or "suspend" in low or "display" in low:
                log(f"    console: {line}")
        time.sleep(max(0.0, STEP_S - 1.5))


def probe(raw, log):
    from station import camera
    s = camera.snapshot_settings(camera.SETTINGS)
    if not s.quads:
        log("  no outlines saved in Adjust view: cannot tell the halves apart; stopping")
        return False
    boards = {side: camera.load_board(side) for side in ("left", "right")}
    log(f"  idle timeout reported by the console at boot, if any:")
    for line in TAP.since(0):
        if "fade_out" in line or "idle" in line.lower():
            log(f"    {line}")

    log("  -- phase 1: fresh boot, no HID traffic --")
    _watch(camera, s, boards, log, 240, "boot")

    log("  -- phase 2: DISPLAY_OFF (as after a passing run) --")
    r = raw.send(bytes([POLY_CHANNEL, CMD_DISPLAY_OFF]), attempts=1)
    log(f"  DISPLAY_OFF -> {r[:3]!r}" if r else "  DISPLAY_OFF -> no reply")
    _watch(camera, s, boards, log, 60, "off")

    log("  -- phase 3: one GET_LANG (a host query) --")
    r = raw.send(bytes([POLY_CHANNEL, CMD_GET_LANG]), attempts=1)
    log(f"  GET_LANG -> {r[:3]!r}" if r else "  GET_LANG -> no reply")
    _watch(camera, s, boards, log, 60, "query")
    return True
