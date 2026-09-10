#!/usr/bin/env python3
"""Build a UF2 that writes this half's handedness stamp.

Handedness lives in a flash sector the firmware owns (FW_HAND_STAMP_OFFSET, see
base/hand_stamp.h) precisely so that no EEPROM failure can reach it. That sector
is an ordinary flash address, so BOOTSEL/UF2 can write it directly -- which makes
this the recovery path for a half that has come up on the wrong side, and the way
to provision a fresh board without going through the host at all.

    python3 tools/make_hand_uf2.py --side left
    python3 tools/make_hand_uf2.py --side right --append-to polykybd_split72_default.uf2
    python3 tools/make_hand_uf2.py --verify polykybd-hand-left.uf2

Drag the result onto the RP2040's BOOTSEL drive. A standalone stamp UF2 touches
ONLY that sector: the firmware, the font pack and the EEPROM are all somewhere
else and are left alone.

⚠️ Sides are per HALF, so each board needs its own file -- flashing `left` to both
gives you two lefts and no split link.

Why writing one page is enough: the bootrom's UF2 handler erases each 4 KB sector
before programming a page into it (it must -- otherwise re-flashing firmware over
existing firmware could not work at all, since programming only clears bits), and
it erases nothing beyond the sectors the file actually targets (which is why a
firmware UF2 leaves the font pack and the EEPROM intact). So one block lands as
page 0 of a freshly erased sector, which is exactly the state hand_stamp.c's
append-style reader expects.

Offsets and the magic are parsed out of the firmware headers rather than repeated
here, so a change to the flash map cannot silently desync this tool. The record
LAYOUT is the one thing written out by hand; it is pinned by
`make test:polykybd_hand_stamp` (TheWireLayoutIsPinnedForTheUf2Tool).
"""

import argparse
import pathlib
import re
import struct
import sys
import zlib

HERE = pathlib.Path(__file__).resolve().parent
BASE = HERE.parent / "base"

# UF2 container (github.com/microsoft/uf2). One block = 512 bytes, 256 of payload.
UF2_MAGIC0, UF2_MAGIC1, UF2_MAGIC_END = 0x0A324655, 0x9E5D5157, 0x0AB16F30
UF2_FLAG_FAMILY_ID = 0x00002000
RP2040_FAMILY_ID = 0xE48BFF56
XIP_BASE = 0x10000000
PAGE = 256

# poly_hand_stamp_t: uint32 magic, uint8 is_left, uint8 pad[3], uint32 crc.
# The CRC covers everything before it, i.e. the first 8 bytes.
STAMP_FMT = "<IB3xI"
STAMP_CRC_SPAN = 8


def _defines(path):
    out = {}
    for name, value in re.findall(r"^#define\s+(\w+)\s+(.+?)\s*(?://.*)?$", path.read_text(encoding="utf-8"), re.M):
        out[name] = value.strip()
    return out


def _resolve(defines, name, depth=0):
    """Evaluate a #define that may be written in terms of other #defines."""
    if depth > 8:
        raise SystemExit(f"{name}: #define nesting too deep — is there a cycle?")
    expr = defines[name]
    expr = re.sub(r"\b(0[xX][0-9a-fA-F]+|\d+)(UL|U|L)\b", r"\1", expr)
    for ident in sorted(set(re.findall(r"(?<![\w.])[A-Za-z_]\w*", expr)), key=len, reverse=True):
        if ident not in defines:
            raise SystemExit(f"{name}: cannot resolve `{ident}` — has the flash map moved?")
        expr = re.sub(rf"(?<![\w.]){ident}\b", f"({_resolve(defines, ident, depth + 1)})", expr)
    if not re.fullmatch(r"[0-9a-fA-FxX()\s+\-*/]+", expr):
        raise SystemExit(f"{name}: refusing to evaluate `{expr}`")
    return eval(expr)  # noqa: S307 - the pattern above admits arithmetic only


def firmware_constants():
    stamp_off = _resolve(_defines(BASE / "fw_staging.h"), "FW_HAND_STAMP_OFFSET")
    magic = _resolve(_defines(BASE / "hand_stamp.h"), "POLY_HAND_STAMP_MAGIC")
    return stamp_off, magic


def stamp_record(is_left, magic):
    body = struct.pack("<IB3x", magic, 1 if is_left else 0)
    assert len(body) == STAMP_CRC_SPAN, "STAMP_CRC_SPAN disagrees with the record layout"
    return body + struct.pack("<I", zlib.crc32(body))


def uf2_block(addr, payload, block_no, num_blocks):
    data = payload.ljust(476, b"\x00")
    return struct.pack(
        "<8I476sI",
        UF2_MAGIC0, UF2_MAGIC1, UF2_FLAG_FAMILY_ID, addr,
        len(payload), block_no, num_blocks, RP2040_FAMILY_ID,
        data, UF2_MAGIC_END,
    )


def parse_uf2(blob, path):
    if len(blob) % 512:
        raise SystemExit(f"{path}: {len(blob)} bytes is not a whole number of 512-byte UF2 blocks")
    blocks = []
    for i in range(0, len(blob), 512):
        b = blob[i:i + 512]
        m0, m1, flags, addr, size, no, total, family = struct.unpack("<8I", b[:32])
        if m0 != UF2_MAGIC0 or m1 != UF2_MAGIC1 or struct.unpack("<I", b[508:512])[0] != UF2_MAGIC_END:
            raise SystemExit(f"{path}: block {i // 512} is not a UF2 block")
        if flags & UF2_FLAG_FAMILY_ID and family != RP2040_FAMILY_ID:
            raise SystemExit(f"{path}: block {i // 512} targets family 0x{family:08X}, not RP2040")
        blocks.append((addr, b[32:32 + size]))
    return blocks


def write_uf2(path, blocks):
    total = len(blocks)
    path.write_bytes(b"".join(uf2_block(a, p, i, total) for i, (a, p) in enumerate(blocks)))


def verify(path):
    stamp_off, magic = firmware_constants()
    target = XIP_BASE + stamp_off
    hits = [(a, p) for a, p in parse_uf2(path.read_bytes(), path) if a == target]
    if not hits:
        raise SystemExit(f"{path}: no block targets the handedness stamp at 0x{target:08X}")
    if len(hits) > 1:
        raise SystemExit(f"{path}: {len(hits)} blocks target the stamp sector — a half would end up with the last one")
    payload = hits[0][1]
    got_magic, is_left, crc = struct.unpack_from(STAMP_FMT, payload)
    if got_magic != magic:
        raise SystemExit(f"{path}: stamp magic is 0x{got_magic:08X}, expected 0x{magic:08X}")
    if is_left > 1:
        raise SystemExit(f"{path}: is_left is {is_left}; the firmware rejects anything above 1")
    if crc != zlib.crc32(payload[:STAMP_CRC_SPAN]):
        raise SystemExit(f"{path}: stamp CRC does not check out — the firmware would ignore this record")
    print(f"{path}: valid handedness stamp — {'LEFT' if is_left else 'RIGHT'} at 0x{target:08X}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--side", choices=("left", "right"), help="which half this UF2 is for")
    ap.add_argument("-o", "--out", type=pathlib.Path, help="output file (default polykybd-hand-<side>.uf2)")
    ap.add_argument("--append-to", type=pathlib.Path, metavar="FW.UF2",
                    help="copy a firmware UF2 and add the stamp block, so one file provisions a half")
    ap.add_argument("--verify", type=pathlib.Path, metavar="FILE", help="check a UF2's stamp block and exit")
    args = ap.parse_args()

    if args.verify:
        verify(args.verify)
        return
    if not args.side:
        ap.error("--side is required (or use --verify)")

    stamp_off, magic = firmware_constants()
    is_left = args.side == "left"
    block = (XIP_BASE + stamp_off, stamp_record(is_left, magic))

    blocks = []
    if args.append_to:
        blocks = parse_uf2(args.append_to.read_bytes(), args.append_to)
        clash = [a for a, _ in blocks if (a - XIP_BASE) // 4096 == stamp_off // 4096]
        if clash:
            raise SystemExit(f"{args.append_to}: already writes the stamp sector — refusing to add a second record")
    blocks.append(block)

    out = args.out or pathlib.Path(
        f"{args.append_to.stem}-hand-{args.side}.uf2" if args.append_to else f"polykybd-hand-{args.side}.uf2")
    write_uf2(out, blocks)
    verify(out)
    if not args.append_to:
        print(f"  touches one 4 KB sector only; firmware, font pack and EEPROM are untouched.")
    print(f"  drag onto the BOOTSEL drive of the {args.side.upper()} half — each half needs its own file.")


if __name__ == "__main__":
    sys.exit(main())
