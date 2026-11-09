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


class FirmwareLimitTest(unittest.TestCase):
    def test_max_cps_matches_the_firmware(self):
        """build() refuses what emjdict_load_at() would refuse."""
        import re
        with open(os.path.join(HERE, "..", "..", "base", "emoji_dict.h"), encoding="utf-8") as f:
            m = re.search(r"#define\s+EMJDICT_MAX_CPS\s+(\d+)u?", f.read())
        self.assertIsNotNone(m)
        self.assertEqual(ed.MAX_CPS, int(m.group(1)))
        self.assertLessEqual(ed.MAX_CPS, ed.POST_INDEX_MASK + 1)


class UserBuildTest(unittest.TestCase):
    def test_user_build_keeps_the_number_and_takes_revision_255(self):
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            out = os.path.join(tmp, "de.plyd")
            self.assertEqual(ed.main(["build", "de", "--user", "-o", out]), 0)
            with open(out, "rb") as f:
                ver = struct.unpack_from("<I", f.read(), 8)[0]
        self.assertEqual(ver, (ed.content_version("de") & 0xFF00) | ed.USER_REVISION)
        self.assertNotEqual(ver, ed.content_version("de"))

    def test_shipped_revisions_stay_below_the_user_revision(self):
        for d in ed.dictionaries():
            self.assertLess(d["revision"], ed.USER_REVISION, d["id"])


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

    def test_hangul_is_stored_as_its_keys(self):
        # Stored in the conjoining jamo the ko-KR keys show (HANGEUL_* in lang_lut.c).
        J = lambda compat: "".join(ed._TO_CONJOINING.get(c, c) for c in compat)
        self.assertEqual(ed.hangul_keys("한국"), J("ㅎㅏㄴㄱㅜㄱ"))
        self.assertEqual(ed.hangul_keys("왜 닭"), J("ㅇㅗㅐ ㄷㅏㄹㄱ"))   # compound vowel and final: two keys
        self.assertEqual(ed.hangul_keys("한")[0], "\u1112")                # ᄒ, the key's legend
        # Shift doubles fold to their base key, like other letters to their base letter.
        self.assertEqual(ed.fold("까치"), J("ㄱㅏㅊㅣ"))
        self.assertEqual(ed.fold_letter("\u1168"), "\u1166")             # ᅨ -> ᅦ

    def test_indic_vowel_signs_stay(self):
        self.assertEqual(ed.fold("புன்னகை"), "புன்னகை")


class RomanizeTest(unittest.TestCase):
    """Keywords written as the keys that type them (emoji_romanize.py)."""

    def test_kana_to_romaji(self):
        R = ed.emoji_romanize.kana_to_romaji
        self.assertEqual(R("スマイル"), "sumairu")
        self.assertEqual(R("にっこり"), "nikkori")        # small tsu doubles the consonant
        self.assertEqual(R("コーヒー"), "kohi")            # the long-vowel mark is dropped
        self.assertEqual(R("ちゃ"), "cha")
        self.assertIsNone(R("笑顔"))                      # kanji: no reading here

    def test_pinyin_to_zhuyin(self):
        Z = ed.emoji_romanize.pinyin_to_zhuyin
        self.assertEqual(Z("xiao"), "ㄒㄧㄠ")
        self.assertEqual(Z("shi"), "ㄕ")
        self.assertEqual(Z("ju"), "ㄐㄩ")                  # j/q/x + u is ü
        self.assertEqual(Z("you"), "ㄧㄡ")
        self.assertEqual(Z("lü"), "ㄌㄩ")

    def test_han_to_keys(self):
        class FakeUnihan:
            pinyin = {"笑": "xiào", "脸": "liǎn", "绿": "lǜ"}
            cangjie = {"笑": "HHK"}
        H = ed.emoji_romanize.han_to
        self.assertEqual(H("pinyin", "笑脸", FakeUnihan), "xiaolian")
        self.assertEqual(H("pinyin", "绿", FakeUnihan), "lü")
        self.assertEqual(H("zhuyin", "笑脸", FakeUnihan), "ㄒㄧㄠㄌㄧㄢ")
        self.assertEqual(H("cangjie", "笑", FakeUnihan), "hhk")
        self.assertIsNone(H("cangjie", "脸", FakeUnihan))   # no reading: keyword dropped

    def test_dictionary_ids(self):
        self.assertEqual(ed.parse_id("zh-HK@cangjie"), ("zh", "HK", "cangjie"))
        self.assertEqual(ed.parse_id("de"), ("de", "", ""))
        self.assertEqual(ed.variant_code("", ""), 0)
        v = ed.variant_code("HK", "cangjie")
        self.assertEqual(ed.iso_lang_country.COUNTRY_CODES[(v >> 8) - 1], "HK")
        self.assertEqual(ed.emoji_romanize.METHODS[(v & 0xFF) - 1], "cangjie")
        with self.assertRaises(SystemExit):
            ed.parse_id("zh@wubi")


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

    def test_german_finds_common_english_words(self):
        for q, want in (("smile", "😀"), ("happy", "😀"), ("love", "😍"), ("cat", "😸")):
            self.assertIn(ord(want), self.dicts["de"].search(q)[0], q)
        # The language's own name words still rank first.
        self.assertEqual(self._first("de", "katze", 1), ["😸"])

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

    def test_words_are_six_bit(self):
        self.assertEqual(ed.unpack_word(ed.pack_word([1, 63, 5]), 0, 99), bytes([1, 63, 5]))
        self.assertEqual(len(ed.pack_word([1] * 4)), 4)       # 5 codes x 6 bits = 30 bits -> 4 bytes
        for d in self.dicts.values():
            self.assertLessEqual(len(d.alphabet), 63)

    def test_only_typeable_words(self):
        # en and de keyboards type Latin letters only, and German folds ä ö ü ß.
        for d in self.dicts.values():
            self.assertTrue(set(d.alphabet) <= set("abcdefghijklmnopqrstuvwxyz"), d.alphabet)
        ru = ed.typeable_letters("ru", "")
        self.assertIn("ж", ru)
        self.assertNotIn("a", ru)                            # no Latin on a Russian layout
        self.assertIn("\u2F47", ed.typeable_letters("zh", "HK"))   # ⽇, the Cangjie key A
        self.assertIn("ㄅ", ed.typeable_letters("zh", "TW"))   # on the number row, moved in search mode
        self.assertEqual(ed.key_remap("zh", "HK")["a"], "\u2F47")
        self.assertEqual(ed.key_remap("zh", "CN"), {})

    def test_english_by_key_position_on_non_latin_layouts(self):
        latin = ed.key_remap("ru", "")
        self.assertEqual("".join(latin[c] for c in "smile"), "ыьшду")
        # German types every Latin letter, so its English words keep their letters
        # (the y/z swap of QWERTZ is not applied to them).
        self.assertTrue(set("abcdefghijklmnopqrstuvwxyz") <= ed.typeable_letters("de", ""))
        self.assertIn(ed.fold("smile").encode() and self.dicts["de"].encode("smile"), self.dicts["de"].words)

    def test_dictionary_list_and_content_version(self):
        ds = ed.dictionaries()
        self.assertEqual(ds[0]["id"], "en")
        self.assertEqual([d["number"] for d in ds], list(range(len(ds))))
        self.assertEqual(len({d["id"] for d in ds}), len(ds))
        self.assertEqual(ed.content_version("de"), (1 << 8) | ds[1]["revision"])
        for d in ds:                                            # every listed source exists
            self.assertTrue((ed.SRC_DIR / f"{d['id']}.tsv").exists(), d["id"])

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
