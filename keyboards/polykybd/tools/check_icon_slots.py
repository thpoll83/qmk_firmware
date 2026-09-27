#!/usr/bin/env python3
"""Report which resident IconsFont codepoints are taken, free, or mis-named.

`base/fonts/gfx_icons.h` is hand-maintained and is the only authority on which
icon slots hold a glyph. The named_glyphs sheet cannot answer that — its "Distance
Helper" column measures the sheet against ITSELF, so a codepoint that is taken in
the font but absent from the sheet reads as free space.

IconsFont lives in the plane-16 private-use area (U+100000..U+10FFFD), which no
other font and no real character uses. It used to own the C1 block 0x7F..0xA0,
where every slot next to the band shadowed a real character (0xA1 is ¡, which the
es-* layouts need). The move left the old gates below as one: the range must stay
inside plane-16 PUA.

    python3 tools/check_icon_slots.py            # from keyboards/polykybd/
    python3 tools/check_icon_slots.py --free     # just the next free slot

Exit 1 if anything is inconsistent, so it can gate a commit.
"""
import re, sys, os

HERE = os.path.dirname(os.path.abspath(__file__))
KB   = os.path.dirname(HERE)
ICONS = os.path.join(KB, "base", "fonts", "gfx_icons.h")
NAMES = os.path.join(KB, "lang", "named_glyphs.h")

PUA_FIRST, PUA_LAST = 0x100000, 0x10FFFD   # Supplementary Private Use Area-B


def icons_font():
    """-> (first, last, {cp: (w, h)}) reading the glyph table, gaps excluded."""
    src = open(ICONS, encoding="utf-8").read()
    m = re.search(r'IconsGlyphs\[\]\s*PROGMEM\s*=\s*\{(.*?)\n\};', src, re.S)
    rng = re.search(r'\(GFXglyph \*\)IconsGlyphs,\s*(0x[0-9A-Fa-f]+),\s*(0x[0-9A-Fa-f]+)', src)
    first, last = int(rng.group(1), 16), int(rng.group(2), 16)
    glyphs, cp = {}, first
    for line in m.group(1).splitlines():
        f = re.match(r'\s*\{\s*(\d+),\s*(\d+),\s*(\d+),', line)
        if not f:
            continue
        w, h = int(f.group(2)), int(f.group(3))
        if w and h:                       # w==h==0 is a deliberate gap
            glyphs[cp] = (w, h)
        cp += 1
    return first, last, glyphs


def named():
    """-> {cp: MACRO} for every single-codepoint named_glyphs macro in plane-16 PUA."""
    out = {}
    for line in open(NAMES, encoding="utf-8"):
        m = re.match(r'#define\s+(\w+)\s+U"\\x([0-9A-Fa-f]{2,6})"\s*(?://.*)?$', line.strip())
        if m and int(m.group(2), 16) >= PUA_FIRST:
            out[int(m.group(2), 16)] = m.group(1)
    return out


first, last, glyphs = icons_font()
names = named()
problems = []

if not (PUA_FIRST <= first <= last <= PUA_LAST):
    problems.append(f"IconsFont range U+{first:X}..U+{last:X} leaves plane-16 PUA "
                    "and would shadow a real character")

print(f"IconsFont range U+{first:X}..U+{last:X}  ({len(glyphs)} glyphs)\n")
print(f"{'cp':<9} {'glyph':<8} {'macro':<24} state")
for cp in range(first, max(last, max(names, default=last)) + 1):
    g = glyphs.get(cp)
    nm = names.get(cp, "")
    if cp > last:
        state = "past last, but NAMED" if nm else "free (past last)"
        if nm:
            problems.append(f"U+{cp:X} macro {nm} points past IconsFont.last (no glyph)")
    elif g:
        state = "taken"
        if not nm:
            state = "taken, UNNAMED"
            problems.append(f"U+{cp:X} has a glyph but no named_glyphs macro")
    else:
        state = "free (gap)"
        if nm:
            problems.append(f"U+{cp:X} macro {nm} points at an emptied gap")
            state = "gap, but NAMED"
    print(f"U+{cp:X}  {(f'{g[0]}x{g[1]}' if g else '-'):<8} {nm:<24} {state}")

free = [cp for cp in range(first, last + 1) if cp not in glyphs]
nxt = free[0] if free else last + 1
print(f"\nfree gaps: {', '.join(f'U+{c:X}' for c in free) or '(none)'}; "
      f"otherwise extend `last` to U+{last + 1:X}")

if "--free" in sys.argv:
    print(f"\nnext free: U+{nxt:X}")

if problems:
    print("\nPROBLEMS:")
    for p in problems:
        print("  -", p)
    sys.exit(1)
print("\nconsistent: every glyph is named, every macro points at a real glyph")
