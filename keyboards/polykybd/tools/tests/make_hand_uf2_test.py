#!/usr/bin/env python3
"""Hold make_hand_uf2.py's output to the RP2040 BOOTROM's acceptance rules.

⚠️ THE POINT OF THIS FILE: the handedness UF2s shipped in v0.23.0, v0.25.0 and
v0.27.1 were INERT, and nothing noticed for three releases. Every block declared
the 12-byte stamp record as its `payloadSize`, and the bootrom's vd_write_block()
tests `uf2->payload_size == 256` before it looks at the block at all -- so nothing
was written, the download never completed, and safe_reboot() never ran. The half
sat in BOOTSEL with the drive mounted, which from outside is a dead board.

It survived because of two gaps this file closes:

  1. The tool's own `verify()` read the size field back out of the file it had
     just written. That proves SELF-CONSISTENCY, never CONFORMANCE: a wrong value
     round-trips perfectly. So the check here is deliberately written against the
     bootrom's rules restated independently (bootrom_accepts() below), not against
     anything make_hand_uf2.py believes.
  2. A GENERATED ARTIFACT THAT NO TEST CONSUMES IS CHECKED BY NOTHING. The
     firmware build never reads this tool's output, and the HIL rig flashes
     elf2uf2-made images over GPIO BOOTSEL, so no build and no hardware round
     could ever have caught it. Only the release workflow ran the tool at all.

Run: python3 -m unittest discover -s keyboards/polykybd/tools/tests -p '*_test.py'
"""

import importlib.util
import pathlib
import struct
import sys
import unittest
import zlib

HERE = pathlib.Path(__file__).resolve().parent
TOOL = HERE.parent / "make_hand_uf2.py"


def _load_tool():
    spec = importlib.util.spec_from_file_location("make_hand_uf2", TOOL)
    mod = importlib.util.module_from_spec(spec)
    sys.modules["make_hand_uf2"] = mod
    spec.loader.exec_module(mod)
    return mod


uf2 = _load_tool()

# --- the bootrom's rules, restated here on purpose --------------------------
# Transcribed from pico-bootrom bootrom/virtual_disk.c. Deliberately NOT imported
# from the tool: if the tool's idea of a valid block were the reference, this
# suite would agree with a wrong file exactly as verify() used to.
#
#   vd_write_block():             magics, FAMILY_ID_PRESENT, family == RP2040,
#                                 !NOT_MAIN_FLASH, payload_size == 256
#   _update_current_uf2_info():   num_blocks != 0, target in flash,
#                                 target 256-byte aligned, block_no < num_blocks
UF2_MAGIC0, UF2_MAGIC1, UF2_MAGIC_END = 0x0A324655, 0x9E5D5157, 0x0AB16F30
FLAG_NOT_MAIN_FLASH, FLAG_FAMILY_ID = 0x00000001, 0x00002000
RP2040_FAMILY_ID = 0xE48BFF56
XIP_BASE, FLASH_MAX = 0x10000000, 0x01000000
PAGE = 256


def bootrom_accepts(block, index, total):
    """True when the RP2040 bootrom would program this 512-byte block."""
    if len(block) != 512:
        return False, "not a 512-byte block"
    m0, m1, flags, addr, size, no, num, family = struct.unpack("<8I", block[:32])
    if m0 != UF2_MAGIC0 or m1 != UF2_MAGIC1:
        return False, "start magic"
    if struct.unpack("<I", block[508:512])[0] != UF2_MAGIC_END:
        return False, "end magic"
    if not flags & FLAG_FAMILY_ID:
        return False, "no family id"
    if family != RP2040_FAMILY_ID:
        return False, f"family 0x{family:08X}"
    if flags & FLAG_NOT_MAIN_FLASH:
        return False, "NOT_MAIN_FLASH set"
    if size != PAGE:                       # <-- the one that shipped wrong
        return False, f"payloadSize {size}"
    if num == 0:
        return False, "num_blocks 0"
    if not (XIP_BASE <= addr < XIP_BASE + FLASH_MAX):
        return False, f"target 0x{addr:08X} not in flash"
    if addr % PAGE:
        return False, f"target 0x{addr:08X} not page aligned"
    if no >= num:
        return False, f"block_no {no} >= num_blocks {num}"
    if (no, num) != (index, total):
        return False, f"numbered {no}/{num}, expected {index}/{total}"
    return True, ""


def build(side):
    stamp_off, magic = uf2.firmware_constants()
    page = uf2.stamp_page(side == "left", magic)
    return uf2.uf2_block(XIP_BASE + stamp_off, page, 0, 1), stamp_off, magic


class BootromConformance(unittest.TestCase):
    def test_both_sides_are_accepted_by_the_bootrom(self):
        for side in ("left", "right"):
            with self.subTest(side=side):
                block, _, _ = build(side)
                self.assertEqual(len(block), 512)
                ok, why = bootrom_accepts(block, 0, 1)
                self.assertTrue(ok, f"{side}: bootrom would drop this block — {why}")

    def test_the_shipped_bug_is_rejected(self):
        """The v0.23.0–v0.27.1 shape must FAIL, or this suite proves nothing.

        Same record, same address, only payloadSize set to the record's own
        length — which is exactly what three releases published.
        """
        stamp_off, magic = uf2.firmware_constants()
        rec = uf2.stamp_record(True, magic)
        broken = struct.pack("<8I476sI", UF2_MAGIC0, UF2_MAGIC1, FLAG_FAMILY_ID,
                             XIP_BASE + stamp_off, len(rec), 0, 1, RP2040_FAMILY_ID,
                             rec.ljust(476, b"\x00"), UF2_MAGIC_END)
        ok, why = bootrom_accepts(broken, 0, 1)
        self.assertFalse(ok, "the released shape must not pass")
        self.assertIn("payloadSize 12", why)

    def test_the_tool_refuses_to_emit_a_bad_block(self):
        stamp_off, magic = uf2.firmware_constants()
        rec = uf2.stamp_record(True, magic)
        with self.assertRaises(SystemExit):           # short payload
            uf2.uf2_block(XIP_BASE + stamp_off, rec, 0, 1)
        with self.assertRaises(SystemExit):           # unaligned target
            uf2.uf2_block(XIP_BASE + stamp_off + 1, uf2.stamp_page(True, magic), 0, 1)

    def test_verify_rejects_a_released_style_file(self):
        """verify() must audit the CONTAINER, not just the record inside it."""
        stamp_off, magic = uf2.firmware_constants()
        rec = uf2.stamp_record(True, magic)
        broken = struct.pack("<8I476sI", UF2_MAGIC0, UF2_MAGIC1, FLAG_FAMILY_ID,
                             XIP_BASE + stamp_off, len(rec), 0, 1, RP2040_FAMILY_ID,
                             rec.ljust(476, b"\x00"), UF2_MAGIC_END)
        import tempfile
        with tempfile.TemporaryDirectory() as d:
            p = pathlib.Path(d) / "released-style.uf2"
            p.write_bytes(broken)
            with self.assertRaises(SystemExit):
                uf2.verify(p)


class RecordContents(unittest.TestCase):
    def test_record_matches_what_the_firmware_reads(self):
        """magic / is_left / CRC as base/hand_stamp.c's stamp_valid() checks them.

        The CRC is recomputed here with zlib rather than reusing the tool's, and
        crc32_1byte(p, 8, 0) is standard zlib CRC-32: it seeds with ~previousCrc32
        and returns ~crc, so seed 0 is the usual init/final inversion pair.
        """
        for side, want_left in (("left", 1), ("right", 0)):
            with self.subTest(side=side):
                block, _, magic = build(side)
                page = block[32:32 + PAGE]
                got_magic, is_left, crc = struct.unpack_from("<IB3xI", page)
                self.assertEqual(got_magic, magic)
                self.assertEqual(is_left, want_left)
                self.assertEqual(crc, zlib.crc32(page[:8]))

    def test_provenance_byte_is_in_the_crc_span(self):
        """pad[0] = 0x55 marks a tool-written record; the banner prints it.

        It has to be covered by the CRC, or a record could carry a provenance the
        firmware never validated. The span is 8 bytes = magic + is_left + pad.
        """
        block, _, _ = build("left")
        page = block[32:32 + PAGE]
        self.assertEqual(page[5], uf2.WRITER_UF2)
        self.assertEqual(struct.unpack_from("<I", page, 8)[0], zlib.crc32(page[:8]))

    def test_page_tail_is_erased_not_zeroed(self):
        """A UF2-written page must be byte-identical to one stamp_write() makes,
        and that pads with 0xFF (hand_stamp.c memsets the page before the record).
        The bootrom programs a full 256 bytes regardless of payloadSize, so the
        tail really does reach flash."""
        block, _, _ = build("left")
        self.assertEqual(set(block[32 + 12:32 + PAGE]), {0xFF})


class FlashMap(unittest.TestCase):
    def test_target_is_the_stamp_sector_and_sector_aligned(self):
        """FW_HAND_STAMP_OFFSET is derived by subtraction from the regions above
        it, so its 4096-alignment rides on constants that can move. The bootrom
        erases a whole sector before programming, so a misaligned stamp would take
        a neighbour's sector with it."""
        stamp_off, _ = uf2.firmware_constants()
        self.assertEqual(stamp_off % 4096, 0, "stamp sector is not 4096-aligned")
        self.assertEqual(stamp_off % PAGE, 0)


if __name__ == "__main__":
    unittest.main()
