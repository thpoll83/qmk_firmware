#!/usr/bin/env python3
# Copyright 2026 thpoll83
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check that every codepoint a keycap legend can draw is in a font the board has.

    python3 tools/check_glyph_coverage.py        # from keyboards/polykybd/ ; exit 1 on a gap
    python3 tools/check_glyph_coverage.py -v     # also list every language checked

Walks every cell of the language table (lang/lang_lut.c: base, Shift, Caps and AltGr
for each of the 160 layouts), expands the named glyphs, skips the display-list op
bytes and their arguments the way bbox_walk() does, and resolves each remaining
codepoint against the runtime font table (g_all_fonts, in the order
base/fonts/generated/all_fonts_order.json records) with the same rule as
kdisp_gfx_glyph_font(): the first font whose range holds it, skipping a 0x0 gap record.

⚠️ WHY THIS EXISTS: a codepoint no font covers took the missing-glyph fallback, which
read '!' out of g_all_fonts[0] — IconsFont, at U+100000 — so the glyph index
underflowed and the board HardFaulted. hy-AM's U+2014 Shift legend did it on hardware
(2026-10-01). The fallback is fixed, but a missing glyph still draws as '!' (or, in a
SMALL run, as nothing), so a gap here is a visible defect even when it no longer
crashes. No build, test or rig run looked for it: the LUT and the fonts are generated
from two different sources and nothing compared them.

Every input is resolved strictly. An unknown named glyph, a font in the order list with
no header, or a LUT that parses to nothing is an ERROR, not a pass — a check that
silently reads less than it claims is how the original gap survived.
"""
import argparse
import glob
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
KB = os.path.dirname(HERE)

COLUMNS = ("base", "shift", "caps", "altgr")   # enum variation_index, lang_lut.h

# Display-list ops that take ARGUMENTS (base/font_lookup.c bbox_walk). Each entry is
# (argument count, which argument positions are GLYPHS to resolve). Every other byte
# below 0x20 is an op without arguments.
OP_ARGS = {
    0x0F: (1, (0,)),     # HALF  (glyph)
    0x11: (1, (0,)),     # THIN  (glyph)
    0x15: (2, (1,)),     # ROT   (angle, glyph)
    0x0E: (2, ()),       # MOVE  (x, y)
    0x12: (2, ()),       # FRAME (w, h)
    0x13: (3, ()),       # BADGE (w, h, style)
}

_STR = r'U"(?:[^"\\]|\\.)*"'


class CoverageError(Exception):
    """An input the check could not read completely. Fails the run."""


# ---- fonts ---------------------------------------------------------------------

def parse_fonts(paths):
    """{font name: (first, last, [(w, h, xAdvance), ...]) or CoverageError} from GFX
    font headers. A font that does not parse is recorded, not raised: only fonts the
    runtime table actually uses are allowed to fail the run."""
    fonts = {}
    for path in paths:
        with open(path, encoding="utf-8", errors="replace") as f:
            src = f.read()
        for m in re.finditer(r"const\s+GFXfont\s+(\w+)\s+PROGMEM\s*=\s*\{(.*?)\};", src, re.S):
            name, body = m.group(1), m.group(2)
            gm = re.search(r"\(GFXglyph\s*\*\)\s*(\w+)", body)
            rest = body[gm.end():] if gm else ""
            nums = re.findall(r"0x[0-9A-Fa-f]+|\b\d+\b", rest)
            if not gm or len(nums) < 2:
                continue
            first, last = int(nums[0], 0), int(nums[1], 0)
            gs = re.search(r"GFXglyph\s+" + re.escape(gm.group(1)) +
                           r"\s*\[\]\s*(?:PROGMEM)?\s*=\s*\{(.*?)\n\s*\};", src, re.S)
            if not gs:
                fonts[name] = CoverageError(f"{name}: glyph table {gm.group(1)} not found in {path}")
                continue
            recs = re.findall(r"\{\s*-?\d+\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,"
                              r"\s*-?\d+\s*,\s*-?\d+\s*\}", gs.group(1))
            glyphs = [tuple(int(v) for v in r) for r in recs]
            if len(glyphs) != last - first + 1:
                fonts[name] = CoverageError(f"{name}: {len(glyphs)} glyph records for range "
                                            f"0x{first:X}..0x{last:X} ({last - first + 1} expected)")
                continue
            fonts[name] = (first, last, glyphs)
    return fonts


def load_font_table(kb=KB):
    """The runtime font table, front to back, as (name, first, last, glyphs)."""
    with open(os.path.join(kb, "base", "fonts", "generated", "all_fonts_order.json")) as f:
        order = json.load(f)["order"]
    paths = sorted(glob.glob(os.path.join(kb, "base", "fonts", "generated", "*.h")) +
                   glob.glob(os.path.join(kb, "base", "fonts", "*.h")))
    fonts = parse_fonts(paths)
    missing = [n for n in order if n not in fonts]
    if missing:
        raise CoverageError(f"fonts in all_fonts_order.json with no header: {missing}")
    for n in order:
        if isinstance(fonts[n], CoverageError):
            raise fonts[n]
    return [(n,) + fonts[n] for n in order]


def font_for(table, cp):
    """kdisp_gfx_glyph_font(): first font holding cp, gap records skipped; or None."""
    for name, first, last, glyphs in table:
        if first <= cp <= last:
            w, h, adv = glyphs[cp - first]
            if w == 0 and h == 0 and adv == 0:
                continue
            return name
    return None


# ---- legends -------------------------------------------------------------------

def decode_literal(body):
    """Codepoints of the contents of one U"..." literal (C escapes resolved)."""
    out, i = [], 0
    simple = {"f": 12, "r": 13, "v": 11, "n": 10, "t": 9, "a": 7, "b": 8,
              "\\": 92, '"': 34, "'": 39, "?": 63, "0": 0}
    while i < len(body):
        c = body[i]
        if c != "\\":
            out.append(ord(c))
            i += 1
            continue
        n = body[i + 1]
        if n == "x":
            j = i + 2
            while j < len(body) and body[j] in "0123456789abcdefABCDEF":
                j += 1
            out.append(int(body[i + 2:j], 16))
            i = j
        elif n in "uU":
            k = 6 if n == "u" else 10
            out.append(int(body[i + 2:i + k], 16))
            i += k
        elif n in simple:
            out.append(simple[n])
            i += 2
        else:
            raise CoverageError(f"unsupported escape \\{n} in U\"{body}\"")
    return out


def load_named_glyphs(paths):
    """{NAME: C source of its value} from `#define NAME U"..."...` lines."""
    defs = {}
    for path in paths:
        with open(path, encoding="utf-8") as f:
            for m in re.finditer(r"^#define\s+(\w+)\s+(.+?)\s*(?://.*)?$", f.read(), re.M):
                defs[m.group(1)] = m.group(2)
    return defs


def expand_cell(cell, defs, _depth=0):
    """Codepoints of one LUT cell: string literals and named glyphs, concatenated."""
    if _depth > 8:
        raise CoverageError(f"named glyph expansion too deep in {cell!r}")
    out = []
    for tok in re.findall(_STR + r"|\b[A-Za-z_]\w*\b", cell):
        if tok.startswith('U"'):
            out += decode_literal(tok[2:-1])
        elif tok == "NULL":
            continue
        elif tok in defs:
            out += expand_cell(defs[tok], defs, _depth + 1)
        else:
            raise CoverageError(f"unknown named glyph {tok!r} in cell {cell!r}")
    return out


def drawn_codepoints(cps):
    """The codepoints of a legend the renderer resolves against fonts — op bytes and
    non-glyph op arguments removed, exactly as bbox_walk() walks the string."""
    out, i = [], 0
    while i < len(cps):
        c = cps[i]
        if c < 0x20:
            n, glyph_pos = OP_ARGS.get(c, (0, ()))
            args = cps[i + 1:i + 1 + n]
            if len(args) == n and all(args):   # bbox_walk consumes only a complete set
                out += [args[p] for p in glyph_pos]
                i += n
        else:
            out.append(c)
        i += 1
    return out


def split_cells(row):
    """The four comma-separated cells of one LUT row (strings may hold commas)."""
    cells, cur, i = [], "", 0
    while i < len(row):
        m = re.match(_STR, row[i:])
        if m:
            cur += m.group(0)
            i += m.end()
            continue
        if row[i] == ",":
            cells.append(cur.strip())
            cur = ""
        else:
            cur += row[i]
        i += 1
    if cur.strip():
        cells.append(cur.strip())
    return cells


def lut_cells(path):
    """Yield (keycode name, language, column, cell source) for every LUT cell."""
    with open(path, encoding="utf-8") as f:
        src = f.read()
    start = src.index("lang_plane")
    end = src.index("//[[[end]]]", start)
    key = None
    count = 0
    for line in src[start:end].splitlines():
        km = re.match(r"\s*\{/\*\s*(\w+):\s*\*/", line)
        if km:
            key = km.group(1)
            continue
        lm = re.match(r"\s*/\*\s+([a-z]{2}-[A-Z]{2})\s+\*/(.*)$", line)
        if not lm:
            continue
        cells = split_cells(lm.group(2))
        if len(cells) != len(COLUMNS):
            raise CoverageError(f"{key} {lm.group(1)}: {len(cells)} cells, expected 4: {line.strip()}")
        for col, cell in zip(COLUMNS, cells):
            count += 1
            yield key, lm.group(1), col, cell
    if count == 0:
        raise CoverageError(f"no LUT cells parsed from {path}")


def find_gaps(kb=KB):
    """[(codepoint, language, keycode, column)] for every legend codepoint no font has."""
    table = load_font_table(kb)
    defs = load_named_glyphs([os.path.join(kb, "lang", "named_glyphs.h")])
    gaps, langs = [], set()
    for key, lang, col, cell in lut_cells(os.path.join(kb, "lang", "lang_lut.c")):
        langs.add(lang)
        for cp in drawn_codepoints(expand_cell(cell, defs)):
            if font_for(table, cp) is None:
                gaps.append((cp, lang, key, col))
    return gaps, langs


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args(argv)
    try:
        gaps, langs = find_gaps()
    except CoverageError as e:
        print(f"check_glyph_coverage: ERROR: {e}")
        return 2
    if args.verbose:
        print(f"checked {len(langs)} languages: {' '.join(sorted(langs))}")
    if not gaps:
        print(f"check_glyph_coverage: OK — every legend codepoint of {len(langs)} languages "
              "is in a font")
        return 0
    print(f"check_glyph_coverage: {len(gaps)} legend codepoint(s) are in no font:")
    for cp, lang, key, col in sorted(gaps):
        print(f"  U+{cp:04X} {chr(cp)!r:6} {lang} {key} ({col})")
    print("Add each codepoint to a font in fonts/fonts.yaml (a resident `latin` range is "
          "the cheap case), regenerate, and re-run.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
