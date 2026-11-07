# Copyright 2026 thpoll83
# SPDX-License-Identifier: GPL-2.0-or-later
"""Tests for keyboards/polykybd/emoji/emoji_dict.py (the emoji search dictionary).

Builds the committed en and de sources and checks the bundle layout the firmware
will read, the 128 KB slot limit, and search results a user would expect.
"""
import os
import struct
import sys
import unittest
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "emoji"))

import emoji_dict as ed  # noqa: E402


class FoldTest(unittest.TestCase):
    def test_latin_greek_cyrillic_lose_diacritics(self):
        self.assertEqual(ed.fold("Lächeln"), "lacheln")
        self.assertEqual(ed.fold("Straße"), "strase")
        self.assertEqual(ed.fold("İstanbul ŀl ŒUF"), "istanbul ll ouf")
        self.assertEqual(ed.fold("e\u0301"), "e")          # decomposed é
        self.assertEqual(ed.fold("ёжик"), "ежик")
        self.assertEqual(ed.fold("καρδιά"), "καρδια")

    def test_one_letter_stays_one_letter(self):
        for c in range(0x41, ed.FOLD_SCRIPT_END):
            if chr(c).isalpha():
                self.assertEqual(len(ed.fold_letter(chr(c))), 1, hex(c))

    def test_indic_vowel_signs_stay(self):
        self.assertEqual(ed.fold("புன்னகை"), "புன்னகை")


class WithinOneTest(unittest.TestCase):
    def test_edits(self):
        for a, b in [(b"grin", b"grin"), (b"grni", b"grin"), (b"grn", b"grin"),
                     (b"griin", b"grin"), (b"gron", b"grin")]:
            self.assertTrue(ed._within_one(a, b), (a, b))
        for a, b in [(b"gnri", b"grin"), (b"gr", b"grin"), (b"xyz", b"grin")]:
            self.assertFalse(ed._within_one(a, b), (a, b))


class BundleTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.blobs = {lang: ed.build(lang, revision=7) for lang in ("en", "de")}
        cls.dicts = {lang: ed.Dict(b) for lang, b in cls.blobs.items()}

    def test_plyf_header(self):
        for lang, blob in self.blobs.items():
            magic, abi, flags, ver, fonts, table, total, crc, _r = struct.unpack_from(
                ed.PLYF_HEADER_FMT, blob)
            self.assertEqual((magic, abi, flags, ver, fonts, table), (b"PlyF", 2, ed.FLAG_DATA, 7, 0, 32))
            self.assertEqual(total, len(blob))
            self.assertEqual(crc, zlib.crc32(blob[32:]) & 0xFFFFFFFF)
            self.assertEqual(len(blob) % 4, 0)

    def test_fits_the_slot(self):
        for lang, blob in self.blobs.items():
            self.assertLessEqual(len(blob), ed.SLOT_SIZE, lang)

    def test_language_index(self):
        self.assertEqual(self.dicts["en"].lang, "en")
        self.assertEqual(self.dicts["de"].lang, "de")

    def test_words_sorted_and_unique(self):
        for d in self.dicts.values():
            self.assertEqual(d.words, sorted(set(d.words)))

    def test_every_picker_codepoint_is_indexed(self):
        picker = ed.picker_codepoints()
        for d in self.dicts.values():
            self.assertEqual(list(d.cps), picker)

    def test_one_byte_per_letter(self):
        d = self.dicts["de"]
        self.assertEqual(d.alphabet, sorted(set(d.alphabet)))
        self.assertLessEqual(len(d.alphabet), ed.ALPHA_MAX)
        for w in d.words[:200]:
            self.assertEqual(len(w), len(d.decode(w)))
            self.assertEqual(d.encode(d.decode(w)), w)
        # byte order of the coded words is the order the binary search relies on
        self.assertEqual([d.decode(w) for w in d.words], sorted(d.decode(w) for w in d.words))

    def test_fold_table_matches_fold(self):
        for d in self.dicts.values():
            self.assertEqual(sorted(d.fold_map), list(d.fold_map))
            for cp, code in d.fold_map.items():
                self.assertEqual(ed.fold_letter(chr(cp)), d.alphabet[code - 1], hex(cp))
            for ch in d.alphabet:
                self.assertIn(ord(ch.upper()) if len(ch.upper()) == 1 else ord(ch), d.fold_map)

    def test_typed_accents_and_case_match_the_base_word(self):
        d = self.dicts["de"]
        for q in ("lächeln", "LÄCHELN", "lacheln", "läçhèln"):
            self.assertIn(ord("😊"), d.search(q)[0], q)

    def test_stop_word_in_query_is_ignored(self):
        self.assertEqual(self._first("en", "face with tears", 1), self._first("en", "face tears", 1))

    def test_stop_word_being_typed_still_searches(self):
        self.assertTrue(self.dicts["en"].search("a")[0])
        self.assertIn(ord("🍎"), self.dicts["en"].search("red a")[0])

    def test_letter_outside_the_alphabet(self):
        d = self.dicts["en"]
        self.assertEqual(d.encode("rαcket")[1], ed.CODE_UNKNOWN)
        cps, typo = d.search("rαcket")
        self.assertTrue(typo)
        self.assertIn(ord("🚀"), cps)

    def test_corrupt_bundle_is_refused(self):
        blob = bytearray(self.blobs["en"])
        blob[100] ^= 1
        with self.assertRaises(ValueError):
            ed.Dict(bytes(blob))

    def _first(self, lang, query, n=3):
        return [chr(c) for c in self.dicts[lang].search(query)[0][:n]]

    def test_english_results(self):
        self.assertIn("😀", self._first("en", "grin"))
        self.assertIn("👍", self._first("en", "thumbs"))
        self.assertEqual(self._first("en", "red heart", 1), ["❤"])
        self.assertEqual(self._first("en", "rocket", 1), ["🚀"])

    def test_typo_fallback(self):
        cps, typo = self.dicts["en"].search("rokcet")
        self.assertTrue(typo)
        self.assertIn(ord("🚀"), cps)

    def test_german_results_and_english_fallback(self):
        self.assertIn("😀", self._first("de", "lachen", 3))
        self.assertEqual(self._first("de", "rotes herz", 1), ["❤"])
        # Dingbat stars have no German CLDR text, so the English words index them.
        self.assertIn("★", self._first("de", "star", 3))


if __name__ == "__main__":
    unittest.main()
