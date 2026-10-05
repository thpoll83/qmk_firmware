// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
// The idle screen's poems (split72 status panel): the texts, the typing and Backspace
// timeline, and the random pick. Pure: the caller passes "ms into the poem phase", so no
// timer, font or display is linked and the timeline is unit-tested
// (make test:polykybd_idle_poem_plan). The firmware binding is status_idle.c.
#pragma once

#include <stdbool.h>
#include <stdint.h>

// The poem phase, in order:
//   lead:    the cursor blinks at the start of the left panel;
//   typing:  a key every IDLE_POEM_KEY_MS, plus IDLE_POEM_LINE_MS after each '\n',
//            while the typist waits with the cursor blinking;
//   hold:    the whole poem stands, the cursor blinking after it;
//   delete:  Backspace from the end, one character every IDLE_POEM_BS_MS (a held key's
//            repeat), the cursor steady;
//   empty:   the cursor blinks on the emptied line;
//   then nothing until the phase ends.
// 150 and 75 ms divide both frame periods of status_idle.c (150 and 75 ms), so every
// keystroke lasts a whole number of frames; so do the 1200 ms waits.
#define IDLE_POEM_LEAD_MS  1200u
#define IDLE_POEM_KEY_MS   150u
#define IDLE_POEM_LINE_MS  1200u
#define IDLE_POEM_HOLD_MS  4000u
#define IDLE_POEM_BS_MS    75u
#define IDLE_POEM_EMPTY_MS 1200u
#define IDLE_POEM_BLINK_MS 530u   // the cursor's half-period while it waits

// The poems: printable ASCII and '\n' for a line break, each at most 255 characters.
// They are const, so they live in flash and cost no RAM.
extern const char *const idle_poems[];
extern const uint8_t     idle_poem_count;
// The same texts by name, so another const table can point at one (an array element is
// not a constant expression in C, an object's address is). The showroom demo types
// these rather than storing prose of its own (base/demo_plan.c).
extern const char idle_poem_0[], idle_poem_1[], idle_poem_2[], idle_poem_3[], idle_poem_4[];

// The length of `poem`, and how long typing it takes (every keystroke and line wait).
uint8_t  idle_poem_len(const char *poem);
uint32_t idle_poem_type_ms(const char *poem);

// How long the phase must last to play every poem in full: the lead, the longest
// poem's typing plus deleting, the hold and the empty cursor.
uint32_t idle_poem_phase_ms(void);

// What shows `u` ms into the phase: how many characters are on the line and whether
// the cursor is lit. False once the phase is past its empty cursor (nothing shows).
typedef struct {
    uint8_t typed;
    bool    cursor;
} idle_poem_frame_t;
bool idle_poem_frame(const char *poem, uint32_t u, idle_poem_frame_t *out);

// The poem for cycle `cycle` of a session seeded with `seed`: at random, and never the
// one the cycle before typed. A pure function of the two numbers, so two halves that
// share them type the same poem with no sync field of its own. Needs at least two poems.
uint8_t idle_poem_pick(uint16_t seed, uint32_t cycle);
