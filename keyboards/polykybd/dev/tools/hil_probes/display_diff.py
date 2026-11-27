# SPDX-License-Identifier: GPL-2.0-only
"""How much each status display's picture changes during staging (camera, 2026-11-27).

Calibrates the screen-change limit of polykybd-ctnd's cue_checks: one clip
before BEGIN as the reference, then the staging recording. Per half and frame,
the mean absolute luma difference to the reference inside the status display
box. Logs the distribution, and two control clips of the normal status page a
few seconds apart (the difference with no screen change).

Reports only; passes when it got through.
"""
import os
import time

NAME = "probe: status display picture change during staging (camera)"
NEEDS_CONSOLE = True
SIDES = ("left", "right")


def _bin():
    from station.uf2 import uf2_file_to_bin
    uf2 = os.path.join("firmware", os.environ.get("HIL_LEFT", "polykybd_split72_hil_left.uf2"))
    data = uf2_file_to_bin(uf2).rstrip(b"\xff")
    data += b"\x00" * (-len(data) % 4)
    path = "/tmp/display_diff_probe.bin"
    with open(path, "wb") as fh:
        fh.write(data)
    return path


def _samples(camera, s, board, side, luma, w, h):
    box = board.get("status_display")
    if not box:
        return []
    to_px = camera.board_mapper(s, side, board, w, h)
    out = []
    x0, y0, x1, y1 = (int(v) for v in box)
    for y in range(y0, y1):
        for x in range(x0, x1):
            px, py = to_px(x + 0.5, y + 0.5)
            xi, yi = int(px), int(py)
            if px >= 0 and py >= 0 and xi < w and yi < h:
                out.append(luma[yi * w + xi])
    return out


def _diff(a, b):
    n = min(len(a), len(b))
    return sum(abs(a[i] - b[i]) for i in range(n)) / n if n else 0.0


def _clip(camera, s, boards, label):
    rec = camera.record(s, seconds=0.3, label={"text": label}, keep_frames=False)
    luma = rec.lumas[len(rec.lumas) // 2]
    return {sd: _samples(camera, s, boards[sd], sd, luma, rec.width, rec.height) for sd in SIDES}


def _dist(vals):
    v = sorted(vals)
    if not v:
        return "none"
    q = lambda p: v[min(len(v) - 1, int(len(v) * p))]
    return f"n={len(v)} min {v[0]:.1f} p10 {q(0.1):.1f} p50 {q(0.5):.1f} p90 {q(0.9):.1f} max {v[-1]:.1f}"


def probe(raw, log):
    from station import camera, fw_update
    s = camera.snapshot_settings(camera.SETTINGS)
    boards = {sd: camera.load_board(sd) for sd in SIDES}
    if not all(b.get("status_display") for b in boards.values()):
        log("  a board has no status_display box; stopping")
        return False
    a = _clip(camera, s, boards, "control A")
    time.sleep(3.0)
    b = _clip(camera, s, boards, "control B")
    log("  control (status page, 3 s apart): "
        + ", ".join(f"{sd} {_diff(a[sd], b[sd]):.1f}" for sd in SIDES))
    for run in range(2):
        base = _clip(camera, s, boards, "before staging")
        rec_bg = camera.BackgroundRecording(s, seconds=10.0)

        def on_begin():
            rec_bg.start()
            rec_bg.wait_ready(10.0) if hasattr(rec_bg, "wait_ready") else time.sleep(1.5)
        ok = fw_update.stage_and_verify(_bin(), log, on_begin=on_begin)
        rec = rec_bg.result(timeout=90)
        diffs = {sd: [] for sd in SIDES}
        for luma in rec.lumas:
            for sd in SIDES:
                diffs[sd].append(_diff(base[sd], _samples(camera, s, boards[sd], sd, luma,
                                                          rec.width, rec.height)))
        log(f"  run {run + 1} (staging -> {ok}):")
        for sd in SIDES:
            log(f"    {sd}: {_dist(diffs[sd])}")
            for thr in (5, 10, 15, 20, 30):
                share = sum(d >= thr for d in diffs[sd]) / len(diffs[sd])
                log(f"      share of frames >= {thr}: {share:.0%}")
        time.sleep(4.0)
    return True
