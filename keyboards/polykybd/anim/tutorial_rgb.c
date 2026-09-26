// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
// The key LEDs during the first-run show and the lesson — see anim/tutorial_rgb.h.
#include "tutorial_rgb.h"

#if defined(KEYBOARD_polykybd_split72) && defined(RGB_MATRIX_ENABLE)

#include "quantum.h"
#include "color.h"
#include "base/tutorial_plan.h"     // TUT_SLOT_*, tut_pulse_level()
#include "startup_anim.h"           // startup_anim_rainbow_level()
#include "tutorial.h"
#include "menu_cascade.h"           // menu_cascade_key_level()
#include "focus_ring.h"             // poly_focus_sweep_band()

// Raw LED values, not scaled by RGB_MATRIX_MAXIMUM_BRIGHTNESS. For scale, the flash
// cue breathes at 5..36.
#define TRGB_PULSE_VAL   40u    // the key the lesson points at, at the top of its pulse
#define TRGB_SWEEP_VAL   40u    // a key under a board-wide sweep, at full density
#define TRGB_NAME_VAL    16u    // a letter of a spelled language name: "very lightly"
#define TRGB_FALL_MS     450u   // a glow that is no longer wanted fades this slowly
// Round 41: a dim mix collapses to its STRONGER channel ("an orange becomes red when
// fading out"). Orange at value 5 is r=5 g=2, and a channel below about 3 does not
// light reliably. So each colour has a floor: the lowest value at which its weaker channel
// still reaches TRGB_MIN_CH. A pulse swings between that floor and its peak; a fade
// goes dark at the floor instead of passing through the primary.
// ⚠️ Full saturation for the same reason: the white tint of s=230 is ~10% of the value,
// the first thing to vanish when dimmed, so the colour drifted as it faded. Every hue
// below is already a two-channel mix, which is what "never pure R, G or B" asks.
#define TRGB_SAT         255u
#define TRGB_MIN_CH      4u
// Name keys breathe out of step with each other ("pulse individually"): each key's
// pulse clock is offset by this much per slot. Not a divisor of the 1400 ms period, so
// neighbouring slots land far apart in phase.
#define TRGB_NAME_STAGGER_MS 571u

// Round 40: no pure red, green or blue ("always mix the colour"). QMK hues: red 0,
// green 85, blue 170; every entry here sits between two of them.
static const uint8_t k_hues[] = {21, 43, 64, 106, 128, 149, 191, 213, 234};
#define TRGB_NHUES (sizeof(k_hues) / sizeof(k_hues[0]))

static const uint8_t k_split[2] = RGB_MATRIX_SPLIT;

static bool     s_owned;
static bool     s_was_enabled;
static bool     s_map_built;
static uint8_t  s_led_slot[RGB_MATRIX_LED_COUNT];   // TUT_SLOT of each LED's key
static uint8_t  s_led_rc[RGB_MATRIX_LED_COUNT];     // its matrix row << 4 | col (0xFF: none)
static uint8_t  s_lvl[RGB_MATRIX_LED_COUNT];        // the glow, 0..255
static uint8_t  s_hue[RGB_MATRIX_LED_COUNT];        // …its colour and peak, kept while it fades
static uint8_t  s_val[RGB_MATRIX_LED_COUNT];
static uint32_t s_last;

// The opening rainbow is the STOCK effect ("like the default when the keyboard gets a
// fresh firmware"), not a copy of it: the master switches the matrix to
// CYCLE_LEFT_RIGHT at the stock speed and brightness, without saving, and fades it out
// through the brightness. The split transport carries the mode to the slave.
static bool    s_rb_on;
static uint8_t s_saved_mode, s_saved_speed;
static hsv_t   s_saved_hsv;
static uint8_t s_rb_val;

static bool owns(void) {
    return (startup_anim_active() && !startup_anim_is_loop()) || tutorial_active();
}

static void rainbow_end(void) {
    if (!s_rb_on) return;
    s_rb_on = false;
    rgb_matrix_mode_noeeprom(s_saved_mode);
    rgb_matrix_sethsv_noeeprom(s_saved_hsv.h, s_saved_hsv.s, s_saved_hsv.v);
    rgb_matrix_set_speed_noeeprom(s_saved_speed);
}

static void rainbow_tick(void) {
    if (!is_keyboard_master()) return;
    const uint8_t rb = s_owned ? startup_anim_rainbow_level() : 0u;
    if (rb == 0u) {
        rainbow_end();
        return;
    }
    if (!s_rb_on) {
        s_rb_on        = true;
        s_saved_mode   = rgb_matrix_get_mode();
        s_saved_hsv    = rgb_matrix_get_hsv();
        s_saved_speed  = rgb_matrix_get_speed();
        s_rb_val       = 0xFFu;
        rgb_matrix_mode_noeeprom(RGB_MATRIX_CYCLE_LEFT_RIGHT);
        rgb_matrix_set_speed_noeeprom(RGB_MATRIX_DEFAULT_SPD);
    }
    const uint8_t v = (uint8_t)((RGB_MATRIX_DEFAULT_VAL * (uint16_t)rb) / 255u);
    if (v != s_rb_val) {
        s_rb_val = v;
        rgb_matrix_sethsv_noeeprom(0, 255, v);
    }
}

void tutorial_rgb_tick(void) {
    const bool want = owns();
    if (want && !s_owned) {
        s_owned       = true;
        s_was_enabled = rgb_matrix_is_enabled();
        if (!s_was_enabled) rgb_matrix_enable_noeeprom();
        for (uint8_t i = 0; i < RGB_MATRIX_LED_COUNT; ++i) s_lvl[i] = 0u;
        s_last = timer_read32();
    }
    rainbow_tick();
    if (!want && s_owned) {
        s_owned = false;
        rainbow_end();
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

// The lowest value at which hue `h` still shows both of its channels.
static uint8_t hue_floor(uint8_t h) {
    const rgb_t   c  = hsv_to_rgb((hsv_t){h, TRGB_SAT, 255u});
    uint8_t       mn = 255u;
    if (c.r && c.r < mn) mn = c.r;
    if (c.g && c.g < mn) mn = c.g;
    if (c.b && c.b < mn) mn = c.b;
    return (uint8_t)((TRGB_MIN_CH * 255u + mn - 1u) / mn);
}

// Level 0..255 of a glow whose peak is `val`, drawn in hue `h`. Below the hue's floor
// the key is dark: that is what keeps a fading orange from reading as red.
static void set_glow(uint8_t led, uint8_t h, uint8_t val, uint8_t lvl) {
    const uint8_t v = (uint8_t)(((uint16_t)val * lvl) / 255u);
    if (v < hue_floor(h)) {
        rgb_matrix_set_color(led, 0, 0, 0);
        return;
    }
    const rgb_t c = hsv_to_rgb((hsv_t){h, TRGB_SAT, v});
    rgb_matrix_set_color(led, c.r, c.g, c.b);
}

// A pulse between the hue's floor and its peak: the ease of `wave` (0..255) mapped
// onto the levels that still show the colour, so the bottom of a breath is the colour
// dimmed, never its primary and never off.
static uint8_t pulse_lvl(uint8_t h, uint8_t val, uint8_t wave) {
    const uint16_t fl = ((uint16_t)hue_floor(h) * 255u + val - 1u) / val;   // floor as a level
    const uint16_t lo = fl > 255u ? 255u : fl;
    return (uint8_t)(lo + ((255u - lo) * wave) / 255u);
}

bool tutorial_rgb_paint(void) {
    if (!s_owned) return false;
    // The opening rainbow is the stock effect itself: let it show.
    if (startup_anim_rainbow_level() > 0u) return false;
    if (!s_map_built) build_map();
    const bool    left = is_keyboard_left();
    const uint8_t lo   = left ? 0u : k_split[0];
    const uint8_t hi   = left ? k_split[0] : (uint8_t)(k_split[0] + k_split[1]);

    const uint32_t now  = timer_read32();
    const uint32_t dt   = (uint32_t)(now - s_last);
    s_last              = now;
    const uint32_t down = (dt * 255u) / TRGB_FALL_MS;
    // The pulse follows the keycap's own (tut_pulse_level(), same clock, same curve).
    const uint8_t  wave = tut_pulse_level(now, 255u);

    // The pointed-at key: its own colour for as long as it is asked for. Both halves
    // know the slot and the phase from the sync, so both pick the same colour.
    const uint8_t pslot = tutorial_pulsed_slot();
    const uint8_t phue  = k_hues[(uint8_t)(pslot * 7u + tutorial_rgb_phase() * 3u) % TRGB_NHUES];
    // A language name: its colour, fading in with each key's cascade, each key breathing
    // on its own clock (see TRGB_NAME_STAGGER_MS).
    const bool    named = tutorial_showing_name();
    const uint8_t nhue  = k_hues[(uint8_t)(tutorial_preview_entry() * 4u) % TRGB_NHUES];

    // A board-wide sweep (the reveal, a language wipe) lights the keys under its band,
    // each fading once it has passed. One colour per sweep, the same on both halves:
    // the centre, the phase and the preview item are all synced.
    poly_focus_band_t band;
    const bool    sweep = poly_focus_sweep_band(&band);
    const uint8_t shue  = sweep ? k_hues[(uint8_t)(band.cx * 3 + band.cy * 5 + tutorial_rgb_phase() +
                                                   tutorial_preview_entry() * 2u) % TRGB_NHUES]
                                : 0u;

    for (uint8_t i = lo; i < hi; ++i) {
        const uint8_t slot = s_led_slot[i];
        uint8_t target = 0u, hue = s_hue[i], val = 0u;
        if (sweep && slot != TUT_SLOT_NONE) {
            const sa_geom_t g = startup_anim_key_geom(TUT_SLOT_RIGHT(slot), TUT_SLOT_IDX(slot));
            if (g.valid) {
                const int32_t  dx = (int32_t)g.cx - band.cx, dy = (int32_t)g.cy - band.cy;
                const uint32_t d2 = (uint32_t)(dx * dx + dy * dy);
                if (d2 <= band.outer2 && d2 >= band.inner2) {
                    target = band.dens;
                    hue    = shue;
                    val    = TRGB_SWEEP_VAL;
                }
            }
        }
        if (slot != TUT_SLOT_NONE && slot == pslot) {
            target = pulse_lvl(phue, TRGB_PULSE_VAL, wave);
            hue    = phue;
            val    = TRGB_PULSE_VAL;
        } else if (named && slot != TUT_SLOT_NONE && s_led_rc[i] != 0xFFu &&
                   tutorial_is_name_key((uint8_t)(s_led_rc[i] >> 4), (uint8_t)(s_led_rc[i] & 0x0Fu))) {
            const uint8_t in    = menu_cascade_key_level(TUT_SLOT_RIGHT(slot), TUT_SLOT_IDX(slot));
            // Full pulse depth, floor to peak: at a peak of 16 that is about 7..16, still
            // "very lightly", and the per-key stagger needs the depth to be seen at all.
            const uint8_t kwave = tut_pulse_level(now + (uint32_t)slot * TRGB_NAME_STAGGER_MS, 255u);
            target = (uint8_t)(((uint16_t)in * pulse_lvl(nhue, TRGB_NAME_VAL, kwave)) / 255u);
            hue    = nhue;
            val    = TRGB_NAME_VAL;
        }
        uint32_t lvl = s_lvl[i];
        if (target > 0u) {
            // Wanted: follow the pulse and the fade-in exactly, in its colour.
            lvl      = target;
            s_hue[i] = hue;
            s_val[i] = val;
            s_lvl[i] = (uint8_t)lvl;
            set_glow(i, hue, val, (uint8_t)lvl);
            continue;
        }
        // No longer wanted: fade out in the colour it had.
        lvl      = lvl < down ? 0u : lvl - down;
        s_lvl[i] = (uint8_t)lvl;
        set_glow(i, s_hue[i], s_val[i], (uint8_t)lvl);
    }
    return true;
}

#else

void tutorial_rgb_tick(void) {}
bool tutorial_rgb_paint(void) { return false; }

#endif
