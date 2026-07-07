#!/usr/bin/env python3
# Copyright 2026 thpoll83
# SPDX-License-Identifier: GPL-2.0-or-later
"""Generate doom_weapon_icons.h — 1-bit weapon icons for the slave-half
weapon pad, from the shareware DOOM1.WAD pickup sprites.

Slots with a pickup sprite in the shareware IWAD get an icon (1 chainsaw,
3 shotgun, 4 chaingun, 5 rocket launcher — plus 2 from the first-person
pistol sprite); the rest render as digits (6/7 plasma/BFG sprites are
registered-only, and those weapons are unobtainable in the shareware
episode anyway). Bitmaps are emitted in kdisp_draw_bitmap()'s layout:
row-major, MSB-first, byte-padded per row.

Usage: python3 weapon_icons.py <doom1.wad> <out.h>
"""
import sys

from PIL import Image

from oled_logo import decode_picture, read_lump

# slot -> (lump, max_w, max_h); keycap is 72x40, leave room for the corner digit
SLOT_SPRITES = {
    1: (b"CSAWA0", 62, 26),
    2: (b"PISGA0", 40, 30),
    3: (b"SHOTA0", 62, 22),
    4: (b"MGUNA0", 60, 24),
    5: (b"LAUNA0", 62, 24),
}


def to_gray(pic, w, h, pal):
    # Solid silhouette (opaque -> white): the pickup sprites are dark and
    # dither to unreadable dust at keycap size; the outlines are iconic.
    del pal
    img = Image.new("L", (w, h), 0)
    px = img.load()
    for y in range(h):
        for x in range(w):
            if pic[y][x] is not None:
                px[x, y] = 255
    return img


def main() -> None:
    wad_path, out_path = sys.argv[1], sys.argv[2]
    wad = open(wad_path, "rb").read()
    pal = read_lump(wad, b"PLAYPAL")[:768]

    chunks = []
    table = ["    {NULL, 0, 0},"] * 8  # index = slot, 0 unused
    for slot, (lump, max_w, max_h) in sorted(SLOT_SPRITES.items()):
        try:
            raw = read_lump(wad, lump)
        except KeyError:
            continue
        w, h, pic = decode_picture(raw)
        img = to_gray(pic, w, h, pal)
        bbox = img.getbbox()
        img = img.crop(bbox)
        img.thumbnail((max_w, max_h), Image.LANCZOS)
        img = img.convert("1")  # FS dither
        iw, ih = img.size
        row_bytes = (iw + 7) // 8
        data = bytearray(row_bytes * ih)
        ip = img.load()
        for y in range(ih):
            for x in range(iw):
                if ip[x, y]:
                    data[y * row_bytes + (x >> 3)] |= 0x80 >> (x & 7)
        name = f"WPN_ICON_{slot}"
        lines = []
        for i in range(0, len(data), 12):
            lines.append("    " + ", ".join("0x%02X" % b for b in data[i : i + 12]) + ",")
        chunks.append(
            f"// slot {slot}: {lump.decode()} {iw}x{ih}\n"
            f"static const uint8_t {name}[] = {{\n" + "\n".join(lines) + "\n};\n"
        )
        table[slot] = f"    {{{name}, {iw}, {ih}}},"

    body = "\n".join(chunks)
    rows = "\n".join(table)
    with open(out_path, "w") as f:
        f.write(f"""// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Weapon-pad icons from the freely-redistributable v1.9 shareware DOOM1.WAD
// pickup sprites (doom/tools/weapon_icons.py) — kdisp_draw_bitmap() layout
// (row-major, MSB-first, byte-padded per row). Indexed by number-key slot;
// a NULL bmp means "render the digit instead" (plasma/BFG sprites are not
// in the shareware IWAD; those weapons are unobtainable in it anyway).
#pragma once
#include <stddef.h>
#include <stdint.h>

{body}
typedef struct {{
    const uint8_t *bmp;
    uint8_t        w, h;
}} doom_wpn_icon_t;

static const doom_wpn_icon_t DOOM_WPN_ICONS[8] = {{
{rows}
}};
""")
    print("wrote", out_path)


if __name__ == "__main__":
    main()
