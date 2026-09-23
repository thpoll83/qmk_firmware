#!/usr/bin/env python3
# Copyright 2026 thpoll83
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check that key_display_index() names the panel the render walk actually lands on.

    python3 tools/check_disp_index.py          # from keyboards/polykybd/ ; exit 1 on a mismatch
    python3 tools/check_disp_index.py -v       # print every key's mapping

⚠️ THE MATRIX INDEX AND THE DISPLAY INDEX ARE DIFFERENT SPACES ON split72's RIGHT HALF.
update_displays() walks the KEYMAP (matrix rows/cols) while addressing PANELS — the
chip-select table, SA_GEOM_*[], the per-panel dirty-window bboxes are all in display
space. On the right half the upper four matrix rows carry no col-0 key, so matrix col c
sits behind display col c-1.

It used `LAYOUT_TO_INDEX(r, c)`, which performs no fold, so for as long as the
dirty-window feature existed every right-half panel's bbox was remembered under its
NEIGHBOUR's index. Nothing showed, because legends are similar centred boxes and
union(neighbour's previous, new) happened to cover the old ink — until the focus ripple
drew a thin off-centre arc, whose bbox is nothing like a legend's, and parts of the ring
stopped being erased. On the SLAVE only, because only the right half folds.

This is the mechanical check that settles it: replay the walking-zero panel walk against
the real keymap and the real layout, and compare each key's landing panel with what
key_display_index() says. Reading the two and agreeing that they agree is what let 28
wrong mappings sit there; this does not.
"""
import argparse, json, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
KB   = os.path.dirname(HERE)


def layout_order(board):
    with open(os.path.join(KB, board, "keyboard.json")) as f:
        d = json.load(f)
    (name, v), = d["layouts"].items()
    return name, [tuple(k["matrix"]) for k in v["layout"]]


def base_layer(board):
    """The _BL layer's keycodes, in layout order.

    The macro name is read from the keymap rather than assumed: split42 wraps the
    layout macro in POLY_LAYOUT, and hardcoding LAYOUT_* would silently miss it.
    """
    with open(os.path.join(KB, board, "keymaps", "default", "keymap.c")) as f:
        src = f.read()
    m = re.search(r"\[_L0\]\s*=\s*([A-Za-z_][A-Za-z0-9_]*)\s*\(", src)
    if not m:
        raise SystemExit("%s: could not find the [_L0] block" % board)
    start = m.end() - 1
    depth = 0
    for i in range(start, len(src)):
        if src[i] == "(":
            depth += 1
        elif src[i] == ")":
            depth -= 1
            if depth == 0:
                end = i
                break
    args, dep, cur = [], 0, ""
    for ch in src[start + 1:end]:
        if ch in "([":
            dep += 1
        elif ch in ")]":
            dep -= 1
        if ch == "," and dep == 0:
            args.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        args.append(cur.strip())
    return args


def fold(board):
    """key_display_index(), transcribed from <board>/<board>.c — and checked against it."""
    with open(os.path.join(KB, board, board + ".c")) as f:
        src = f.read()
    body = src[src.index("uint8_t key_display_index("):]
    body = body[:body.index("\n}")]
    folds = "c--;" in body or "c -= 1;" in body
    rng = re.search(r"r\s*>=\s*(\d+)\s*&&\s*r\s*<=\s*(\d+)", body)
    if folds != bool(rng):
        raise SystemExit("%s: could not read the fold out of key_display_index()" % board)
    lo, hi = (int(rng.group(1)), int(rng.group(2))) if rng else (None, None)
    return lo, hi


def table_select(board):
    """True when the variant selects each panel from key_display[] directly."""
    with open(os.path.join(KB, board, board + ".h")) as f:
        return "#define POLY_DISP_SELECT_BY_TABLE" in f.read()


def check(board, rows_per_side, cols, panels, verbose):
    # ⚠️ Only the WALKING-ZERO path can be checked this way. With table select
    # (split42) the panel is whatever key_display[idx] holds, so there is no running
    # position to compare against and replaying the walk reports mismatches that mean
    # nothing — it did, on split42's thumb row, before this gate existed. The fold is
    # a split72 property anyway; split42 has none.
    if table_select(board):
        print("%s: n/a — selects panels from key_display[] directly, no walking zero "
              "and no right-half column fold." % board)
        return 0
    _, order = layout_order(board)
    args = base_layer(board)
    if len(args) != len(order):
        # A wrapper macro that reorders or pads (split42's POLY_LAYOUT) cannot be
        # zipped against the layout. Say so rather than checking nothing: a check
        # that quietly passes on a board it never read is worse than no check.
        print("%s: SKIP — the keymap's layout macro takes %d entries, keyboard.json "
              "lists %d positions; cannot map keycodes to the matrix here."
              % (board, len(args), len(order)))
        return 0
    kc = dict(zip(order, args))
    lo, hi = fold(board)

    def key_display_index(r, c):
        if lo is not None and lo <= r <= hi:
            if c == 0:
                return 255
            c -= 1
        idx = (r % rows_per_side) * cols + c
        return idx if idx < panels else 255

    bad = []
    for left in (True, False):
        off = 0 if left else rows_per_side
        pos, skip = 0, 0
        for r in range(rows_per_side):
            for c in range(cols):
                if kc.get((r + off, c), "KC_NO") == "KC_NO":
                    skip += 1          # no OLED behind an absent key; realigned at row end
                    continue
                got = key_display_index(r + off, c)
                if verbose:
                    print("   %s (%d,%d) -> panel %d  helper %s" %
                          ("L" if left else "R", r + off, c, pos, got))
                if got != pos:
                    bad.append((("L" if left else "R"), r + off, c, pos, got))
                pos += 1
            pos += skip
            skip = 0
    for half, r, c, pos, got in bad:
        print("%s: %s half matrix (%d,%d) renders to panel %d but key_display_index() says %s"
              % (board, half, r, c, pos, got))
    print("%s: %d key(s) checked, %d mismatch(es)" % (board, len(kc), len(bad)))
    return len(bad)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-v", "--verbose", action="store_true")
    a = ap.parse_args()
    bad = check("split72", 5, 8, 40, a.verbose)
    bad += check("split42", 4, 6, 24, a.verbose)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
