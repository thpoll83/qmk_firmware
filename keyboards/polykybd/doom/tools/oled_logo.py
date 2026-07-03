#!/usr/bin/env python3
# Copyright 2026 thpoll83
# SPDX-License-Identifier: GPL-2.0-or-later
"""Generate doom_logo_oled.h — the 128x64 1-bit DOOM title screen for the
status OLED, shown while game mode runs (oled_helper.c oled_task_user).

Decodes TITLEPIC (DOOM "picture format": 8-byte header, per-column post
lists) + PLAYPAL from the freely-redistributable v1.9 shareware DOOM1.WAD
(the same IWAD doom1.whx is converted from), downscales the full 320x200
page to 128x64 (the status OLED is 2:1 wider than the 1.6:1 frame — the
mild horizontal squeeze reads fine for the logo), Floyd-Steinberg dithers
to 1bpp and emits the bitmap in the QMK oled driver's raw page-major
layout: byte = (row/8)*128 + col, bit k = row (row/8)*8 + k, bit 0 at the
top of the page.

Usage: python3 oled_logo.py <doom1.wad> <out.h>
"""
import struct
import sys

from PIL import Image


def read_lump(wad: bytes, name: bytes) -> bytes:
    ident, numlumps, diroff = struct.unpack("<4sII", wad[:12])
    assert ident in (b"IWAD", b"PWAD"), "not a WAD"
    for i in range(numlumps):
        off, size, lname = struct.unpack("<II8s", wad[diroff + 16 * i : diroff + 16 * i + 16])
        if lname.rstrip(b"\0") == name:
            return wad[off : off + size]
    raise KeyError(name.decode())


def decode_picture(lump: bytes):
    w, h, _lo, _to = struct.unpack("<hhhh", lump[:8])
    colofs = struct.unpack("<%dI" % w, lump[8 : 8 + 4 * w])
    img = [[None] * w for _ in range(h)]
    for x in range(w):
        p = colofs[x]
        while lump[p] != 0xFF:
            topdelta = lump[p]
            length = lump[p + 1]
            p += 3  # topdelta, length, pad
            for i in range(length):
                y = topdelta + i
                if 0 <= y < h:
                    img[y][x] = lump[p]
                p += 1
            p += 1  # trailing pad
    return w, h, img


def main() -> None:
    wad_path, out_path = sys.argv[1], sys.argv[2]
    wad = open(wad_path, "rb").read()
    pal = read_lump(wad, b"PLAYPAL")[:768]
    # M_DOOM (the menu-title logo, 123x60): almost exactly the panel size,
    # transparent background — a far better 1-bit source than TITLEPIC,
    # whose dark-on-fire logo dithers to speckle at this resolution.
    w, h, pic = decode_picture(read_lump(wad, b"M_DOOM"))

    gray = Image.new("L", (w, h), 0)
    px = gray.load()
    for y in range(h):
        for x in range(w):
            idx = pic[y][x]
            if idx is None:
                continue  # transparent -> black
            r, g, b = pal[3 * idx], pal[3 * idx + 1], pal[3 * idx + 2]
            # Saturation-floored brightness (like the keycap dither table),
            # with extra gain: the logo's red gradient tops out well below
            # white and would dither too sparse otherwise.
            v = max(0.2126 * r + 0.7152 * g + 0.0722 * b, 0.6 * max(r, g, b))
            px[x, y] = min(255, int(round(v * 1.8)))

    logo = Image.new("L", (128, 64), 0)
    logo.paste(gray, ((128 - w) // 2, (64 - h) // 2))
    logo = logo.convert("1")  # FS dither

    pages = bytearray(128 * 8)
    op = logo.load()
    for y in range(64):
        for x in range(128):
            if op[x, y]:
                pages[(y // 8) * 128 + x] |= 1 << (y % 8)

    lines = []
    for i in range(0, len(pages), 16):
        lines.append("    " + ", ".join("0x%02X" % b for b in pages[i : i + 16]) + ",")
    body = "\n".join(lines)
    with open(out_path, "w") as f:
        f.write(f"""// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The DOOM title screen for the 128x64 status OLED, shown while game mode
// runs. Derived from TITLEPIC of the freely-redistributable v1.9 shareware
// DOOM1.WAD (the same IWAD doom1.whx is converted from) by
// doom/tools/oled_logo.py — full 320x200 page downscaled to 128x64 and
// Floyd-Steinberg dithered to 1bpp, in the QMK oled driver's raw page-major
// layout (oled_write_raw).
#pragma once
#include <stdint.h>

static const char DOOM_LOGO_OLED[128 * 8] = {{
{body}
}};
""")
    print("wrote", out_path)


if __name__ == "__main__":
    main()
