#!/usr/bin/env python3
"""Wrap a rp2040-doom WHX (or any blob) into a UF2 targeting the PolyKybd
resource-region WAD slot, for flashing via the BOOTSEL drive.

The Doom easter egg expects the game data XIP-mapped at 0x10600000 (flash
offset 0x600000 — the free upper half of the resource region; the font-pack
slots below 0x600000 and the EEPROM journal at the very top are untouched).
Until the HID `doomwad` staging slot exists, BOOTSEL is the delivery path:

    python3 whx2uf2.py doom1.whx doom1_whx.uf2
    # hold BOOTSEL while plugging the half in, then copy doom1_whx.uf2 onto
    # the RPI-RP2 drive. Firmware is untouched (different flash region) —
    # reflash nothing else.

doom1.whx (1,800,344 B, WHD_SUPER_TINY format, magic "IWHX") ships at the
rp2040-doom repo root — see ../engine/PROVENANCE.md for the fetch commands.
"""
import struct
import sys

UF2_MAGIC0    = 0x0A324655
UF2_MAGIC1    = 0x9E5D5157
UF2_MAGIC_END = 0x0AB16F30
UF2_FLAG_FAMILY_ID = 0x00002000
RP2040_FAMILY = 0xE48BFF56
TARGET_ADDR   = 0x10600000  # keep in sync with TINY_WAD_ADDR (doom_tiny_defs.h)
PAYLOAD       = 256


def main():
    if len(sys.argv) != 3:
        sys.exit(f"usage: {sys.argv[0]} <doom1.whx> <out.uf2>")
    data = open(sys.argv[1], 'rb').read()
    if data[:4] != b'IWHX':
        sys.exit(f"{sys.argv[1]}: bad magic {data[:4]!r} — expected IWHX "
                 "(the WHD_SUPER_TINY conversion of the shareware WAD)")
    nblocks = (len(data) + PAYLOAD - 1) // PAYLOAD
    with open(sys.argv[2], 'wb') as out:
        for i in range(nblocks):
            chunk = data[i * PAYLOAD:(i + 1) * PAYLOAD].ljust(PAYLOAD, b'\0')
            block = struct.pack('<IIIIIIII', UF2_MAGIC0, UF2_MAGIC1,
                                UF2_FLAG_FAMILY_ID, TARGET_ADDR + i * PAYLOAD,
                                PAYLOAD, i, nblocks, RP2040_FAMILY)
            block += chunk.ljust(476, b'\0')
            block += struct.pack('<I', UF2_MAGIC_END)
            assert len(block) == 512
            out.write(block)
    print(f"{sys.argv[2]}: {nblocks} blocks, {len(data)} bytes at 0x{TARGET_ADDR:08x}")


if __name__ == '__main__':
    main()
