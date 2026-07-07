// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Placeholder scene: the classic PSX-Doom fire, rendered full-frame into the
// borrowed overlay pool. Exists to prove the whole game-mode pipeline (pool
// handoff -> 8bpp framebuffer -> dither -> keycap blit -> restore) with a
// thematically appropriate, measurable workload before the engine port lands.
#include "doom_game.h"
#include "doom_blit.h"

#include <string.h>

#ifdef POLYKYBD_DOOM

#define FIRE_MAX 36 // palette indices 0 (black) .. 36 (white-hot)

// Luminance of the 37-entry fire palette (black -> dark red -> orange ->
// yellow -> white), approx round(255 * (i/36)^1.6) — dark reds stay dark.
static uint8_t s_luma[256];

static uint32_t s_rng = 0x29A29A2Bu;

static inline uint32_t rnd(void) {
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}

void doom_fire_init(uint8_t *fb) {
    static const uint8_t FIRE_LUMA[FIRE_MAX + 1] = {
          0,   2,   5,   9,  13,  18,  23,  29,  35,  41,
         48,  55,  62,  70,  78,  86,  95, 104, 113, 122,
        132, 141, 151, 162, 172, 183, 193, 204, 215, 227,
        238, 250, 252, 253, 254, 254, 255,
    };
    memset(s_luma, 255, sizeof(s_luma));
    memcpy(s_luma, FIRE_LUMA, sizeof(FIRE_LUMA));

    memset(fb, 0, DOOM_FB_SIZE);
    memset(fb + (size_t)(DOOM_FB_HEIGHT - 1) * DOOM_FB_WIDTH, FIRE_MAX, DOOM_FB_WIDTH);
}

void doom_fire_step(uint8_t *fb) {
    // Bottom-up propagation with random sideways drift + decay (Fabien
    // Sanglard's PSX-fire formulation). One xorshift draw feeds 16 pixels.
    for (uint16_t y = 1; y < DOOM_FB_HEIGHT; ++y) {
        const uint8_t *src_row = fb + (size_t)y * DOOM_FB_WIDTH;
        uint8_t       *dst_row = fb + (size_t)(y - 1) * DOOM_FB_WIDTH;
        uint32_t       bits    = 0;
        for (uint16_t x = 0; x < DOOM_FB_WIDTH; ++x) {
            if ((x & 15) == 0) {
                bits = rnd();
            }
            const uint8_t px = src_row[x];
            if (px == 0) {
                dst_row[x] = 0;
            } else {
                const uint8_t r  = bits & 3;
                bits >>= 2;
                int16_t dx = (int16_t)x - (int16_t)r + 1;
                if (dx < 0) dx = 0;
                if (dx >= DOOM_FB_WIDTH) dx = DOOM_FB_WIDTH - 1;
                dst_row[dx] = px - (r & 1);
            }
        }
    }
}

const uint8_t *doom_fire_luma(void) {
    return s_luma;
}

#endif // POLYKYBD_DOOM
