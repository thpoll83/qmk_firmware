#!/usr/bin/env python3
"""Report which resident IconsFont codepoints are taken, free, or mis-named.

`base/fonts/gfx_icons.h` is hand-maintained and is the only authority on which
C1 slots hold a glyph. The named_glyphs sheet cannot answer that — its "Distance
Helper" column measures the sheet against ITSELF, so a codepoint that is taken in
the font but absent from the sheet reads as free space. At the time of writing
0x98/0x99/0x9C/0x9D were exactly that: real glyphs sitting inside a run the sheet
reported as a 10-wide gap.

Picking an occupied slot fails SILENTLY. IconsFont is g_all_fonts[0], so it wins
the lookup and the wrong icon simply renders — the same shape as the documented
0xA0+ trap, where a custom icon at 0xA4 shadowed the real ¤.

    python3 tools/check_icon_slots.py            # from keyboards/polykybd/
    python3 tools/check_icon_slots.py --free     # just the next free slot

Exit 1 if anything is inconsistent, so it can gate a commit.
"""
import re, sys, os

HERE = os.path.dirname(os.path.abspath(__file__))
KB   = os.path.dirname(HERE)
ICONS = os.path.join(KB, "base", "fonts", "gfx_icons.h")
NAMES = os.path.join(KB, "lang", "named_glyphs.h")

# Latin-1 codepoints that must never hold a custom icon: IconsFont shadows them.
PRINTABLE_LATIN1 = {0xA0: "nbsp", **{c: chr(c) for c in range(0xA1, 0x100)}}
# End of the non-printable C1 block: the only band a custom icon may live in.
C1_END = 0xA0


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
    """-> {cp: MACRO} for every named_glyphs macro pointing into 0x80..0xFF."""
    out = {}
    for line in open(NAMES, encoding="utf-8"):
        m = re.match(r'#define\s+(\w+)\s+U"\\x([0-9A-Fa-f]{2,4})"\s*(?://.*)?$', line.strip())
        if m:
            out[int(m.group(2), 16)] = m.group(1)
    return out


first, last, glyphs = icons_font()
names = named()
problems = []

print(f"IconsFont range 0x{first:02X}..0x{last:02X}  ({len(glyphs)} glyphs)\n")
print(f"{'cp':<6} {'glyph':<12} {'macro':<24} state")
for cp in range(0x80, max(last, C1_END) + 6):
    g = glyphs.get(cp)
    nm = names.get(cp, "")
    if cp > last:
        # Past IconsFont's `last` these fall through to NotoSans, so a macro at
        # 0xA0+ naming the real character is correct — the caution there is about
        # putting a custom GLYPH at the codepoint, which would shadow it. Below
        # 0xA0 there is no character to fall through to, so a macro with no glyph
        # behind it is simply broken and used to print as consistent.
        if cp < 0xA0 and nm:
            problems.append(f"0x{cp:02X} macro {nm} points past IconsFont.last (no glyph)")
            state = "past last, but NAMED"
        elif cp in PRINTABLE_LATIN1:
            state = f"Latin-1 {PRINTABLE_LATIN1[cp]!r} (never put a glyph here)"
        else:
            state = "free (past last)"
    elif g:
        state = "taken"
        if not nm:
            state = "taken, UNNAMED"
            problems.append(f"0x{cp:02X} has a glyph but no named_glyphs macro")
    else:
        state = "free (gap)"
        if nm:
            problems.append(f"0x{cp:02X} macro {nm} points at an emptied gap")
            state = "gap, but NAMED"
    print(f"0x{cp:02X}   {(f'{g[0]}x{g[1]}' if g else '-'):<12} {nm:<24} {state}")

free = [cp for cp in range(0x80, C1_END) if cp not in glyphs]
print(f"\nfree C1 slots: {', '.join(f'0x{c:02X}' for c in free) or '(none — the C1 range is full)'}")
print("⚠️  0xA0+ is printable Latin-1; a custom icon there shadows a real character.")

if "--free" in sys.argv:
    print(f"\nnext free: 0x{free[0]:02X}" if free else "\nnext free: NONE")

if problems:
    print("\nPROBLEMS:")
    for p in problems:
        print("  -", p)
    sys.exit(1)
print("\nconsistent: every glyph is named, every macro points at a real glyph")
