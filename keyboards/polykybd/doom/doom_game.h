// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Internal interface between doom_mode.c (mode/state machine) and the scene
// being rendered into the borrowed framebuffer. Currently implemented by
// doom_fire.c (the pipeline-proving placeholder); the rp2040-doom engine port
// will replace this with the real D_DoomLoop-driven renderer.
#pragma once

#include <stdint.h>

// Placeholder scene: the classic PSX-Doom fire, full 320x200 8bpp.
void doom_fire_init(uint8_t *fb);
void doom_fire_step(uint8_t *fb);
// 256-entry palette-index -> 0..255 luminance table for the dither pass.
const uint8_t *doom_fire_luma(void);
