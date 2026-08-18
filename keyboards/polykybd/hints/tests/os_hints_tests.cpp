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

#include <cstdio>   // snprintf in describe()
#include <string>
#include <utility>  // std::pair — badge grid-cell uniqueness
#include <vector>

namespace {

// Compare hints by CONTENT, never by pointer.
//
// The two tables are separate translation units returning pointers to string
// literals, and whether the compiler pools two identical literals to one address
// is not something a test may depend on — in either direction. A pointer test
// would spuriously fail when a wider chord legitimately gains its own entry whose
// text happens to match the narrower one, and would spuriously pass for two
// distinct literals with equal content.
bool same_hint(const uint32_t* a, const uint32_t* b) {
    if (a == nullptr || b == nullptr) return a == b;
    size_t i = 0;
    while (a[i] != 0 && b[i] != 0 && a[i] == b[i]) ++i;
    return a[i] == b[i];
}

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
        // ⚠️ Mod-taps are DELIBERATELY excluded: their block was rewritten to fix the
        // 5-bit/8-bit encoding bug and to render as a corner badge, so it no longer
        // matches the reference — that is the point of the change. The reference file
        // stays a verbatim pre-extraction copy (its value is being unedited), and the
        // new mod-tap behaviour is pinned by the ModTap* tests at the bottom of this
        // file instead. Everything else still has to match exactly.
        if (IS_QK_MOD_TAP(kc)) continue;
        for (unsigned mods = 0; mods < 256; ++mods) {
            for (uint8_t os : os_values()) {
                const uint32_t* got  = os_hint_for_keycode(kc, static_cast<uint8_t>(mods), os);
                const uint32_t* want = os_hint_reference(kc, static_cast<uint8_t>(mods), os);
                ASSERT_TRUE(same_hint(got, want)) << describe(kc, mods, os);
                if (want != nullptr) ++hits;
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
                ASSERT_TRUE(same_hint(plain, flagged)) << describe(kc, mods, os);
            }
        }
    }
}

// Left and right modifiers are the same chord as far as a hint is concerned.
TEST(OsHints, LeftAndRightModifiersAreEquivalent) {
    for (uint16_t kc : interesting_keycodes()) {
        for (uint8_t os = 0; os < POLY_OS_COUNT; ++os) {
            for (int i = 0; i < 4; ++i) {
                ASSERT_TRUE(same_hint(os_hint_for_keycode(kc, kLeftBits[i], os),
                                      os_hint_for_keycode(kc, kRightBits[i], os)))
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
                    ASSERT_FALSE(same_hint(got, base))
                        << describe(kc, wider, os)
                        << " still shows the hint for the narrower chord 0x" << std::hex << mods;
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

// ---------------------------------------------------------------------------
// Mod-tap badge. The block derives its marks from the KEYCODE's own modifiers,
// which arrive in QMK's 5-bit form (bits 0-3 = CSAG, bit 4 = the left/right
// flag) while the MOD_MASK_* constants it is compared against are the 8-bit
// paired form. These tests pin both halves of the fix and replace the earlier
// ModTapCombinedModifierBranchesAreCurrentlyUnreachable, which pinned the bug.
// ---------------------------------------------------------------------------

namespace {
uint16_t mod_tap_kc(uint8_t mods_5bit) {
    return static_cast<uint16_t>(QK_MOD_TAP | (mods_5bit << 8) | KC_A);
}
// os_hints.c builds one badge table PER OS; they differ only at the GUI-bearing
// indices 8-15, through the MTB_GUI_<os>_R1/R2 position macros. Pinning a single
// OS would leave a wrong position macro (or a table wired to the wrong OS) untested
// on six of the seven, so every structural test below loops over os_values().
const uint32_t* mod_tap_hint(uint8_t mods_5bit, uint8_t os = POLY_OS_WINDOWS) {
    return os_hint_for_keycode(mod_tap_kc(mods_5bit), 0, os);
}
}  // namespace

// All fifteen non-empty modifier combinations must produce a DISTINCT badge.
// Before the fix only four answers existed across the whole 32-value domain:
// the eleven combined branches were unreachable (they demanded both the left
// AND right bit of each modifier), so HYPR_T and MEH_T drew a bare Ctrl.
TEST(OsHints, ModTapEveryModifierCombinationIsDistinct) {
    for (uint8_t os : os_values()) {
        std::vector<std::u32string> seen;
        for (uint8_t combo = 1; combo <= 0x0F; ++combo) {
            const uint32_t* got = mod_tap_hint(combo, os);
            ASSERT_NE(got, nullptr) << "combo 0x" << std::hex << static_cast<int>(combo)
                                    << " os " << static_cast<int>(os);
            std::u32string s;
            for (size_t i = 0; got[i]; ++i) s.push_back(static_cast<char32_t>(got[i]));
            for (size_t j = 0; j < seen.size(); ++j) {
                EXPECT_NE(s, seen[j]) << "combo 0x" << std::hex << static_cast<int>(combo)
                                      << " os " << static_cast<int>(os)
                                      << " duplicates an earlier badge — the Shift+Alt row"
                                         " carrying Ctrl+Alt's glyphs was exactly this";
            }
            seen.push_back(s);
        }
        EXPECT_EQ(seen.size(), 15u);
    }
}

// Bit 4 is the left/right FLAG, not a modifier. Reading it as one made every
// right-hand mod-tap (RCTL_T/RSFT_T/RALT_T/RGUI_T) collide with MOD_MASK_CTRL's
// 0x10 bit and draw Ctrl. Both sides must now agree.
TEST(OsHints, ModTapLeftAndRightSidesAgree) {
    for (uint8_t os : os_values()) {
        for (uint8_t combo = 1; combo <= 0x0F; ++combo) {
            EXPECT_TRUE(same_hint(mod_tap_hint(combo, os),
                                  mod_tap_hint(static_cast<uint8_t>(combo | 0x10), os)))
                << "left/right disagree for combo 0x" << std::hex << static_cast<int>(combo)
                << " os " << static_cast<int>(os);
        }
    }
}

// LGUI_T is 0x08, which intersects NO MOD_MASK_* value, so it used to fall to
// the final else and draw the no-modifier legend — a blank-looking keycap.
TEST(OsHints, ModTapGuiOnlyIsNotTheEmptyCase) {
    const uint32_t* gui = mod_tap_hint(MOD_LGUI);
    ASSERT_NE(gui, nullptr);
    EXPECT_FALSE(same_hint(gui, mod_tap_hint(MOD_LCTL)))
        << "GUI-only must not render as Ctrl";
    // A mod-tap carrying no modifier at all is the one case with no hint.
    EXPECT_EQ(mod_tap_hint(0), nullptr);
    EXPECT_EQ(mod_tap_hint(0x10), nullptr);
}

// The badge is a corner mark, not a second legend: every mark must be MOVE-
// positioned, and a mark is drawn in exactly one of the three modes — decimated
// (Ctrl, GUI), 2x2-OR (Alt) or full size (Shift, whose icon is already small).
// This also pins the NUL trap: a MOVE coordinate is a codepoint, so a zero byte
// would terminate the string and silently truncate the badge. That shipped once
// (row 1 sat at y=0) and every badge came back two codepoints long, which is why
// the length is asserted rather than assumed.
TEST(OsHints, ModTapBadgeIsPositionedAndComplete) {
    for (uint8_t os : os_values()) {
    for (uint8_t combo = 1; combo <= 0x0F; ++combo) {
        const uint32_t* got = mod_tap_hint(combo, os);
        ASSERT_NE(got, nullptr);
        EXPECT_EQ(got[0], U'\x0E') << "badge must start with a MOVE to the corner";
        size_t moves = 0, marks = 0, len = 0;
        std::vector<std::pair<uint32_t, uint32_t>> cells;
        while (got[len] != 0) {
            const uint32_t c = got[len];
            if (c == U'\x0E') {                 // MOVE consumes x,y
                ASSERT_NE(got[len + 1], 0u) << "a zero MOVE x truncates the badge";
                ASSERT_NE(got[len + 2], 0u) << "a zero MOVE y truncates the badge";
                cells.emplace_back(got[len + 1], got[len + 2]);
                ++moves;
                len += 3;
            } else if (c == U'\x0F' || c == U'\x11') {   // HALF / THIN + its glyph
                ++marks;
                len += 2;
            } else {                            // a full-size glyph (Shift)
                ++marks;
                len += 1;
            }
        }
        const size_t want = static_cast<size_t>(__builtin_popcount(combo));
        EXPECT_EQ(moves, want) << "one MOVE per modifier, combo 0x"
                               << std::hex << static_cast<int>(combo);
        EXPECT_EQ(marks, want) << "one mark per modifier, combo 0x"
                               << std::hex << static_cast<int>(combo);
        // Two marks sharing a cell would draw on top of each other. The counts
        // above cannot see that — the positions are hand-written per table row,
        // so a copy-paste of the wrong MTB_* macro reads as a complete badge.
        for (size_t a = 0; a + 1 < cells.size(); ++a) {
            for (size_t b = a + 1; b < cells.size(); ++b) {
                EXPECT_NE(cells[a], cells[b])
                    << "two marks share grid cell (" << cells[a].first << ","
                    << cells[a].second << "), combo 0x" << std::hex
                    << static_cast<int>(combo) << " os " << static_cast<int>(os);
            }
        }
    }
    }
}

}  // namespace
