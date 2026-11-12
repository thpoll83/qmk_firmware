// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#include "attn_guide.h"

#include <string.h>

bool attn_guide_parse(const uint8_t *data, uint8_t length, uint8_t rows, uint8_t cols,
                      uint8_t layers, uint8_t period_min, uint8_t period_max,
                      attn_guide_t *out) {
    if (!data || !out || length <= ATTN_GUIDE_IDX_NSTEPS) return false;
    attn_guide_t g;
    memset(&g, 0, sizeof g);
    g.count  = data[ATTN_GUIDE_IDX_COUNT];
    g.period = data[ATTN_GUIDE_IDX_PERIOD];
    g.target = data[ATTN_GUIDE_IDX_TARGET];
    g.nsteps = data[ATTN_GUIDE_IDX_NSTEPS];
    if (g.period < period_min || g.period > period_max) return false;
    if (g.target >= layers) return false;
    if (g.nsteps == 0u || g.nsteps > ATTN_GUIDE_MAX_STEPS) return false;
    if ((unsigned)length < ATTN_GUIDE_IDX_STEP0 + 3u * g.nsteps) return false;
    for (uint8_t i = 0; i < g.nsteps; ++i) {
        const uint8_t *p = &data[ATTN_GUIDE_IDX_STEP0 + 3u * i];
        attn_guide_step_t s = {p[0], p[1], p[2]};
        if (s.row >= rows || s.col >= cols || s.layer >= layers) return false;
        if (s.layer == g.target) return false;
        for (uint8_t j = 0; j < i; ++j) {
            if (g.step[j].layer == s.layer) return false;
        }
        g.step[i] = s;
    }
    *out = g;
    return true;
}

uint8_t attn_guide_select(const attn_guide_t *g, uint8_t active_layer) {
    if (active_layer == g->target) return ATTN_GUIDE_DONE;
    for (uint8_t i = 0; i < g->nsteps && i < ATTN_GUIDE_MAX_STEPS; ++i) {
        if (g->step[i].layer == active_layer) return i;
    }
    return ATTN_GUIDE_IDLE;
}
