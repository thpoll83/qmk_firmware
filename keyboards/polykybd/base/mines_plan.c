// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Pure mine sweeper logic. See mines_plan.h.
#include "mines_plan.h"

#include <string.h>

// In the .c, not the header: the header is also compiled as C++ by the unit test.
_Static_assert(sizeof(mines_board_t) % 4 == 0, "mines_board_t must keep poly_sync_t padding-free");

static void set_bit(uint8_t *mask, uint8_t cell, bool on) {
    if (on) {
        mask[cell >> 3] |= (uint8_t)(1u << (cell & 7));
    } else {
        mask[cell >> 3] &= (uint8_t)~(1u << (cell & 7));
    }
}

static uint8_t cells(const mines_geom_t *g) {
    return (uint8_t)(g->w * g->h);
}

// True when `n` is a neighbour of `c` (or `c` itself).
static bool near(const mines_geom_t *g, uint8_t c, uint8_t n) {
    const int dx = (int)(n % g->w) - (int)(c % g->w);
    const int dy = (int)(n / g->w) - (int)(c / g->w);
    return dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1;
}

void mines_reset(mines_board_t *b) {
    const uint8_t active = b->state & MINES_ACTIVE;
    const uint8_t level  = b->level;
    memset(b, 0, sizeof(*b));
    b->state = active;
    b->level = level;
    b->boom  = MINES_NO_CELL;
}

bool mines_over(const mines_board_t *b) {
    return (b->state & (MINES_LOST | MINES_WON)) != 0;
}

uint8_t mines_neighbour_count(const mines_geom_t *g, const mines_board_t *b, uint8_t cell) {
    const uint8_t n = cells(g);
    uint8_t count = 0;
    for (uint8_t i = 0; i < n; ++i) {
        if (i != cell && near(g, cell, i) && mines_bit(b->mine, i)) ++count;
    }
    return count;
}

static uint32_t xorshift32(uint32_t *s) {
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

static void place_mines(const mines_geom_t *g, mines_board_t *b, uint8_t first, uint32_t seed) {
    const uint8_t n = cells(g);
    uint32_t s = seed ? seed : 0x9E3779B9u;   // xorshift has a fixed point at 0
    // Keep the first cell's whole neighbourhood clear when the board has room for it,
    // otherwise only the cell itself (a board this small is a test, not a game).
    uint8_t clear = 0;
    for (uint8_t i = 0; i < n; ++i) {
        if (near(g, first, i)) ++clear;
    }
    const bool keep_area = (uint8_t)(n - clear) >= g->mines;
    for (uint8_t placed = 0; placed < g->mines; ++placed) {
        uint8_t free = 0;
        for (uint8_t i = 0; i < n; ++i) {
            const bool blocked = keep_area ? near(g, first, i) : (i == first);
            if (!blocked && !mines_bit(b->mine, i)) ++free;
        }
        if (free == 0) break;
        uint8_t pick = (uint8_t)(xorshift32(&s) % free);
        for (uint8_t i = 0; i < n; ++i) {
            const bool blocked = keep_area ? near(g, first, i) : (i == first);
            if (blocked || mines_bit(b->mine, i)) continue;
            if (pick == 0) {
                set_bit(b->mine, i, true);
                break;
            }
            --pick;
        }
    }
    b->state |= MINES_PLACED;
}

void mines_check_win(const mines_geom_t *g, mines_board_t *b) {
    if (mines_over(b)) return;
    const uint8_t n = cells(g);
    for (uint8_t i = 0; i < n; ++i) {
        if (!mines_bit(b->mine, i) && !mines_bit(b->open, i)) return;
    }
    b->state |= MINES_WON;
    memcpy(b->flag, b->mine, sizeof(b->flag));   // flag what is left, as the classic does
}

// Open one hidden, unflagged cell; spread through cells with no neighbouring mines.
static void open_cell(const mines_geom_t *g, mines_board_t *b, uint8_t cell) {
    if (mines_bit(b->open, cell) || mines_bit(b->flag, cell)) return;
    if (mines_bit(b->mine, cell)) {
        b->state |= MINES_LOST;
        b->boom = cell;
        return;
    }
    set_bit(b->open, cell, true);
    if (mines_neighbour_count(g, b, cell) != 0) return;
    // Iterate to a fixed point instead of recursing: the board is at most 56 cells, so
    // the repeated scan is cheap and the stack use is constant.
    const uint8_t n = cells(g);
    bool grew = true;
    while (grew) {
        grew = false;
        for (uint8_t c = 0; c < n; ++c) {
            if (!mines_bit(b->open, c) || mines_neighbour_count(g, b, c) != 0) continue;
            for (uint8_t i = 0; i < n; ++i) {
                if (near(g, c, i) && !mines_bit(b->open, i) && !mines_bit(b->flag, i)) {
                    set_bit(b->open, i, true);
                    grew = true;
                }
            }
        }
    }
}

void mines_reveal(const mines_geom_t *g, mines_board_t *b, uint8_t cell, uint32_t seed) {
    if (cell >= cells(g) || mines_over(b)) return;
    if (mines_bit(b->flag, cell) || mines_bit(b->open, cell)) return;
    if (!(b->state & MINES_PLACED)) place_mines(g, b, cell, seed);
    open_cell(g, b, cell);
    mines_check_win(g, b);
}

void mines_chord(const mines_geom_t *g, mines_board_t *b, uint8_t cell) {
    if (cell >= cells(g) || mines_over(b) || !mines_bit(b->open, cell)) return;
    const uint8_t want = mines_neighbour_count(g, b, cell);
    if (want == 0) return;
    const uint8_t n = cells(g);
    uint8_t flags = 0;
    for (uint8_t i = 0; i < n; ++i) {
        if (i != cell && near(g, cell, i) && mines_bit(b->flag, i)) ++flags;
    }
    if (flags != want) return;
    for (uint8_t i = 0; i < n && !mines_over(b); ++i) {
        if (i != cell && near(g, cell, i)) open_cell(g, b, i);
    }
    mines_check_win(g, b);
}

void mines_toggle_flag(const mines_geom_t *g, mines_board_t *b, uint8_t cell) {
    if (cell >= cells(g) || mines_over(b) || mines_bit(b->open, cell)) return;
    set_bit(b->flag, cell, !mines_bit(b->flag, cell));
}

uint8_t mines_flags_left(const mines_geom_t *g, const mines_board_t *b) {
    const uint8_t n = cells(g);
    uint8_t flags = 0;
    for (uint8_t i = 0; i < n; ++i) {
        if (mines_bit(b->flag, i)) ++flags;
    }
    return flags >= g->mines ? 0 : (uint8_t)(g->mines - flags);
}

enum mines_show mines_cell_show(const mines_geom_t *g, const mines_board_t *b, uint8_t cell) {
    const bool mine = mines_bit(b->mine, cell);
    const bool flag = mines_bit(b->flag, cell);
    if (b->state & MINES_LOST) {
        if (cell == b->boom) return MINES_SHOW_BOOM;
        if (mine && !flag) return MINES_SHOW_MINE;
        if (flag && !mine) return MINES_SHOW_WRONG_FLAG;
    }
    if (flag) return MINES_SHOW_FLAG;
    if (!mines_bit(b->open, cell)) return MINES_SHOW_HIDDEN;
    return (enum mines_show)(MINES_SHOW_0 + mines_neighbour_count(g, b, cell));
}

uint8_t mines_next_pending(const mines_geom_t *g, const uint8_t *pending, uint8_t origin) {
    const uint8_t n = cells(g);
    uint8_t best = MINES_NO_CELL, best_d = 0xFF;
    for (uint8_t i = 0; i < n; ++i) {
        if (!mines_bit(pending, i)) continue;
        int dx = (int)(i % g->w) - (int)(origin % g->w);
        int dy = (int)(i / g->w) - (int)(origin / g->w);
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        const uint8_t d = (uint8_t)(dx > dy ? dx : dy);
        if (d < best_d) {   // strict: the lowest index wins a tie
            best   = i;
            best_d = d;
        }
    }
    return best;
}

uint8_t mines_for_level(const mines_geom_t *base, uint8_t level) {
    const uint8_t n   = cells(base);
    const uint8_t cap = n > 9 ? (uint8_t)(n - 9) : 1;
    const unsigned want = (unsigned)base->mines + level;
    return want > cap ? cap : (uint8_t)want;
}
