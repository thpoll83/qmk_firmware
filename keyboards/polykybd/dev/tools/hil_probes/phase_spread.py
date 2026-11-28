# SPDX-License-Identifier: GPL-2.0-only
"""The spread of the cyan cue's phase between the halves over several stagings (camera).

The extended HIL run of 2026-11-28 measured -46.7 ms, after -10.7 and -8.3 ms
earlier that day. This stages the flashed image RUNS times, records the cue during
each staging the way the runner's phase check does, and logs each offset, its
correlation and both halves' LED step, then the spread. Reports only.
"""
import os
import time

NAME = "probe: phase spread of the staging cue over several stagings (camera)"
NEEDS_CONSOLE = True
RUNS = 5


def _bin():
    from station.uf2 import uf2_file_to_bin
    uf2 = os.path.join("firmware", os.environ.get("HIL_LEFT", "polykybd_split72_hil_left.uf2"))
    data = uf2_file_to_bin(uf2).rstrip(b"\xff")
    data += b"\x00" * (-len(data) % 4)
    path = "/tmp/phase_spread_probe.bin"
    with open(path, "wb") as fh:
        fh.write(data)
    return path


def probe(raw, log):
    from station import camera, fw_update
    s = camera.snapshot_settings(camera.SETTINGS)
    path = _bin()
    offsets = []
    for run in range(RUNS):
        rec_bg = camera.BackgroundRecording(s)

        def on_begin():
            rec_bg.start()
            if hasattr(rec_bg, "wait_ready"):
                rec_bg.wait_ready(10.0)
        ok = fw_update.stage_and_verify(path, lambda _m: None, on_begin=on_begin)
        try:
            res, _, _ = camera.analyse_traces(rec_bg.result(timeout=90), s)
        except Exception as exc:
            log(f"  run {run + 1}: recording failed: {exc}")
            continue
        offsets.append(res.offset_ms)
        log(f"  run {run + 1} (staging {ok}): offset {res.offset_ms:+.1f} ms, corr "
            f"{res.correlation:.2f}, LED step master {res.step_left_ms:.0f} slave "
            f"{res.step_right_ms:.0f} ms, {res.frames} frames")
        time.sleep(4.0)
    if offsets:
        mean = sum(offsets) / len(offsets)
        log(f"  spread over {len(offsets)} runs: min {min(offsets):+.1f} max {max(offsets):+.1f} "
            f"mean {mean:+.1f} ms")
    return True
