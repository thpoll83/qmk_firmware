#!/usr/bin/env python3
"""Measure the vertical layout of a PolyKybd status-OLED panel.

Wraps the preview module's draw helpers so every lit pixel is attributed to the
call that produced it, then reduces the panel to contiguous lit-row BANDS and the
GAPS between them. That is the thing you actually tune -- `--diag` only reports
pixels off the panel, so it cannot see two rows colliding or slack pooling at the
bottom.

Run from keyboards/polykybd/:
    python3 .claude/skills/status-oled-layout/measure_bands.py 72          # both modes
    python3 .claude/skills/status-oled-layout/measure_bands.py 72 --calls  # per-draw-call
    python3 .claude/skills/status-oled-layout/measure_bands.py 42
"""
import argparse
import os
import sys

# Resolve tools/ from THIS file, not cwd, so the script runs from anywhere.
_HERE = os.path.dirname(os.path.abspath(__file__))
_TOOLS = os.path.normpath(os.path.join(_HERE, "..", "..", "..", "tools"))
sys.path.insert(0, _TOOLS if os.path.isdir(_TOOLS) else os.path.join(os.getcwd(), "tools"))

# Draw helpers worth attributing; missing names are skipped per module.
WRAPPED = ("draw", "draw_bitmap", "draw_glyph", "draw_glyph_half", "draw_glyph_center",
           "draw_text", "draw_text_center", "draw_text_center_half", "draw_right",
           "draw_brightness_row", "draw_brightness", "draw_brightness_bars",
           "draw_lang_column", "draw_speed_gauge", "round_rect", "fill_rect")


def instrument(mod, sink):
    """Replace each draw helper with one that also records its pixels into `sink`.

    ⚠️ These helpers NEST (draw_text -> draw_glyph, draw_brightness_row -> draw_bitmap
    + draw), so a naive wrapper reports the same pixels two or three times and the
    derived "gap above" is nonsense. Only depth-0 (outermost) calls are recorded.
    """
    depth = [0]
    for name in WRAPPED:
        fn = getattr(mod, name, None)
        if fn is None:
            continue

        def make(fn=fn, name=name):
            def wrapper(setpix, *a, **k):
                got = []

                def tap(x, y):
                    got.append((x, y))
                    return setpix(x, y)

                depth[0] += 1
                try:
                    r = fn(tap, *a, **k)
                finally:
                    depth[0] -= 1
                if got and depth[0] == 0:
                    sink.append((name, min(y for _, y in got), max(y for _, y in got),
                                 min(x for x, _ in got), max(x for x, _ in got)))
                return r
            return wrapper
        setattr(mod, name, make())


def bands(points, xlo, xhi):
    """Contiguous runs of lit rows within the x-slice [xlo, xhi]."""
    rows = sorted({y for x, y in points if xlo <= x <= xhi})
    out, start, prev = [], None, None
    for y in rows:
        if start is None:
            start = y
        elif y != prev + 1:
            out.append((start, prev))
            start = y
        prev = y
    if start is not None:
        out.append((start, prev))
    return out


def report(label, points, xlo, xhi, height):
    b = bands(points, xlo, xhi)
    if not b:
        print(f"  {label:26s} (empty)")
        return
    gaps = [b[i + 1][0] - b[i][1] - 1 for i in range(len(b) - 1)]
    slack = height - 1 - b[-1][1]
    flag = "  <-- OVERLAP" if any(g < 0 for g in gaps) else ("  <-- TOUCHING" if 0 in gaps else "")
    print(f"  {label:26s} bands={b}")
    print(f"  {'':26s} gaps={gaps}  bottom slack={slack}{flag}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("variant", choices=["72", "42"])
    ap.add_argument("--calls", action="store_true", help="list every draw call's extent")
    ap.add_argument("--lang", default="en-US")
    ap.add_argument("--wpm", type=int, default=62)
    ap.add_argument("--brightness", type=int, default=35)
    args = ap.parse_args()

    calls = []
    if args.variant == "72":
        import status_oled_preview as P
        fonts = P.load_fonts()
        instrument(P, calls)
        # (label, x-slice) -- the indicator column sits on each panel's INNER edge,
        # so slice it apart from the text area or the two get merged into one band.
        views = [("L", "layout text", 0, 107), ("L", "lock column", 108, 127),
                 ("R", "RGB text", 20, 127), ("R", "speed column", 0, 19)]
        modes = [("RGB ON", (128, 255, 100, 80, 5, "Rainbow")), ("RGB OFF", None)]
        for mname, rgb in modes:
            print(f"--- split72 {mname} ---")
            for side, label, xlo, xhi in views:
                calls.clear()
                pts = P.build_panel(side, *fonts, args.brightness, rgb, args.lang, args.wpm)
                report(f"{side} {label}", pts, xlo, xhi, 64)
                if args.calls:
                    for n, ylo, yhi, cxlo, cxhi in calls:
                        if cxlo >= xlo and cxhi <= xhi:
                            print(f"      {n:22s} y={ylo:3d}..{yhi:3d}  x={cxlo:3d}..{cxhi:3d}")
    else:
        import status_oled42_preview as Q
        fonts = Q.load()
        instrument(Q, calls)
        # split42 composes in a LOGICAL 32x128 portrait space (pset rotates into the
        # 128x32 page buffer), so bands run over the full logical height, not 64.
        for side, label in (("L", "layout half"), ("R", "lock half")):
            calls.clear()
            pts = Q.build(side, *fonts, args.brightness, Q.SHORT_NAMES[0],
                          args.lang, args.wpm)
            print(f"--- split42 {label} ---")
            report(label, pts, 0, 31, 128)
            if args.calls:
                for n, ylo, yhi, cxlo, cxhi in calls:
                    print(f"      {n:22s} y={ylo:3d}..{yhi:3d}  x={cxlo:3d}..{cxhi:3d}")


if __name__ == "__main__":
    main()
