// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

// How bright the agent status light is, given the state and how long it has held.
//
// The point of the whole file: a light that never goes out is furniture. IDLE (green)
// and ATTENTION (blinking red) are STANDING states — an agent can sit in either for
// hours — so after a minute they fade away and the keyboard looks like a keyboard
// again. WORKING (breathing amber) does NOT fade: it is bounded by the work itself,
// and it is the one state where a glance at the board has to answer "is it still
// going?" whatever o'clock it is.
//
// Pure and dependency-free (no quantum.h, no clock) for the same reason as
// fw_up_verdict.c and macro_decode.c: the arithmetic is the part with a bug history,
// and it is only testable once it stops sharing a function with the I/O. Timer
// handling in particular has bitten this firmware before — see the `int32_t
// last_update` sign-bit note in CLAUDE.md — so the caller passes an already-computed
// elapsed from timer_elapsed32() and everything downstream is plain arithmetic.
//
// ⚠️ The AI_LIGHT_* values MIRROR `enum poly_ai_state` (state.h), which cannot be
// included here — it pulls in quantum.h. poly_keymap.c _Static_asserts the two agree,
// so a reordered enum is a build failure rather than a light of the wrong colour.
#define AI_LIGHT_OFF       0
#define AI_LIGHT_IDLE      1
#define AI_LIGHT_WORKING   2
#define AI_LIGHT_ATTENTION 3
#define AI_LIGHT_COUNT     4

// Full brightness for a minute, then down to nothing over three seconds. The hold is
// what the user asked for; the ramp is what makes it read as a light going out rather
// than a light being switched off, which is the difference between "it finished" and
// "it broke".
#define AI_LIGHT_HOLD_MS 60000u
#define AI_LIGHT_FADE_MS  3000u

// 255 = paint the state's colour as-is, 0 = paint nothing. Scaling rather than a
// boolean keeps the fade in ONE place: every state's colour rides the same curve, so
// a fourth state added later cannot forget to fade.
static inline uint8_t ai_light_scale(uint8_t state, uint32_t elapsed_ms) {
    if (state == AI_LIGHT_OFF || state >= AI_LIGHT_COUNT) {
        return 0;
    }
    if (state == AI_LIGHT_WORKING) {
        return 255;   // busy: stays lit for as long as it takes
    }
    if (elapsed_ms < AI_LIGHT_HOLD_MS) {
        return 255;
    }
    const uint32_t into = elapsed_ms - AI_LIGHT_HOLD_MS;
    if (into >= AI_LIGHT_FADE_MS) {
        return 0;
    }
    // 3000 * 255 fits a uint32 with room to spare, so no intermediate overflow.
    return (uint8_t)(255u - (into * 255u) / AI_LIGHT_FADE_MS);
}

// Does this state still want the LED at all? The borrow that switches a disabled RGB
// matrix ON for the status light asks through here, so a faded-out light RELEASES the
// matrix instead of holding it on to display black.
static inline bool ai_light_wants_led(uint8_t state, uint32_t elapsed_ms) {
    return ai_light_scale(state, elapsed_ms) != 0;
}

// Apply the scale to one colour channel. Rounds to nearest so a dim channel (IDLE's
// green is 14) spends its last step at 1 rather than dropping two levels at once.
static inline uint8_t ai_light_apply(uint8_t channel, uint8_t scale) {
    return (uint8_t)(((uint16_t)channel * scale + 127u) / 255u);
}
