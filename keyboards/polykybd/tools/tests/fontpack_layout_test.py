# Copyright 2026 thpoll83
# SPDX-License-Identifier: GPL-2.0-or-later
"""Tests for fonts/fontpack.py compute_bundle_layout(): slot placement from
fonts.yaml, including reserve_kb / offset_kb (layout v4, the emojidict slot inside
emoji's reserve) and the overlap check."""
import copy
import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
POLY = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(POLY, "fonts"))

import fontpack  # noqa: E402

try:
    import yaml  # noqa: F401  (fontpack._load_yaml reads fonts.yaml with it)
    HAVE_YAML = True
except ImportError:
    HAVE_YAML = False

# Slot starts and sizes before layout v4. Every slot but emoji's size must keep them,
# or a deployed bundle reads as missing and the host reflashes it.
V3 = {"symbol": (0x1000, 0x18000), "mideast": (0x19000, 0x18000),
      "syllabic": (0x31000, 0x18000), "asia": (0x49000, 0x20000),
      "flags": (0x69000, 0x20000), "emoji": (0x89000, 0x80000),
      "fantasy": (0x109000, 0x20000), "latinbig": (0x129000, 0x57000),
      "icons": (0x180000, 0x80000)}


@unittest.skipUnless(HAVE_YAML, "PyYAML is not installed: fonts.yaml cannot be read")
class LayoutTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cfg = fontpack._load_yaml(fontpack.Path(POLY) / "fonts" / "fonts.yaml")
        cls.layout = fontpack.compute_bundle_layout(cls.cfg)
        cls.slots = {s["id"]: s for s in cls.layout["slots"]}

    def test_v3_slots_do_not_move(self):
        for sid, (off, size) in V3.items():
            self.assertEqual(self.slots[sid]["offset"], off, sid)
            if sid != "emoji":
                self.assertEqual(self.slots[sid]["size"], size, sid)

    def test_emojidict_sits_in_emoji_reserve(self):
        e, d = self.slots["emoji"], self.slots["emojidict"]
        self.assertEqual(e["size"], 0x60000)
        self.assertEqual(d["index"], 9)
        self.assertEqual(d["kind"], "emojidict")
        self.assertEqual((d["offset"], d["size"]), (e["offset"] + e["size"], 0x20000))
        self.assertEqual(d["offset"] + d["size"], self.slots["fantasy"]["offset"])

    def test_header_names_the_dictionary_slot(self):
        h = fontpack.bundle_layout_header([], self.layout)
        self.assertIn("#define FONTPACK_EMOJIDICT_BUNDLE_ID 9u", h)
        self.assertIn("#define FONTPACK_ICONS_BUNDLE_ID 8u", h)
        self.assertIn("X(emojidict, 9, 0xE9000UL, 0x20000UL)", h)

    def test_overlap_is_refused(self):
        cfg = copy.deepcopy(self.cfg)
        for e in cfg["bundles"]["list"]:
            if e["id"] == "emojidict":
                e["offset_kb"] = 932 - 4          # one sector into emoji's slot
        with self.assertRaises(ValueError):
            fontpack.compute_bundle_layout(cfg)

    def test_reserve_smaller_than_slot_is_refused(self):
        cfg = copy.deepcopy(self.cfg)
        for e in cfg["bundles"]["list"]:
            if e["id"] == "emoji":
                e["reserve_kb"] = 256
        with self.assertRaises(ValueError):
            fontpack.compute_bundle_layout(cfg)


if __name__ == "__main__":
    unittest.main()
