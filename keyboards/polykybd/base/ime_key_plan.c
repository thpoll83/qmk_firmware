// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#include "ime_key_plan.h"

uint8_t ime_ja_next(uint8_t current, bool shift) {
    if (shift) {
        return IME_JA_KATAKANA;
    }
    return (current == IME_JA_OFF) ? IME_JA_HIRAGANA : IME_JA_OFF;
}

static ime_stroke_t stroke(uint8_t usage, uint8_t mods, bool hold) {
    ime_stroke_t s = {usage, mods, hold};
    return s;
}

static ime_stroke_t korean_stroke(uint8_t os) {
    if (os == IME_OS_MACOS) {
        // The macOS Korean input source ignores LANG1 (that is the JIS かな key
        // there). Ctrl+Space is the system default "select the previous input
        // source", which toggles Korean <-> ABC when those are the two in use, and
        // works without the optional "Caps Lock switches to and from ABC" setting.
        return stroke(IME_HID_SPACE, IME_MOD_LCTL, false);
    }
    // Windows: LANG1 reaches the Microsoft Korean IME as VK_HANGUL.
    // Linux/Android: KEY_HANGEUL, bound by ibus-hangul and fcitx5-hangul.
    // HELD, not tapped, so a long press behaves like the physical 한/영 key.
    return stroke(IME_HID_LANG1, 0, true);
}

static ime_stroke_t japanese_stroke(uint8_t os, uint8_t mode) {
    if (os == IME_OS_MACOS) {
        switch (mode) {
            case IME_JA_HIRAGANA: return stroke(IME_HID_LANG1, 0, false);  // かな
            case IME_JA_KATAKANA: return stroke(IME_HID_K, IME_MOD_LCTL | IME_MOD_LSFT, false);  // ⌃⇧K
            default:              return stroke(IME_HID_LANG2, 0, false);  // 英数
        }
    }
    // MS-IME (and mozc's MS-IME keymap): カタカナ/ひらがな selects hiragana and
    // Shift+ it katakana. 無変換 turns the IME OFF only with MS-IME's
    // "無変換: IME-オフ" key setting; there is no absolute off key by default.
    switch (mode) {
        case IME_JA_HIRAGANA: return stroke(IME_HID_INT2, 0, false);
        case IME_JA_KATAKANA: return stroke(IME_HID_INT2, IME_MOD_LSFT, false);
        default:              return stroke(IME_HID_INT5, 0, false);
    }
}

ime_stroke_t ime_key_stroke(uint8_t family, uint8_t os, bool shift, uint8_t* ja_mode) {
    switch (family) {
        case IME_FAMILY_KOREAN:
            return korean_stroke(os);
        case IME_FAMILY_JAPANESE: {
            const uint8_t next = ime_ja_next(ja_mode ? *ja_mode : IME_JA_OFF, shift);
            if (ja_mode) {
                *ja_mode = next;
            }
            return japanese_stroke(os, next);
        }
        default:
            return stroke(IME_HID_NUBS, 0, true);
    }
}
