// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
// The idle screen's poems and their timeline. See idle_poem_plan.h.

#include "idle_poem_plan.h"

#include <stddef.h>

const char idle_poem_0[] =
    "Every key knows its letter,\n"
    "every letter finds its key.\n"
    "Type a word, then type a better -\n"
    "the keyboard waits for me.";
const char idle_poem_1[] =
    "Seventy-two small screens,\n"
    "each one a tiny page.\n"
    "Press one, and watch it change -\n"
    "a stage upon a stage.";
const char idle_poem_2[] =
    "Soft clicks in the evening,\n"
    "letters falling into line.\n"
    "One key, then another,\n"
    "until the words are mine.";
const char idle_poem_3[] =
    "A thousand words a day,\n"
    "and not one of them mine.\n"
    "I only hold the letters\n"
    "until you make them shine.";
const char idle_poem_4[] =
    "When the room is quiet\n"
    "and the cursor stops to rest,\n"
    "I dream in little letters -\n"
    "the short words are the best.";
_Static_assert(sizeof(idle_poem_0) - 1u <= 255u, "a poem is indexed by a uint8_t");
_Static_assert(sizeof(idle_poem_1) - 1u <= 255u, "a poem is indexed by a uint8_t");
_Static_assert(sizeof(idle_poem_2) - 1u <= 255u, "a poem is indexed by a uint8_t");
_Static_assert(sizeof(idle_poem_3) - 1u <= 255u, "a poem is indexed by a uint8_t");
_Static_assert(sizeof(idle_poem_4) - 1u <= 255u, "a poem is indexed by a uint8_t");

const char *const idle_poems[] = {idle_poem_0, idle_poem_1, idle_poem_2, idle_poem_3, idle_poem_4};
#define POEM_N (sizeof(idle_poems) / sizeof(idle_poems[0]))
_Static_assert(POEM_N >= 2u, "idle_poem_pick() needs two poems");
const uint8_t idle_poem_count = (uint8_t)POEM_N;

static uint32_t key_ms(char c) { return IDLE_POEM_KEY_MS + (c == '\n' ? IDLE_POEM_LINE_MS : 0u); }

static bool blink(uint32_t u) { return ((u / IDLE_POEM_BLINK_MS) & 1u) == 0u; }

uint8_t idle_poem_len(const char *poem) {
    uint8_t n = 0;
    while (poem[n] != '\0') ++n;
    return n;
}

uint32_t idle_poem_type_ms(const char *poem) {
    uint32_t ms = 0;
    for (const char *c = poem; *c; ++c) ms += key_ms(*c);
    return ms;
}

uint32_t idle_poem_phase_ms(void) {
    uint32_t longest = 0;
    for (uint8_t p = 0; p < POEM_N; ++p) {
        const uint32_t run = idle_poem_type_ms(idle_poems[p]) + (uint32_t)idle_poem_len(idle_poems[p]) * IDLE_POEM_BS_MS;
        if (run > longest) longest = run;
    }
    return IDLE_POEM_LEAD_MS + longest + IDLE_POEM_HOLD_MS + IDLE_POEM_EMPTY_MS;
}

bool idle_poem_frame(const char *poem, uint32_t u, idle_poem_frame_t *out) {
    const uint8_t  len  = idle_poem_len(poem);
    const uint32_t type = idle_poem_type_ms(poem);
    if (u < IDLE_POEM_LEAD_MS) {
        *out = (idle_poem_frame_t){0, blink(u)};
        return true;
    }
    u -= IDLE_POEM_LEAD_MS;
    if (u < type) {
        // Character i appears at the sum of the keystroke times before it.
        uint32_t at = 0, last = 0;
        uint8_t  typed = 0;
        while (typed < len && at <= u) {
            last = at;
            at += key_ms(poem[typed]);
            ++typed;
        }
        // Steady while keys are coming, blinking while the typist waits at a line break.
        const uint32_t since = u - last;
        *out = (idle_poem_frame_t){typed, since < IDLE_POEM_KEY_MS || blink(since - IDLE_POEM_KEY_MS)};
        return true;
    }
    u -= type;
    if (u < IDLE_POEM_HOLD_MS) {
        *out = (idle_poem_frame_t){len, blink(u)};
        return true;
    }
    u -= IDLE_POEM_HOLD_MS;
    if (u < (uint32_t)len * IDLE_POEM_BS_MS) {
        // Backspace: the first press takes the last character at once.
        *out = (idle_poem_frame_t){(uint8_t)(len - 1u - u / IDLE_POEM_BS_MS), true};
        return true;
    }
    u -= (uint32_t)len * IDLE_POEM_BS_MS;
    if (u < IDLE_POEM_EMPTY_MS) {
        *out = (idle_poem_frame_t){0, blink(u)};
        return true;
    }
    return false;
}

// A 32-bit mix of two numbers (multiply-xorshift).
static uint32_t mix(uint32_t a, uint32_t b) {
    uint32_t x = a * 0x9E3779B1u ^ b * 0x85EBCA6Bu;
    x ^= x >> 15;
    x *= 0x2C1B3C6Du;
    x ^= x >> 12;
    return x;
}

// Each cycle steps 1..N-1 poems on from the last, the step drawn from a hash of the
// seed and the cycle, so a poem never repeats back to back and a new seed starts
// somewhere new. The walk from the session's first cycle is a few dozen steps at most:
// the panels turn off (TURN_OFF_TIME) long before then.
uint8_t idle_poem_pick(uint16_t seed, uint32_t cycle) {
    uint8_t p = (uint8_t)(mix(seed, 0xFFFFFFFFu) % POEM_N);
    for (uint32_t c = 0; c <= cycle; ++c) p = (uint8_t)((p + 1u + mix(seed, c) % (POEM_N - 1u)) % POEM_N);
    return p;
}
