// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#include "gtest/gtest.h"

extern "C" {
#include "os_hints.h"
#include "quantum/quantum_keycodes.h"
#include "quantum/modifiers.h"
#include "poly_os.h"

// The pre-extraction table, compiled into this binary alongside the live one.
const uint32_t* os_hint_reference(uint16_t keycode, uint8_t mods_raw, uint8_t active_os_packed);
}

#include <string>
#include <vector>

namespace {

// Every keycode the table can answer for, plus a generous margin of ones it
// cannot, so "returns NULL" is covered as thoroughly as "returns a hint".
std::vector<uint16_t> interesting_keycodes() {
    std::vector<uint16_t> kcs;
    for (uint16_t kc = KC_A; kc <= KC_EXSEL; ++kc) kcs.push_back(kc);   // the whole basic block
    for (uint16_t kc = 0; kc < 8; ++kc) kcs.push_back(kc);              // NO/transparent/rollover
    kcs.push_back(KC_NO);
    kcs.push_back(KC_TRNS);
    // Mod-tap keycodes take a separate branch at the end of the table, so the
    // exhaustive comparison has to reach it. Every 5-bit mod combination, on a
    // couple of tapped keys.
    for (uint8_t mt = 0; mt < 0x20; ++mt) {
        kcs.push_back(static_cast<uint16_t>(QK_MOD_TAP | (mt << 8) | KC_A));
        kcs.push_back(static_cast<uint16_t>(QK_MOD_TAP | (mt << 8) | KC_SPACE));
    }
    return kcs;
}

// The right-hand modifier bits AS THEY APPEAR IN poly_layer_t.mods.
//
// This is a trap worth naming: poly_layer_t.mods comes from get_mods(), which is
// QMK's 8-bit PACKED form (enum mods_8bit — MOD_BIT_RCTRL == 0x10). The MOD_RCTL
// family in enum mods_5bit is a DIFFERENT encoding (0x11: bit4 is an L/R flag, not
// a right-Ctrl bit) used for mod-tap and one-shot keycodes. Reaching for MOD_RCTL
// here silently tests a value that can never occur in the byte the table receives —
// which is exactly what the first draft of this file did.
constexpr uint8_t kRightBits[4] = {MOD_BIT_RCTRL, MOD_BIT_RSHIFT, MOD_BIT_RALT, MOD_BIT_RGUI};
constexpr uint8_t kLeftBits[4]  = {MOD_BIT_LCTRL, MOD_BIT_LSHIFT, MOD_BIT_LALT, MOD_BIT_LGUI};

// The OS values the table branches on, plus the reserved/unknown ones, each also
// exercised with POLY_OS_AUTO_FLAG set — the flag must not reach the comparison.
std::vector<uint8_t> os_values() {
    std::vector<uint8_t> v;
    for (uint8_t os = 0; os < POLY_OS_COUNT + 2; ++os) {
        v.push_back(os);
        v.push_back(static_cast<uint8_t>(os | POLY_OS_AUTO_FLAG));
    }
    return v;
}

std::string describe(uint16_t kc, uint8_t mods, uint8_t os) {
    char buf[96];
    snprintf(buf, sizeof(buf), "keycode=0x%04X mods=0x%02X os=0x%02X", kc, mods, os);
    return std::string(buf);
}

// ---------------------------------------------------------------------------
// The equivalence proof for the extraction.
// ---------------------------------------------------------------------------

// Exhaustive over (keycode x mods x os): the extracted pure table must return
// exactly what the pre-extraction table in poly_keymap.c returned. This is the
// evidence that moving 310 lines into their own translation unit and replacing
// two global reads with parameters changed no behaviour. Binary comparison cannot
// answer this one — extracting a function into another TU legitimately changes
// codegen (inlining, stack slots, switch-table numbering).
TEST(OsHintsExtraction, MatchesPreExtractionTableExhaustively) {
    size_t compared = 0, hits = 0;
    for (uint16_t kc : interesting_keycodes()) {
        for (unsigned mods = 0; mods < 256; ++mods) {
            for (uint8_t os : os_values()) {
                const uint32_t* got  = os_hint_for_keycode(kc, static_cast<uint8_t>(mods), os);
                const uint32_t* want = os_hint_reference(kc, static_cast<uint8_t>(mods), os);
                // Both tables return pointers to string literals; the compiler may
                // or may not pool identical literals across the two translation
                // units, so compare CONTENT, not pointer identity.
                if (want == nullptr || got == nullptr) {
                    ASSERT_EQ(got == nullptr, want == nullptr) << describe(kc, mods, os);
                } else {
                    size_t i = 0;
                    while (want[i] != 0 && got[i] != 0 && want[i] == got[i]) ++i;
                    ASSERT_EQ(want[i], got[i]) << describe(kc, mods, os) << " diverges at index " << i;
                    ++hits;
                }
                ++compared;
            }
        }
    }
    // Guard against a vacuous pass: if the harness ever stopped producing hints
    // (wrong keycode range, table short-circuited) every comparison would be
    // NULL == NULL and this test would still be green.
    EXPECT_GT(hits, 1000u) << "suspiciously few non-NULL hints — is the table reachable?";
    EXPECT_GT(compared, 100000u);
}

// ---------------------------------------------------------------------------
// Behaviour the table is required to have, pinned independently of the reference
// so these survive the reference file being retired.
// ---------------------------------------------------------------------------

// The auto-mode bit rides in the same byte as the OS; the table must mask it.
// Before extraction the mask lived at the call site, where a second caller could
// have forgotten it.
TEST(OsHints, AutoFlagDoesNotChangeTheAnswer) {
    for (uint16_t kc : interesting_keycodes()) {
        for (unsigned mods = 0; mods < 256; ++mods) {
            for (uint8_t os = 0; os < POLY_OS_COUNT; ++os) {
                const uint32_t* plain = os_hint_for_keycode(kc, static_cast<uint8_t>(mods), os);
                const uint32_t* flagged =
                    os_hint_for_keycode(kc, static_cast<uint8_t>(mods),
                                        static_cast<uint8_t>(os | POLY_OS_AUTO_FLAG));
                ASSERT_EQ(plain, flagged) << describe(kc, mods, os);
            }
        }
    }
}

// Left and right modifiers are the same chord as far as a hint is concerned.
TEST(OsHints, LeftAndRightModifiersAreEquivalent) {
    for (uint16_t kc : interesting_keycodes()) {
        for (uint8_t os = 0; os < POLY_OS_COUNT; ++os) {
            for (int i = 0; i < 4; ++i) {
                ASSERT_EQ(os_hint_for_keycode(kc, kLeftBits[i], os),
                          os_hint_for_keycode(kc, kRightBits[i], os))
                    << describe(kc, kLeftBits[i], os);
            }
        }
    }
}

// A chord matches on the EXACT collapsed modifier set. Adding a modifier must not
// leave the narrower hint showing — the regression this rule was written for is
// "Win+Ctrl+Shift+X must not show the Win+Ctrl+X hint". Verified as a property:
// for every chord that produces a hint, adding one more modifier must produce a
// different answer (another hint or none), never the same one.
TEST(OsHints, ExtraModifierNeverLeaksTheNarrowerHint) {
    size_t checked = 0;
    for (uint16_t kc : interesting_keycodes()) {
        // KC_F2/KC_F5 answer before modifiers are consulted, and a mod-tap key's
        // hint is derived from the KEYCODE's mods, not from what is held — neither
        // is a chord, so neither is in scope for this rule.
        if (kc == KC_F2 || kc == KC_F5 || IS_QK_MOD_TAP(kc)) continue;
        for (uint8_t os = 0; os < POLY_OS_COUNT; ++os) {
            for (unsigned mods = 0; mods < 16; ++mods) {
                const uint32_t* base = os_hint_for_keycode(kc, static_cast<uint8_t>(mods), os);
                if (base == nullptr) continue;
                for (uint8_t extra : kLeftBits) {
                    if (mods & extra) continue;
                    const uint8_t wider = static_cast<uint8_t>(mods | extra);
                    const uint32_t* got = os_hint_for_keycode(kc, wider, os);
                    ASSERT_NE(got, base) << describe(kc, wider, os)
                                         << " still shows the hint for the narrower chord 0x"
                                         << std::hex << mods;
                    ++checked;
                }
            }
        }
    }
    EXPECT_GT(checked, 100u) << "no widening cases exercised — the table looks unreachable";
}

// KC_F2 / KC_F5 answer before any modifier or OS is consulted, so they hold for
// every combination. This is the one deliberate early-out in the table.
TEST(OsHints, UnconditionalKeysAnswerRegardlessOfModsAndOs) {
    for (unsigned mods = 0; mods < 256; ++mods) {
        for (uint8_t os = 0; os < POLY_OS_COUNT; ++os) {
            EXPECT_NE(os_hint_for_keycode(KC_F2, static_cast<uint8_t>(mods), os), nullptr);
            EXPECT_NE(os_hint_for_keycode(KC_F5, static_cast<uint8_t>(mods), os), nullptr);
        }
    }
}

// With no modifiers held there is nothing to preview, except the two above.
TEST(OsHints, NoModifiersMeansNoHint) {
    for (uint16_t kc : interesting_keycodes()) {
        // The unconditional keys answer before modifiers are read, and a mod-tap's
        // hint comes from the keycode rather than from what is held.
        if (kc == KC_F2 || kc == KC_F5 || IS_QK_MOD_TAP(kc)) continue;
        for (uint8_t os = 0; os < POLY_OS_COUNT; ++os) {
            EXPECT_EQ(os_hint_for_keycode(kc, 0, os), nullptr) << describe(kc, 0, os);
        }
    }
}

// An OS index the firmware does not know behaves as the NON-APPLE default, i.e. it
// gets the Windows/Linux chords. That is the table's actual shape — it branches
// `if (apple) ... else ...`, so everything that is not macOS lands in the same arm —
// and this test pins it rather than asserting the "returns nothing" rule the glyph-
// script index follows. Documented because it is a deliberate-looking choice that
// nothing else states: a keyboard that has never been told its host OS previews
// Windows shortcuts, which is the right default for the common case but is silent
// about being a default.
TEST(OsHints, UnknownOsBehavesAsTheNonAppleDefault) {
    // Ctrl+A is "select all" on Windows/Linux and nothing on macOS (Cmd+A is), so
    // it separates the two arms cleanly.
    for (uint8_t os = POLY_OS_COUNT; os < POLY_OS_COUNT + 2; ++os) {
        EXPECT_NE(os_hint_for_keycode(KC_A, MOD_BIT_LCTRL, os), nullptr)
            << "unknown OS " << static_cast<int>(os) << " should take the non-Apple arm";
    }
    EXPECT_EQ(os_hint_for_keycode(KC_A, MOD_BIT_LCTRL, POLY_OS_MACOS), nullptr)
        << "Ctrl+A is not a macOS chord — if this changes the test above needs a new probe";
}

// The mod-tap hint block derives its glyphs from the KEYCODE's own mods, and its
// eleven combined-modifier branches are UNREACHABLE: they test
// `(mods & MOD_MASK_XY) == MOD_MASK_XY` where mods is the 5-bit mod-tap encoding
// (0x00..0x1F) and MOD_MASK_* are 8-bit packed masks (MOD_MASK_CS == 0x33), so the
// equality can never hold. Only the three single-modifier `&` tests and the final
// else can fire — a Ctrl+Shift mod-tap shows "¤" alone, never "¤⇧".
//
// This is PRE-EXISTING behaviour, pinned here rather than fixed: this change set is
// a behaviour-preserving extraction, and correcting it would alter what appears on
// a keycap. Tracked separately; when it is fixed, this test is the one to update.
TEST(OsHints, ModTapCombinedModifierBranchesAreCurrentlyUnreachable) {
    for (uint8_t mt = 0; mt < 0x20; ++mt) {
        const uint16_t kc = static_cast<uint16_t>(QK_MOD_TAP | (mt << 8) | KC_A);
        const uint32_t* got = os_hint_for_keycode(kc, 0, POLY_OS_WINDOWS);
        ASSERT_NE(got, nullptr) << "mod-tap always yields some hint";
        // Every reachable answer is one of the four terminal cases, so the glyph
        // count never exceeds one modifier symbol after the leading spaces.
        size_t len = 0;
        while (got[len] != 0) ++len;
        EXPECT_LE(len, 5u) << "a combined-modifier mod-tap hint became reachable for mods=0x"
                           << std::hex << static_cast<int>(mt)
                           << " — the dead branches are alive, update this test";
    }
}

}  // namespace
