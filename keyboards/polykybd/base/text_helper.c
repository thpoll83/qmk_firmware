// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#include "text_helper.h"

#include QMK_KEYBOARD_H

// Names are the longest form that fits the status OLED's effect field (~86px in the
// 8pt font, TEXT_X+22..TEXT_R) — the row lost its speed readout to the vertical gauge
// in the indicator column, so there is finally room for words instead of 4-letter
// codes. Measure any new name against that budget; it is clipped, not wrapped.
const uint32_t* get_led_matrix_text(uint8_t rgb_mode) {
    switch(rgb_mode) {
        case RGB_MATRIX_SPLASH:
            return U"Splash";
        case RGB_MATRIX_MULTISPLASH:
            return U"Splash";
        case RGB_MATRIX_SOLID_SPLASH:
            return U"SolidSpl";
        case RGB_MATRIX_SOLID_MULTISPLASH:
            return U"SolidMulti";
        case RGB_MATRIX_RAINBOW_MOVING_CHEVRON:
            return U"Rainbow";
        case RGB_MATRIX_BREATHING:
            return U"Breathing";
        case RGB_MATRIX_SOLID_REACTIVE_SIMPLE:
            return U"Simple";
        case RGB_MATRIX_SOLID_REACTIVE_CROSS:
            return U"Cross";
        case RGB_MATRIX_SOLID_REACTIVE:
            return U"Reactive";
        case RGB_MATRIX_SOLID_REACTIVE_WIDE:
            return U"ReactWide";
        case RGB_MATRIX_SOLID_REACTIVE_MULTIWIDE:
            return U"MultiWide";
        case RGB_MATRIX_SOLID_REACTIVE_MULTICROSS:
            return U"MultiCross";
        case RGB_MATRIX_SOLID_REACTIVE_MULTINEXUS:
            return U"MultNexus";
        case RGB_MATRIX_SOLID_REACTIVE_NEXUS:
            return U"Nexus";
        case RGB_MATRIX_ALPHAS_MODS:
            return U"AlphaMod";
        case RGB_MATRIX_GRADIENT_UP_DOWN:
            return U"GradUpDn";
        case RGB_MATRIX_GRADIENT_LEFT_RIGHT:
            return U"GradLtRt";
        case RGB_MATRIX_BAND_SAT:
            return U"BandSat";
        case RGB_MATRIX_BAND_VAL:
            return U"BandVal";
        case RGB_MATRIX_BAND_PINWHEEL_SAT:
            return U"PinwhlSat";
        case RGB_MATRIX_BAND_PINWHEEL_VAL:
            return U"PinwhlVal";
        case RGB_MATRIX_BAND_SPIRAL_SAT:
            return U"SpiralSat";
        case RGB_MATRIX_CYCLE_ALL:
            return U"CycleAll";
        case RGB_MATRIX_CYCLE_LEFT_RIGHT:
            return U"CycleLtRt";
        case RGB_MATRIX_CYCLE_UP_DOWN:
            return U"CycleUpDn";
        case RGB_MATRIX_CYCLE_OUT_IN:
            return U"CycOutIn";
        case RGB_MATRIX_CYCLE_OUT_IN_DUAL:
            return U"CycleDual";
        case RGB_MATRIX_CYCLE_PINWHEEL:
            return U"CyclePnwl";
        case RGB_MATRIX_CYCLE_SPIRAL:
            return U"CycSpiral";
        case RGB_MATRIX_DUAL_BEACON:
            return U"DualBeacn";
        case RGB_MATRIX_RAINBOW_BEACON:
            return U"Beacon";
        case RGB_MATRIX_RAINBOW_PINWHEELS:
            return U"Wheel";
        case RGB_MATRIX_RAINDROPS:
            return U"Raindrops";
        case RGB_MATRIX_JELLYBEAN_RAINDROPS:
            return U"Jellybean";
        case RGB_MATRIX_HUE_BREATHING:
            return U"HueBreath";
        case RGB_MATRIX_HUE_PENDULUM:
            return U"Pendulum";
        case RGB_MATRIX_HUE_WAVE:
            return U"HueWave";
        case RGB_MATRIX_PIXEL_FRACTAL:
            return U"PixFract";
        case RGB_MATRIX_PIXEL_FLOW:
            return U"PixelFlow";
        case RGB_MATRIX_PIXEL_RAIN:
            return U"PixelRain";
        default:
            return U"Unknown";
    }
}

// MULTISPLASH shares SPLASH's name and is distinguished by a superscript 2 the caller
// draws after it ("Splash²"). The status OLED's font is ASCII 0x20..0x7e, so the
// superscript cannot live in the string itself.
bool led_matrix_text_superscript2(uint8_t rgb_mode) {
    return rgb_mode == RGB_MATRIX_MULTISPLASH;
}

// Sector boundaries are the usual colour-wheel names in DEGREES (hue byte scaled to
// 0..359), so they stay readable if the underlying 0..255 encoding ever changes.
// Below ~10% saturation the hue is not perceptible, so it reports "White" — showing
// "Green" for what the user sees as white would be worse than showing nothing.
const uint32_t* get_hue_name(uint8_t hue, uint8_t sat) {
    if (sat < 26) return U"White";
    const uint16_t deg = (uint16_t)(((uint16_t)hue * 360u) / 255u);
    if (deg <  15) return U"Red";
    if (deg <  45) return U"Orange";
    if (deg <  70) return U"Yellow";
    if (deg < 100) return U"Lime";
    if (deg < 165) return U"Green";
    if (deg < 195) return U"Cyan";
    if (deg < 240) return U"Azure";
    if (deg < 270) return U"Blue";
    if (deg < 300) return U"Violet";
    if (deg < 330) return U"Magenta";
    if (deg < 345) return U"Pink";
    return U"Red";   // wraps back past 345 deg
}

