#!/usr/bin/env python3
# Copyright 2026 thpoll83
# SPDX-License-Identifier: GPL-2.0-or-later
"""Pre-render the tutorial's native language names that the keycap renderer cannot draw.

The keycaps draw one glyph at a time with no text shaper, so a name whose letters JOIN
(Arabic), whose vowel signs attach (Devanagari), or whose glyphs are not in the flashed
fonts (日本語, 한국어) cannot be spelled key by key. This script shapes each such name
with HarfBuzz from the Noto font listed in fonts/noto-fonts.yaml, renders it 1-bit with
FreeType, cuts it into keycap-sized tiles and writes anim/tutorial_names_gen.h.

    pip install uharfbuzz freetype-py pillow
    python3 tools/gen_tutorial_names.py --fonts <dir with the .ttf files> [--png sheet.png]

Tiles: one per key, 72x40, row-major, 9 bytes per row, MSB = leftmost pixel. All tiles of
one name share a baseline, so the word stays level across keys. Tiles are listed in
VISUAL left-to-right order (HarfBuzz lays an RTL run out visually), which is the order the
firmware places them on the half's keys.
"""
import argparse
import os
import sys

import freetype
import uharfbuzz as hb

W, H = 72, 40
MAX_INK_H = 34          # leave a few px of the 40 for air
MAX_TILE_W = 66

# (C symbol, text, font file, mode, start px size)
#   tiles   : one HarfBuzz cluster per key (separate characters or syllables)
#   strip   : the joined word, cut at glyph boundaries into key-width pieces. A '|' in
#             the text forces the cut there instead (ภาษา|ไทย: "language" | "Thai"), so a
#             word never breaks mid-syllable; the '|' itself is not drawn.
NAMES = [
    ("AR", "العربية", "NotoSansArabic.ttf",     "strip", 34),
    ("HI", "हिन्दी",   "NotoSansDevanagari.ttf", "tiles", 34),
    ("JA", "日本語",   "NotoSansJP.ttf",         "tiles", 34),
    ("KO", "한국어",   "NotoSansKR.ttf",         "tiles", 34),
]
WEIGHT = 500   # the fonts are variable; 400 is thin at 1 bit per pixel


def shape(path, text, px):
    blob = hb.Blob.from_file_path(path)
    face = hb.Face(blob)
    font = hb.Font(face)
    font.scale = (px * 64, px * 64)
    try:
        font.set_variations({"wght": WEIGHT})
    except Exception:
        pass
    buf = hb.Buffer()
    buf.add_str(text)
    buf.guess_segment_properties()
    buf.cluster_level = hb.BufferClusterLevel.MONOTONE_GRAPHEMES
    hb.shape(font, buf)
    return buf.glyph_infos, buf.glyph_positions


def render(path, text, px):
    """Render the shaped word onto a big canvas; return pixels + per-glyph (cluster, x0, x1)."""
    infos, poss = shape(path, text, px)
    ft = freetype.Face(path)
    try:
        axes = ft.get_variation_info().axes
        coords = [WEIGHT if a.tag == "wght" else a.default for a in axes]
        ft.set_var_design_coords(coords)
    except Exception:
        pass
    ft.set_pixel_sizes(0, px)
    CW, CH, BASE = 1200, 200, 120
    canvas = [[0] * CW for _ in range(CH)]
    pen = 20 * 64
    glyphs = []
    for info, pos in zip(infos, poss):
        ft.load_glyph(info.codepoint, freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_MONO)
        g = ft.glyph
        bm = g.bitmap
        ox = (pen + pos.x_offset) // 64 + g.bitmap_left
        oy = BASE - (pos.y_offset // 64) - g.bitmap_top
        xs = []
        for r in range(bm.rows):
            for c in range(bm.width):
                byte = bm.buffer[r * bm.pitch + (c >> 3)]
                if byte & (0x80 >> (c & 7)):
                    canvas[oy + r][ox + c] = 1
                    xs.append(ox + c)
        if xs:
            glyphs.append((info.cluster, min(xs), max(xs)))
        pen += pos.x_advance
    return canvas, glyphs


def ink_box(canvas):
    ys = [y for y, row in enumerate(canvas) if any(row)]
    xs = [x for x in range(len(canvas[0])) if any(canvas[y][x] for y in ys)]
    return min(xs), max(xs), min(ys), max(ys)


def groups(glyphs, mode, breaks=()):
    """Column ranges per tile, left to right."""
    if breaks:   # explicit cuts: one tile per segment, by the text offset of each glyph
        seg = {}
        for cl, x0, x1 in glyphs:
            k = sum(1 for b in breaks if cl >= b)
            a, b = seg.get(k, (x0, x1))
            seg[k] = (min(a, x0), max(b, x1))
        return sorted(seg.values())
    by_cluster = {}
    for cl, x0, x1 in glyphs:
        a, b = by_cluster.get(cl, (x0, x1))
        by_cluster[cl] = (min(a, x0), max(b, x1))
    units = sorted(by_cluster.values())
    # Overlapping units (a mark hanging over its neighbour) must share a tile.
    merged = []
    for x0, x1 in units:
        if merged and x0 <= merged[-1][1]:
            merged[-1] = (merged[-1][0], max(merged[-1][1], x1))
        else:
            merged.append((x0, x1))
    if mode == "tiles":
        return merged
    out = []
    for x0, x1 in merged:           # strip: pack units greedily into key-width pieces
        if out and x1 - out[-1][0] + 1 <= MAX_TILE_W:
            out[-1] = (out[-1][0], x1)
        else:
            out.append((x0, x1))
    return out


def build(path, text, mode, px):
    breaks, clean = [], ""
    for ch in text:              # '|' marks a forced cut. Offsets are CHARACTER indices:
        if ch == "|":            # uharfbuzz's add_str numbers clusters per codepoint.
            breaks.append(len(clean))
        else:
            clean += ch
    text = clean
    while px > 10:
        canvas, glyphs = render(path, text, px)
        x0, x1, y0, y1 = ink_box(canvas)
        tiles = groups(glyphs, mode, breaks)
        if y1 - y0 + 1 <= MAX_INK_H and all(b - a + 1 <= MAX_TILE_W for a, b in tiles):
            break
        px -= 1
    top = y0 - (H - (y1 - y0 + 1)) // 2       # one baseline for every tile
    out = []
    for a, b in tiles:
        left = a - (W - (b - a + 1)) // 2
        tile = [[canvas[top + y][left + x] if 0 <= left + x < len(canvas[0]) and a <= left + x <= b
                 else 0 for x in range(W)] for y in range(H)]
        out.append(tile)
    return out, px


def pack(tile):
    data = []
    for row in tile:
        for bx in range(0, W, 8):
            v = 0
            for i in range(8):
                if row[bx + i]:
                    v |= 0x80 >> i
            data.append(v)
    return data


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fonts", required=True)
    ap.add_argument("--out", default=os.path.join(os.path.dirname(__file__), "..", "anim",
                                                  "tutorial_names_gen.h"))
    ap.add_argument("--png")
    args = ap.parse_args()

    lines = [
        "// Copyright 2026 thpoll83",
        "// SPDX-License-Identifier: GPL-2.0-or-later",
        "// GENERATED by tools/gen_tutorial_names.py — do not edit by hand.",
        "// The tutorial's native language names that need a shaper or glyphs the keycap",
        "// fonts lack, pre-rendered from Noto (OFL) and cut into 72x40 keycap tiles.",
        "// Tiles are row-major, 9 bytes per row, MSB = leftmost pixel, in visual order.",
        "#pragma once",
        "#include <stdint.h>",
        "",
        "typedef struct {",
        "    uint8_t        n;       // tiles (one per key)",
        "    const uint8_t *tiles;   // n * 360 bytes",
        "} tut_name_strip_t;",
        "",
    ]
    rendered = {}
    total = 0
    for sym, text, font, mode, px in NAMES:
        tiles, used = build(os.path.join(args.fonts, font), text, mode, px)
        rendered[sym] = tiles
        total += len(tiles) * 360
        print(f"{sym}: {text}  {mode}  {len(tiles)} tile(s) at {used} px", file=sys.stderr)
        lines.append(f"// {text}  ({font}, {mode}, {used} px)")
        lines.append(f"static const uint8_t TUT_NAME_TILES_{sym}[{len(tiles)} * 360] = {{")
        for t in tiles:
            d = pack(t)
            for i in range(0, len(d), 18):
                lines.append("    " + ", ".join(f"0x{v:02X}" for v in d[i:i + 18]) + ",")
        lines.append("};")
        lines.append(f"static const tut_name_strip_t TUT_NAME_STRIP_{sym} = "
                     f"{{{len(tiles)}, TUT_NAME_TILES_{sym}}};")
        lines.append("")
    lines.append(f"// {total} bytes of tiles in total.")
    with open(args.out, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print(f"wrote {args.out} ({total} bytes of tiles)", file=sys.stderr)

    if args.png:
        from PIL import Image
        rows = len(rendered)
        img = Image.new("L", (7 * (W + 6) + 6, rows * (H + 8) + 8), 40)
        for r, (sym, tiles) in enumerate(rendered.items()):
            start = (7 - len(tiles)) // 2
            for k, t in enumerate(tiles):
                tile = Image.new("L", (W, H), 0)
                for y in range(H):
                    for x in range(W):
                        if t[y][x]:
                            tile.putpixel((x, y), 255)
                img.paste(tile, (6 + (start + k) * (W + 6), 8 + r * (H + 8)))
        img.save(args.png)


if __name__ == "__main__":
    main()
