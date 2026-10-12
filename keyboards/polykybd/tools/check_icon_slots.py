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
# The OS hint icons continue the plane-16 range in a font-PACK font of their own
# (tools/hint_icons.py). It shares the macro namespace and the slot space, so the
# check reads both: one range, and the two fonts must not overlap.
HINTS = os.path.join(KB, "base", "fonts", "hint_icons.h")
NAMES = os.path.join(KB, "lang", "named_glyphs.h")

PUA_FIRST, PUA_LAST = 0x100000, 0x10FFFD   # Supplementary Private Use Area-B


def icons_font(path=ICONS, sym="Icons"):
    """-> (first, last, {cp: (w, h)}) reading the glyph table, gaps excluded."""
    src = open(path, encoding="utf-8").read()
    m = re.search(sym + r'Glyphs\[\]\s*PROGMEM\s*=\s*\{(.*?)\n\};', src, re.S)
    rng = re.search(r'\(GFXglyph \*\)' + sym + r'Glyphs,\s*(0x[0-9A-Fa-f]+),\s*(0x[0-9A-Fa-f]+)', src)
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
if os.path.exists(HINTS):
    h_first, h_last, h_glyphs = icons_font(HINTS, "PolyHintIcons")
    if h_first <= last:
        problems.append(f"hint_icons.h starts at U+{h_first:X}, inside IconsFont (ends U+{last:X})")
    hint_range = (h_first, h_last)
    glyphs.update(h_glyphs)
else:
    hint_range = None

if not (PUA_FIRST <= first <= last <= PUA_LAST):
    problems.append(f"IconsFont range U+{first:X}..U+{last:X} leaves plane-16 PUA "
                    "and would shadow a real character")

ranges = [("IconsFont", first, last)] + ([("PolyHintIcons", *hint_range)] if hint_range else [])
for nm_, lo, hi in ranges:
    print(f"{nm_} range U+{lo:X}..U+{hi:X}")
if hint_range and hint_range[0] <= hint_range[1] and not (PUA_FIRST <= hint_range[0] <= hint_range[1] <= PUA_LAST):
    problems.append("hint_icons.h range leaves plane-16 PUA")
print(f"\n{'cp':<9} {'glyph':<8} {'macro':<24} state")


def in_font(cp):
    return any(lo <= cp <= hi for _, lo, hi in ranges)


for cp in sorted(set(c for _, lo, hi in ranges for c in range(lo, hi + 1)) | set(names)):
    g = glyphs.get(cp)
    nm = names.get(cp, "")
    if not in_font(cp):
        state = "outside every font, but NAMED"
        problems.append(f"U+{cp:X} macro {nm} points outside IconsFont and the hint font (no glyph)")
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

# A new resident icon extends IconsFont's `last`; the hint block above it is the ceiling.
free = [cp for cp in range(first, last + 1) if cp not in glyphs]
ceiling = hint_range[0] if hint_range else PUA_LAST + 1
nxt = free[0] if free else last + 1
print(f"\nfree gaps: {', '.join(f'U+{c:X}' for c in free) or '(none)'}; "
      f"otherwise extend `last` to U+{last + 1:X} ({ceiling - last - 1} slots before U+{ceiling:X})")
if nxt >= ceiling:
    problems.append(f"IconsFont is full: the next slot U+{nxt:X} is the hint block")

if "--free" in sys.argv:
    print(f"\nnext free: U+{nxt:X}")

if problems:
    print("\nPROBLEMS:")
    for p in problems:
        print("  -", p)
    sys.exit(1)
print("\nconsistent: every glyph is named, every macro points at a real glyph")
