"""Unit tests for the Shift-preview redundancy rule (lang/shift_preview.py).

Run: python3 -m unittest discover -s keyboards/polykybd/lang/tests -p "*_test.py"
"""
import os, sys, unittest
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import shift_preview as sp

NAMES = {
    'UMLAUT_A_SMALL': 0xE4, 'UMLAUT_A': 0xC4,
    'C_WITH_CEDILLA_SMALL': 0xE7, 'C_WITH_CEDILLA': 0xC7,
    'CYRILLIC_SM_HA': 0x445, 'CYRILLIC_HA': 0x425,
    'ESZETT': 0xDF, 'SECTION': 0xA7, 'QUOTE': 0x22,
}


class ResolveCellTest(unittest.TestCase):
    def test_named_glyph(self):
        self.assertEqual(sp.resolve_cell('UMLAUT_A_SMALL', NAMES), [0xE4])

    def test_literal(self):
        self.assertEqual(sp.resolve_cell('U"a"', NAMES), [0x61])

    def test_literal_prefix_then_name(self):
        # the shape most base cells take after a tuning pass
        self.assertEqual(sp.resolve_cell('U"\\f\\f" UMLAUT_A_SMALL', NAMES),
                         [0x0C, 0x0C, 0xE4])

    def test_hex_escapes(self):
        self.assertEqual(sp.resolve_cell('U"\\x06\\x06?"', NAMES), [6, 6, ord('?')])

    def test_bare_character(self):
        self.assertEqual(sp.resolve_cell('?', NAMES), [ord('?')])

    def test_empty_and_null(self):
        for v in (None, '', '   ', 'NULL'):
            self.assertIsNone(sp.resolve_cell(v, NAMES))

    def test_unknown_name_is_unresolvable(self):
        # fail SAFE: an unrecognised token must not resolve to something plausible
        self.assertIsNone(sp.resolve_cell('NO_SUCH_GLYPH', NAMES))
        self.assertIsNone(sp.resolve_cell('U"\\f" NO_SUCH_GLYPH', NAMES))
        # ...in the GAP position too (a name BEFORE a literal), which is a separate
        # branch of the walk and was the one mutation testing found untested.
        self.assertIsNone(sp.resolve_cell('NO_SUCH_GLYPH U"a"', NAMES))
        self.assertEqual(sp.resolve_cell('ESZETT U"a"', NAMES), [0xDF, 0x61])

    def test_a_name_may_map_to_a_SEQUENCE_of_codepoints(self):
        """The build-time emitter's table comes from the sheet (one int per name);
        the host preview's comes from named_glyphs.h, where a macro can expand to
        several codepoints. Both feed THIS resolver, so both shapes must work -- the
        alternative is a second resolver, and two resolvers drift (measured: 59 of
        ~3300 keys, when the emitter's own table silently lost 942 formula cells)."""
        names = dict(NAMES, MULTI=(0x0C, 0xE4))
        self.assertEqual(sp.resolve_cell('MULTI', names), [0x0C, 0xE4])
        self.assertEqual(sp.resolve_cell('MULTI U"a"', names), [0x0C, 0xE4, 0x61])
        # ...and the cursor op still does not count as a glyph, so the pair reads
        # exactly as the single-codepoint form would.
        self.assertTrue(sp.preview_is_redundant('MULTI', 'UMLAUT_A', names))

    def test_bad_escape_is_unresolvable(self):
        self.assertIsNone(sp.resolve_cell('U"\\q"', NAMES))
        self.assertIsNone(sp.resolve_cell('U"\\x"', NAMES))


class RedundancyTest(unittest.TestCase):
    def red(self, b, s):
        return sp.preview_is_redundant(b, s, NAMES)

    def test_case_pair_is_redundant(self):
        self.assertTrue(self.red('UMLAUT_A_SMALL', 'UMLAUT_A'))
        self.assertTrue(self.red('U"\\f\\f" UMLAUT_A_SMALL', 'UMLAUT_A'))
        self.assertTrue(self.red('CYRILLIC_SM_HA', 'CYRILLIC_HA'))

    def test_the_cursor_ops_do_not_count_as_glyphs(self):
        # the base cell almost always carries a leading nudge; it must not make the
        # comparison "two glyphs" and silently keep every preview.
        self.assertTrue(self.red('U"\\f\\f\\f" C_WITH_CEDILLA_SMALL', 'C_WITH_CEDILLA'))

    def test_inverted_pair_is_redundant(self):
        # da-DK / fo-FO store the CAPITAL as the base and the small form as Shift
        self.assertTrue(self.red('U"\\xD8"', 'U"\\xF8"'))

    def test_identical_glyphs_are_redundant(self):
        self.assertTrue(self.red('U"^"', 'U"^"'))                    # fr-CA dead key
        self.assertTrue(self.red('UMLAUT_A_SMALL', 'UMLAUT_A_SMALL'))  # hu-HU duplicate

    def test_a_different_character_is_NOT_redundant(self):
        self.assertFalse(self.red('U"1"', 'U"!"'))
        self.assertFalse(self.red('U"7"', 'U"&"'))
        self.assertFalse(self.red('UMLAUT_A_SMALL', 'ESZETT'))

    def test_a_caseless_script_is_NOT_redundant(self):
        # Hebrew alef has no case, so upper()==itself -- but so does the base, and
        # the pair is only redundant when they are the SAME character.
        self.assertFalse(self.red('U"\\x5D0"', 'U"\\x5D1"'))

    def test_multi_glyph_legends_are_NOT_redundant(self):
        # two printable glyphs on either side -> cannot be a simple case pair
        self.assertFalse(self.red('U"ab"', 'U"AB"'))

    def test_missing_cells_are_NOT_redundant(self):
        self.assertFalse(self.red(None, 'UMLAUT_A'))
        self.assertFalse(self.red('UMLAUT_A_SMALL', None))

    def test_unresolvable_cells_fail_SAFE(self):
        # an unknown token must keep the preview, never hide it
        self.assertFalse(self.red('NO_SUCH_GLYPH', 'UMLAUT_A'))
        self.assertFalse(self.red('UMLAUT_A_SMALL', 'NO_SUCH_GLYPH'))


class CodepointRuleTest(unittest.TestCase):
    """The rule itself, on resolved legends -- the entry point oled_preview.py uses,
    so that the host picture and the keycap cannot disagree about which previews the
    firmware suppresses."""

    def test_it_agrees_with_the_cell_form(self):
        for base, shift in (('UMLAUT_A_SMALL', 'UMLAUT_A'),
                            ('U"\\f\\f" UMLAUT_A_SMALL', 'UMLAUT_A'),
                            ('U"1"', 'U"!"'),
                            ('U"ab"', 'U"AB"'),
                            (None, 'UMLAUT_A')):
            self.assertEqual(
                sp.preview_is_redundant(base, shift, NAMES),
                sp.redundant_from_codepoints(sp.resolve_cell(base, NAMES),
                                             sp.resolve_cell(shift, NAMES)),
                f"{base!r} / {shift!r}")

    def test_leading_cursor_ops_are_ignored(self):
        self.assertTrue(sp.redundant_from_codepoints([0x0C, 0x0C, 0xE4], [0xC4]))

    def test_none_is_not_redundant(self):
        self.assertFalse(sp.redundant_from_codepoints(None, [0xC4]))
        self.assertFalse(sp.redundant_from_codepoints([0xE4], None))


if __name__ == '__main__':
    unittest.main()
