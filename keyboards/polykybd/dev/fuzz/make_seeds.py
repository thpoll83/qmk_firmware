#!/usr/bin/env python3
# Copyright 2026 thpoll83
# SPDX-License-Identifier: GPL-2.0-or-later
"""Write the seed corpus of the PolyKybd fuzzers (corpus/<target>/).

Run from anywhere: python3 make_seeds.py. The output is deterministic. Each seed
is a valid input in the layout the target expects (README.md), so the fuzzers
start from inputs that pass the parsers' checks. The harnesses write the CRC32
of the bundles themselves, but the seeds have the correct CRC32 too.
"""

import binascii
import re
import struct
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
POLY = HERE.parent.parent
ROOT = POLY.parent.parent
CORPUS = HERE / "corpus"

W, H = 72, 40


def write(target, name, data):
    d = CORPUS / target
    d.mkdir(parents=True, exist_ok=True)
    (d / name).write_bytes(bytes(data))


# ── overlay_roi ─────────────────────────────────────────────────────────────
def roi_header(keycode, mod, x, y, xx, yy, compressed):
    """Inverse of set_fragment_context_from_buffer() in base/overlay.c."""
    b1 = (mod & 0x0F) | (((y >> 2) & 0x0F) << 4)
    b2 = ((yy & 0x3F) << 2) | (y & 0x03)
    b4 = (xx & 0x7F) | (0x80 if compressed else 0)
    return bytes([keycode, b1, b2, x, b4])


def rle(bits):
    """Encode a bit list in the polymod_rle format (one byte per run)."""
    out, i = [], 0
    while i < len(bits):
        v, n = bits[i], 0
        while i < len(bits) and bits[i] == v and n < 127:
            i, n = i + 1, n + 1
        out.append((0x80 if v else 0) | n)
    return bytes(out)


def overlay_seeds():
    full = bytes((i * 37) & 0xFF for i in range(W * H // 8))
    write("overlay_roi", "full_frame_plain", roi_header(4, 0, 0, 0, W, H, False) + full)
    icon = bytes((0xF0 if r % 2 else 0x0F) for r in range(13))   # 10x10 = 100 bits
    write("overlay_roi", "icon_10x10_plain", roi_header(5, 2, 20, 10, 30, 20, False) + icon)
    checker = [((x // 4) + (y // 4)) % 2 for y in range(H) for x in range(W)]
    write("overlay_roi", "full_frame_rle", roi_header(6, 0, 0, 0, W, H, True) + rle(checker))
    box = [1 if (x in (0, 15) or y in (0, 7)) else 0 for y in range(8) for x in range(16)]
    write("overlay_roi", "box_16x8_rle", roi_header(7, 1, 50, 30, 66, 38, True) + rle(box))
    write("overlay_roi", "edge_1px", roi_header(4, 0, 71, 39, 72, 40, False) + b"\x80")
    write("overlay_roi", "clamped_bounds", roi_header(4, 15, 200, 60, 127, 63, False) + b"\xff" * 64)


# ── prc_report ──────────────────────────────────────────────────────────────
def prc_seeds():
    """Records from the polymod_prc golden vectors (made by the host encoder)."""
    text = (ROOT / "modules/polykybd/polymod_prc/tests/prc_vectors.h").read_text()
    records = []
    for m in re.finditer(r"prc_vec(\d+)_record\[\] = \{([^}]*)\}", text):
        records.append(bytes(int(v, 16) for v in m.group(2).split(",")))
    if not records:
        sys.exit("no records found in prc_vectors.h")
    for i, r in enumerate(records):
        write("prc_report", f"vec{i}", r.ljust(62, b"\0"))
    # Two records back to back, as one report the host builds.
    pair = (records[0] + records[2])[:62]
    write("prc_report", "two_records", pair.ljust(62, b"\0"))


# ── bundles ─────────────────────────────────────────────────────────────────
def header(magic, flags, version, count, total, body):
    crc = binascii.crc32(body) & 0xFFFFFFFF
    return struct.pack("<4sHHIIIIII", magic, 2, flags, version, count, 32, total, crc, 0)


def bundle(magic, records, glyphs, bitmaps, flags=0, version=1):
    """records: (first, last, yAdvance, reserved); glyphs/bitmaps: one entry each."""
    n = len(records)
    off = 32 + 20 * n
    glyph_offs, bitmap_offs, blob = [], [], b""
    for g in glyphs:
        glyph_offs.append(off + len(blob))
        blob += g
        blob += b"\0" * (-len(blob) % 4)
    for bm in bitmaps:
        bitmap_offs.append(off + len(blob))
        blob += bm
        blob += b"\0" * (-len(blob) % 4)
    table = b"".join(
        struct.pack("<IIIIhH", bitmap_offs[i], glyph_offs[i], first, last, yadv, res)
        for i, (first, last, yadv, res) in enumerate(records))
    body = table + blob
    return header(magic, flags, version, n, 32 + len(body), body) + body


def glyph(offset, w, h, xadv, xoff, yoff):
    return struct.pack("<Hbbbbbx", offset, w, h, xadv, xoff, yoff)


def fontpack_seeds():
    # Two fonts, out of global order, so assembly has to sort them.
    g_a = b"".join(glyph(i * 2, 8, 8, 9, 0, -8) for i in range(3))   # 'A'..'C'
    g_b = glyph(0, 8, 16, 9, 0, -16)
    pack = bundle(b"PlyF", [(0x41, 0x43, 10, 7), (0x1F600, 0x1F600, 18, 3)],
                  [g_a, g_b], [bytes(range(6)), bytes(16)])
    write("fontpack", "two_fonts", pack)
    write("fontpack", "wipe_sentinel", header(b"PlyF", 0, 5, 0, 32, b""))
    many = [(0x100 * i, 0x100 * i, 8, (i * 7) % 11) for i in range(12)]
    pack = bundle(b"PlyF", many, [glyph(0, 1, 1, 1, 0, 0)] * 12, [b"\x80"] * 12)
    write("fontpack", "twelve_fonts", pack)
    # Regression: 130 fonts in reverse global order. fontpack_assemble() wrote
    # below its sort array from the 130th font on (int8_t index).
    n = 130
    rev = [(0x10000 + i, 0x10000 + i, 8, n - 1 - i) for i in range(n)]
    pack = bundle(b"PlyF", rev, [glyph(0, 1, 1, 1, 0, 0)] * n, [b"\x80"] * n)
    write("fontpack", "regress_130_fonts_reversed", pack)


def iconlib_seeds():
    # Three icons in two records, contiguous ids 0..2. Row-major bitmaps.
    g0 = glyph(0, 8, 8, 0, 10, 10) + glyph(8, 16, 4, 0, 0, 0)
    g1 = glyph(0, 72, 40, 0, 0, 0)
    bm0 = bytes(8) + bytes(range(8))
    bm1 = bytes((i * 13) & 0xFF for i in range(360))
    lib = bundle(b"PlyI", [(0, 1, 0, 0), (2, 2, 0, 0)], [g0, g1], [bm0, bm1])
    # width 8: pairs (slot 0, id 0), (slot 1, id 2), (slot 3, id 1)
    pairs = bytes([0, 0, 1, 2, 3, 1])
    write("iconlib", "three_icons_w8", bytes([8, len(pairs)]) + pairs + lib)
    # width 12, one pair (slot 2, id 1), padded by repeating it.
    pairs = bytes([0x00, 0x20, 0x01, 0x00, 0x20, 0x01])
    write("iconlib", "three_icons_w12", bytes([12, len(pairs)]) + pairs + lib)
    write("iconlib", "wipe_sentinel", bytes([8, 0]) + header(b"PlyI", 0, 1, 0, 32, b""))


def emjdict_seeds(scratch):
    tool = POLY / "features/emoji/emoji_dict.py"
    src = POLY / "features/emoji/dict_src"
    # The smallest dictionary keeps the fuzzer fast; a second one has accents.
    for lang, query in (("smallest", "smile"), ("fr", "étoile")):
        if lang == "smallest":
            tsvs = sorted((p for p in src.glob("*.tsv")), key=lambda p: p.stat().st_size)
            lang = tsvs[0].stem
        out = Path(scratch) / f"{lang}.plyd"
        subprocess.run([sys.executable, str(tool), "build", lang, "-o", str(out)],
                       check=True, stdout=subprocess.DEVNULL)
        # The harness reads a byte >= 0x80 as U+00C0 + its low 6 bits.
        q = bytes(ord(ch) if ord(ch) < 0x80 else 0x80 | (ord(ch) - 0xC0) for ch in query)
        write("emjdict", f"dict_{lang}", bytes([len(q), 0]) + q + out.read_bytes())
    write("emjdict", "wipe_sentinel", bytes([3, 0]) + b"cat" + header(b"PlyF", 1, 1, 0, 32, b""))


# ── HID command parsers ─────────────────────────────────────────────────────
# The fixtures of the googletests in base/tests/, in the harnesses' layouts.
def report(cmd, op, args=b""):
    """A 64-byte raw HID report: 'P', the command, the op, the arguments."""
    return (bytes([ord("P"), cmd, op]) + bytes(args)).ljust(64, b"\0")


def host_tut_seeds():
    """host_tutorial_plan_tests.cpp: TEXT, SPOTLIGHT and ARM reports (cmd 49)."""
    def text(a, b):
        return report(49, 1, bytes([len(a)]) + a + bytes([len(b)]) + b)
    write("host_tut", "text_two_lines", text(b"Hold Fn", b"then tap"))
    write("host_tut", "text_empty", text(b"", b""))
    write("host_tut", "text_longest", text(b"i" * 59, b"")[:64])
    mask = bytearray(10)
    for row, col in ((2, 0), (8, 7)):                  # split72: bit = row * 8 + col
        b = row * 8 + col
        mask[b // 8] |= 1 << (b % 8)
    write("host_tut", "spotlight_two_keys", report(49, 2, bytes([1]) + mask))
    write("host_tut", "spotlight_off", report(49, 2, bytes(11)))
    conds = bytes([1, 2, 0, 2, 5, 0, 3, 0x04, 0x00])   # key (2,0), layer 5, KC_A
    write("host_tut", "arm_three", report(49, 4, bytes([3]) + conds))
    write("host_tut", "arm_none", report(49, 4, bytes([0])))
    write("host_tut", "arm_short", report(49, 4, bytes([2, 1, 1, 1, 1, 1, 2]))[:9])


def macro_seeds():
    """macro_decode_tests.cpp: NUL-terminated macros in a zero-padded buffer."""
    def tap(kc):
        return bytes([1, 1, kc])

    def down(kc):
        return bytes([1, 2, kc])

    def up(kc):
        return bytes([1, 3, kc])

    def delay(digits):
        return bytes([1, 4]) + digits

    def buf(macros, size=64):
        return b"".join(m + b"\0" for m in macros).ljust(size, b"\0")

    write("macro_decode", "chord_and_delay",
          buf([down(0xE0) + tap(0x06) + up(0xE0) + delay(b"50") + b"ok"]))
    write("macro_decode", "three_macros", buf([b"hello", b"", tap(0x04) + down(0xE0) + up(0xE0)]))
    write("macro_decode", "delay_clamped", buf([delay(b"9999999") + b"x"]))
    write("macro_decode", "unknown_op", buf([bytes([1, 0x7F]) + b"XY"]))
    # Cut short mid-write: the last byte is POLY_MACRO_INCOMPLETE.
    write("macro_decode", "incomplete", buf([b"abc" + tap(0x05)], 32)[:-1] + b"\xff")
    write("macro_decode", "truncated_prefix", b"ab\0\x01")


def attn_guide_seeds():
    """attn_guide_tests.cpp: fn_then_midi() and a four-step guide (cmd 47 op 2)."""
    def guide(count, period, target, steps):
        flat = bytes(v for s in steps for v in s)
        return (bytes([0, 47, 2, count, period, target, len(steps)]) + flat).ljust(64, b"\0")
    write("attn_guide", "fn_then_midi", guide(12, 25, 8, [(2, 0, 4), (8, 7, 5)]))
    write("attn_guide", "four_steps", guide(0, 100, 12, [(0, 0, 0), (1, 1, 1), (7, 5, 2), (3, 4, 3)]))
    write("attn_guide", "one_step_exact", guide(1, 23, 1, [(4, 2, 0)])[:10])


def ai_seeds():
    """ai_events_tests.cpp: make_payload(), the page bytes, a card header, ring ops."""
    def payload(state, flags, slot):
        return bytes([state] * 10) + bytes([flags]) + struct.pack("<H", slot) * 12

    # Five pushes, read since 3, then 12 pushes (overflow), read since 0 and since 7.
    ops = list(range(5)) + [0x80, 3] + [i % 11 for i in range(12)] + [0x80, 0, 0x80, 7]
    write("ai_events", "stale_all_slots",
          payload(5, 0x0F, 599) + bytes([1, 3, 7]) + struct.pack("<H", (5 << 12) | 599) + bytes(ops))
    write("ai_events", "empty_no_slot",
          payload(0, 0, 0xFFFF) + bytes([0, 0, 0]) + struct.pack("<H", 0) + bytes([0x80, 0]))
    write("ai_events", "wrap_seq", payload(2, 2, 0) + bytes([0, 2, 1]) + struct.pack("<H", 0x1000 | 42)
          + bytes([i % 13 for i in range(260)] + [0x80, 255, 0x80, 1]))


def combo_seeds():
    """combo_store_tests.cpp: records and the slot-pick cases (cmd 48)."""
    A, B, C, ESC, MO1, TG2, TO3, LSFT_A = 0x04, 0x05, 0x06, 0x29, 0x5221, 0x5262, 0x5203, 0x0204

    def rec(ctl, words):
        return bytes([ctl]) + struct.pack("<5H", *words)
    combos = (rec(0, [A, B, 0, 0, ESC]) + rec(0, [A, B, C, MO1, ESC]) + rec(0, [0, 0, 0, 0, 0])
              + rec(0, [A, 0, B, 0, ESC]) + rec(0, [A, A, 0, 0, ESC]))
    write("combo_store", "combos", combos)
    dances = b"".join(rec(1 | (count << 1) | (0x10 if held else 0), [A, LSFT_A, TG2, MO1, 200])
                      for count in range(4) for held in (False, True))
    dances += rec(1 | (3 << 1), [A, 0, 0, 0, 0]) + rec(1, [TO3, 0x2204, 0, 0, 105])
    write("combo_store", "tap_dances", dances)
    # Slot pick: clean / tried bits of both sectors and the newest sector.
    picks = b"".join(bytes([0, c0, c1, t0, t1, newest]) + bytes(5)
                     for c0, c1, t0, t1, newest in ((0xFF, 0xFF, 0, 0, 2), (0xF0, 0xFF, 0, 0, 0),
                                                    (0, 0xFF, 0, 0, 0), (0, 0, 0, 0, 1),
                                                    (0x01, 0, 0x01, 0, 0), (0, 0, 0, 0x03, 0)))
    write("combo_store", "slot_picks", picks)


def map_codec_seeds():
    """map_codec_tests.cpp: cmd 21 10-bit pairs and cmd 33 streams at 8, 9, 11 and 16 bits."""
    def pack(values, width, nbytes):
        acc, nbits = 0, 0
        for v in values:
            acc |= (v & ((1 << width) - 1)) << nbits
            nbits += width
        return acc.to_bytes(nbytes, "little")
    # Width selector: width = 8 + selector % 9.
    pairs = [v for i in range(24) for v in (i * 61 % 1440, i * 25 % 1200)]
    write("map_codec", "cmd21_w10", bytes([2]) + pack(pairs, 10, 62))
    write("map_codec", "cmd33_w8", bytes([0]) + pack([i * 7 % 256 for i in range(61)], 8, 61))
    write("map_codec", "cmd33_w9", bytes([1]) + pack([(i * 37) % 512 for i in range(54)], 9, 61))
    write("map_codec", "cmd33_w11_no_legend",
          bytes([3]) + pack([v for i in range(22) for v in (i * 65, 600 + i * 27)], 11, 61))
    write("map_codec", "cmd33_w16", bytes([8]) + pack([0xFFFF, 0, 0x8001, 1440] * 7, 16, 61))


def main():
    import tempfile
    overlay_seeds()
    prc_seeds()
    fontpack_seeds()
    iconlib_seeds()
    with tempfile.TemporaryDirectory() as scratch:
        emjdict_seeds(scratch)
    host_tut_seeds()
    macro_seeds()
    attn_guide_seeds()
    ai_seeds()
    combo_seeds()
    map_codec_seeds()
    for d in sorted(CORPUS.iterdir()):
        print(f"{d.name}: {len(list(d.iterdir()))} seeds")


if __name__ == "__main__":
    main()
