// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
// The language-preview sparkles — see anim/lang_sparkle.h.
#include "lang_sparkle.h"

#if defined(KEYBOARD_polykybd_split72)

#include "quantum.h"
#include "base/disp_array.h"
#include "base/shift_reg.h"
#include "base/tutorial_plan.h"     // TUT_SLOT()
#include "side.h"
#include QMK_KEYBOARD_H             // get_key_disp_bitmask
#include "startup_anim.h"           // startup_anim_key_geom
#include "tutorial.h"               // tutorial_sparkle_live()
#include "focus_ring.h"             // poly_focus_draw_legend()
#include "menu_cascade.h"           // poly_render_live(), poly_slot_visible()

#define SPK_MAX       3u     // twinkling at once, per half
#define SPK_SPAWN_MS  150u   // a new one this often
#define SPK_LIFE_MS   480u   // dot, cross, star, star, cross, dot
#define SPK_TICK_MS   30u
#define SPK_KEYS      40u    // display slots per half (8 x 5, some phantom)

typedef struct {
    bool     on;
    uint8_t  idx;     // this half's display slot
    int8_t   x, y;    // the star's centre, keycap pixels
    uint8_t  frame;   // what is on the panel now (0 = nothing)
    uint32_t born;
} spk_t;

static spk_t    s_spk[SPK_MAX];
static bool     s_was_live;
static uint32_t s_at;
static uint32_t s_spawn_at;
static uint32_t s_rng;

static uint32_t rnd(void) {   // xorshift32
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}

// The star grows and shrinks: a dot, a small cross, a four-point star with a bright
// core, then back down.
static uint8_t frame_at(uint32_t age) {
    static const uint8_t seq[] = {1u, 2u, 3u, 3u, 2u, 1u};
    if (age >= SPK_LIFE_MS) return 0u;
    return seq[(age * sizeof(seq)) / SPK_LIFE_MS];
}

// One star pixel with a black edge, so it reads over a legend's ink as well as on
// the dark around it. Keycap coordinates; the buffer's window starts at BUFFER_X.
static void halo(int8_t x, int8_t y) { kdisp_clear_rect((int8_t)(BUFFER_X + x - 1), (int8_t)(y - 1), 3, 3); }
static void ink(int8_t x, int8_t y)  { kdisp_fill_rect((int8_t)(BUFFER_X + x), y, 1, 1); }

static void draw_star(int8_t x, int8_t y, uint8_t frame) {
    const int8_t arm = frame >= 3u ? 3 : (frame == 2u ? 1 : 0);
    // Black first, all of it, then the ink: a halo drawn after a neighbour's ink would
    // cut the star itself.
    for (int8_t d = -arm; d <= arm; ++d) {
        halo((int8_t)(x + d), y);
        halo(x, (int8_t)(y + d));
    }
    for (int8_t d = -arm; d <= arm; ++d) {
        ink((int8_t)(x + d), y);
        ink(x, (int8_t)(y + d));
    }
    if (frame >= 3u) {   // the bright core: the four diagonal neighbours
        ink((int8_t)(x - 1), (int8_t)(y - 1));
        ink((int8_t)(x + 1), (int8_t)(y - 1));
        ink((int8_t)(x - 1), (int8_t)(y + 1));
        ink((int8_t)(x + 1), (int8_t)(y + 1));
    }
}

// Repaint the key: its own legend, then the star on top (frame 0 = legend only).
static void repaint(const spk_t *k, uint8_t frame) {
    const uint8_t slot = TUT_SLOT(is_left_side() ? 0 : 1, k->idx);
    sr_shift_out_buffer_latch(get_key_disp_bitmask(k->idx), get_disp_bitmask_size());
    // Tracked, like every draw outside update_displays(), so the next full render
    // diffs against what is really on the panel.
    kdisp_track_panel(k->idx);
    kdisp_set_buffer(0x00);
    (void)poly_focus_draw_legend(slot);
    kdisp_set_gfx_erase(false);   // the plotter flags are static: never leave erase on
    if (frame != 0u) draw_star(k->x, k->y, frame);
    kdisp_send_window();
}

static void spawn(uint32_t now) {
    spk_t *k = NULL;
    for (uint8_t i = 0; i < SPK_MAX; ++i) {
        if (!s_spk[i].on) {
            k = &s_spk[i];
            break;
        }
    }
    if (k == NULL) return;
    const bool right = !is_left_side();
    for (uint8_t tries = 0; tries < 8u; ++tries) {
        const uint8_t idx = (uint8_t)(rnd() % SPK_KEYS);
        if (!startup_anim_key_geom(right, idx).valid) continue;
        if (!poly_slot_visible(TUT_SLOT(right ? 1 : 0, idx))) continue;
        bool taken = false;
        for (uint8_t i = 0; i < SPK_MAX; ++i) taken |= (s_spk[i].on && s_spk[i].idx == idx);
        if (taken) continue;
        k->on    = true;
        k->idx   = idx;
        k->x     = (int8_t)(5u + rnd() % (SCREEN_WIDTH - 10u));
        k->y     = (int8_t)(5u + rnd() % (SCREEN_HEIGHT - 10u));
        k->frame = 0u;
        k->born  = now;
        return;
    }
}

void lang_sparkle_tick(void) {
    const bool live = tutorial_sparkle_live() && poly_render_live();
    if (!live) {
        if (s_was_live) {
            // The preview moved on: wipe any star still up. Only while the keycaps are
            // still ours to draw; otherwise whoever owns them repaints anyway.
            for (uint8_t i = 0; i < SPK_MAX; ++i) {
                if (s_spk[i].on && s_spk[i].frame != 0u && poly_render_live()) repaint(&s_spk[i], 0u);
                s_spk[i].on = false;
            }
            s_was_live = false;
        }
        return;
    }
    const uint32_t now = timer_read32();
    if (!s_was_live) {
        s_was_live = true;
        s_rng      = now ^ (is_left_side() ? 0x9E3779B9u : 0x7F4A7C15u);
        if (s_rng == 0u) s_rng = 1u;
        s_spawn_at = now;
        s_at       = now - SPK_TICK_MS;
    }
    if ((uint32_t)(now - s_at) < SPK_TICK_MS) return;
    s_at = now;
    for (uint8_t i = 0; i < SPK_MAX; ++i) {
        spk_t *k = &s_spk[i];
        if (!k->on) continue;
        const uint8_t f = frame_at(now - k->born);
        if (f != k->frame) {
            repaint(k, f);
            k->frame = f;
        }
        if (f == 0u) k->on = false;
    }
    if ((uint32_t)(now - s_spawn_at) >= SPK_SPAWN_MS) {
        s_spawn_at = now;
        spawn(now);
    }
}

#else   // split42: no geometry table, so no tutorial and no sparkles

void lang_sparkle_tick(void) {}

#endif
