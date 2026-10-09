// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// KC_IME: one key that switches the input method of the active language, is a
// second Right Alt on the ANSI English layouts, and a plain Non-US Backslash
// everywhere else. This file is the DECISION only —
// which HID usage, with which modifiers, held or tapped — so it links without
// QMK and is unit-tested (make test:polykybd_ime_key_plan). The firmware binding
// is in poly_keymap.c (process_record_user + the display normalisation).
//
// Why the language decides "is there an IME": the firmware cannot see the host's
// input methods, but the host selects ko-KR / ja-JP on the board only when it has
// switched the OS to Korean / Japanese. The ANSI English layouts get Right Alt,
// because their NUBS only repeats Backslash; every other language gets the NUBS
// key the base layout carried before.
//
// Korean has no absolute "Hangul on" key on any OS, so its stroke is a TOGGLE.
// Japanese has absolute keys (Mac 英数/かな, MS-IME katakana/hiragana), so the
// firmware walks its own state and sends a "go to X" each time: a wrong guess
// (the user changed mode with the mouse) costs one press, never a permanent
// inversion the way a toggle would.

#include <stdbool.h>
#include <stdint.h>

// HID keyboard usages. QMK's basic keycodes ARE these values; poly_keymap.c
// static-asserts each against its KC_* name so a typo cannot hide here.
#define IME_HID_K      0x0Eu   // KC_K
#define IME_HID_CAPS   0x39u   // KC_CAPS_LOCK        (英数 on a Japanese layout)
#define IME_HID_SPACE  0x2Cu   // KC_SPACE
#define IME_HID_NUBS   0x64u   // KC_NONUS_BACKSLASH
#define IME_HID_INT2   0x88u   // KC_INTERNATIONAL_2  カタカナ/ひらがな
#define IME_HID_INT5   0x8Bu   // KC_INTERNATIONAL_5  無変換
#define IME_HID_LANG1  0x90u   // KC_LANGUAGE_1       한/영 (Windows, Linux), かな (macOS)
#define IME_HID_LANG2  0x91u   // KC_LANGUAGE_2       英数 (macOS)
#define IME_HID_RALT   0xE6u   // KC_RIGHT_ALT
#define IME_HID_RGUI   0xE7u   // KC_RIGHT_GUI        the macOS stand-in: the swap sends it as Right Alt (Option)

// Modifier bits in QMK's 8-bit mod layout (MOD_BIT(KC_LCTL) etc.).
#define IME_MOD_LCTL   0x01u
#define IME_MOD_LSFT   0x02u
#define IME_MOD_LALT   0x04u

// Which input-method family the active language has. Values are internal.
enum ime_family {
    IME_FAMILY_NONE = 0,     // no IME: the key is NUBS
    IME_FAMILY_KOREAN,
    IME_FAMILY_JAPANESE,
    // An ANSI layout whose NUBS only repeats the Backslash key (en-US and friends):
    // the key is a second, right-hand Alt on every OS (the split72 base layouts carry
    // Alt on the left only): the WinCompose compose key on Windows, a Compose/AltGr
    // candidate on Linux, and Right OPTION on macOS (not the swapped Cmd the board's
    // Alt key gives there), because Option is the macOS key that types characters.
    IME_FAMILY_RALT,
};

// The Japanese mode the firmware last SENT (its belief, not the host's truth).
enum ime_ja_mode {
    IME_JA_OFF = 0,          // alphanumeric / IME off. Boot default.
    IME_JA_HIRAGANA,
    IME_JA_KATAKANA,
};

// OS values the planner understands. They equal enum poly_os (poly_os.h); the
// binding passes the masked active_os straight through and poly_keymap.c pins
// the equality. GNOME/KDE fold to Linux inside the planner.
enum ime_os {
    IME_OS_UNKNOWN = 0,
    IME_OS_WINDOWS = 1,
    IME_OS_MACOS   = 2,
    IME_OS_LINUX   = 3,
    IME_OS_ANDROID = 4,
    IME_OS_LINUX_GNOME = 6,
    IME_OS_LINUX_KDE   = 7,
};

typedef struct {
    uint8_t usage;   // HID usage; 0 = send nothing
    uint8_t mods;    // IME_MOD_* added for the stroke (a TAP only)
    bool    hold;    // true: register on press, unregister on release (keeps
                     // auto-repeat and the user's own press length, which macOS
                     // needs for Caps-style keys); false: tap once on the press
    bool    drop_shift; // a TAP only: lift the user's own Shift for the stroke, so
                        // the host sees exactly `mods` (Shift is how the user ASKED
                        // for katakana, not part of the shortcut that selects it)
} ime_stroke_t;

// The basic keycode KC_IME stands in for, or 0 when it is an input-method key of
// its own (Korean, Japanese). The display draws that keycode's legend, and
// ime_key_stroke() sends exactly it -- through the macOS GUI/Alt swap on macOS, where
// the Right Alt family stands in for KC_RGUI (drawn ⌥, sent as Right Alt = Option) --
// so the two cannot disagree.
uint8_t ime_key_stand_in(uint8_t family, uint8_t os);

// The stroke for one press of KC_IME. `shift` is whether a Shift is held at the
// press. For Japanese, `*ja_mode` is read (the current belief) and updated to the
// mode the stroke selects; it is left untouched for the other families.
ime_stroke_t ime_key_stroke(uint8_t family, uint8_t os, bool shift, uint8_t* ja_mode);

// The Japanese mode a press moves to: Shift+tap -> katakana; a plain tap toggles
// off <-> hiragana (katakana counts as on, so it goes to off).
uint8_t ime_ja_next(uint8_t current, bool shift);
