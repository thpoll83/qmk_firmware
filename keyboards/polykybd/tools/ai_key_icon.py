#!/usr/bin/env python3
"""The AI key legend (ICON_AI_KEY, IconsFont U+100029): five sigmoid curves of
different slope, a few tiny stars and dots, and a small drawn "AI" in the lower
right, the A leaning on the I with its right stroke parallel to it.

One source for both outputs, so the bitmap and the preview cannot disagree:

    python3 tools/ai_key_icon.py --c          # the IconsBitmaps block + glyph row
    python3 tools/ai_key_icon.py --png out.png

The glyph is the whole 72x40 keycap (xOffset 0, yOffset -23: the legend cursor is
(BUFFER_X, 23) and IconsFont's yAdvance 40 is the baseline every font aligns to).
Stars that would touch the "AI" are left out (art() raises on that; see SPARKS).
"""
import argparse
import math
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[2] / '.claude' / 'skills' / 'keycap-layout-preview'))
import keycap_preview as K  # noqa: E402

W, H = 72, 40
CODEPOINT = 0x100029
# (k, b): slope and shift of each curve y = 36 - 30 / (1 + exp(-k (8u - 4 - b)))
CURVES = ((0.6, 0), (1.2, -0.8), (2.5, 0.6), (5, 1.4), (0.35, -1.6))
# ('s', x, y, arm) a star; ('d', x, y, size) a dot of 1 or 2 px. Keycap coords.
SPARKS = (('s', 6, 5, 2), ('s', 18, 13, 3), ('d', 29, 4, 1), ('d', 3, 15, 1),
          ('s', 52, 31, 2), ('d', 60, 24, 1), ('d', 45, 36, 2))


def _curve(fn, n=2500):
    pts = set()
    for i in range(n):
        x, y = fn(2 * math.pi * i / n)
        pts.add((round(x), round(y)))
    return {(x, y) for x, y in pts if 0 <= x < W and 0 <= y < H}


def _dilate(ink, r):
    return {(x + dx, y + dy) for x, y in ink for dx in range(-r, r + 1)
            for dy in range(-r, r + 1) if dx * dx + dy * dy <= r * r}


def _star(cx, cy, r):
    ink = {(cx, cy)}
    for d in range(1, r + 1):
        ink |= {(cx + d, cy), (cx - d, cy), (cx, cy + d), (cx, cy - d)}
    if r >= 3:
        ink |= {(cx + 1, cy + 1), (cx - 1, cy - 1), (cx + 1, cy - 1), (cx - 1, cy + 1)}
    return ink


TAG_SCALE = 0.6   # for a font-drawn word only: 0.5 = HINT_SMALL, larger = the face scaled
# The tag is a drawn "AI": the A leans on the I, its right stroke parallel to the I.
# (I bar x, top row, stroke px, gap A->I, A width) on a 26x18 grid, 8x supersampled.
LEAN = (20, 7, 2.0, 1, 13)


def _lean_ai(spec=LEAN):
    from PIL import Image, ImageDraw
    ss, w, h = 8, 26, 18
    ix, top, t, gap, a_w = spec
    im = Image.new('L', (w * ss, h * ss), 0)
    d = ImageDraw.Draw(im)
    b = h - 1
    rect = lambda x0, y0, x1, y1: d.rectangle([x0 * ss, y0 * ss, x1 * ss - 1, y1 * ss - 1], fill=255)
    line = lambda x0, y0, x1, y1, th: d.line([(x0 * ss, y0 * ss), (x1 * ss, y1 * ss)],
                                             fill=255, width=int(th * ss))
    rect(ix, top, ix + t, b + 1)                          # I
    apx, apy = ix - gap, top + 0.5
    lfx = apx - a_w
    line(lfx, b + 0.5, apx, apy, t)                       # A, left stroke
    rect(apx - t, apy - 0.5, apx, b + 1)                  # A, right stroke, parallel to the I
    yb = top + (b - top) * 0.62
    line(lfx + (apx - lfx) * (b + 0.5 - yb) / (b + 0.5 - apy), yb, apx - t, yb, max(1, t * 0.7))
    ink = set()
    for y in range(h):
        for x in range(w):
            if sum(im.crop((x * ss, y * ss, x * ss + ss, y * ss + ss)).get_flattened_data()) >= 255 * ss * ss * 0.4:
                ink.add((x, y))
    return ink


def _tag(word=None, scale=None):
    scale = TAG_SCALE if scale is None else scale
    ink, x = set(), 0
    if word is None:
        ink = _lean_ai()
    elif scale == 0.5:
        for c in word:
            f, g = K.find(ord(c))
            ink |= K.small_ink(ord(c), x, 0)
            x += (g['xAdvance'] + 1) // 2
    else:
        full = set()
        for c in word:
            f, g = K.find(ord(c))
            full |= K.full_ink(ord(c), x, 0)
            x += g['xAdvance']
        x0 = min(p[0] for p in full); y0 = min(p[1] for p in full)
        w = max(p[0] for p in full) - x0 + 1; h = max(p[1] for p in full) - y0 + 1
        # Area sampling: a target pixel is lit when most of its source area is ink.
        for ty in range(math.ceil(h * scale)):
            for tx in range(math.ceil(w * scale)):
                sx0, sx1 = int(tx / scale), max(int((tx + 1) / scale), int(tx / scale) + 1)
                sy0, sy1 = int(ty / scale), max(int((ty + 1) / scale), int(ty / scale) + 1)
                cells = [(x0 + sx, y0 + sy) for sx in range(sx0, sx1) for sy in range(sy0, sy1)]
                if sum(c in full for c in cells) * 2 >= len(cells):
                    ink.add((tx, ty))
    dx = W - 2 - max(p[0] for p in ink)
    dy = H - 2 - max(p[1] for p in ink)
    return {(x + dx, y + dy) for x, y in ink}


def art():
    curves = set()
    for k, b in CURVES:
        curves |= _curve(lambda t, k=k, b=b: (
            t / (2 * math.pi) * (W - 1),
            36 - 30 / (1 + math.exp(-k * ((t / (2 * math.pi)) * 8 - 4 - b)))))
    tag = _tag()
    free = {(x, y) for x in range(W) for y in range(H)} - _dilate(curves, 2) - _dilate(tag, 3)
    ink = curves - _dilate(tag, 2)
    for kind, x, y, r in SPARKS:
        s = _star(x, y, r) if kind == 's' else \
            ({(x, y)} if r == 1 else {(x, y), (x + 1, y), (x, y + 1), (x + 1, y + 1)})
        if not s <= free:
            raise ValueError(f'{kind} at ({x},{y}) touches a curve or the tag')
        ink |= s
    ink |= tag
    if any(not (0 <= x < W and 0 <= y < H) for x, y in ink):
        raise ValueError('ink outside the 72x40 keycap')
    return ink


def column_bytes(ink):
    """IconsFont's column-native layout: per column, ceil(H/8) bytes, bit y&7."""
    cb = (H + 7) // 8
    out = []
    for x in range(W):
        for page in range(cb):
            out.append(sum(1 << b for b in range(8) if (x, page * 8 + b) in ink))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--c', action='store_true')
    ap.add_argument('--png')
    a = ap.parse_args()
    ink = art()
    if a.c:
        data = column_bytes(ink)
        print('  /* 0x%X ICON_AI_KEY 72x40: the AI key legend, generated by '
              'tools/ai_key_icon.py (five sigmoids, stars, "ai"). */' % CODEPOINT)
        for i in range(0, len(data), 12):
            print('  ' + ', '.join('0x%02X' % b for b in data[i:i + 12]) + ',')
        print('// glyph: { <offset>, %d, %d, %d, 0, -23 },   // 0x%X ICON_AI_KEY' % (W, H, W, CODEPOINT))
    if a.png:
        K.sheet([({(x + K.BUFFER_X, y) for x, y in ink}, 'ICON_AI_KEY')], a.png)


if __name__ == '__main__':
    main()
