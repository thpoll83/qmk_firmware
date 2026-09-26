// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
// The key LEDs during the first-run show and the lesson — see anim/tutorial_rgb.h.
#include "tutorial_rgb.h"

#if defined(KEYBOARD_polykybd_split72) && defined(RGB_MATRIX_ENABLE)

#include "quantum.h"
#include "color.h"
#include "base/tutorial_plan.h"     // TUT_SLOT_*
#include "startup_anim.h"           // startup_anim_rainbow_level(), key geometry
#include "tutorial.h"
#include "focus_ring.h"             // poly_focus_led_band()

// Raw LED values, not scaled by RGB_MATRIX_MAXIMUM_BRIGHTNESS. For scale, the flash
// cue breathes at 5..36.
#define TRGB_RAINBOW_VAL 64u    // the opening rainbow
#define TRGB_GLOW_VAL    40u    // a key under a ring, at full density: "very subtle"
#define TRGB_NAME_LVL    100u   // a name letter, of 255: TRGB_GLOW_VAL * 100/255 = ~16
#define TRGB_RISE_MS     100u   // a key comes up this fast as the ring reaches it…
#define TRGB_FALL_MS     450u   // …and fades this slowly once it has passed

static const uint8_t k_split[2] = RGB_MATRIX_SPLIT;

static bool     s_owned;
static bool     s_was_enabled;
static bool     s_map_built;
static uint8_t  s_led_slot[RGB_MATRIX_LED_COUNT];   // TUT_SLOT of each LED's key
static uint8_t  s_led_rc[RGB_MATRIX_LED_COUNT];     // its matrix row << 4 | col (0xFF: none)
static uint8_t  s_lvl[RGB_MATRIX_LED_COUNT];        // the glow, 0..255
static uint8_t  s_hue[RGB_MATRIX_LED_COUNT];
static uint32_t s_last;

static bool owns(void) {
    return (startup_anim_active() && !startup_anim_is_loop()) || tutorial_active();
}

void tutorial_rgb_tick(void) {
    const bool want = owns();
    if (want && !s_owned) {
        s_owned       = true;
        s_was_enabled = rgb_matrix_is_enabled();
        if (!s_was_enabled) rgb_matrix_enable_noeeprom();
        for (uint8_t i = 0; i < RGB_MATRIX_LED_COUNT; ++i) s_lvl[i] = 0u;
        s_last = timer_read32();
    } else if (!want && s_owned) {
        s_owned = false;
        if (!s_was_enabled) rgb_matrix_disable_noeeprom();   // the user's mode resumes
    }
}

static void build_map(void) {
    for (uint8_t i = 0; i < RGB_MATRIX_LED_COUNT; ++i) {
        s_led_slot[i] = TUT_SLOT_NONE;
        s_led_rc[i]   = 0xFFu;
    }
    for (uint8_t r = 0; r < MATRIX_ROWS; ++r) {
        for (uint8_t c = 0; c < MATRIX_COLS; ++c) {
            const uint8_t led = g_led_config.matrix_co[r][c];
            if (led == NO_LED || led >= RGB_MATRIX_LED_COUNT) continue;
            s_led_slot[led] = tutorial_slot_at(r, c);
            s_led_rc[led]   = (uint8_t)((r << 4) | (c & 0x0Fu));
        }
    }
    s_map_built = true;
}

// Each ring its own colour, and the same one on both halves: the halves share no
// counter, but both know the phase, the preview item and the ring's centre.
static uint8_t ring_hue(const poly_focus_band_t *b) {
    uint16_t h = (uint16_t)(b->cx * 73 + b->cy * 151);
    h = (uint16_t)(h + tutorial_rgb_phase() * 41u + tutorial_preview_entry() * 97u);
    return (uint8_t)(h ^ (h >> 8));
}

static void set_hsv(uint8_t led, uint8_t h, uint8_t v) {
    const rgb_t c = hsv_to_rgb((hsv_t){h, 255, v});
    rgb_matrix_set_color(led, c.r, c.g, c.b);
}

bool tutorial_rgb_paint(void) {
    if (!s_owned) return false;
    if (!s_map_built) build_map();
    const bool    left = is_keyboard_left();
    const uint8_t lo   = left ? 0u : k_split[0];
    const uint8_t hi   = left ? k_split[0] : (uint8_t)(k_split[0] + k_split[1]);

    // The opening rainbow: the stock left-to-right cycle.
    const uint8_t rb = startup_anim_rainbow_level();
    if (rb > 0u) {
        const uint8_t v = (uint8_t)((TRGB_RAINBOW_VAL * rb) / 255u);
        const uint8_t t = (uint8_t)(timer_read32() >> 3);
        for (uint8_t i = lo; i < hi; ++i) set_hsv(i, (uint8_t)(g_led_config.point[i].x - t), v);
        return true;
    }

    const uint32_t now  = timer_read32();
    const uint32_t dt   = (uint32_t)(now - s_last);
    s_last              = now;
    const uint32_t up   = (dt * 255u) / TRGB_RISE_MS;
    const uint32_t down = (dt * 255u) / TRGB_FALL_MS;

    poly_focus_band_t band;
    const bool    ring  = poly_focus_led_band(&band);
    const uint8_t rhue  = ring ? ring_hue(&band) : 0u;
    const bool    named = tutorial_showing_name();
    const uint8_t nhue  = (uint8_t)(tutorial_preview_entry() * 53u + 20u);

    for (uint8_t i = lo; i < hi; ++i) {
        uint8_t target = 0u, hue = s_hue[i];
        if (named && s_led_rc[i] != 0xFFu &&
            tutorial_is_name_key((uint8_t)(s_led_rc[i] >> 4), (uint8_t)(s_led_rc[i] & 0x0Fu))) {
            target = TRGB_NAME_LVL;
            hue    = nhue;
        }
        if (ring && s_led_slot[i] != TUT_SLOT_NONE) {
            const sa_geom_t g = startup_anim_key_geom(TUT_SLOT_RIGHT(s_led_slot[i]),
                                                      TUT_SLOT_IDX(s_led_slot[i]));
            if (g.valid) {
                const int32_t  dx = (int32_t)g.cx - band.cx, dy = (int32_t)g.cy - band.cy;
                const uint32_t d2 = (uint32_t)(dx * dx + dy * dy);
                if (d2 <= band.outer2 && d2 >= band.inner2 && band.dens > target) {
                    target = band.dens;
                    hue    = rhue;
                }
            }
        }
        uint32_t lvl = s_lvl[i];
        if (target > lvl) {
            lvl      = lvl + up > target ? target : lvl + up;
            s_hue[i] = hue;
        } else {
            lvl = lvl < target + down ? target : lvl - down;
        }
        s_lvl[i] = (uint8_t)lvl;
        set_hsv(i, s_hue[i], (uint8_t)((TRGB_GLOW_VAL * lvl) / 255u));
    }
    return true;
}

#else

void tutorial_rgb_tick(void) {}
bool tutorial_rgb_paint(void) { return false; }

#endif
