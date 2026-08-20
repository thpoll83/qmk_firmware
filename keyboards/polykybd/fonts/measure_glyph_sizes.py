#!/usr/bin/env python3
# Copyright 2026 thpoll83
# SPDX-License-Identifier: GPL-2.0-or-later
"""Measure the tallest/widest glyph of each `latin` fonts.yaml entry at a range of
candidate pixel sizes.

This is how the `latinbig` tiers (the bigger keycap legends, HID cmd 34) were
sized, and how to re-size them after any change. The keycap OLED is 40 px tall
and the tallest latin glyph already inks 33 of them at the base 27 px size, so a
uniform scale factor clips the accent stacks long before the plain letters run
out of room: each entry needs the largest size whose tallest glyph still fits.
A `!` marks a size whose tallest glyph would exceed 40 px — never ship one.

    python3 measure_glyph_sizes.py /tmp/fontconvert_pinned 27,33,35,39

Needs the Noto sources (fonts/dl-fonts.sh) and a fontconvert build.
"""
import re
import subprocess
import sys
from pathlib import Path

FC = sys.argv[1] if len(sys.argv) > 1 else "fontconvert"
ROOT = Path(__file__).resolve().parent.parent
TTF = ROOT / "fonts/noto-sans/NotoSans-Regular.ttf"

# (name, [(first,last), ...], base_ppem)
ENTRIES = [
    ("Base",          [(0x20, 0x7e)], 27),
    ("SupAndExtA",    [(0xa1, 0x17e)], 27),
    ("LatinExtB",     [(0x180, 0x24f)], 27),
    ("Schwa",         [(0x253, 0x253), (0x257, 0x257), (0x259, 0x259), (0x260, 0x260),
                       (0x272, 0x272), (0x288, 0x288), (0x28b, 0x28b)], 27),
    ("LatinExtAdd",   [(0x1e00, 0x1eff)], 27),
    ("Cyrillic",      [(0x401, 0x46b), (0x490, 0x4bb), (0x4d8, 0x4e9)], 31),
    ("Greek",         [(0x384, 0x385), (0x391, 0x3a1), (0x3a3, 0x3c9)], 27),
    ("CurrencySigns", [(0x20aa, 0x20ac), (0x20b1, 0x20b1), (0x20b4, 0x20b4),
                       (0x20bc, 0x20bd), (0x2116, 0x2116)], 27),
    ("LetterMod",     [(0x2c6, 0x2dd)], 27),
    ("Okina",         [(0x2bb, 0x2bb)], 27),
    ("Naira",         [(0x20a6, 0x20a6)], 27),
    ("SZ",            [(0x1e9e, 0x1e9e)], 27),
]

REC = re.compile(r"\{\s*(-?\d+),\s*(-?\d+),\s*(-?\d+),\s*(-?\d+),\s*(-?\d+),\s*(-?\d+)\s*\}")


def measure(ranges, ppem):
    argv = [FC, "-f", str(TTF), "-p", str(ppem), "-H", "auto", "-v", "_M_", "-b", "32"]
    for a, b in ranges:
        argv += [str(a), str(b)]
    out = subprocess.run(argv, capture_output=True, text=True)
    if out.returncode != 0:
        sys.exit(f"fontconvert failed: {' '.join(argv)}\n{out.stderr}")
    body = re.search(r"const GFXglyph \w+Glyphs\[\] PROGMEM = \{(.*?)\};", out.stdout, re.S).group(1)
    hmax = wmax = 0
    for o, w, h, xa, xo, yo in REC.findall(body):
        hmax = max(hmax, int(h))
        wmax = max(wmax, int(w))
    return hmax, wmax


def cap_height(ppem):
    """Ink height of 'A' — the tier's perceived size."""
    return measure([(ord("A"), ord("A"))], ppem)[0]


if __name__ == "__main__":
    ppems = [int(x) for x in (sys.argv[2].split(",") if len(sys.argv) > 2
                              else ["27", "31", "33", "35", "37", "39", "41"])]
    print("cap('A'):  " + "  ".join(f"{p}px->{cap_height(p)}" for p in ppems))
    print(f"{'entry':16s} {'base':>5s} " + " ".join(f"{p:>7d}" for p in ppems))
    for name, ranges, base in ENTRIES:
        h0, _ = measure(ranges, base)
        cells = []
        for p in ppems:
            h, w = measure(ranges, p)
            flag = "!" if h > 40 else " "
            cells.append(f"{h:3d}/{w:2d}{flag}")
        print(f"{name:16s} {base:3d}px {h0:2d} " + " ".join(f"{c:>7s}" for c in cells))
