// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#include "tutorial_plan.h"

// ---- phase table ----------------------------------------------------------
// Duration of each timed phase. TUT_LETTER_WAIT and TUT_DONE are 0 = "no timeout":
// only a press (or the skip) leaves them, which is what makes the tutorial wait for
// the user rather than run away from them.
static uint32_t tut_phase_ms(uint8_t phase) {
    switch (phase) {
        case TUT_BLANK:     return TUT_BLANK_MS;
        case TUT_TEXT:      return TUT_TEXT_MS;
        case TUT_LETTER_IN: return TUT_LETTER_IN_MS;
        case TUT_RIPPLE:    return TUT_RIPPLE_MS;
        case TUT_GAP:       return TUT_GAP_MS;
        default:            return 0;
    }
}

static void tut_enter(tut_state_t *st, uint8_t phase, uint32_t now) {
    st->phase       = phase;
    st->phase_start = now;
}

void tut_init(tut_state_t *st, const uint8_t slots[TUT_LETTERS], uint32_t now) {
    st->step        = 0;
    st->ripple_seq  = 0;
    st->ripple_slot = TUT_SLOT_NONE;
    st->skipped     = false;
    for (uint8_t i = 0; i < TUT_LETTERS; ++i) {
        st->slots[i] = slots ? slots[i] : TUT_SLOT_NONE;
    }
    tut_enter(st, TUT_BLANK, now);
}

bool tut_tick(tut_state_t *st, uint32_t now) {
    const uint32_t dur = tut_phase_ms(st->phase);
    if (dur == 0) return false;                       // waiting on the user, not the clock
    // Modular subtraction, so the 49.7-day timer wrap cannot strand a phase (the same
    // arithmetic base/update.c had to be corrected to).
    if ((uint32_t)(now - st->phase_start) < dur) return false;

    switch (st->phase) {
        case TUT_BLANK:     tut_enter(st, TUT_TEXT, now);      return true;
        case TUT_TEXT:      tut_enter(st, TUT_LETTER_IN, now); return true;
        case TUT_LETTER_IN: tut_enter(st, TUT_LETTER_WAIT, now); return true;
        case TUT_RIPPLE:    tut_enter(st, TUT_GAP, now);       return true;
        case TUT_GAP:
            if ((uint8_t)(st->step + 1u) >= TUT_LETTERS) {
                tut_enter(st, TUT_DONE, now);
            } else {
                st->step++;
                tut_enter(st, TUT_LETTER_IN, now);
            }
            return true;
        default: return false;
    }
}

bool tut_press(tut_state_t *st, uint8_t slot, uint32_t now) {
    // A press during the fade-in counts: the key is already visible and refusing it
    // would read as the board ignoring you.
    if (st->phase != TUT_LETTER_IN && st->phase != TUT_LETTER_WAIT) return false;
    if (slot == TUT_SLOT_NONE || slot != st->slots[st->step]) return false;

    st->ripple_slot = slot;
    st->ripple_seq++;                 // wraps harmlessly; the slave only tests for CHANGE
    tut_enter(st, TUT_RIPPLE, now);
    return true;
}

void tut_skip(tut_state_t *st, uint32_t now) {
    st->skipped = true;
    tut_enter(st, TUT_DONE, now);
}

uint8_t tut_current_slot(const tut_state_t *st) {
    switch (st->phase) {
        case TUT_LETTER_IN:
        case TUT_LETTER_WAIT:
        case TUT_RIPPLE:
            return st->step < TUT_LETTERS ? st->slots[st->step] : TUT_SLOT_NONE;
        default:
            return TUT_SLOT_NONE;
    }
}

uint8_t tut_phase_progress(const tut_state_t *st, uint32_t now) {
    const uint32_t dur = tut_phase_ms(st->phase);
    if (dur == 0) return 255;
    const uint32_t el = (uint32_t)(now - st->phase_start);
    if (el >= dur) return 255;
    return (uint8_t)((el * 255u) / dur);
}

// ---- curves ---------------------------------------------------------------

uint8_t tut_fade_contrast(uint8_t p) {
    // Smoothstep 3t^2 - 2t^3, i.e. t^2(3-2t), scaled to 0..255. Eased at both ends so
    // the letter emerges and settles rather than ramping linearly into place.
    const uint32_t x = p;
    return (uint8_t)((x * x * (765u - 2u * x)) / 65025u);
}

uint16_t tut_ripple_radius(uint8_t p) {
    // Ease-OUT: quick off the key, slowing as it goes — a struck-surface ripple, not a
    // constant-speed circle. Ends past the far corner of the 1673x563 board (the longest
    // reach is ~1765 board units) so it always leaves cleanly rather than stopping.
    const uint32_t q = 255u - (uint32_t)p;
    const uint32_t e = 65025u - q * q;                 // 0..65025, ease-out
    return (uint16_t)(5u + (1795u * e) / 65025u);
}

uint8_t tut_ripple_density(uint8_t p) {
    // Quadratic fade: solid as it leaves the key, thinning to nothing at the edge.
    const uint32_t q = 255u - (uint32_t)p;
    return (uint8_t)((q * q) / 255u);
}

// ---- letter selection -----------------------------------------------------

static uint32_t tut_hash32(uint32_t v) {
    v ^= v >> 15; v *= 0x2c1b3c6dU;
    v ^= v >> 12; v *= 0x297a2d39U;
    v ^= v >> 15; return v;
}

static bool tut_taken(const uint8_t *out, uint8_t n, uint8_t slot) {
    for (uint8_t i = 0; i < n; ++i) {
        if (out[i] == slot) return true;
    }
    return false;
}

uint8_t tut_choose_slots(const uint8_t *cand, uint8_t n_cand, uint32_t seed,
                         uint8_t out[TUT_LETTERS]) {
    for (uint8_t i = 0; i < TUT_LETTERS; ++i) out[i] = TUT_SLOT_NONE;
    if (!cand || n_cand == 0) return 0;

    // Rejection sampling rather than a shuffle: bounded, and it needs no scratch array
    // the size of the candidate list (72 keys) on the main-loop stack.
    uint8_t count = 0;
    for (uint16_t a = 0; a < 96u && count < TUT_LETTERS; ++a) {
        const uint8_t s = cand[tut_hash32(seed + a) % n_cand];
        if (!tut_taken(out, count, s)) out[count++] = s;
    }
    // Deterministic top-up when the pool is small or unlucky.
    for (uint8_t i = 0; i < n_cand && count < TUT_LETTERS; ++i) {
        if (!tut_taken(out, count, cand[i])) out[count++] = cand[i];
    }
    if (count < TUT_LETTERS) return count;            // caller: too few keys, no tutorial

    // Force the split to be crossed when it can be. The ripple travelling from one half
    // to the other is the thing this step exists to show, so a same-half draw is
    // repaired rather than accepted: swap the LAST pick for a candidate on the missing
    // half. Picking the last keeps the first (most-noticed) choice as drawn.
    bool have_left = false, have_right = false;
    for (uint8_t i = 0; i < TUT_LETTERS; ++i) {
        if (TUT_SLOT_RIGHT(out[i])) have_right = true; else have_left = true;
    }
    if (have_left != have_right) {
        const bool want_right = !have_right;
        for (uint16_t a = 0; a < 96u; ++a) {
            const uint8_t s = cand[tut_hash32(seed ^ (0xA5A5u + a)) % n_cand];
            if (TUT_SLOT_RIGHT(s) == want_right && !tut_taken(out, TUT_LETTERS, s)) {
                out[TUT_LETTERS - 1] = s;
                break;
            }
        }
        // No candidate on the other half at all (one half unpopulated): leave it be
        // rather than failing — a one-half tutorial still teaches the gesture.
    }
    return TUT_LETTERS;
}
