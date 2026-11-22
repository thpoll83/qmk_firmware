// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The key LEDs during the first-run show and the lesson (round 39):
//   * Eden opens on the stock rainbow, which fades out while POLYKYBD is first written.
//   * A focus ring (a key being pointed at, the board reveal, a language wipe) lights
//     the keys under it, faintly, in a colour of its own, and they fade as it passes.
//   * A language's name, spelled on the keys, glows very lightly in that language's
//     colour.
// While either runs it OWNS the matrix: everything else is dark, and RGB is switched on
// (not saved) for the duration if the user had it off.
#pragma once
#include <stdbool.h>

// Housekeeping: take the matrix when the show or the lesson starts, give it back after.
void tutorial_rgb_tick(void);
// From rgb_matrix_indicators_kb(): paint this frame. False when it does not own the
// matrix, so the caller carries on.
bool tutorial_rgb_paint(void);
