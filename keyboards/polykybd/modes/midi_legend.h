// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdint.h>

// Keycap legends for QMK's MIDI keycodes (QK_MIDI_*), MIDI_ADVANCED set.
//
// Notes name the note they will play right now: letter, sharp and octave in
// scientific pitch notation (middle C = C4), from the live octave and transpose,
// plus a treble staff with the note head on its line. The setting keys show the
// current value under a label (OCT, TRNS, VEL, CHAN). The symbols are IconsFont
// glyphs drawn by tools/midi_glyphs.py, each at its own place on the keycap.
//
// Pure: no QMK state is read. The values come from `cfg`, the packed word the
// master syncs in poly_sync_t.midi_cfg, so both halves draw the same thing.

// One byte each, so the slave can unpack it without knowing midi_config_t's
// bitfield layout. octave is QMK's index (0 = OCTAVE_N2), transpose is signed.
// Bit 31 marks the word as set: 0 is what a build without MIDI, or a slave that has
// not had its first sync yet, carries, and those draw QMK's start values instead.
#define MIDI_CFG_VALID 0x80000000u
static inline uint32_t midi_cfg_pack(uint8_t octave, int8_t transpose, uint8_t velocity, uint8_t channel) {
    return MIDI_CFG_VALID | (uint32_t)octave | ((uint32_t)(uint8_t)transpose << 8) |
           ((uint32_t)velocity << 16) | ((uint32_t)(channel & 0x0F) << 24);
}

// The cap on a legend, in codepoints. The longest is 23: a sharp note in octave -1
// (staff, space, letter, half-size op, 6 lifts, '#', 6 drops, 4 back, "-1"). The cap
// is one above it, so a legend that ever reached the cap would be a truncated one;
// midi_legend_tests.cpp checks every keycode at the extremes stays below it.
#define MIDI_LEGEND_MAX 24

// The legend for a QK_MIDI_* keycode, or NULL for any other keycode. The pointer
// is to one static buffer: use it before the next call (the same rule as the emoji
// layer's legend buffer).
const uint32_t* midi_legend(uint16_t keycode, uint32_t cfg);
