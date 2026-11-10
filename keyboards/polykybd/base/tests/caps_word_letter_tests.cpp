// Copyright 2026 Thomas Pollak
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Tests for base/caps_word_letter.h: which keys Caps Word treats as letters.

#include "gtest/gtest.h"

extern "C" {
#include "caps_word_letter.h"
}

namespace {

TEST(CapsWordLetter, LatinLetters) {
    EXPECT_TRUE(is_case_pair('a', 'A'));
    EXPECT_TRUE(is_case_pair('z', 'Z'));
    EXPECT_TRUE(is_case_pair(0xF6, 0xD6));     // ö Ö
    EXPECT_TRUE(is_case_pair(0xE9, 0xC9));     // é É
    EXPECT_TRUE(is_case_pair(0x105, 0x104));   // ą Ą
    EXPECT_TRUE(is_case_pair(0x15F, 0x15E));   // ş Ş
    EXPECT_TRUE(is_case_pair(0x131, 'I'));     // ı I
    EXPECT_TRUE(is_case_pair('i', 0x130));     // i İ
    EXPECT_TRUE(is_case_pair(0x1EA1, 0x1EA0)); // ạ Ạ
}

TEST(CapsWordLetter, OtherCasedScripts) {
    EXPECT_TRUE(is_case_pair(0x3B1, 0x391));   // α Α
    EXPECT_TRUE(is_case_pair(0x3C2, 0x3A3));   // ς Σ
    EXPECT_TRUE(is_case_pair(0x430, 0x410));   // а А
    EXPECT_TRUE(is_case_pair(0x451, 0x401));   // ё Ё
    EXPECT_TRUE(is_case_pair(0x456, 0x406));   // і І
    EXPECT_TRUE(is_case_pair(0x561, 0x531));   // ա Ա
    EXPECT_TRUE(is_case_pair(0x491, 0x490));   // ґ Ґ
    EXPECT_TRUE(is_case_pair(0x4D9, 0x4D8));   // ә Ә
    EXPECT_TRUE(is_case_pair(0x259, 0x18F));   // ə Ə
}

TEST(CapsWordLetter, PunctuationAndDigitsAreNotLetters) {
    // de-DE's comma and period: the bug this header fixes.
    EXPECT_FALSE(is_case_pair(',', ';'));
    EXPECT_FALSE(is_case_pair('.', ':'));
    EXPECT_FALSE(is_case_pair('1', '!'));
    EXPECT_FALSE(is_case_pair('-', '_'));
    EXPECT_FALSE(is_case_pair(0xDF, '?'));     // ß ?
    EXPECT_FALSE(is_case_pair(0xF7, 0xD7));    // ÷ × are not letters
    EXPECT_FALSE(is_case_pair(0xE9, '2'));     // fr-FR é / 2: a letter, but Shift types 2
    EXPECT_FALSE(is_case_pair('a', 'B'));
    EXPECT_FALSE(is_case_pair(0x5D0, 0x5D1));  // Hebrew has no case
    EXPECT_FALSE(is_case_pair(0, 0));
}

TEST(CapsWordLetter, LegendControlsAreSkipped) {
    static const uint32_t o_small[] = {0x0C, 0x0C, 0xF6, 0};   // "\f\fö"
    static const uint32_t o_big[]   = {0xD6, 0};
    static const uint32_t comma[]   = {',', 0};
    static const uint32_t semi[]    = {';', 0};
    EXPECT_TRUE(caps_word_is_letter(o_small, o_big));
    EXPECT_FALSE(caps_word_is_letter(comma, semi));
    EXPECT_FALSE(caps_word_is_letter(nullptr, o_big));
    EXPECT_EQ(legend_first_char(o_small), 0xF6u);
}

} // namespace
