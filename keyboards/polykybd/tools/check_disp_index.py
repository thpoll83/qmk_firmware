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
union(neighbour's previous, new) happened to cover the old ink — until an animation drew
a thin off-centre arc, whose bbox is nothing like a legend's, and parts of that shape
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
        # ⚠️ This is a FAILURE, not a skip, and the difference is the whole gate.
        # split42 has already returned above through table_select(), so the only
        # board that can reach this line is split72 — the one board this check
        # reads at all. A wrapper macro that reorders or pads cannot be zipped
        # against the layout, so the mapping is UNKNOWN, not fine; returning 0
        # would turn the gate green having checked nothing, which is exactly the
        # silent pass it exists to prevent.
        print("%s: FAIL — the keymap's layout macro takes %d entries, keyboard.json "
              "lists %d positions; cannot map keycodes to the matrix here."
              % (board, len(args), len(order)))
        return 1
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


# The two production walks that must address panels through the fold. Both assign
# `disp_idx` and hand it to panel-space consumers (the chip-select table, SA_GEOM_*[],
# the per-panel dirty-window bbox).
CALLERS = ("update_displays", "kdisp_idle")


def fn_body(src, name):
    """-> the body of a top-level `... name(...) {` definition, or None.

    Relies on this file's formatting: the closing brace of a top-level function is a
    `}` in column 0. That is true throughout poly_keymap.c and is checked by the
    caller, which fails loudly rather than silently reporting no problem.
    """
    m = re.search(r"^[A-Za-z_][\w \t\*]*\b%s\s*\([^;{]*\)\s*\{" % re.escape(name),
                  src, re.M)
    if not m:
        return None
    end = re.compile(r"^\}", re.M).search(src, m.end())
    return src[m.end():end.start()] if end else None


def strip_comments(src):
    """-> `src` with C comments blanked.

    ⚠️ LOAD-BEARING. Without it the substring test below matches the COMMENT that
    explains the fold rather than the call that performs it — and update_displays()
    carries exactly such a comment, so a revert of its call to LAYOUT_TO_INDEX() still
    passed this gate. Caught by mutation; the mutant is the whole reason this exists.
    """
    return re.sub(r"//[^\n]*|/\*.*?\*/", "", src, flags=re.S)


def check_callers(verbose):
    """⚠️ The mapping being right is NOT the same as anything USING it.

    The walk above proves key_display_index() names the right panel. It reads only
    split72.c and keyboard.json, so reverting update_displays() to LAYOUT_TO_INDEX(r, c)
    would leave every assertion above passing while the original bug came straight back
    — the gate would go green over exactly the defect it was written for. That is the
    same shape as the two it already guards, so it is checked here rather than trusted.

    This is a source-text check, not a semantic one: it asserts each walk resolves its
    panel through key_display_index(), and cannot tell you the argument is right. The
    walk above is what does that.
    """
    path = os.path.join(KB, "poly_keymap.c")
    with open(path, encoding="utf-8") as f:
        src = f.read()
    bad = 0
    for name in CALLERS:
        body = fn_body(src, name)
        if body is None:
            # Not "no problem": the check could not be performed at all.
            print("poly_keymap.c: FAIL — could not read %s()'s body; the caller check "
                  "did not run." % name)
            bad += 1
            continue
        body = strip_comments(body)
        if "key_display_index(" in body:
            if verbose:
                print("   %s() resolves its panel through key_display_index()" % name)
        else:
            used = "LAYOUT_TO_INDEX" if "LAYOUT_TO_INDEX" in body else "something else"
            print("poly_keymap.c: FAIL — %s() does not call key_display_index() (uses %s). "
                  "It addresses panels, so on split72's right half every bbox lands under "
                  "the neighbouring panel's index." % (name, used))
            bad += 1
    print("poly_keymap.c: %d of %d display walk(s) go through key_display_index()"
          % (len(CALLERS) - bad, len(CALLERS)))
    return bad


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-v", "--verbose", action="store_true")
    a = ap.parse_args()
    bad = check("split72", 5, 8, 40, a.verbose)
    bad += check("split42", 4, 6, 24, a.verbose)
    bad += check_callers(a.verbose)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
