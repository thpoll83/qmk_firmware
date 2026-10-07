// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#include "gtest/gtest.h"

extern "C" {
#include "ime_key_plan.h"
}

namespace {

const uint8_t kAllOs[] = {IME_OS_UNKNOWN, IME_OS_WINDOWS, IME_OS_MACOS, IME_OS_LINUX,
                          IME_OS_ANDROID, IME_OS_LINUX_GNOME, IME_OS_LINUX_KDE};

ime_stroke_t Press(uint8_t family, uint8_t os, bool shift, uint8_t* mode) {
    return ime_key_stroke(family, os, shift, mode);
}

// ---- no IME: the key is the NUBS the layout carried before ----------------------

TEST(ImeKeyNone, IsAHeldNubsOnEveryOs) {
    for (uint8_t os : kAllOs) {
        SCOPED_TRACE(int(os));
        for (bool shift : {false, true}) {
            uint8_t mode = IME_JA_HIRAGANA;
            const ime_stroke_t s = Press(IME_FAMILY_NONE, os, shift, &mode);
            EXPECT_EQ(s.usage, IME_HID_NUBS);
            EXPECT_EQ(s.mods, 0u);           // Shift+NUBS stays the user's own Shift
            EXPECT_TRUE(s.hold);             // auto-repeat like any key
            EXPECT_EQ(mode, IME_JA_HIRAGANA);  // the Japanese belief is not touched
        }
    }
}

// ---- Korean: a toggle ------------------------------------------------------------

TEST(ImeKeyKorean, HeldLang1OffMac) {
    for (uint8_t os : kAllOs) {
        if (os == IME_OS_MACOS) continue;
        SCOPED_TRACE(int(os));
        const ime_stroke_t s = Press(IME_FAMILY_KOREAN, os, false, nullptr);
        EXPECT_EQ(s.usage, IME_HID_LANG1);
        EXPECT_EQ(s.mods, 0u);
        EXPECT_TRUE(s.hold);
    }
}

TEST(ImeKeyKorean, CtrlSpaceTapOnMac) {
    const ime_stroke_t s = Press(IME_FAMILY_KOREAN, IME_OS_MACOS, false, nullptr);
    EXPECT_EQ(s.usage, IME_HID_SPACE);
    EXPECT_EQ(s.mods, IME_MOD_LCTL);
    EXPECT_FALSE(s.hold);
}

TEST(ImeKeyKorean, LeavesJapaneseBeliefAlone) {
    uint8_t mode = IME_JA_KATAKANA;
    Press(IME_FAMILY_KOREAN, IME_OS_WINDOWS, true, &mode);
    EXPECT_EQ(mode, IME_JA_KATAKANA);
}

// ---- Japanese: absolute targets ---------------------------------------------------

TEST(ImeKeyJapanese, PlainTapTogglesOffAndHiragana) {
    EXPECT_EQ(ime_ja_next(IME_JA_OFF, false), IME_JA_HIRAGANA);
    EXPECT_EQ(ime_ja_next(IME_JA_HIRAGANA, false), IME_JA_OFF);
    EXPECT_EQ(ime_ja_next(IME_JA_KATAKANA, false), IME_JA_OFF);
}

TEST(ImeKeyJapanese, ShiftTapIsAlwaysKatakana) {
    for (uint8_t m : {IME_JA_OFF, IME_JA_HIRAGANA, IME_JA_KATAKANA}) {
        EXPECT_EQ(ime_ja_next(m, true), IME_JA_KATAKANA);
    }
}

TEST(ImeKeyJapanese, MacSendsEisuKanaAndCtrlShiftK) {
    uint8_t mode = IME_JA_OFF;
    ime_stroke_t s = Press(IME_FAMILY_JAPANESE, IME_OS_MACOS, false, &mode);
    EXPECT_EQ(mode, IME_JA_HIRAGANA);
    EXPECT_EQ(s.usage, IME_HID_LANG1);
    EXPECT_EQ(s.mods, 0u);

    s = Press(IME_FAMILY_JAPANESE, IME_OS_MACOS, false, &mode);
    EXPECT_EQ(mode, IME_JA_OFF);
    EXPECT_EQ(s.usage, IME_HID_LANG2);

    s = Press(IME_FAMILY_JAPANESE, IME_OS_MACOS, true, &mode);
    EXPECT_EQ(mode, IME_JA_KATAKANA);
    EXPECT_EQ(s.usage, IME_HID_K);
    EXPECT_EQ(s.mods, IME_MOD_LCTL | IME_MOD_LSFT);
}

TEST(ImeKeyJapanese, OtherOsSendMsImeKeys) {
    for (uint8_t os : kAllOs) {
        if (os == IME_OS_MACOS) continue;
        SCOPED_TRACE(int(os));
        uint8_t mode = IME_JA_OFF;
        ime_stroke_t s = Press(IME_FAMILY_JAPANESE, os, false, &mode);
        EXPECT_EQ(s.usage, IME_HID_INT2);
        EXPECT_EQ(s.mods, 0u);

        s = Press(IME_FAMILY_JAPANESE, os, true, &mode);
        EXPECT_EQ(s.usage, IME_HID_INT2);
        EXPECT_EQ(s.mods, IME_MOD_LSFT);

        s = Press(IME_FAMILY_JAPANESE, os, false, &mode);
        EXPECT_EQ(s.usage, IME_HID_INT5);
        EXPECT_EQ(mode, IME_JA_OFF);
    }
}

TEST(ImeKeyJapanese, EveryStrokeIsATap) {
    // Absolute targets: a repeat would only re-send the same target, and a tap
    // keeps the release edge free of state the press already consumed.
    for (uint8_t os : kAllOs) {
        for (uint8_t m : {IME_JA_OFF, IME_JA_HIRAGANA, IME_JA_KATAKANA}) {
            for (bool shift : {false, true}) {
                uint8_t mode = m;
                EXPECT_FALSE(Press(IME_FAMILY_JAPANESE, os, shift, &mode).hold);
            }
        }
    }
}

TEST(ImeKeyJapanese, NullBeliefStartsFromOff) {
    const ime_stroke_t s = Press(IME_FAMILY_JAPANESE, IME_OS_WINDOWS, false, nullptr);
    EXPECT_EQ(s.usage, IME_HID_INT2);   // off -> hiragana
}

// ---- Right Alt: the ANSI layouts, every OS -----------------------------------------

TEST(ImeKeyRalt, HeldRightAltOffMac) {
    for (uint8_t os : kAllOs) {
        if (os == IME_OS_MACOS) continue;
        SCOPED_TRACE(int(os));
        for (bool shift : {false, true}) {
            uint8_t mode = IME_JA_HIRAGANA;
            const ime_stroke_t s = Press(IME_FAMILY_RALT, os, shift, &mode);
            EXPECT_EQ(s.usage, IME_HID_RALT);
            EXPECT_EQ(s.mods, 0u);
            EXPECT_TRUE(s.hold);               // a modifier stays down with the finger
            EXPECT_EQ(mode, IME_JA_HIRAGANA);  // the Japanese belief is not touched
        }
    }
}

TEST(ImeKeyRalt, FollowsTheMacSwapLikeTheBoardsAltKey) {
    const ime_stroke_t s = Press(IME_FAMILY_RALT, IME_OS_MACOS, false, nullptr);
    EXPECT_EQ(s.usage, IME_HID_RGUI);
    EXPECT_TRUE(s.hold);
}

// ---- the display stand-in agrees with the stroke -----------------------------------

TEST(ImeKeyStandIn, NamesWhatTheKeySendsOffMac) {
    for (uint8_t fam : {IME_FAMILY_NONE, IME_FAMILY_KOREAN, IME_FAMILY_JAPANESE, IME_FAMILY_RALT}) {
        for (uint8_t os : kAllOs) {
            if (os == IME_OS_MACOS) continue;   // the swap is the renderer's job there
            for (bool shift : {false, true}) {
                SCOPED_TRACE(::testing::Message() << int(fam) << "/" << int(os) << "/" << shift);
                uint8_t mode = IME_JA_OFF;
                const ime_stroke_t s = Press(fam, os, shift, &mode);
                const uint8_t in = ime_key_stand_in(fam);
                if (in) {
                    EXPECT_EQ(s.usage, in);
                    EXPECT_TRUE(s.hold);
                } else {
                    EXPECT_NE(s.usage, IME_HID_NUBS);
                    EXPECT_NE(s.usage, IME_HID_RALT);
                }
            }
        }
    }
}

} // namespace
