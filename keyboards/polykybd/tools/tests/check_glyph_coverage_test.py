#!/usr/bin/env python3
"""Offline tests for tools/check_glyph_coverage.py.

The check exists because a legend codepoint no font covered HardFaulted the board
(hy-AM's U+2014, 2026-10-01). These pin the parts of the walk that can make it read
LESS than it claims — op arguments mistaken for glyphs (false gaps) or glyph arguments
skipped (missed gaps), a gap record taken as coverage, a comma inside a legend
splitting a cell, an unknown named glyph read as empty — and then run it on the real
tree.

Run: python3 -m unittest discover -s keyboards/polykybd/tools/tests -p '*_test.py'
"""

import importlib.util
import pathlib
import unittest

HERE = pathlib.Path(__file__).resolve().parent
_spec = importlib.util.spec_from_file_location("check_glyph_coverage",
                                               HERE.parent / "check_glyph_coverage.py")
cgc = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(cgc)


class DecodeTest(unittest.TestCase):
    def test_hex_and_named_escapes(self):
        # C hex escapes are GREEDY: "\x06a" is ONE codepoint, 0x6A. That is why the LUT
        # writes U"\x06\x06" ARMENIAN_054A as two literals; the decoder must agree with
        # the compiler, or it checks a legend the board never draws.
        self.assertEqual(cgc.decode_literal(r"\x06\x06a"), [6, 0x6A])
        self.assertEqual(cgc.decode_literal(r"\x06\x06"), [6, 6])
        self.assertEqual(cgc.decode_literal(r"\x2014"), [0x2014])
        self.assertEqual(cgc.decode_literal(r"\f\r\v\\\""), [12, 13, 11, 92, 34])
        self.assertEqual(cgc.decode_literal(r"\U0001F600"), [0x1F600])

    def test_unknown_escape_is_an_error_not_a_skip(self):
        with self.assertRaises(cgc.CoverageError):
            cgc.decode_literal(r"\q")


class ExpandTest(unittest.TestCase):
    DEFS = {"DASH": 'U"\\x2014"', "TWO": 'DASH U"x"'}

    def test_named_glyphs_concatenate_and_nest(self):
        self.assertEqual(cgc.expand_cell('U"\\x06\\x06" DASH', self.DEFS), [6, 6, 0x2014])
        self.assertEqual(cgc.expand_cell("TWO", self.DEFS), [0x2014, ord("x")])
        self.assertEqual(cgc.expand_cell("NULL", self.DEFS), [])

    def test_unknown_named_glyph_fails_the_run(self):
        # Reading it as empty is exactly how a check passes while reading less.
        with self.assertRaises(cgc.CoverageError):
            cgc.expand_cell("NO_SUCH_GLYPH", self.DEFS)


class OpWalkTest(unittest.TestCase):
    def test_argless_ops_are_dropped(self):
        self.assertEqual(cgc.drawn_codepoints([0x06, 0x06, 0x2014]), [0x2014])
        self.assertEqual(cgc.drawn_codepoints([0x0C, 0x0C, 0x05, ord("a")]), [ord("a")])

    def test_coordinate_arguments_are_not_glyphs(self):
        # MOVE (x, y), FRAME (w, h), BADGE (w, h, style): the values may be any byte,
        # including printable ones that would read as a (missing) glyph.
        self.assertEqual(cgc.drawn_codepoints([0x0E, 0x41, 0x7E, ord("z")]), [ord("z")])
        self.assertEqual(cgc.drawn_codepoints([0x12, 40, 30, ord("z")]), [ord("z")])
        self.assertEqual(cgc.drawn_codepoints([0x13, 40, 30, 2, ord("z")]), [ord("z")])

    def test_glyph_arguments_are_resolved(self):
        # HALF/THIN (glyph), ROT (angle, glyph): the glyph IS drawn, so it is checked.
        self.assertEqual(cgc.drawn_codepoints([0x0F, 0x2014]), [0x2014])
        self.assertEqual(cgc.drawn_codepoints([0x11, 0x2026, ord("a")]), [0x2026, ord("a")])
        self.assertEqual(cgc.drawn_codepoints([0x15, 6, 0x2019]), [0x2019])

    def test_truncated_op_consumes_nothing(self):
        # bbox_walk only consumes a complete argument set.
        self.assertEqual(cgc.drawn_codepoints([0x15, 6]), [6] if 6 >= 0x20 else [])
        self.assertEqual(cgc.drawn_codepoints([0x0E, 0x41]), [0x41])


class CellSplitTest(unittest.TestCase):
    def test_comma_inside_a_literal_stays_in_its_cell(self):
        row = ' COMMA, U"$", COMMA, U"4",'
        self.assertEqual(cgc.split_cells(row), ["COMMA", 'U"$"', "COMMA", 'U"4"'])
        row = ' U",", U"\\",x", NULL, NULL,'
        self.assertEqual(cgc.split_cells(row), ['U","', 'U"\\",x"', "NULL", "NULL"])


class FontLookupTest(unittest.TestCase):
    TABLE = [
        ("Icons", 0x100000, 0x100001, [(5, 5, 6), (5, 5, 6)]),
        ("Gappy", 0x100, 0x102, [(3, 5, 4), (0, 0, 0), (3, 5, 4)]),
        ("Filler", 0x101, 0x101, [(7, 8, 9)]),
        ("Latin", 0x20, 0x7E, [(1, 1, 4)] * 0x5F),
    ]

    def test_first_font_in_range_wins(self):
        self.assertEqual(cgc.font_for(self.TABLE, 0x100), "Gappy")
        self.assertEqual(cgc.font_for(self.TABLE, ord("!")), "Latin")

    def test_gap_record_is_not_coverage(self):
        self.assertEqual(cgc.font_for(self.TABLE, 0x101), "Filler")
        table = [t for t in self.TABLE if t[0] != "Filler"]
        self.assertIsNone(cgc.font_for(table, 0x101))

    def test_uncovered_is_none(self):
        self.assertIsNone(cgc.font_for(self.TABLE, 0x2014))


class RealTreeTest(unittest.TestCase):
    def test_the_whole_table_parses(self):
        cells = list(cgc.lut_cells(str(pathlib.Path(cgc.KB) / "lang" / "lang_lut.c")))
        langs = {lang for _, lang, _, _ in cells}
        self.assertEqual(len(langs), 160)
        self.assertEqual(len(cells) % 4, 0)
        self.assertGreater(len(cells), 160 * 4 * 40)

    def test_every_legend_codepoint_is_in_a_font(self):
        gaps, _ = cgc.find_gaps()
        self.assertEqual(gaps, [], "\n".join(f"U+{cp:04X} {lang} {key} ({col})"
                                             for cp, lang, key, col in gaps))


if __name__ == "__main__":
    unittest.main()
