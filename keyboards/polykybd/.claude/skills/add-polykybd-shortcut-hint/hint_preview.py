#!/usr/bin/env python3
"""Preview + tune PolyKybd keycap shortcut-hint glyphs the way the firmware draws
them, straight from the generated GFX headers (no live TTF).

Run from the PolyKybdHost repo root so `tools/gfx_font.py` imports cleanly:

    cd PolyKybdHost
    python3 ../qmk_firmware/keyboards/polykybd/.claude/skills/add-polykybd-shortcut-hint/hint_preview.py \
        --check 1F5E3 2699 1F4DC               # do these codepoints resolve? lit-pixel count
    python3 .../hint_preview.py --sweep 1F4DC  # best leading-space count for one glyph
    python3 .../hint_preview.py --string '   ' 1F4DC   # render an exact firmware string (spaces+cp) -> PNG

The firmware draws a hint as a text string: N leading U+0020 spaces then the glyph
codepoint, starting at BUFFER_X, advancing by each glyph's xAdvance, baseline-aligned
per font. Hints sit RIGHT-of-center (~x46-50 in the 72px window), not mathematically
centered — match that. Wide emoji need FEWER leading spaces than narrow symbol glyphs.
"""
import argparse, sys, os

# Resolve the gfx_font loader from the PolyKybdHost tools dir.
HERE = os.path.dirname(os.path.abspath(__file__))
for cand in ("tools",  # cwd == PolyKybdHost
             os.path.join(HERE, "../../../../../PolyKybdHost/tools"),
             "/home/user/PolyKybdHost/tools"):
    if os.path.exists(os.path.join(cand, "gfx_font.py")):
        sys.path.insert(0, cand); break
from gfx_font import GfxGlyphRenderer, load_all_fonts, OLED_W, OLED_H, BUFFER_X  # noqa: E402

def _fontdir():
    for cand in ("../qmk_firmware/keyboards/polykybd/base/fonts",
                 "/home/user/qmk_firmware/keyboards/polykybd/base/fonts",
                 os.path.join(HERE, "../../base/fonts")):
        if os.path.isdir(cand):
            return cand
    sys.exit("cannot locate base/fonts — run from the PolyKybdHost repo root")

R = GfxGlyphRenderer(_fontdir())
SP = 0x20

def lit(cps):
    from PIL import Image
    img = Image.new("L", (OLED_W, OLED_H), 0); px = img.load(); x = BUFFER_X
    for cp in cps:
        x = R._blit(px, cp, x)
    pts = [(xx, yy) for yy in range(OLED_H) for xx in range(OLED_W) if px[xx, yy]]
    return img, pts

def owning_font(cp):
    for f in load_all_fonts(_fontdir()):
        if f.first <= cp <= f.last:
            g = f.glyphs[cp - f.first]
            _, pts = lit([cp])
            return f.name, len(pts)
    return None, 0

def cmd_check(cps):
    print("codepoint  owning font (lit px)               verdict")
    for cp in cps:
        name, n = owning_font(cp)
        if name is None:
            v = "NOT IN ANY FONT — add to symbol bundle / IconsFont"
        elif n == 0:
            v = "notdef/blank — pick another glyph or another source"
        else:
            v = "ok"
        print(f"  U+{cp:05X}  {str(name):42} {n:5d}  {v}")

def cmd_sweep(cp):
    print(f"leading-space sweep for U+{cp:05X} (pick the largest with max_x <= 69, no clip):")
    best = None
    for nsp in range(0, 7):
        _, pts = lit([SP] * nsp + [cp])
        if not pts:
            print(f"  {nsp} sp -> BLANK"); continue
        xs = [p[0] for p in pts]; mn, mx = min(xs), max(xs)
        clip = mx >= 71 or mn <= 0
        print(f"  {nsp} sp -> x[{mn:2d}..{mx:2d}] center={(mn+mx)//2}{'  CLIP' if clip else ''}")
        if mx <= 69:
            best = nsp
    print(f"  --> recommended: {best} leading spaces")

def cmd_string(spaces, cps, out):
    from PIL import Image
    img, pts = lit([SP] * len(spaces) + cps)
    scale = 8
    big = img.convert("RGB").resize((OLED_W * scale, OLED_H * scale), Image.NEAREST)
    big.save(out)
    xs = [p[0] for p in pts] or [0]
    print(f"wrote {out}  ({len(pts)} lit px, x[{min(xs)}..{max(xs)}])")

if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", nargs="+", metavar="HEXCP")
    ap.add_argument("--sweep", metavar="HEXCP")
    ap.add_argument("--string", metavar="SPACES")
    ap.add_argument("cps", nargs="*", metavar="HEXCP")
    ap.add_argument("--out", default="/tmp/hint_preview.png")
    a = ap.parse_args()
    if a.check:
        cmd_check([int(c, 16) for c in a.check])
    elif a.sweep:
        cmd_sweep(int(a.sweep, 16))
    elif a.string is not None:
        cmd_string(a.string, [int(c, 16) for c in a.cps], a.out)
    else:
        ap.print_help()
