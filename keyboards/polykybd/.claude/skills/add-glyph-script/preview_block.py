#!/usr/bin/env python3
"""Render a PUA block of the generated gscript font at the real 72x40 OLED size,
exactly as the firmware draws it (per-glyph bitmap, baseline-aligned to yAdvance
40). Use to sign off a new glyph script's mapping + legibility before wiring.

Usage:  preview_block.py <base_hex> <count> [header_path] [out_png]
        preview_block.py 0xEA40 26
        preview_block.py 0xEA40 36 base/fonts/generated/gscript_fonts.h /tmp/gs.png

Requires the PolyKybdHost repo checked out as a sibling (for tools/gfx_font.py)
and Pillow. Prints how many glyphs rendered; writes a labelled contact sheet.
"""
import os
import sys

# locate PolyKybdHost/tools/gfx_font.py (sibling repo, a few common layouts)
HERE = os.path.dirname(os.path.abspath(__file__))
CANDIDATES = [
    os.path.join(HERE, "../../../../../../PolyKybdHost/tools"),  # skill -> ... -> sibling repo
    os.path.expanduser("~/PolyKybdHost/tools"),
    "/home/user/PolyKybdHost/tools",
]
for c in CANDIDATES:
    if os.path.exists(os.path.join(c, "gfx_font.py")):
        sys.path.insert(0, c)
        break
from gfx_font import _parse_header, OLED_W, OLED_H, BASELINE   # noqa: E402
from PIL import Image, ImageDraw                                # noqa: E402

base = int(sys.argv[1], 16)
count = int(sys.argv[2])
header = sys.argv[3] if len(sys.argv) > 3 else \
    os.path.join(HERE, "../../../base/fonts/generated/gscript_fonts.h")
out = sys.argv[4] if len(sys.argv) > 4 else "/tmp/gscript_preview.png"

bm, ga, rf = {}, {}, {}
_parse_header(open(header).read(), bm, ga, rf)
# find the font whose range covers `base`
font = None
for f in rf.values():
    if f["first"] <= base <= f["last"]:
        font = f
        break
if not font:
    sys.exit(f"no font in {header} covers 0x{base:04X}")
bmp, glyphs, first, yadv = bm[font["bmp"]], ga[font["gly"]], font["first"], font["yAdvance"]
base_yadv = 40   # IconsFont yAdvance (fonts[0]); gscript fonts use 40 -> neutral


def blit(cp):
    g = glyphs[cp - first]
    if g["width"] == 0 and g["height"] == 0:
        return None
    img = Image.new("L", (OLED_W, OLED_H), 0)
    px = img.load()
    x0 = (OLED_W - g["width"]) // 2
    y = BASELINE + (yadv - base_yadv)
    bo, bit, bits = g["bitmapOffset"], 0, 0
    for yy in range(g["height"]):
        for xx in range(g["width"]):
            if (bit & 7) == 0:
                bits = bmp[bo]; bo += 1
            if bits & 0x80:
                vx, vy = x0 + xx, y + g["yOffset"] + yy
                if 0 <= vx < OLED_W and 0 <= vy < OLED_H:
                    px[vx, vy] = 255
            bits = (bits << 1) & 0xFF; bit += 1
    return img


KEYS = [chr(c) for c in range(ord("A"), ord("Z") + 1)] + list("1234567890")
cols = 9
rows = (count + cols - 1) // cols
cw, ch = OLED_W + 6, OLED_H + 16
sheet = Image.new("RGB", (cols * cw + 10, rows * ch + 26), (18, 18, 22))
d = ImageDraw.Draw(sheet)
d.text((6, 6), f"0x{base:04X}  first=0x{first:04X} last=0x{font['last']:04X}  {count} glyphs",
       fill=(240, 240, 120))
miss = 0
for i in range(count):
    r, c = divmod(i, cols)
    ox, oy = 6 + c * cw, 22 + r * ch
    img = blit(base + i) if base + i <= font["last"] else None
    if img is None:
        miss += 1; img = Image.new("L", (OLED_W, OLED_H), 0)
    sheet.paste(Image.eval(img.convert("RGB"), lambda v: 255 if v else 30), (ox, oy))
    d.rectangle([ox, oy, ox + OLED_W - 1, oy + OLED_H - 1], outline=(70, 70, 80))
    d.text((ox + 2, oy + OLED_H + 1), KEYS[i] if i < len(KEYS) else str(i), fill=(200, 200, 200))
sheet.save(out)
print(f"{count - miss}/{count} glyphs rendered -> {out}")
