// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Pure game logic for the idle-screen mine sweeper ("Mines"). One keycap is one cell.
//
// Deliberately free of quantum.h, the display stack and the keymap, so a unit test can
// reach all of it (`make test:polykybd_mines_plan`). The firmware binding is
// mines_mode.c: it maps matrix positions to cells, draws the keycaps and owns the
// start / hide / timeout transitions.
//
// ⚠️ The whole board is this one struct, and it lives INSIDE poly_sync_t: the master
// mutates it, the split sync carries it to the slave, and each half draws its own cells
// from it. That is why it holds bitmasks rather than per-cell bytes (24 B instead of
// 56), and why the neighbour counts are not stored: both halves derive them from the
// mine mask. No extra RAM is spent on the board anywhere else.
#pragma once
#include <stdbool.h>
#include <stdint.h>

// The largest board any variant uses (split72: 2 halves x 7 columns x 4 rows).
#define MINES_MAX_CELLS  56
#define MINES_MASK_BYTES ((MINES_MAX_CELLS + 7) / 8)

// mines_board_t.state bits.
#define MINES_ACTIVE 0x01   // the game owns the keycaps (cleared on hide; the board stays)
#define MINES_PLACED 0x02   // mines are on the board (placed on the first reveal)
#define MINES_LOST   0x04
#define MINES_WON    0x08

#define MINES_NO_CELL 0xFF

typedef struct _mines_board_t {
    uint8_t state;                    // MINES_* bits
    uint8_t boom;                     // the mine that was opened (valid while MINES_LOST)
    uint8_t mine[MINES_MASK_BYTES];
    uint8_t open[MINES_MASK_BYTES];
    uint8_t flag[MINES_MASK_BYTES];
    uint8_t pad;                      // keeps the size a multiple of 4 (poly_sync_t's CRC range)
} mines_board_t;

// The board shape: w x h cells, row-major (cell = y * w + x), `mines` mines.
typedef struct _mines_geom_t {
    uint8_t w;
    uint8_t h;
    uint8_t mines;
} mines_geom_t;

// What a cell shows. Revealed numbers are MINES_SHOW_0 + n.
enum mines_show {
    MINES_SHOW_0 = 0,          // ..MINES_SHOW_0 + 8: revealed, n neighbouring mines
    MINES_SHOW_HIDDEN = 9,
    MINES_SHOW_FLAG,
    MINES_SHOW_MINE,           // after a loss: an unflagged mine
    MINES_SHOW_BOOM,           // after a loss: the mine that was opened
    MINES_SHOW_WRONG_FLAG,     // after a loss: a flag on a cell without a mine
};

static inline bool mines_bit(const uint8_t *mask, uint8_t cell) {
    return (mask[cell >> 3] >> (cell & 7)) & 1u;
}

// Clear the board for a new game. Keeps MINES_ACTIVE as it was.
void mines_reset(mines_board_t *b);

bool mines_over(const mines_board_t *b);

// Mines among the up-to-8 neighbours of `cell`.
uint8_t mines_neighbour_count(const mines_geom_t *g, const mines_board_t *b, uint8_t cell);

// Open `cell`. The first reveal of a game places the mines first, from `seed`, keeping
// `cell` and its neighbours clear, so the first tap always opens an area. Opening a
// cell with no neighbouring mines opens its neighbours too. Opening a mine loses.
// No-op on a flagged or already open cell and after the game is over.
void mines_reveal(const mines_geom_t *g, mines_board_t *b, uint8_t cell, uint32_t seed);

// Tap on an open number whose neighbours carry exactly that many flags: open every
// other hidden neighbour (which loses if a flag is wrong). Otherwise a no-op.
void mines_chord(const mines_geom_t *g, mines_board_t *b, uint8_t cell);

// Put a flag on a hidden cell or take it off. No-op on an open cell or a finished game.
void mines_toggle_flag(const mines_geom_t *g, mines_board_t *b, uint8_t cell);

// Mines minus flags, floored at 0 (the counter keycap).
uint8_t mines_flags_left(const mines_geom_t *g, const mines_board_t *b);

// What `cell` should draw.
enum mines_show mines_cell_show(const mines_geom_t *g, const mines_board_t *b, uint8_t cell);
