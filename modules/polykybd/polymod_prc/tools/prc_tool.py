#!/usr/bin/env python3
# Copyright 2026 thpoll83
# SPDX-License-Identifier: GPL-2.0-or-later
"""Train PRC probability tables and encode 1-bit images for the polymod_prc decoder.

PRC (Predictive Range Coding) codes a 1-bit image pixel by pixel. Each pixel's
10 already-coded neighbours select one of 1024 contexts, a fixed table gives
P(pixel is 0) for that context, and a binary range coder turns the
probabilities into bytes. The decoder in polymod_prc.c reverses it exactly.

Two subcommands:

    # Train a table from your own images and write the C header (and a .bin):
    prc_tool.py train icons/*.png --table-id 2 --header prc_table_v2.h --bin prc_table_v2.bin

    # Encode an image (or every cell of a sprite sheet) with a table:
    prc_tool.py encode icon.png --table prc_table_v1.h

A table is part of the format: the encoder and the decoder must use the same
bytes, or the decoder draws garbage and nothing reports an error. So treat a
table that has shipped as frozen and give a retrained one a new id.

The core functions (train, encode, decode, contexts) need only the standard
library; reading image files needs Pillow (`pip install pillow`).

This is a port of PolyKybdHost polyhost/util/prc_codec.py, which encodes what
PolyKybd's keycaps decode. keyboards/polykybd/tools/tests/prc_tool_test.py
checks that this encoder reproduces the host's golden vectors byte for byte.
"""
from __future__ import annotations

import argparse
import hashlib
import os
import re
import sys

# Neighbours as (dy, dx) relative to the pixel, most significant context bit
# first. Only already-decoded pixels: the two rows above and the two to the left.
# Must match TEMPLATE_DY / TEMPLATE_DX in polymod_prc.c.
TEMPLATE = ((-1, -1), (-1, 0), (-1, 1), (0, -2), (0, -1), (-2, -1), (-2, 0), (-2, 1), (-1, -2), (-1, 2))
CONTEXTS = 1 << len(TEMPLATE)  # 1024

_TOP = 1 << 24
_MASK32 = 0xFFFFFFFF

# -- images as lists of rows of 0/1 ---------------------------------------------


def crop_to_roi(mask):
    """(top, left, roi) for the ink bounding box of `mask`, or None if it is empty."""
    rows = [y for y, row in enumerate(mask) if any(row)]
    if not rows:
        return None
    cols = [x for x in range(len(mask[0])) if any(row[x] for row in mask)]
    top, bottom, left, right = rows[0], rows[-1], cols[0], cols[-1]
    return top, left, [list(row[left:right + 1]) for row in mask[top:bottom + 1]]


def contexts(roi):
    """Context index of every ROI pixel, row-major. Pixels outside the ROI count
    as 0, which is also what the decoder reads: it decodes into a cleared frame."""
    h, w = len(roi), len(roi[0])

    def px(y, x):
        return roi[y][x] if 0 <= y < h and 0 <= x < w else 0

    out = []
    for y in range(h):
        for x in range(w):
            c = 0
            for dy, dx in TEMPLATE:
                c = (c << 1) | (1 if px(y + dy, x + dx) else 0)
            out.append(c)
    return out


# -- training -----------------------------------------------------------------


def train(masks) -> bytes:
    """A table from 1-bit masks, each cropped to its ink box first.

    p0 = round(256 * (n0 + 0.5) / (n0 + n1 + 1)), clamped to 1..255, where n0/n1
    count the 0/1 pixels seen in that context. Deterministic, and independent of
    the order of the masks. Same formula as PolyKybdHost's prc_codec.train().
    """
    n0 = [0] * CONTEXTS
    n1 = [0] * CONTEXTS
    for m in masks:
        cropped = crop_to_roi(m)
        if cropped is None:
            continue
        roi = cropped[2]
        bits = [v for row in roi for v in row]
        for bit, c in zip(bits, contexts(roi)):
            if bit:
                n1[c] += 1
            else:
                n0[c] += 1
    table = bytearray(CONTEXTS)
    for c in range(CONTEXTS):
        # Round half to even, like numpy.rint in the host trainer.
        p0 = round(256 * (n0[c] + 0.5) / (n0[c] + n1[c] + 1))
        table[c] = min(255, max(1, p0))
    return bytes(table)


# -- the range coder ----------------------------------------------------------


class _Encoder:
    """LZMA-style range encoder with 8-bit probabilities."""
    def __init__(self):
        self.low = 0
        self.range = _MASK32
        self.cache = 0
        self.cache_size = 1
        self.out = bytearray()

    def _shift_low(self):
        if self.low < 0xFF000000 or self.low > _MASK32:
            carry = self.low >> 32
            temp = self.cache
            while True:
                self.out.append((temp + carry) & 0xFF)
                temp = 0xFF
                self.cache_size -= 1
                if self.cache_size == 0:
                    break
            self.cache = (self.low >> 24) & 0xFF
        self.cache_size += 1
        self.low = (self.low << 8) & _MASK32

    def bit(self, value: int, p0: int):
        bound = (self.range >> 8) * p0
        if value:
            self.low += bound
            self.range -= bound
        else:
            self.range = bound
        while self.range < _TOP:
            self.range = (self.range << 8) & _MASK32
            self._shift_low()

    def finish(self) -> bytes:
        for _ in range(5):
            self._shift_low()
        out = bytes(self.out)
        # The first byte of an LZMA-style stream is always 0 and the decoder does
        # not read it. Trailing zeros are dropped: the decoder reads 0 past the end.
        assert out[0] == 0, "range coder invariant: first byte is 0"
        return out[1:].rstrip(b"\x00")


def encode(roi, table: bytes) -> bytes:
    """Encode a ROI (rows of 0/1) with `table`. Pass the ink box, as crop_to_roi gives it."""
    _check_table(table)
    enc = _Encoder()
    bits = [v for row in roi for v in row]
    for value, c in zip(bits, contexts(roi)):
        enc.bit(1 if value else 0, table[c])
    return enc.finish()


def decode(payload: bytes, height: int, width: int, table: bytes):
    """Reference decoder, step for step what polymod_prc.c does. Returns rows of 0/1."""
    _check_table(table)
    pos = 0

    def next_byte():
        nonlocal pos
        b = payload[pos] if pos < len(payload) else 0
        pos += 1
        return b

    rng, code = _MASK32, 0
    for _ in range(4):
        code = (code << 8) | next_byte()
    out = [[0] * width for _ in range(height)]

    def px(y, x):
        return out[y][x] if 0 <= y < height and 0 <= x < width else 0

    for y in range(height):
        for x in range(width):
            c = 0
            for dy, dx in TEMPLATE:
                c = (c << 1) | px(y + dy, x + dx)
            bound = (rng >> 8) * table[c]
            if code < bound:
                rng = bound
                bit = 0
            else:
                code -= bound
                rng -= bound
                bit = 1
            while rng < _TOP:
                rng = (rng << 8) & _MASK32
                code = ((code << 8) | next_byte()) & _MASK32
            out[y][x] = bit
    return out


# -- tables on disk -----------------------------------------------------------


def _check_table(table: bytes):
    if len(table) != CONTEXTS or min(table) < 1:
        raise ValueError(f"a PRC table is {CONTEXTS} bytes, each 1..255")


def header(table: bytes, table_id: int, source: str) -> str:
    """The C header the decoder includes, holding `table` as prc_table_v<id>."""
    _check_table(table)
    digest = hashlib.sha256(table).hexdigest()
    rows = [", ".join(f"{b:3d}" for b in table[i:i + 16]) for i in range(0, len(table), 16)]
    body = ",\n    ".join(rows)
    return f"""// SPDX-License-Identifier: GPL-2.0-or-later
// GENERATED by polymod_prc tools/prc_tool.py -- do not edit.
//
// PRC (Predictive Range Coding) probability table v{table_id}: P(pixel is 0) * 256
// for each of the 1024 neighbour contexts, trained on {source}.
// sha256 {digest}.
//
// FROZEN once shipped: a retrained table is a new id, never an edit to this one.
#pragma once

#include <stdint.h>

#define PRC_TABLE_V{table_id}_SHA256 "{digest}"

// clang-format off
static const uint8_t prc_table_v{table_id}[1024] = {{
    {body}
}};
// clang-format on
"""


def load_table(path: str) -> bytes:
    """A table from a 1024-byte .bin, or from the array in a generated C header."""
    if path.endswith(".h"):
        with open(path, encoding="utf-8") as fh:
            text = fh.read()
        m = re.search(r"\[1024\]\s*=\s*\{([^}]*)\}", text)
        if not m:
            raise ValueError(f"{path}: no [1024] table array found")
        table = bytes(int(v) for v in re.findall(r"\d+", m.group(1)))
    else:
        with open(path, "rb") as fh:
            table = fh.read()
    _check_table(table)
    return table


# -- reading images -----------------------------------------------------------


def load_masks(path: str, cell=None, threshold: int = 128, invert: bool = False):
    """1-bit masks from an image file: the whole image, or each cell of a sheet.
    A pixel at or above `threshold` (0-255 grey) is lit, unless `invert`."""
    try:
        from PIL import Image
    except ImportError as exc:
        raise SystemExit("reading images needs Pillow: pip install pillow") from exc
    img = Image.open(path).convert("L")
    w, h = img.size
    px = img.load()
    full = [[(1 if px[x, y] >= threshold else 0) ^ (1 if invert else 0) for x in range(w)] for y in range(h)]
    if not cell:
        return [full]
    cw, ch = cell
    return [[row[cx:cx + cw] for row in full[cy:cy + ch]] for cy in range(0, h - ch + 1, ch) for cx in range(0, w - cw + 1, cw)]


def _cell(text: str):
    m = re.fullmatch(r"(\d+)x(\d+)", text)
    if not m:
        raise argparse.ArgumentTypeError("expected WxH, for example 72x40")
    return int(m.group(1)), int(m.group(2))


# -- command line -------------------------------------------------------------


def _cmd_train(args) -> int:
    masks = []
    for path in args.images:
        masks.extend(load_masks(path, args.cell, args.threshold, args.invert))
    distinct = {tuple(map(tuple, m)) for m in masks if crop_to_roi(m) is not None}
    if not distinct:
        raise SystemExit("no image with any lit pixel to train on")
    table = train(sorted(distinct))
    for path in (args.bin, args.header):
        if path and os.path.exists(path) and not args.force:
            if load_table(path) != table:
                raise SystemExit(f"{path} exists with a different table; shipped tables are frozen "
                                 "(use a new --table-id, or --force if it never shipped)")
    if args.bin:
        with open(args.bin, "wb") as fh:
            fh.write(table)
    if args.header:
        with open(args.header, "w", encoding="utf-8", newline="\n") as fh:
            fh.write(header(table, args.table_id, f"{len(distinct)} distinct images"))
    print(f"table v{args.table_id}: {len(distinct)} distinct images, "
          f"sha256 {hashlib.sha256(table).hexdigest()}")
    return 0


def _cmd_encode(args) -> int:
    table = load_table(args.table)
    total = 0
    for i, m in enumerate(load_masks(args.image, args.cell, args.threshold, args.invert)):
        cropped = crop_to_roi(m)
        if cropped is None:
            continue
        top, left, roi = cropped
        payload = encode(roi, table)
        assert decode(payload, len(roi), len(roi[0]), table) == roi, "round trip failed"
        total += len(payload)
        print(f"cell {i}: top {top} left {left} {len(roi)}x{len(roi[0])} "
              f"{len(payload)} B: {payload.hex()}")
    print(f"total {total} bytes")
    return 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("train", "encode"):
        p = sub.add_parser(name)
        p.add_argument("--cell", type=_cell, help="split each image into WxH cells (a sprite sheet)")
        p.add_argument("--threshold", type=int, default=128, help="grey level that counts as lit (default 128)")
        p.add_argument("--invert", action="store_true", help="dark pixels are lit")
        if name == "train":
            p.add_argument("images", nargs="+")
            p.add_argument("--table-id", type=int, required=True)
            p.add_argument("--header", help="write the C header here")
            p.add_argument("--bin", help="write the raw 1024-byte table here")
            p.add_argument("--force", action="store_true", help="overwrite a different existing table (only one that never shipped)")
            p.set_defaults(func=_cmd_train)
        else:
            p.add_argument("image")
            p.add_argument("--table", required=True, help="a .bin table or a generated .h")
            p.set_defaults(func=_cmd_encode)
    args = ap.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
