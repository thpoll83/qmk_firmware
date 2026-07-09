#!/usr/bin/env python3
"""Wrap a linked DoomPack image in its PlyX header (doom_pack_abi.h).

Usage:
    mkpack.py --elf doom_pack.elf --bin doom_pack_image.bin \
              --ram-base 0x20000000 --ram-size 226800 --version 2 \
              --out doom_pack_v2.plyx

The .bin is the objcopy'd image (text + rodata + data initializers at their
flash LMAs); entry/arena offsets come from the .elf symbol table. The CRC is
plain zlib crc32 == the firmware's crc32_1byte with seed 0.
"""
import argparse
import struct
import subprocess
import sys
import zlib

MAGIC = b"PlyX"
ABI = 1
FLASH_IMAGE_BASE = 0x107C0000 + 64  # slot XIP + DOOM_PACK_HDR_SIZE (pack.ld.in PACKF origin)
HDR_SIZE = 64  # doom_pack_abi.h DOOM_PACK_HDR_SIZE


def sym_addr(elf: str, name: str, nm: str) -> int:
    out = subprocess.run([nm, elf], capture_output=True, text=True, check=True).stdout
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] == name:
            return int(parts[0], 16)
    sys.exit(f"mkpack: symbol {name} not found in {elf}")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--elf", required=True)
    ap.add_argument("--bin", required=True)
    ap.add_argument("--ram-base", required=True, type=lambda v: int(v, 0))
    ap.add_argument("--ram-size", required=True, type=lambda v: int(v, 0))
    ap.add_argument("--version", required=True, type=int)
    ap.add_argument("--out", required=True)
    ap.add_argument("--nm", default="arm-none-eabi-nm")
    args = ap.parse_args()

    with open(args.bin, "rb") as f:
        image = f.read()

    entry = sym_addr(args.elf, "doom_pack_init", args.nm) & ~1  # strip Thumb bit
    entry_off = entry - FLASH_IMAGE_BASE
    if not 0 <= entry_off < len(image):
        sys.exit(f"mkpack: entry offset {entry_off:#x} outside the image")

    statics_end = sym_addr(args.elf, "__pack_statics_end__", args.nm)
    arena_off = statics_end - args.ram_base
    if not 0 < arena_off < args.ram_size:
        sys.exit(f"mkpack: arena_off {arena_off:#x} outside the pool")

    hdr = struct.pack(
        "<4s8I",
        MAGIC,
        ABI,
        len(image),
        zlib.crc32(image) & 0xFFFFFFFF,
        entry_off,
        args.ram_base,
        args.ram_size,
        arena_off,
        args.version,
    )
    hdr += b"\x00" * (HDR_SIZE - len(hdr))  # reserved tail
    assert len(hdr) == HDR_SIZE == 64, len(hdr)

    with open(args.out, "wb") as f:
        f.write(hdr)
        f.write(image)

    print(
        f"mkpack: {args.out}: image {len(image)} B, entry_off {entry_off:#x}, "
        f"ram {args.ram_base:#010x}+{args.ram_size}, arena_off {arena_off}, "
        f"v{args.version}"
    )


if __name__ == "__main__":
    main()
