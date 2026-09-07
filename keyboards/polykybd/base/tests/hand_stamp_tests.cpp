// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// poly_hand_decide() is the ordering that decides which side a half comes up on.
// It is pure so it can be tested without a flash part, the same seam as
// fw_up_verdict.c -- and it is worth testing because every case here is a
// silent-wrong-answer case: nothing errors when handedness is wrong, the board
// just draws the other half's keycaps.
#include "gtest/gtest.h"

extern "C" {
#include "hand_stamp.h"
}

// Named for readability at the call sites: decide(stamp?, stampLeft, eeOk, eeLeft).
static poly_hand_decision_t decide(bool stamp_valid, bool stamp_is_left, bool ee_enabled, bool ee_is_left) {
    return poly_hand_decide(stamp_valid, stamp_is_left, ee_enabled, ee_is_left);
}

// --- the stamp is authoritative --------------------------------------------

TEST(HandStampTest, AValidStampDecidesTheSide) {
    EXPECT_TRUE(decide(true, true, true, true).is_left);
    EXPECT_FALSE(decide(true, false, true, false).is_left);
}

// THE case this whole module exists for: the wear-levelling store was cleared, so
// the EEPROM byte reads zero, which EE_HANDS reads as a valid "right".
TEST(HandStampTest, AWipedEepromCannotMoveAStampedHalfToTheOtherSide) {
    poly_hand_decision_t d = decide(true, /*stamp=*/true, /*ee_enabled=*/false, /*ee_is_left=*/false);
    EXPECT_TRUE(d.is_left) << "a cleared EEPROM byte outvoted the stamp";
    EXPECT_TRUE(d.repair_ee) << "the EEPROM byte was left disagreeing with the stamp";
    EXPECT_FALSE(d.migrate);
}

TEST(HandStampTest, AnAgreeingEepromNeedsNoRepair) {
    EXPECT_FALSE(decide(true, true, true, true).repair_ee);
    EXPECT_FALSE(decide(true, false, true, false).repair_ee);
}

// A stamped board never migrates, however healthy or broken the EEPROM looks --
// migrating would rewrite the sector on every boot.
TEST(HandStampTest, AStampedBoardNeverMigrates) {
    for (bool ee_enabled : {false, true}) {
        for (bool ee_left : {false, true}) {
            for (bool stamp_left : {false, true}) {
                EXPECT_FALSE(decide(true, stamp_left, ee_enabled, ee_left).migrate);
            }
        }
    }
}

// --- adopting an existing board --------------------------------------------

// Every board flashed before the stamp sector existed. The EEPROM is the only
// record there is, so capture it while it still looks trustworthy.
TEST(HandStampTest, AnUnstampedBoardWithAHealthyEepromIsMigrated) {
    poly_hand_decision_t d = decide(false, false, /*ee_enabled=*/true, /*ee_is_left=*/true);
    EXPECT_TRUE(d.migrate);
    EXPECT_TRUE(d.is_left) << "migration must stamp what the EEPROM actually says";
    EXPECT_FALSE(d.repair_ee);

    d = decide(false, false, true, false);
    EXPECT_TRUE(d.migrate);
    EXPECT_FALSE(d.is_left);
}

// The one case where the honest answer is "do not write anything". With no stamp
// AND a blank/wiped store, the handedness byte is a guess -- and a guess written
// to the stamp becomes the authoritative answer for the life of the board.
TEST(HandStampTest, AnUnstampedBoardWithAWipedEepromIsNotStamped) {
    poly_hand_decision_t d = decide(false, false, /*ee_enabled=*/false, /*ee_is_left=*/false);
    EXPECT_FALSE(d.migrate) << "a guess was cemented into the stamp";
    EXPECT_FALSE(d.repair_ee);
    EXPECT_FALSE(d.is_left) << "unchanged behaviour: fall back to whatever the byte says";

    // Same rule when the wiped store happens to read `left`.
    d = decide(false, false, false, true);
    EXPECT_FALSE(d.migrate);
    EXPECT_TRUE(d.is_left);
}

// --- properties that must hold everywhere -----------------------------------

// Repairing EEPROM without a stamp to repair it FROM would write the same guess
// back, which is worse than leaving it: it looks like a confirmed value.
TEST(HandStampTest, RepairIsOnlyEverProposedAgainstAStamp) {
    for (bool ee_enabled : {false, true}) {
        for (bool ee_left : {false, true}) {
            EXPECT_FALSE(decide(false, false, ee_enabled, ee_left).repair_ee);
            EXPECT_FALSE(decide(false, true, ee_enabled, ee_left).repair_ee);
        }
    }
}

// Writing the stamp and rewriting the EEPROM byte answer different questions, and
// no input should ever ask for both at once.
TEST(HandStampTest, MigrateAndRepairAreMutuallyExclusive) {
    for (bool stamp_valid : {false, true}) {
        for (bool stamp_left : {false, true}) {
            for (bool ee_enabled : {false, true}) {
                for (bool ee_left : {false, true}) {
                    poly_hand_decision_t d = decide(stamp_valid, stamp_left, ee_enabled, ee_left);
                    EXPECT_FALSE(d.migrate && d.repair_ee);
                }
            }
        }
    }
}

// The decision must never invent a third side: whatever it returns has to be the
// stamp's value when stamped, and the EEPROM's when not.
TEST(HandStampTest, TheChosenSideAlwaysComesFromOneOfTheTwoSources) {
    for (bool stamp_valid : {false, true}) {
        for (bool stamp_left : {false, true}) {
            for (bool ee_enabled : {false, true}) {
                for (bool ee_left : {false, true}) {
                    poly_hand_decision_t d = decide(stamp_valid, stamp_left, ee_enabled, ee_left);
                    EXPECT_EQ(d.is_left, stamp_valid ? stamp_left : ee_left);
                }
            }
        }
    }
}
