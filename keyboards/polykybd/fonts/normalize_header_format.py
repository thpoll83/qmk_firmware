#!/usr/bin/env python3
"""Re-emit a generated GFX font header in the format the committed headers use.

Why this exists
---------------
The committed ``base/fonts/generated/*.h`` were produced during the column-native
(PolyColGfx) work by a fontconvert built from a work-in-progress tree, not from any
committed Adafruit-GFX revision.  The emitter has since been tidied, so today's
pinned build renders **identical bitmaps and identical real glyph records** but
writes them differently:

===========================  ==========================  =========================
                             committed headers            current pinned emitter
===========================  ==========================  =========================
array declaration            ``Bitmaps[]PROGMEM``        ``Bitmaps[] PROGMEM``
bitmap bytes per line        16                          12, with a
                                                         ``/* range N (a - b): */``
                                                         prefix on the first line
glyph record columns         width 5                     width 4
``bitmapOffset`` on a GAP    running cumulative length   0
===========================  ==========================  =========================

Only the last row is data at all, and it is **dead data**: the renderer skips a
glyph on ``w == 0 && h == 0`` (``kdisp_write_gfx_char``) and never reads the
offset, and ``prune_shadowed_glyphs`` writes gaps as ``{off, 0, 0, 0, 0, 0}``
carrying the same running value.  So the two forms are functionally equal.

⚠️ The trailing ``// Approx. N bytes`` comment is deliberately **not** reproduced.
The committed numbers are stale — they disagree with the data in the very file
that carries them (``_Okina_`` claims 17 bytes for a 4-byte bitmap plus one
7-byte record, ``_Naira_`` claims 52 and is the only one that adds up), because
they were written before the row-major → column-native transpose changed every
bitmap length.  Matching them would mean reproducing a wrong number, so the
regenerated (correct) value is kept and ``--verify`` ignores this line.  That is
the whole residual diff of a no-op regeneration: one comment per font.

Running the pinned emitter and reformatting through this script therefore keeps a
regeneration diff down to the glyphs that actually changed, instead of rewriting
every line of a 3.4k-line header for whitespace.  ``--verify`` is the safety net:
it asserts the round trip reproduces the committed file byte for byte, so the
reformatting cannot quietly alter anything.

Usage
-----
    normalize_header_format.py NEW.h                 # normalise in place
    normalize_header_format.py NEW.h -o OUT.h        # normalise to OUT.h
    normalize_header_format.py NEW.h --verify OLD.h  # normalise, then require
                                                     # the result == OLD.h
"""
from __future__ import annotations

import argparse
import pathlib
import re
import sys

BYTES_PER_LINE = 16


def _fmt_bitmaps(name: str, body: str) -> str:
    """Re-emit a Bitmaps[] array: 16 bytes per line, no range-prefix comments."""
    data = re.findall(r"0x([0-9A-Fa-f]{2})", re.sub(r"/\*.*?\*/", "", body, flags=re.S))
    lines = []
    for i in range(0, len(data), BYTES_PER_LINE):
        chunk = ", ".join("0x" + b.upper() for b in data[i : i + BYTES_PER_LINE])
        lines.append("  " + chunk + ",")
    return f"const uint8_t {name}Bitmaps[]PROGMEM = {{\n" + "\n".join(lines) + "\n};\n"


def _fmt_glyphs(name: str, body: str) -> str:
    """Re-emit a Glyphs[] array: width-5 columns, gap offsets = running length.

    The trailing ``// 0x.. name`` comment on each record is preserved verbatim so
    the diff stays readable; only the numeric columns are rewritten.
    """
    out = [f"const GFXglyph {name}Glyphs[]PROGMEM = {{"]
    run = 0
    records: list[tuple[str, str]] = []
    for line in body.strip("\n").split("\n"):
        rec = re.match(
            r"\s*\{\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,"
            r"\s*(-?\d+)\s*,\s*(-?\d+)\s*\}\s*(?:,|\};)?\s*(//.*)?$",
            line,
        )
        if not rec:
            # A bare comment line (the "// bmpOff, w, h, …" per-range banner). It must
            # stay INTERLEAVED with the records, so it goes in the same list rather
            # than straight to `out` — appending it directly floats every banner above
            # every record.
            if line.strip().startswith("//"):
                records.append((line.rstrip(), None))
            continue
        _, w, h, xadv, xoff, yoff = (int(rec.group(i)) for i in range(1, 7))
        comment = rec.group(7) or ""
        # committed column widths: first field 6, the rest 5, trailing space before }
        records.append(
            (f"  {{{run:6d},{w:5d},{h:5d},{xadv:5d},{xoff:5d},{yoff:5d} }}", comment)
        )
        if w and h:
            run += w * ((h + 7) // 8)

    # The committed form closes the array on the LAST record's line ("… } }; // cmt")
    # rather than on a line of its own, so emit that record separately.  `comment is
    # None` marks a passthrough banner line, which is never the closing record.
    last = max((i for i, (_, c) in enumerate(records) if c is not None), default=None)
    if last is None:
        out.append("};")
        return "\n".join(out) + "\n"
    for i, (rec, comment) in enumerate(records):
        if comment is None:
            out.append(rec)
        elif i == last:
            out.append(rec + " };" + (f" {comment}" if comment else ""))
        else:
            out.append(rec + "," + (f"   {comment}" if comment else ""))
    return "\n".join(out) + "\n"


_ARRAY_START = re.compile(r"const (uint8_t|GFXglyph) (\w+?)(Bitmaps|Glyphs)\[\]\s*PROGMEM = \{")


def normalize(text: str) -> str:
    """Rewrite every Bitmaps[]/Glyphs[] array; pass everything else through.

    Line-based rather than one regex over the file: an array can close either on
    its own ``};`` line *or* on the last record's line (``… } };  // comment``),
    and a non-greedy match over the whole text runs past the second form into the
    following struct.
    """
    out: list[str] = []
    lines = text.split("\n")
    i = 0
    while i < len(lines):
        start = _ARRAY_START.match(lines[i])
        if not start:
            out.append(lines[i])
            i += 1
            continue
        name, kind = start.group(2), start.group(3)
        body, i = [], i + 1
        while i < len(lines):
            line = lines[i]
            body.append(line)
            i += 1
            # closes on a bare "};" or trailing "};" on the final record's line
            if line.strip() == "};" or re.search(r"\}\s*\};", line):
                break
        joined = "\n".join(body)
        # drop the inline array close but KEEP the record's trailing comment
        joined = re.sub(r"\}\s*\};", "}", joined)
        joined = re.sub(r"^\};\s*$", "", joined, flags=re.M)
        fmt = _fmt_bitmaps if kind == "Bitmaps" else _fmt_glyphs
        out.extend(fmt(name, joined).rstrip("\n").split("\n"))
    # the font struct itself only differs by the same [] PROGMEM spacing
    return "\n".join(out).replace("[] PROGMEM", "[]PROGMEM")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("header", type=pathlib.Path)
    ap.add_argument("-o", "--out", type=pathlib.Path)
    ap.add_argument("--verify", type=pathlib.Path, metavar="REFERENCE",
                    help="require the normalised output to equal REFERENCE byte for byte")
    args = ap.parse_args()

    result = normalize(args.header.read_text(encoding="utf-8"))

    if args.verify:
        want = args.verify.read_text(encoding="utf-8")
        # the stale size comments are expected to differ -- see the module docstring
        mask = lambda t: re.sub(r"// Approx\. \d+ bytes", "// Approx. N bytes", t)
        if mask(result) != mask(want):
            a, b = mask(result).split("\n"), mask(want).split("\n")
            for i, (x, y) in enumerate(zip(a, b)):
                if x != y:
                    print(f"MISMATCH at line {i + 1}:\n  got:  {x!r}\n  want: {y!r}",
                          file=sys.stderr)
                    break
            else:
                print(f"MISMATCH: line count {len(a)} vs {len(b)}", file=sys.stderr)
            return 1
        print(f"verified: normalised {args.header} == {args.verify}")
        return 0

    (args.out or args.header).write_text(result, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
