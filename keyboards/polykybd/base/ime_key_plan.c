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
    ime_stroke_t s = {usage, mods, hold, false};
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
    if (os == IME_OS_WINDOWS) {
        // MS-IME's mode keys for the English (101/102) layout, all on Caps Lock, so
        // neither the Japanese (106/109) layout nor any IME setting is needed. The
        // 無変換 / カタカナひらがな keys below exist only on that layout: on the
        // English one Windows ignored them entirely (field test, 2026-10-09, which
        // also confirmed all three of these). They are ABSOLUTE, like the Mac's:
        //   Shift+Caps   alphanumeric (英数, indicator "A")
        //   Ctrl+Caps    hiragana
        //   Alt+Caps     katakana
        // Shift+tap asks for katakana, so the user's own Shift is LIFTED for that
        // stroke: the host must see Alt+Caps, not Shift+Alt+Caps.
        switch (mode) {
            case IME_JA_HIRAGANA: return stroke(IME_HID_CAPS, IME_MOD_LCTL, false);
            case IME_JA_KATAKANA: {
                ime_stroke_t s = stroke(IME_HID_CAPS, IME_MOD_LALT, false);
                s.drop_shift = true;
                return s;
            }
            default:              return stroke(IME_HID_CAPS, IME_MOD_LSFT, false);
        }
    }
    // Linux/Android/unknown (mozc, and MS-IME on a Japanese layout): カタカナ/
    // ひらがな selects hiragana and Shift+ it katakana. 無変換 turns MS-IME OFF only
    // with its "無変換: IME-オフ" key setting.
    switch (mode) {
        case IME_JA_HIRAGANA: return stroke(IME_HID_INT2, 0, false);
        case IME_JA_KATAKANA: return stroke(IME_HID_INT2, IME_MOD_LSFT, false);
        default:              return stroke(IME_HID_INT5, 0, false);
    }
}

uint8_t ime_key_stand_in(uint8_t family, uint8_t os) {
    switch (family) {
        case IME_FAMILY_KOREAN:
        case IME_FAMILY_JAPANESE: return 0;
        // On macOS the key is Right OPTION, which is what the board's GUI key is
        // there: poly_keymap.c swaps KC_RGUI to Right Alt, and keycode_helper.c draws
        // KC_RGUI as ⌥. Standing in for KC_RALT would have made it a second Cmd,
        // which macOS gives no meaning of its own; Option is the key that types
        // characters (⌥E, ⌥2), the same job Right Alt does as AltGr or Compose.
        case IME_FAMILY_RALT:     return os == IME_OS_MACOS ? IME_HID_RGUI : IME_HID_RALT;
        default:                  return IME_HID_NUBS;
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
        case IME_FAMILY_RALT:
            // Right Alt on EVERY OS. macOS reads it as Right Option, which is what
            // the stand-in KC_RGUI sends through the swap there (ime_key_stand_in).
            // HELD: a modifier must stay down while the finger does.
            (void)os;
            return stroke(IME_HID_RALT, 0, true);
        default:
            return stroke(IME_HID_NUBS, 0, true);
    }
}
