// Copyright 2026 Thomas Pollak
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Tests for midi_legend.c: the keycap legends of QMK's MIDI keycodes. The legend
// is built from the packed settings word the master syncs (poly_sync_t.midi_cfg),
// so these pin what both halves draw for a given set of settings. Pixel placement
// is checked separately, against the renderer, when the glyphs change
// (tools/midi_glyphs.py); here it is the content: which note, which number.

#include "gtest/gtest.h"

extern "C" {
#include "midi_legend.h"
#include "lang/named_glyphs.h"
#include "quantum_keycodes.h"
}

#include <string>

namespace {

// The legend's printable characters, ops and IconsFont glyphs dropped: "C4", "VEL64".
std::string text(const uint32_t* s) {
    std::string out;
    for (; s && *s; ++s) {
        if (*s >= 0x20 && *s < 0x7F) out += (char)*s;
    }
    return out;
}

bool has(const uint32_t* s, uint32_t cp) {
    for (; s && *s; ++s) {
        if (*s == cp) return true;
    }
    return false;
}

const uint32_t START   = 0;                                   // nothing synced yet
const uint32_t DEFAULT = midi_cfg_pack(5, 0, 127, 0);         // what the board starts at

}  // namespace

TEST(MidiLegendTest, OtherKeycodesHaveNoLegend) {
    EXPECT_EQ(midi_legend(KC_A, DEFAULT), nullptr);
    EXPECT_EQ(midi_legend(QK_MIDI - 1, DEFAULT), nullptr);
    EXPECT_EQ(midi_legend(QK_MIDI_MAX + 1, DEFAULT), nullptr);
}

TEST(MidiLegendTest, EveryDefinedMidiKeycodeHasALegend) {
    // The 144 keycodes the host editor's Midi tab offers. A gap here is a blank
    // keycap for a key a user can assign.
    int n = 0;
    for (uint32_t kc = QK_MIDI; kc <= QK_MIDI_PITCH_BEND_UP; ++kc) {
        EXPECT_NE(midi_legend((uint16_t)kc, DEFAULT), nullptr) << std::hex << kc;
        ++n;
    }
    EXPECT_EQ(n, 144);
}

TEST(MidiLegendTest, TheHomeRowCIsMiddleC) {
    EXPECT_EQ(text(midi_legend(QK_MIDI_NOTE_C_0, DEFAULT)), " C4");
    EXPECT_EQ(text(midi_legend(QK_MIDI_NOTE_C_1, DEFAULT)), " C5");
    EXPECT_EQ(text(midi_legend(QK_MIDI_NOTE_B_0, DEFAULT)), " B4");
}

TEST(MidiLegendTest, AnUnsyncedSlaveDrawsTheStartValues) {
    EXPECT_EQ(text(midi_legend(QK_MIDI_NOTE_C_0, START)), text(midi_legend(QK_MIDI_NOTE_C_0, DEFAULT)));
    EXPECT_EQ(text(midi_legend(QK_MIDI_VELOCITY_UP, START)), "VEL127");
}

TEST(MidiLegendTest, SharpsAreRaisedAndCarryTheirOctave) {
    const uint32_t* s = midi_legend(QK_MIDI_NOTE_C_SHARP_0, DEFAULT);
    EXPECT_EQ(text(s), " C#4");
    EXPECT_TRUE(has(s, ICON_MIDI_STAFF_CS[0]));
}

TEST(MidiLegendTest, OctaveAndTransposeMoveTheNoteName) {
    // Two octaves down and two semitones down: MI_C sounds A#1.
    const uint32_t moved = midi_cfg_pack(3, -2, 64, 2);
    EXPECT_EQ(text(midi_legend(QK_MIDI_NOTE_C_0, moved)), " A#1");
    EXPECT_EQ(text(midi_legend(QK_MIDI_OCTAVE_UP, moved)), "OCT2");
    EXPECT_EQ(text(midi_legend(QK_MIDI_TRANSPOSE_DOWN, moved)), "TRNS-2");
    EXPECT_EQ(text(midi_legend(QK_MIDI_VELOCITY_DOWN, moved)), "VEL64");
    EXPECT_EQ(text(midi_legend(QK_MIDI_CHANNEL_UP, moved)), "CHAN+ 3");
}

TEST(MidiLegendTest, NotesPastTheMidiRangeNameTheWrappedNoteQmkSends) {
    // midi_compute_note() is a uint8_t sum and midi_send_noteon() masks it with 0x7F.
    // Octave index 9 + transpose +6 on B5: 108 + 71 + 6 = 185 -> 57, A3.
    const uint32_t top = midi_cfg_pack(9, 6, 127, 0);
    EXPECT_EQ(text(midi_legend(QK_MIDI_NOTE_B_5, top)), " A3");
    // Octave index 0 + transpose -6 on C: -6 -> 250 -> 122, D9.
    const uint32_t bottom = midi_cfg_pack(0, -6, 127, 0);
    EXPECT_EQ(text(midi_legend(QK_MIDI_NOTE_C_0, bottom)), " D9");
    // Inside the range nothing changes: index 0, no transpose, C is note 0, C-1.
    EXPECT_EQ(text(midi_legend(QK_MIDI_NOTE_C_0, midi_cfg_pack(0, 0, 127, 0))), " C-1");
}

TEST(MidiLegendTest, PresetsNameTheirOwnValue) {
    EXPECT_EQ(text(midi_legend(QK_MIDI_OCTAVE_3, DEFAULT)), "OCT4");      // index 5, MI_C = C4
    EXPECT_EQ(text(midi_legend(QK_MIDI_TRANSPOSE_N3, DEFAULT)), "TRNS-3");
    EXPECT_EQ(text(midi_legend(QK_MIDI_TRANSPOSE_3, DEFAULT)), "TRNS+3");
    EXPECT_EQ(text(midi_legend(QK_MIDI_CHANNEL_16, DEFAULT)), "CHAN16");
    EXPECT_EQ(text(midi_legend(QK_MIDI_VELOCITY_7, DEFAULT)), "VEL84");   // 7 * (128 / 10)
    EXPECT_EQ(text(midi_legend(QK_MIDI_VELOCITY_6, DEFAULT)), "");        // the mf mark
}

TEST(MidiLegendTest, ThePackedWordRoundTripsNegativeTranspose) {
    const uint32_t w = midi_cfg_pack(4, -6, 1, 15);
    EXPECT_EQ(text(midi_legend(QK_MIDI_TRANSPOSE_UP, w)), "TRNS-6");
    EXPECT_EQ(text(midi_legend(QK_MIDI_CHANNEL_DOWN, w)), "CHAN- 16");
    EXPECT_NE(w & MIDI_CFG_VALID, 0u);
}

TEST(MidiLegendTest, NoLegendReachesTheBufferCap) {
    // put() drops codepoints past MIDI_LEGEND_MAX silently, so the cap must be
    // above the longest legend at every extreme the settings can reach.
    const uint32_t extremes[] = {
        DEFAULT, START,
        midi_cfg_pack(0, -6, 0, 0),   midi_cfg_pack(0, 6, 127, 15),
        midi_cfg_pack(9, -6, 127, 0), midi_cfg_pack(9, 6, 1, 15),
    };
    size_t longest = 0;
    for (uint32_t cfg : extremes) {
        for (uint32_t kc = QK_MIDI; kc <= QK_MIDI_PITCH_BEND_UP; ++kc) {
            const uint32_t* s = midi_legend((uint16_t)kc, cfg);
            size_t n = 0;
            while (s[n]) ++n;
            EXPECT_LT(n, (size_t)MIDI_LEGEND_MAX) << std::hex << kc;
            if (n > longest) longest = n;
        }
    }
    // A sharp in octave -1 is the longest, one below the cap.
    EXPECT_EQ(longest, (size_t)MIDI_LEGEND_MAX - 1);
}
