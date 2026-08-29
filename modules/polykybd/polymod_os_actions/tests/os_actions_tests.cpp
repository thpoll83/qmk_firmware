// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Drives the REAL emitter against a recorded mock of the five quantum
// primitives it calls. What is worth pinning is the behaviour learned the hard
// way, not the table's every cell: the per-OS column routing, the Unknown
// fallback (both for POLY_OS_UNKNOWN and for an os value the module has never
// heard of), the NA no-op, both bounds, and the held-modifier save/restore —
// a chord that leaks the user's held Shift into Ctrl+C is the classic bug shape
// here. The GNOME/KDE fold helper (poly_os.h) is tested in the same suite
// because folding those values is what keeps the action keys alive on a Linux
// desktop the host has refined over cmd 29 (they were silently dead before).

#include "gtest/gtest.h"

extern "C" {
#include "polymod_os_actions.h"
#include "os_actions_test_shim.h"
#include "poly_os.h"
}

#include <vector>

namespace {

// ---- the recording quantum mock -------------------------------------------

struct Event {
    enum Kind { GET, SET, CLEAR, REG, UNREG, TAP } kind;
    uint8_t arg;
    bool operator==(const Event& o) const { return kind == o.kind && arg == o.arg; }
};

static std::vector<Event> s_log;
static uint8_t            s_mods;

extern "C" {
uint8_t get_mods(void) {
    s_log.push_back({Event::GET, s_mods});
    return s_mods;
}
void set_mods(uint8_t mods) {
    s_log.push_back({Event::SET, mods});
    s_mods = mods;
}
void clear_mods(void) {
    s_log.push_back({Event::CLEAR, 0});
    s_mods = 0;
}
void register_mods(uint8_t mods) {
    s_log.push_back({Event::REG, mods});
    s_mods |= mods;
}
void unregister_mods(uint8_t mods) {
    s_log.push_back({Event::UNREG, mods});
    s_mods &= (uint8_t)~mods;
}
void tap_code(uint8_t code) {
    s_log.push_back({Event::TAP, code});
}
}

class OsActionsTest : public ::testing::Test {
   protected:
    void SetUp() override {
        s_log.clear();
        s_mods = 0;
    }

    // The (mods, kc) chord the emitter produced, read from the log.
    bool emitted(uint8_t* mods, uint8_t* kc) const {
        uint8_t m = 0;
        for (const Event& e : s_log) {
            if (e.kind == Event::REG) m = e.arg;
            if (e.kind == Event::TAP) {
                *mods = m;
                *kc   = e.arg;
                return true;
            }
        }
        return false;
    }
};

// ---- per-OS routing --------------------------------------------------------

TEST_F(OsActionsTest, CopyOnMacosIsCmdC) {
    emit_os_action(OSA_COPY, OSA_OS_MACOS);
    uint8_t m, k;
    ASSERT_TRUE(emitted(&m, &k));
    EXPECT_EQ(MOD_LGUI, m);
    EXPECT_EQ(KC_C, k);
}

TEST_F(OsActionsTest, CopyOnUnknownFallsBackToTheCtrlConvention) {
    emit_os_action(OSA_COPY, OSA_OS_UNKNOWN);
    uint8_t m, k;
    ASSERT_TRUE(emitted(&m, &k));
    EXPECT_EQ(MOD_LCTL, m);
    EXPECT_EQ(KC_C, k);
}

TEST_F(OsActionsTest, RedoOnMacosIsAMultiModChord) {
    emit_os_action(OSA_REDO, OSA_OS_MACOS);
    uint8_t m, k;
    ASSERT_TRUE(emitted(&m, &k));
    EXPECT_EQ(MOD_LGUI | MOD_LSFT, m);
    EXPECT_EQ(KC_Z, k);
}

TEST_F(OsActionsTest, SearchOnWindowsTapsBareGui) {
    emit_os_action(OSA_SEARCH, OSA_OS_WINDOWS);
    uint8_t m, k;
    ASSERT_TRUE(emitted(&m, &k));
    EXPECT_EQ(0, m);
    EXPECT_EQ(KC_LGUI, k);
}

TEST_F(OsActionsTest, LineHomeOnMacosIsCmdLeft) {
    emit_os_action(OSA_LINE_HOME, OSA_OS_MACOS);
    uint8_t m, k;
    ASSERT_TRUE(emitted(&m, &k));
    EXPECT_EQ(MOD_LGUI, m);
    EXPECT_EQ(KC_LEFT, k);
}

// ---- no-ops and bounds -----------------------------------------------------

TEST_F(OsActionsTest, AnUnboundCellEmitsNothing) {
    emit_os_action(OSA_LOCK, OSA_OS_ANDROID);   // NA on Android
    EXPECT_TRUE(s_log.empty());
}

TEST_F(OsActionsTest, AnUnknownOsValueFallsBackToTheUnknownColumn) {
    // An os the module has never heard of (a future poly_os value reaching an
    // older module) must degrade to the Unknown column, never index past it.
    emit_os_action(OSA_COPY, 200);
    uint8_t m, k;
    ASSERT_TRUE(emitted(&m, &k));
    EXPECT_EQ(MOD_LCTL, m);
    EXPECT_EQ(KC_C, k);
}

TEST_F(OsActionsTest, AnOutOfRangeActionEmitsNothing) {
    emit_os_action(OSA_ACTION_COUNT, OSA_OS_WINDOWS);
    EXPECT_TRUE(s_log.empty());
}

TEST_F(OsActionsTest, TheCountMatchesTheEnum) {
    EXPECT_EQ(OSA_ACTION_COUNT, os_action_count());
}

// ---- held-modifier hygiene -------------------------------------------------

TEST_F(OsActionsTest, HeldUserModsAreClearedForTheChordAndRestoredAfter) {
    s_mods = MOD_LSFT;   // the user is holding Shift
    emit_os_action(OSA_COPY, OSA_OS_WINDOWS);
    // The chord must go out as exactly Ctrl+C — no Shift leaked in — and the
    // held Shift must be back afterwards.
    std::vector<Event> expect = {
        {Event::GET, MOD_LSFT},  {Event::CLEAR, 0},        {Event::REG, MOD_LCTL},
        {Event::TAP, KC_C},      {Event::UNREG, MOD_LCTL}, {Event::SET, MOD_LSFT},
    };
    EXPECT_EQ(expect, s_log);
    EXPECT_EQ(MOD_LSFT, s_mods);
}

// ---- the PolyKybd os→column fold (poly_os.h) -------------------------------

TEST_F(OsActionsTest, GnomeAndKdeFoldToTheLinuxColumn) {
    // The DE refinements only refine hints; for chords they ARE Linux. Before
    // the fold they indexed columns the table never had — every action key
    // silently dead on a host-pushed GNOME/KDE desktop.
    EXPECT_EQ(POLY_OS_LINUX, poly_os_action_column(POLY_OS_LINUX_GNOME));
    EXPECT_EQ(POLY_OS_LINUX, poly_os_action_column(POLY_OS_LINUX_KDE));
}

TEST_F(OsActionsTest, EveryOtherOsValuePassesThroughTheFoldUnchanged) {
    for (uint8_t os = 0; os < POLY_OS_COUNT; os++) {
        if (os == POLY_OS_LINUX_GNOME || os == POLY_OS_LINUX_KDE) continue;
        EXPECT_EQ(os, poly_os_action_column(os));
    }
}

TEST_F(OsActionsTest, AFoldedGnomeEmitsTheLinuxChord) {
    // End to end: the folded value drives the emitter to the Linux column.
    emit_os_action(OSA_SCRSHOT, poly_os_action_column(POLY_OS_LINUX_GNOME));
    uint8_t m, k;
    ASSERT_TRUE(emitted(&m, &k));
    EXPECT_EQ(MOD_LSFT, m);   // Shift+PrtSc, the GNOME area screenshot
    EXPECT_EQ(KC_PSCR, k);
}

}  // namespace
