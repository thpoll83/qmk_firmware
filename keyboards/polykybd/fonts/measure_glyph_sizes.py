#!/usr/bin/env python3
# Copyright 2026 thpoll83
# SPDX-License-Identifier: GPL-2.0-or-later
"""Measure the tallest/widest glyph a fonts.yaml category would ink at a range of
candidate pixel sizes — the check that decides whether a script can carry a bigger
keycap-legend tier at all.

This is how the `latinbig` tiers (the bigger keycap legends, HID cmd 34) were
sized, and how to re-size them after any change. The keycap OLED is 40 px tall
and the tallest latin glyph already inks 33 of them at the base 27 px size, so a
uniform scale factor clips the accent stacks long before the plain letters run
out of room: each entry needs the largest size whose tallest glyph still fits.
A `!` marks a size whose tallest glyph would exceed 40 px — never ship one.

    python3 measure_glyph_sizes.py /tmp/fontconvert_pinned 27,33,35,39
    python3 measure_glyph_sizes.py /tmp/fontconvert_pinned --category hebrew,arabic,kr

The second form measures ANY category, reading its real source font, ranges and
options straight out of fonts.yaml through generate_fonts.py's own `resolve()` +
`build_argv()` — so what it measures is what would actually be emitted, and it
cannot drift from the config the way a second hardcoded table would.

⚠️ Measure before adding a script, every time. The obvious proxy — "current ink
height x the scale factor" — is an estimate, not a measurement: grid-fitting snaps
cap-height to whole pixels, so the reachable heights come in steps and a script can
lose a whole tier to rounding. The number that decides it is the tallest glyph
fontconvert actually emits at the candidate ppem, which is what this prints.

Needs the Noto sources (fonts/dl-fonts.sh) and a fontconvert build.
"""
import re
import subprocess
import sys
from pathlib import Path

import yaml

import generate_fonts as GF

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
    # Sourcery/opengrep flags every non-literal argv here. Audited: this is a
    # developer measuring tool, run by hand from a checkout. Its argv is the
    # fontconvert path (argv[1], chosen by whoever runs it), a repo-relative TTF,
    # and integers/ranges from this file or from the committed fonts.yaml through
    # generate_fonts.py's own resolver — no external or network input reaches it.
    # shell=False with a list means no shell parses the command, so there is
    # nothing to inject through; shlex.quote, the rule's suggested remedy, escapes
    # for a SHELL string and would only corrupt an argv element here.
    #
    # ⚠️ The marker must stay on the line IMMEDIATELY above the call — semgrep
    # applies it to the next line only, so moving it to the top of this block
    # (where it reads better) silently does nothing and the check stays red.
    # nosemgrep: python.lang.security.audit.dangerous-subprocess-use-audit,python.lang.security.audit.dangerous-subprocess-use-tainted-env-args
    out = subprocess.run(argv, capture_output=True, text=True)
    if out.returncode != 0:
        sys.exit(f"fontconvert failed: {' '.join(argv)}\n{out.stderr}")
    body = re.search(r"const GFXglyph \w+Glyphs\[\] PROGMEM = \{(.*?)\};", out.stdout, re.S).group(1)
    hmax = wmax = 0
    for o, w, h, xa, xo, yo in REC.findall(body):
        hmax = max(hmax, int(h))
        wmax = max(wmax, int(w))
    return hmax, wmax


def category_entries(cat: str):
    """Every fonts.yaml entry of `cat`, resolved against its category defaults."""
    cfg = yaml.safe_load((ROOT / "fonts/fonts.yaml").read_text(encoding="utf-8"))
    out = []
    for e in cfg["fonts"]:
        if e.get("category") != cat:
            continue
        r = GF.resolve(e, cfg["categories"])
        if r.get("sequence"):
            continue          # sequence mode has no range to re-size; measure by hand
        out.append(r)
    if not out:
        sys.exit(f"no non-sequence fonts.yaml entries in category {cat!r}")
    return cfg, out


def measure_entry(cfg, entry: dict, ppem=None):
    """Tallest/widest glyph this entry inks, via its REAL fontconvert argv.

    `ppem=None` measures the entry exactly as it ships — no inference from `size`,
    which is what a "current size" column must do or it reports a number nobody
    renders (it read Hebrew as 43 px tall against 25 px on the actual keycaps).
    """
    e = dict(entry)
    if ppem is not None:
        e.pop("size", None)               # -s and -p are mutually exclusive
        e.pop("render_height", None)      # a tier IS a pixel size; see the note below
        e["pixel_size"] = ppem
        e["bits"] = 32                    # relocated into plane 15
    argv = GF.build_argv(FC, e, cfg["sources"], ROOT)
    # Sourcery/opengrep flags every non-literal argv here. Audited: this is a
    # developer measuring tool, run by hand from a checkout. Its argv is the
    # fontconvert path (argv[1], chosen by whoever runs it), a repo-relative TTF,
    # and integers/ranges from this file or from the committed fonts.yaml through
    # generate_fonts.py's own resolver — no external or network input reaches it.
    # shell=False with a list means no shell parses the command, so there is
    # nothing to inject through; shlex.quote, the rule's suggested remedy, escapes
    # for a SHELL string and would only corrupt an argv element here.
    #
    # ⚠️ The marker must stay on the line IMMEDIATELY above the call — semgrep
    # applies it to the next line only, so moving it to the top of this block
    # (where it reads better) silently does nothing and the check stays red.
    # nosemgrep: python.lang.security.audit.dangerous-subprocess-use-audit,python.lang.security.audit.dangerous-subprocess-use-tainted-env-args
    out = subprocess.run(argv, capture_output=True, text=True)
    if out.returncode != 0:
        sys.exit(f"fontconvert failed: {' '.join(argv)}\n{out.stderr}")
    m = re.search(r"const GFXglyph \w+Glyphs\[\] PROGMEM = \{(.*?)\};", out.stdout, re.S)
    if not m:
        return 0, 0
    hmax = wmax = 0
    for _o, w, h, _xa, _xo, _yo in REC.findall(m.group(1)):
        hmax, wmax = max(hmax, int(h)), max(wmax, int(w))
    return hmax, wmax


def measure_categories(cats, ppems):
    """Per fonts.yaml ENTRY: what it inks as shipped, and at each candidate ppem.

    Per ENTRY, not per category, and that granularity is the point — latin needed
    four of its twelve entries capped below the tier target because one tall
    sub-font (`_LatinExtAdd_`) would otherwise clip, while `_Base_` had room to
    spare. A per-category maximum hides exactly that, and reports the category as
    unable to grow because of one glyph.

    ⚠️ `render_height` (fontconvert -r) is NOT an ink ceiling — latin carries
    `render_height: 44` and grows fine, because a tier overrides it with a pixel
    size. Read the measured ink, never the flag. (An earlier version of this
    function verdicted off the flag and declared latin unable to grow.)

    ⚠️ And the tallest glyph of a RANGE is not necessarily a legend: Hebrew's
    range includes standalone nikud marks that ink 43 px and never appear on a
    keycap. A `!` here means "some glyph in this range would clip", which is the
    right gate for emitting the range wholesale — but before adding a script,
    confirm against the codepoints that layout actually puts on its keys.
    """
    print(f"{'category':11s} {'#':>2s} {'range':>13s} {'now':>4s} " +
          " ".join(f"{p:>7d}" for p in ppems) + "   best  gain")
    for cat in cats:
        cfg, entries = category_entries(cat)
        for i, e in enumerate(entries):
            rs = e.get("ranges", [])
            span = f"{rs[0][0]:04X}..{rs[-1][1]:04X}" if rs else "-"
            cur = measure_entry(cfg, e)[0]
            cells, best, best_h = [], None, None
            for p in ppems:
                h, w = measure_entry(cfg, e, p)
                over = h > 40
                cells.append(f"{h:2d}/{w:2d}{'!' if over else ' '}")
                if not over:
                    best, best_h = p, h
            gain = f"x{best_h / cur:.2f}" if (best_h and cur) else "-"
            print(f"{cat:11s} {i:2d} {span:>13s} {cur:3d}h " +
                  " ".join(f"{c:>7s}" for c in cells) +
                  f"  {best if best else '-':>5}  {gain}")


def cap_height(ppem):
    """Ink height of 'A' — the tier's perceived size."""
    return measure([(ord("A"), ord("A"))], ppem)[0]


if __name__ == "__main__":
    if "--category" in sys.argv:
        i = sys.argv.index("--category")
        cats = sys.argv[i + 1].split(",")
        rest = [a for a in sys.argv[2:i] if not a.startswith("-")]
        ppems = [int(x) for x in (rest[0].split(",") if rest
                                  else ["27", "31", "33", "35", "37", "39", "41"])]
        measure_categories(cats, ppems)
        raise SystemExit(0)
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
