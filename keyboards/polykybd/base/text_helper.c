// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#include "text_helper.h"

#include QMK_KEYBOARD_H

const uint32_t* get_led_matrix_text(uint8_t rgb_mode) {
    switch(rgb_mode) {
        case RGB_MATRIX_SPLASH:
            return U"Sp";
        case RGB_MATRIX_MULTISPLASH:
            return U"SpMu";
        case RGB_MATRIX_SOLID_SPLASH:
            return U"SpSo";
        case RGB_MATRIX_SOLID_MULTISPLASH:
            return U"SpSM";
        case RGB_MATRIX_RAINBOW_MOVING_CHEVRON:
            return U"Rnbw";
        case RGB_MATRIX_BREATHING:
            return U"Brth";
        case RGB_MATRIX_SOLID_REACTIVE_SIMPLE:
            return U"Smpl";
        case RGB_MATRIX_SOLID_REACTIVE_CROSS:
            return U"Crss";
        case RGB_MATRIX_SOLID_REACTIVE:
            return U"Rctv";
        case RGB_MATRIX_SOLID_REACTIVE_WIDE:
            return U"RtWd";
        case RGB_MATRIX_SOLID_REACTIVE_MULTIWIDE:
            return U"RtMW";
        case RGB_MATRIX_SOLID_REACTIVE_MULTICROSS:
            return U"RtMC";
        case RGB_MATRIX_SOLID_REACTIVE_MULTINEXUS:
            return U"RtMN";
        case RGB_MATRIX_SOLID_REACTIVE_NEXUS:
            return U"RtNe";
        case RGB_MATRIX_ALPHAS_MODS:
            return U"AlpM";
        case RGB_MATRIX_GRADIENT_UP_DOWN:
            return U"GrUD";
        case RGB_MATRIX_GRADIENT_LEFT_RIGHT:
            return U"GrLR";
        case RGB_MATRIX_BAND_SAT:
            return U"BndS";
        case RGB_MATRIX_BAND_VAL:
            return U"BndV";
        case RGB_MATRIX_BAND_PINWHEEL_SAT:
            return U"BPwS";
        case RGB_MATRIX_BAND_PINWHEEL_VAL:
            return U"BPwV";
        case RGB_MATRIX_BAND_SPIRAL_SAT:
            return U"BSpS";
        case RGB_MATRIX_CYCLE_ALL:
            return U"All";
        case RGB_MATRIX_CYCLE_LEFT_RIGHT:
            return U"CyLR";
        case RGB_MATRIX_CYCLE_UP_DOWN:
            return U"CyUD";
        case RGB_MATRIX_CYCLE_OUT_IN:
            return U"CyOI";
        case RGB_MATRIX_CYCLE_OUT_IN_DUAL:
            return U"CyDu";
        case RGB_MATRIX_CYCLE_PINWHEEL:
            return U"CyPw";
        case RGB_MATRIX_CYCLE_SPIRAL:
            return U"CySp";
        case RGB_MATRIX_DUAL_BEACON:
            return U"DuBc";
        case RGB_MATRIX_RAINBOW_BEACON:
            return U"RnBc";
        case RGB_MATRIX_RAINBOW_PINWHEELS:
            return U"RnPw";
        case RGB_MATRIX_RAINDROPS:
            return U"Rndp";
        case RGB_MATRIX_JELLYBEAN_RAINDROPS:
            return U"JRnp";
        case RGB_MATRIX_HUE_BREATHING:
            return U"HueB";
        case RGB_MATRIX_HUE_PENDULUM:
            return U"HueP";
        case RGB_MATRIX_HUE_WAVE:
            return U"HueW";
        case RGB_MATRIX_PIXEL_FRACTAL:
            return U"PxlF";
        case RGB_MATRIX_PIXEL_FLOW:
            return U"PxlF";
        case RGB_MATRIX_PIXEL_RAIN:
            return U"PxlR";
        default:
            return U"Unkn";
    }
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

