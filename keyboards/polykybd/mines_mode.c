// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Firmware binding for "Mines" (mines_mode.h): the start gesture, the key handling, the
// hide / timeout hand-back to the idle animation, and the keycap drawing. The rules of
// the game are base/mines_plan.c.
#include "mines_mode.h"

#include "quantum.h"
#include QMK_KEYBOARD_H            // MATRIX_ROWS_PER_SIDE, keyboard variant
#include "base/mines_plan.h"
#include "base/update.h"           // update_performed / backdate_last_update / request_disp_refresh
#include "base/com.h"              // DISP_IDLE
#include "base/disp_array.h"
#include "base/font_lookup.h"      // kdisp_gfx_glyph_font
// Defined in base/fonts/util_font.h, which carries the data and is included once
// (poly_keymap.c).
extern const GFXfont NotoSans_Regular_Mid_19px7b;
#include "state.h"
#include "poly_keymap.h"           // poly_set_awake_state / poly_fw_screen
#include "anim/startup_anim.h"     // startup_anim_stop (the Eden idle loop)
#include "anim/demo_mode.h"
#include "anim/tutorial.h"

// ---- board shape and control keys, per variant ----------------------------------
// The board is the main block of both halves, side by side: every key above the thumb
// row. The thumb row carries the controls. Cells on the inner edge of the left half
// neighbour the inner edge of the right half, as if the gap were not there.
#if defined(KEYBOARD_polykybd_split72)
#    define MINES_HALF_W     7     // left cols 0..6, right cols 1..7
#    define MINES_ROWS       4
#    define MINES_RIGHT_COL0 1     // right half rows 0..3 have no col-0 key
#    define MINES_COUNT      9     // 56 cells, 16 %
#    define MINES_KEY_HIDE    4, 0 // left thumb row, outermost
#    define MINES_KEY_COUNTER 4, 1
#    define MINES_KEY_STATUS  9, 6
#    define MINES_KEY_NEW     9, 7 // right thumb row, outermost
#elif defined(KEYBOARD_polykybd_split42)
#    define MINES_HALF_W     6
#    define MINES_ROWS       3
#    define MINES_RIGHT_COL0 0
#    define MINES_COUNT      6     // 36 cells, 17 %
#    define MINES_KEY_HIDE    3, 3
#    define MINES_KEY_COUNTER 3, 4
#    define MINES_KEY_STATUS  7, 1
#    define MINES_KEY_NEW     7, 2
#else
#    error "mines_mode.c: no board layout for this PolyKybd variant"
#endif

_Static_assert(2 * MINES_HALF_W * MINES_ROWS <= MINES_MAX_CELLS, "board larger than mines_board_t holds");

static const mines_geom_t s_geom = {2 * MINES_HALF_W, MINES_ROWS, MINES_COUNT};

// A press held this long toggles a flag instead of opening the cell.
#define MINES_FLAG_HOLD_MS 300
// After hiding, how long the normal legends get to repaint (both halves) before the
// idle animation takes the keycaps. The pulse style dims whatever the panels hold, so
// handing over sooner would leave the board pulsing on them.
#define MINES_HIDE_REPAINT_MS 400
// The reveal animation opens one key per step, outward from the tapped key.
#define MINES_REVEAL_STEP_MS 35

#define NO_KEY 0xFF

static uint8_t  s_press_row = NO_KEY;   // the press this game saw (its release acts)
static uint8_t  s_press_col = NO_KEY;
static uint16_t s_press_t;
static uint32_t s_last_input;           // for MINES_INACTIVE_MS
static uint32_t s_hide_t;               // when the hide began (0 = not hiding)
static uint32_t s_hide_stamp;           // get_last_update() at the hide
// Reveal animation (master only): cells the last move opened that are not shown yet.
// The synced board's open bits are held back and released one per step, so the slave
// animates too without any extra synced state.
static uint8_t  s_pending[MINES_MASK_BYTES];
static uint8_t  s_origin;
static uint16_t s_step_t;

static const GFXfont *const s_fonts[] = {&NotoSans_Regular_Mid_19px7b};

static mines_board_t *board(void) {
    return &access_local_state()->mines;
}

static const mines_board_t *board_ro(void) {
    return &get_local_state()->mines;
}

bool mines_active(void) {
    return (board_ro()->state & MINES_ACTIVE) != 0;
}

static uint8_t cell_of(uint8_t row, uint8_t col) {
    if (row < MATRIX_ROWS_PER_SIDE) {
        if (row < MINES_ROWS && col < MINES_HALF_W) return (uint8_t)(row * s_geom.w + col);
        return MINES_NO_CELL;
    }
    const uint8_t r = (uint8_t)(row - MATRIX_ROWS_PER_SIDE);
    // Unsigned wrap: a col left of MINES_RIGHT_COL0 becomes large and fails the test.
    if (r < MINES_ROWS && (uint8_t)(col - MINES_RIGHT_COL0) < MINES_HALF_W) {
        return (uint8_t)(r * s_geom.w + MINES_HALF_W + (col - MINES_RIGHT_COL0));
    }
    return MINES_NO_CELL;
}

static bool is_key(uint8_t row, uint8_t col, uint8_t kr, uint8_t kc) {
    return row == kr && col == kc;
}
#define IS_KEY(row, col, which) is_key((row), (col), which)

// ---- transitions ----------------------------------------------------------------

bool mines_try_start(uint16_t keycode, keyrecord_t *record) {
    if (!record->event.pressed || keycode != KC_HYPR) return false;
    poly_sync_t *ls = access_local_state();
    if ((ls->flags & DISP_IDLE) == 0) return false;   // only from a running idle animation
    if (demo_active() || tutorial_active() || poly_fw_screen() != POLY_FW_SCREEN_NONE) return false;

    // Release anything held before swallowing begins (the FW-2 prompt's reason).
    clear_keyboard();
    startup_anim_stop();
    poly_set_awake_state(ls);
    mines_board_t *b = board();
    if (mines_over(b) || (b->state & MINES_PLACED) == 0) mines_reset(b);   // else resume
    b->state |= MINES_ACTIVE;
    s_press_row = NO_KEY;   // Hyper's own release must not act
    s_press_col = NO_KEY;
    s_hide_t    = 0;
    s_last_input = timer_read32();
    update_performed();
    request_disp_refresh();
    uprint("mines: start\n");
    return true;
}

static bool anim_running(void) {
    for (uint8_t i = 0; i < MINES_MASK_BYTES; ++i) {
        if (s_pending[i]) return true;
    }
    return false;
}

static void open_bit(mines_board_t *b, uint8_t cell) {
    b->open[cell >> 3] |= (uint8_t)(1u << (cell & 7));
    s_pending[cell >> 3] &= (uint8_t)~(1u << (cell & 7));
}

// The win waits for the animation: the move's own win check ran on a board whose
// open bits were then held back.
static void anim_done(mines_board_t *b) {
    mines_check_win(&s_geom, b);
    if (b->state & MINES_WON) uprint("mines: won\n");
}

static void anim_step(void) {
    const uint8_t cell = mines_next_pending(&s_geom, s_pending, s_origin);
    if (cell == MINES_NO_CELL) return;
    mines_board_t *b = board();
    open_bit(b, cell);
    if (!anim_running()) anim_done(b);
    s_step_t = timer_read();
    request_disp_refresh();
}

// Show the rest of a running animation at once (before the next move, a hide or a
// new game).
static void anim_flush(void) {
    if (!anim_running()) return;
    mines_board_t *b = board();
    for (uint8_t i = 0; i < MINES_MASK_BYTES; ++i) {
        b->open[i] |= s_pending[i];
        s_pending[i] = 0;
    }
    anim_done(b);
    request_disp_refresh();
}

static void mines_hide(void) {
    anim_flush();
    board()->state &= (uint8_t)~MINES_ACTIVE;
    request_disp_refresh();   // the normal legends, which idle then takes over
    update_performed();
    s_hide_stamp = get_last_update();
    s_hide_t     = timer_read32() | 1u;   // 0 means "not hiding"
    uprint("mines: hide\n");
}

void mines_tick(void) {
    if (mines_active()) {
        // The game is on screen because the user put it there: keep the board's own
        // idle countdown (which may be far shorter) from fading it. The game has its
        // own, fixed timeout instead.
        update_performed();
        if (timer_elapsed32(s_last_input) > MINES_INACTIVE_MS) {
            mines_hide();
        } else if (anim_running() && timer_elapsed(s_step_t) >= MINES_REVEAL_STEP_MS) {
            anim_step();
        }
        return;
    }
    if (s_hide_t != 0 && timer_elapsed32(s_hide_t) >= MINES_HIDE_REPAINT_MS) {
        s_hide_t = 0;
        // A key typed since the hide means the user is at the keyboard: stay awake.
        if (get_last_update() == s_hide_stamp) {
            // Past the fade, so the next housekeeping pass enters the idle style.
            backdate_last_update(get_idle_timeout_ms() + FADE_TRANSITION_TIME);
        }
    }
}

void mines_process_record(keyrecord_t *record) {
    const uint8_t row = record->event.key.row;
    const uint8_t col = record->event.key.col;
    s_last_input = timer_read32();
    update_performed();

    // Act on the RELEASE of a press this game saw: matrix_scan_kb inverts the keycap
    // on press and restores it on release, so acting on the press would redraw a key
    // that is still inverted. The hold time also tells a flag from an open.
    if (record->event.pressed) {
        s_press_row = row;
        s_press_col = col;
        s_press_t   = timer_read();
        return;
    }
    if (row != s_press_row || col != s_press_col) return;
    s_press_row = NO_KEY;
    s_press_col = NO_KEY;
    const bool held = timer_elapsed(s_press_t) >= MINES_FLAG_HOLD_MS;

    anim_flush();
    mines_board_t *b = board();
    if (IS_KEY(row, col, MINES_KEY_HIDE)) {
        mines_hide();
        return;
    }
    if (IS_KEY(row, col, MINES_KEY_NEW)) {
        mines_reset(b);
        request_disp_refresh();
        return;
    }
    const uint8_t cell = cell_of(row, col);
    if (cell == MINES_NO_CELL) return;
    if (held) {
        mines_toggle_flag(&s_geom, b, cell);
        request_disp_refresh();
        return;
    }
    // Play the move on a copy, then animate what it opened. A loss shows at once.
    mines_board_t next = *b;
    if (mines_bit(b->open, cell)) {
        mines_chord(&s_geom, &next, cell);
    } else {
        mines_reveal(&s_geom, &next, cell, timer_read32() ^ ((uint32_t)s_press_t << 16));
    }
    if (next.state & MINES_LOST) {
        *b = next;
        uprint("mines: boom\n");
        request_disp_refresh();
        return;
    }
    for (uint8_t i = 0; i < MINES_MASK_BYTES; ++i) {
        s_pending[i] = (uint8_t)(next.open[i] & ~b->open[i]);
    }
    // Keep everything the move decided (mine placement) except what the animation
    // releases: the open bits, and the win with the flags it sets.
    memcpy(next.open, b->open, sizeof(next.open));
    memcpy(next.flag, b->flag, sizeof(next.flag));
    next.state &= (uint8_t)~MINES_WON;
    *b = next;
    s_origin = cell;
    anim_step();   // the tapped key opens at once
}

// ---- drawing ----------------------------------------------------------------------

#define CX (BUFFER_X + SCREEN_WIDTH / 2)
#define CY (SCREEN_HEIGHT / 2)

static void rect(int x, int y, int w, int h, bool ink) {
    if (ink) {
        kdisp_fill_rect((int8_t)x, (int8_t)y, (int8_t)w, (int8_t)h);
    } else {
        kdisp_clear_rect((int8_t)x, (int8_t)y, (int8_t)w, (int8_t)h);
    }
}

// A round mine with eight spikes and a highlight, centred on (cx, cy).
static void draw_mine(int cx, int cy, bool ink) {
    static const uint8_t half[15] = {3, 5, 6, 6, 7, 7, 7, 7, 7, 7, 7, 6, 6, 5, 3};
    for (int i = 0; i < 15; ++i) rect(cx - half[i], cy - 7 + i, 2 * half[i] + 1, 1, ink);
    rect(cx - 11, cy - 1, 23, 3, ink);
    rect(cx - 1, cy - 11, 3, 23, ink);
    for (int k = 6; k <= 9; ++k) {
        rect(cx - k - 1, cy - k - 1, 2, 2, ink);
        rect(cx + k, cy - k - 1, 2, 2, ink);
        rect(cx - k - 1, cy + k, 2, 2, ink);
        rect(cx + k, cy + k, 2, 2, ink);
    }
    rect(cx - 4, cy - 4, 3, 3, !ink);
}

// A solid pennant flying right from a pole, on a foot; `px` is the pole's x. Pointing
// right and solid so it cannot be read as the digit 1, whose serif points left.
// `ink` false cuts the flag out of a lit square.
static void draw_flag(int px, bool ink) {
    rect(px, 7, 2, 24, ink);
    rect(px - 6, 29, 14, 3, ink);
    for (int i = 0; i < 15; ++i) {
        const int w = (i < 8 ? i + 1 : 15 - i) * 2;
        rect(px + 2, 7 + i, w, 1, ink);
    }
}

// A filled disc of radius r, centred on (cx, cy).
static void draw_disc(int cx, int cy, int r, bool ink) {
    for (int dy = -r; dy <= r; ++dy) {
        int half = 0;
        while ((half + 1) * (half + 1) + dy * dy <= r * r) ++half;
        rect(cx - half, cy + dy, 2 * half + 1, 1, ink);
    }
}

static void draw_big_char(int cx, uint32_t ch) {
    const GFXglyph *g = kdisp_gfx_glyph_font(s_fonts, 1, ch, NULL);
    if (g == NULL) return;
    const int w = (int)pgm_read_byte(&g->width) * 2;
    const int h = (int)pgm_read_byte(&g->height) * 2;
    kdisp_draw_glyph_double_at(s_fonts, 1, (int8_t)(cx - w / 2), (int8_t)((SCREEN_HEIGHT - h) / 2), ch);
}

static void draw_text(const uint32_t *text) {
    int8_t xmin, xmax, ymin, ymax;
    kdisp_gfx_text_bbox(s_fonts, 1, text, &xmin, &xmax, &ymin, &ymax);
    kdisp_write_gfx_text(s_fonts, 1, (int8_t)(BUFFER_X + (SCREEN_WIDTH - (xmax - xmin + 1)) / 2 - xmin),
                         (int8_t)((SCREEN_HEIGHT - (ymax - ymin + 1)) / 2 - ymin), text);
}

// A hidden cell: a square outline, the same size on every key, filled with a dot grid
// (one lit pixel in nine) when `dots`. A flagged cell drops the dots for the flag.
#define MINES_TILE 32
#define MINES_TILE_X (CX - MINES_TILE / 2)
#define MINES_TILE_Y ((SCREEN_HEIGHT - MINES_TILE) / 2)
static void draw_tile(bool dots) {
    kdisp_draw_round_rect(MINES_TILE_X, MINES_TILE_Y, MINES_TILE, MINES_TILE, 3);
    if (!dots) return;
    for (int y = MINES_TILE_Y + 3; y < MINES_TILE_Y + MINES_TILE - 2; y += 3) {
        for (int x = MINES_TILE_X + 3; x < MINES_TILE_X + MINES_TILE - 2; x += 3) rect(x, y, 1, 1, true);
    }
}

// The mine that was hit: diagonal hatching, a clear ring, the mine in the middle.
static void draw_boom(void) {
    for (int y = 0; y < SCREEN_HEIGHT; ++y) {
        for (int x = 0; x < SCREEN_WIDTH; ++x) {
            if ((x + y) % 5 == 0) rect(BUFFER_X + x, y, 1, 1, true);
        }
    }
    draw_disc(CX, CY, 16, false);
    draw_mine(CX, CY, true);
}

void mines_render_key(uint8_t row, uint8_t col) {
    const mines_board_t *b = board_ro();
    kdisp_set_buffer(0x00);

    if (IS_KEY(row, col, MINES_KEY_HIDE)) {
        draw_text(U"Hide");
        return;
    }
    if (IS_KEY(row, col, MINES_KEY_NEW)) {
        draw_text(U"New");
        return;
    }
    if (IS_KEY(row, col, MINES_KEY_STATUS)) {
        draw_text((b->state & MINES_LOST) ? U"Boom" : (b->state & MINES_WON) ? U"Win!" : U"Mines");
        return;
    }
    if (IS_KEY(row, col, MINES_KEY_COUNTER)) {
        draw_flag(CX - 24, true);
        draw_big_char(CX + 16, (uint32_t)('0' + mines_flags_left(&s_geom, b)));
        return;
    }
    const uint8_t cell = cell_of(row, col);
    if (cell == MINES_NO_CELL) return;   // every other key stays dark

    const enum mines_show show = mines_cell_show(&s_geom, b, cell);
    switch (show) {
        case MINES_SHOW_HIDDEN:
            draw_tile(true);
            break;
        case MINES_SHOW_FLAG:
            draw_tile(false);
            draw_flag(CX - 8, true);
            break;
        case MINES_SHOW_WRONG_FLAG:
            draw_flag(CX - 8, true);
            for (int i = 0; i < 30; ++i) {
                rect(CX - 15 + i, 5 + i, 2, 1, true);
                rect(CX + 14 - i, 5 + i, 2, 1, true);
            }
            break;
        case MINES_SHOW_MINE:
            draw_mine(CX, CY, true);
            break;
        case MINES_SHOW_BOOM:
            draw_boom();
            break;
        default:
            if (show > MINES_SHOW_0) draw_big_char(CX, (uint32_t)('0' + (show - MINES_SHOW_0)));
            break;   // an open 0 stays dark
    }
}
