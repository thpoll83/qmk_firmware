#!/usr/bin/env python3
# Copyright 2026 thpoll83
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Offline preview of the slave-side readable menu mirror tiles — a Python
# port of qmk_shim.c's draw_vpatch8 + shim_menu_stamp/shim_menu_emit_row
# against the real doom1.whx, so the letter on/off rule can be tuned by
# LOOKING at the result instead of flashing hardware per iteration (the
# rounds 19-26 letter saga). Renders each menu item as the 4-tile keycap
# strip (view cols 1-4) for one or more rule variants plus the raw source
# luma, into a labelled contact-sheet PNG.
#
#   python3 menu_preview.py --whx ~/rp2040-doom/doom1.whx --out /tmp/menu.png
#
# The vpatch decode mirrors qmk_shim.c draw_vpatch8 exactly (4/6/8-bit runs,
# alpha, solid, border; per-patch palettes; the shared-palette table). The
# tile stamp mirrors shim_menu_stamp (x0=52, the 3:2 -> 5:4 -> 1:1 -> 3:4
# scale chain, 72x40 windows at win0 = vc*72 - 20).

import argparse
import re
import struct
import sys
from pathlib import Path

from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
DOOM = HERE.parent
ENGINE = DOOM / "engine" / "src"

# --- tables parsed from the sources (single source of truth) ---------------

def load_luma():
    text = (DOOM / "doom_playpal_luma.h").read_text()
    body = text[text.index("{") + 1:text.index("}")]
    vals = [int(v) for v in re.findall(r"\d+", body)]
    assert len(vals) == 256, len(vals)
    return vals

def load_vpatch_names():
    text = (ENGINE / "whddata.h").read_text()
    body = text[text.index("#define VPATCH_LIST"):]
    body = body[:body.index("VPATCH_INVALID_HANDLE")] if "VPATCH_INVALID_HANDLE" in body else body
    names = ["INVALID"]
    for m in re.finditer(r"VPATCH_NAME\((\w+)\)", body):
        names.append(m.group(1))
    return names

# vpatch_for_shared_palette (v_video.c): shared pal i = palette of this patch
SHARED_PAL_PATCHES = ["STBAR", "STCFN033", "WIBP1"]

# --- WHX access -------------------------------------------------------------

class Whx:
    def __init__(self, path):
        self.data = Path(path).read_bytes()
        ident, numlumps, infotableofs = struct.unpack_from("<4sii", self.data, 0)
        assert ident == b"IWHX", ident
        self.numlumps = numlumps
        self.offsets = struct.unpack_from(f"<{numlumps + 1}I", self.data, infotableofs)
        # whdheader_t (24 B) follows wadinfo (12 B); the named-lump table
        # follows the (numlumps+1) u32 offsets that sit right after it.
        (self.num_named,) = struct.unpack_from("<H", self.data, 12 + 22)
        names_off = 12 + 24 + (numlumps + 1) * 4
        self.names = {}
        for i in range(self.num_named):
            raw = self.data[names_off + i * 12:names_off + i * 12 + 12]
            name = raw[:10].split(b"\0")[0].decode("ascii", "replace")
            (num,) = struct.unpack_from("<H", raw, 10)
            self.names[name] = num

    def lump(self, num):
        # w_wad.h lump_data(): the offset is the LOW 24 bits (high byte =
        # flags). There is no explicit size — decoders read what they need;
        # slice a generous window.
        off = self.offsets[num] & 0xFFFFFF
        return self.data[off:off + 65536]

class VpatchTable:
    def __init__(self, whx):
        self.whx = whx
        self.enum = load_vpatch_names()
        raw = whx.lump(whx.names["p_start"])  # WHX lump names are lowercase
        self.numbers = struct.unpack_from(f"<{len(raw) // 2}H", raw, 0)
        # shared palettes as PLAYPAL indices (shim_fill_shared_pal8)
        self.shared = []
        for nm in SHARED_PAL_PATCHES:
            p = self.resolve(nm)
            self.shared.append(list(p.palette[:16]) + [0] * max(0, 16 - len(p.palette)))

    def resolve(self, name):
        return Vpatch(self.whx.lump(self.numbers[self.enum.index(name)]), self)

class Vpatch:
    def __init__(self, b, table):
        self.b = b
        self.table = table
        self.w = b[0] | ((b[3] & 0x2) << 7)
        self.h = b[1]
        self.colorcount = b[2]
        self.type = b[3] >> 2
        self.has_shared = b[3] & 1
        self.palette = b[6:6 + self.colorcount]
        self.shared_idx = b[6 + self.colorcount] if self.has_shared else None
        self.data_off = 6 + self.colorcount + (1 if self.has_shared else 0)

    # one row per call, mirrors draw_vpatch8 (returns new off)
    def decode_row(self, row, off):
        w = self.w
        d = self.b
        pos = self.data_off + off
        if self.has_shared:
            pal = self.table.shared[self.shared_idx]
        else:
            pal = self.palette
        t = self.type % 6  # clipped variants decode identically
        if t == 1 or t == 3 or t == 4:  # vp4_runs / vp6_runs / vp8_runs
            p = 0
            while True:
                gap = d[pos]; pos += 1
                if gap == 0xFF:
                    break
                p += gap
                ln = d[pos]; pos += 1
                if t == 1:  # vp4_runs: pairs of nibbles, count field is odd-coded
                    i = 1
                    while i < ln:
                        v = d[pos]; pos += 1
                        row[p] = pal[v & 0xF]; p += 1
                        row[p] = pal[v >> 4]; p += 1
                        i += 2
                    if ln & 1:
                        row[p] = pal[d[pos] & 0xF]; pos += 1; p += 1
                elif t == 3:  # vp6_runs: 4 pixels per 3 bytes
                    i = 3
                    while i < ln:
                        v = d[pos] | (d[pos + 1] << 8) | (d[pos + 2] << 16)
                        pos += 3
                        for k in range(4):
                            row[p] = pal[(v >> (6 * k)) & 0x3F]; p += 1
                        i += 4
                    rem = ln & 3
                    if rem:
                        v = d[pos]; pos += 1
                        row[p] = pal[v & 0x3F]; p += 1
                        if rem > 1:
                            v = (v >> 6) | (d[pos] << 2); pos += 1
                            row[p] = pal[v & 0x3F]; p += 1
                            if rem > 2:
                                v = (v >> 6) | (d[pos] << 4); pos += 1
                                row[p] = pal[v & 0x3F]; p += 1
                else:  # vp8_runs
                    for _ in range(ln):
                        row[p] = pal[d[pos]]; pos += 1; p += 1
                if p == w:
                    break
        elif t == 2:  # vp4_alpha
            p = 0
            for _ in range(w // 2):
                v = d[pos]; pos += 1
                if v & 0xF:
                    row[p] = pal[v & 0xF]
                if v >> 4:
                    row[p + 1] = pal[v >> 4]
                p += 2
            if w & 1:
                v = d[pos]; pos += 1
                if v & 0xF:
                    row[p] = pal[v & 0xF]
        elif t == 0:  # vp4_solid
            p = 0
            for _ in range(w // 2):
                v = d[pos]; pos += 1
                row[p] = pal[v & 0xF]
                row[p + 1] = pal[v >> 4]
                p += 2
            if w & 1:
                row[w - 1] = pal[d[pos] & 0xF]; pos += 1
        elif t == 5:  # vp_border
            row[0] = d[pos]; pos += 1
            col = d[pos]; pos += 1
            for i in range(1, w - 1):
                row[i] = col
            row[w - 1] = d[pos]; pos += 1
        return pos - self.data_off

# --- tile stamp (shim_menu_stamp / shim_menu_emit_row port) -----------------

SCALES = [(3, 2), (5, 4), (1, 1), (3, 4)]

def pick_scale(w, h):
    for num, den in SCALES:
        if 52 + w * num // den <= 320 and h * num // den <= 40:
            return num, den
    return None

def stamp_item(patch, luma, rule):
    """Render one menu item into the 4 view-col tiles (view cols 1-4 as one
    288x40 canvas strip; col 0 is the skull)."""
    strip = [[0] * 288 for _ in range(40)]  # canvas x 52.. lands here
    w, h = patch.w, patch.h
    sc = pick_scale(w, h)
    if not sc:
        return strip, None
    num, den = sc
    x0, y0 = 52, (40 - h * num // den) // 2
    rows = []
    off = 0
    for sy in range(h):
        row = [0] * w
        off = patch.decode_row(row, off)
        rows.append([luma[v] for v in row])
    for k in range(h):
        up = rows[k - 1] if k > 0 else None
        cur = rows[k]
        dn = rows[k + 1] if k + 1 < h else None
        for oy in range(k * num // den, (k + 1) * num // den):
            y = y0 + oy
            if not (0 <= y < 40):
                continue
            for sx in range(w):
                if not rule(cur[sx], sx, up, cur, dn, w):
                    continue
                for ox in range(sx * num // den, (sx + 1) * num // den):
                    cx = x0 + ox
                    tx = cx - 52  # strip x (win0 of col 1 = 52)
                    if 0 <= tx < 288:
                        strip[y][tx] = 255
    return strip, rows

def gray_strip(rows, w, h):
    """The raw source luma at the same scale — the 'actual outline'."""
    sc = pick_scale(w, h)
    if not sc:
        return [[0] * 288 for _ in range(40)]
    num, den = sc
    x0, y0 = 52, (40 - h * num // den) // 2
    strip = [[0] * 288 for _ in range(40)]
    for k in range(h):
        for oy in range(k * num // den, (k + 1) * num // den):
            y = y0 + oy
            if not (0 <= y < 40):
                continue
            for sx in range(w):
                for ox in range(sx * num // den, (sx + 1) * num // den):
                    tx = x0 + ox - 52
                    if 0 <= tx < 288:
                        strip[y][tx] = rows[k][sx]
    return strip

# --- rule variants -----------------------------------------------------------

def neigh(sx, up, cur, dn, w, thr):
    n = 0
    if sx > 0 and cur[sx - 1] >= thr:
        n += 1
    if sx + 1 < w and cur[sx + 1] >= thr:
        n += 1
    if up and up[sx] >= thr:
        n += 1
    if dn and dn[sx] >= thr:
        n += 1
    return n

def rule_v22(v, sx, up, cur, dn, w):
    return v >= 76 or (v >= 40 and neigh(sx, up, cur, dn, w, 76) != 1)

def rule_v23(v, sx, up, cur, dn, w):
    return v >= 56 or (v >= 40 and neigh(sx, up, cur, dn, w, 76) != 1)

def rule_v27(v, sx, up, cur, dn, w):
    if v >= 56:
        return True
    return v >= 40 and neigh(sx, up, cur, dn, w, 40) >= 2 and neigh(sx, up, cur, dn, w, 76) != 1

def axis_pairs(sx, up, cur, dn, w):
    l = cur[sx - 1] if sx > 0 else 0
    r = cur[sx + 1] if sx + 1 < w else 0
    u = up[sx] if up else 0
    d = dn[sx] if dn else 0
    return (l, r), (u, d)

def rule_axis(v, sx, up, cur, dn, w):
    if v >= 76:
        return True
    if v < 40:
        return False
    for a, b in axis_pairs(sx, up, cur, dn, w):
        if (a >= 76 and b < 40) or (b >= 76 and a < 40):
            return False  # one-sided fringe of a bright stroke
    return neigh(sx, up, cur, dn, w, 40) >= 2

def make_thresh(t):
    return lambda v, sx, up, cur, dn, w: v >= t

RULES = [
    ("source luma", None),
    ("v22  >=76 | 40+ n76!=1", rule_v22),
    ("v23  >=56 | 40..55 n76!=1", rule_v23),
    ("v27  >=56 | 40..55 n40>=2,n76!=1", rule_v27),
    ("axis >=76 | 40..75 !one-sided", rule_axis),
    ("plain >=76", make_thresh(76)),
    ("plain >=64", make_thresh(64)),
    ("plain >=56", make_thresh(56)),
]

ITEMS = ["M_NGAME", "M_OPTION", "M_LOADG", "M_SAVEG", "M_RDTHIS", "M_QUITG",
         "M_JKILL", "M_ROUGH", "M_HURT", "M_ULTRA", "M_NMARE",
         "M_EPI1", "M_EPI2", "M_EPI3"]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--whx", default=str(Path.home() / "rp2040-doom" / "doom1.whx"))
    ap.add_argument("--out", default="/tmp/menu_preview.png")
    ap.add_argument("--zoom", type=int, default=2)
    ap.add_argument("--items", nargs="*", default=ITEMS)
    args = ap.parse_args()

    luma = load_luma()
    table = VpatchTable(Whx(args.whx))

    z = args.zoom
    cell_w, cell_h = 288, 40
    label_w = 210
    pad = 6
    img_w = label_w + (cell_w * z + pad) * len(args.items) if False else 0
    # layout: one row per RULE, one column per item
    img_w = label_w + (cell_w + pad) * len(args.items)
    img_h = (cell_h + pad) * len(RULES) + pad
    img = Image.new("RGB", (img_w * z // z, img_h), (24, 24, 24))
    # draw at 1x then scale up at the end for crispness
    d = ImageDraw.Draw(img)

    for ri, (label, rule) in enumerate(RULES):
        y = pad + ri * (cell_h + pad)
        d.text((4, y + 14), label, fill=(200, 200, 80))
        for ci, name in enumerate(args.items):
            p = table.resolve(name)
            x = label_w + ci * (cell_w + pad)
            if rule is None:
                _, rows = stamp_item(p, luma, rule_v22)
                strip = gray_strip(rows, p.w, p.h)
                for yy in range(40):
                    for xx in range(288):
                        v = strip[yy][xx]
                        if v:
                            img.putpixel((x + xx, y + yy), (v, v, v))
            else:
                strip, _ = stamp_item(p, luma, rule)
                for yy in range(40):
                    for xx in range(288):
                        if strip[yy][xx]:
                            img.putpixel((x + xx, y + yy), (255, 255, 255))
            # tile borders every 72 px (the keycap boundaries)
            for tb in range(0, 289, 72):
                for yy in range(40):
                    px = img.getpixel((x + min(tb, 287), y + yy))
                    if px == (24, 24, 24):
                        img.putpixel((x + min(tb, 287), y + yy), (60, 60, 60))

    if z != 1:
        img = img.resize((img.width * z, img.height * z), Image.NEAREST)
    img.save(args.out)
    print(f"wrote {args.out} ({img.width}x{img.height})")

if __name__ == "__main__":
    main()
