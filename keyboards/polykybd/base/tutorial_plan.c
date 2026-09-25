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
        case TUT_REVEAL:    return TUT_REVEAL_MS;
        case TUT_SHIFT_SWEEP: return TUT_SWEEP_MS;
        case TUT_SHIFT_HELD:  return TUT_SHIFT_HELD_MS;
        case TUT_LAYER_SWEEP: return TUT_SWEEP_MS;
        case TUT_LAYER_HELD:  return TUT_LAYER_HELD_MS;
        case TUT_NOTATION:    return TUT_NOTATION_MS;
        case TUT_BOARD_REVEAL: return TUT_BOARD_REVEAL_MS;
        case TUT_BOARD_SHOW:   return TUT_BOARD_SHOW_MS;
        case TUT_LANG_INTRO:   return TUT_LANG_INTRO_MS;
        case TUT_LANG_NAME:    return TUT_LANG_NAME_MS;
        case TUT_LANG_SHOW:    return TUT_LANG_ITEM_MS;
        case TUT_LANG_POINT:   return TUT_LANG_POINT_MS;
        case TUT_FINALE:       return TUT_FINALE_MS;
        default:            return 0;
    }
}

static uint8_t tut_point_slot_of(const tut_state_t *st, uint8_t phase);

static void tut_enter(tut_state_t *st, uint8_t phase, uint32_t now) {
    st->phase       = phase;
    st->phase_start = now;
    // ⚠️ A phase that POINTS fires its ring at once, not a period later. Back-dating is
    // how, and it belongs here rather than at each transition: entering a pointing wait
    // and then showing nothing for TUT_POINT_PERIOD_MS is three silent seconds at
    // exactly the moment the user is being asked to find a key.
    if (tut_point_slot_of(st, phase) != TUT_SLOT_NONE) {
        st->point_at = now - TUT_POINT_PERIOD_MS;
    }
}

void tut_init(tut_state_t *st, const uint8_t slots[TUT_LETTERS],
              const uint8_t shift_slots[TUT_SHIFT_STAGES], uint32_t now) {
    st->step        = 0;
    st->ripple_seq  = 0;
    st->ripple_slot = TUT_SLOT_NONE;
    st->hold_on     = false;
    st->skipped     = false;
    st->shift_stage = 0;
    st->point_at    = now;
    st->lang_slot   = TUT_SLOT_NONE;
    st->n_preview   = 0;
    st->preview     = 0;
    for (uint8_t i = 0; i < TUT_LETTERS; ++i) {
        st->slots[i] = slots ? slots[i] : TUT_SLOT_NONE;
    }
    for (uint8_t i = 0; i < TUT_SHIFT_STAGES; ++i) {
        st->shift_slots[i] = shift_slots ? shift_slots[i] : TUT_SLOT_NONE;
    }
    tut_enter(st, TUT_BLANK, now);
}

static uint8_t tut_point_slot_of(const tut_state_t *st, uint8_t phase) {
    switch (phase) {
        case TUT_SHIFT_WAIT:  return st->shift_slots[0];
        case TUT_SHIFT_AGAIN: return st->shift_slots[1];
        case TUT_LANG_POINT:  return st->lang_slot;
        default:              return TUT_SLOT_NONE;
    }
}

uint8_t tut_point_slot(const tut_state_t *st) { return tut_point_slot_of(st, st->phase); }

void tut_set_chapter3(tut_state_t *st, uint8_t lang_slot, uint8_t n_preview) {
    st->lang_slot = lang_slot;
    st->n_preview = (n_preview > TUT_PREVIEW_MAX) ? (uint8_t)TUT_PREVIEW_MAX : n_preview;
}

int16_t tut_preview_index(const tut_state_t *st) {
    if (st->phase != TUT_LANG_SHOW || st->preview >= st->n_preview) return -1;
    return st->preview;
}

int16_t tut_preview_pos(const tut_state_t *st) {
    if (st->phase != TUT_LANG_NAME && st->phase != TUT_LANG_SHOW) return -1;
    if (st->preview >= st->n_preview) return -1;
    return st->preview;
}

// The board reveal starts from the Shift the user last held — the right one when the
// board has it, since that is the stage that just ended — and bumps the ripple sequence
// so the slave arms its own wavefront from the same key.
static void tut_enter_reveal(tut_state_t *st, uint32_t now) {
    uint8_t origin = st->shift_slots[1];
    if (origin == TUT_SLOT_NONE) origin = st->shift_slots[0];
    st->ripple_slot = origin;
    st->ripple_seq++;
    tut_enter(st, TUT_BOARD_REVEAL, now);
}

// After the languages: point at the Lang key if the board has one, else finish.
static void tut_enter_after_preview(tut_state_t *st, uint32_t now) {
    tut_enter(st, st->lang_slot != TUT_SLOT_NONE ? TUT_LANG_POINT : TUT_FINALE, now);
}

// Re-fire the pointing ring if this phase is pointing and the period has elapsed.
// Returns true when it fired (the caller then syncs, and the ring starts on both
// halves). Split out of tut_tick because it runs on the phases tut_tick returns early
// from: a pointing wait has NO duration, which is exactly why the ring has to keep
// saying where to look.
static bool tut_point_tick(tut_state_t *st, uint32_t now) {
    const uint8_t slot = tut_point_slot(st);
    if (slot == TUT_SLOT_NONE) return false;
    if ((uint32_t)(now - st->point_at) < TUT_POINT_PERIOD_MS) return false;
    st->point_at    = now;
    st->ripple_slot = slot;
    st->ripple_seq++;          // wraps harmlessly; the slave only tests for CHANGE
    return true;
}

bool tut_tick(tut_state_t *st, uint32_t now) {
    // ⚠️ BEFORE the duration gate. A pointing wait has no duration, so anything past
    // that early return can never run on the phases that need a pointer.
    const bool pointed = tut_point_tick(st, now);

    const uint32_t dur = tut_phase_ms(st->phase);
    if (dur == 0) return pointed;                     // waiting on the user, not the clock
    // Modular subtraction, so the 49.7-day timer wrap cannot strand a phase (the same
    // arithmetic base/update.c had to be corrected to).
    if ((uint32_t)(now - st->phase_start) < dur) return pointed;

    switch (st->phase) {
        case TUT_BLANK:     tut_enter(st, TUT_TEXT, now);      return true;
        case TUT_TEXT:      tut_enter(st, TUT_LETTER_IN, now); return true;
        case TUT_LETTER_IN: tut_enter(st, TUT_LETTER_WAIT, now); return true;
        case TUT_RIPPLE:    tut_enter(st, TUT_GAP, now);       return true;
        case TUT_GAP:
            // The last letter hands over to chapter 2 rather than ending the tutorial.
            if ((uint8_t)(st->step + 1u) >= TUT_LETTERS) {
                tut_enter(st, TUT_REVEAL, now);
            } else {
                st->step++;
                tut_enter(st, TUT_LETTER_IN, now);
            }
            return true;
        case TUT_REVEAL:      tut_enter(st, TUT_SHIFT_WAIT, now); return true;
        case TUT_SHIFT_SWEEP: tut_enter(st, TUT_SHIFT_HELD, now); return true;
        // Chapter 2 hands over to chapter 3. The lit set does not change, so there is
        // no repaint to pay for: the layer keys have been on screen since the reveal,
        // which is also why the one being asked for is already familiar.
        case TUT_SHIFT_HELD:
            st->hold_on = false;
            // ⚠️ THE CHAPTER RUNS ONCE PER HAND. Shift is the one modifier that exists
            // twice, and a user who only ever meets the left one has been taught that
            // the LEFT key does this — not that the key does. The second pass costs a
            // few seconds, reuses every phase, and is the only place the lesson can say
            // "either hand" without saying it in words.
            if (st->shift_stage == 0 && st->shift_slots[1] != TUT_SLOT_NONE) {
                st->shift_stage = 1;
                tut_enter(st, TUT_SHIFT_AGAIN, now);   // which points at the other one at once
                return true;
            }
            // ⚠️ THE LAYER CHAPTER IS POSTPONED, not deleted. Its phases, its lit set,
            // its prose and its tests are all still here and still covered — only this
            // one transition is redirected. Chapter 3 is now the board reveal and the
            // languages; restoring the layer chapter means routing here to
            // TUT_LAYER_WAIT and sending TUT_NOTATION on to the reveal.
            tut_enter_reveal(st, now);
            return true;
        case TUT_LAYER_SWEEP: tut_enter(st, TUT_LAYER_HELD, now); return true;
        case TUT_LAYER_HELD:  tut_enter(st, TUT_NOTATION, now);   return true;
        case TUT_NOTATION:    tut_enter(st, TUT_DONE, now);       return true;
        case TUT_BOARD_REVEAL: tut_enter(st, TUT_BOARD_SHOW, now); return true;
        case TUT_BOARD_SHOW:
            // No renderable language or script (a board with no font pack yet): the
            // reveal alone is the chapter, and the board goes straight to the finale.
            if (st->n_preview == 0) {
                tut_enter_after_preview(st, now);
            } else {
                tut_enter(st, TUT_LANG_INTRO, now);
            }
            return true;
        case TUT_LANG_INTRO:
            st->preview = 0;
            tut_enter(st, TUT_LANG_NAME, now);
            return true;
        case TUT_LANG_NAME:
            tut_enter(st, TUT_LANG_SHOW, now);
            return true;
        case TUT_LANG_SHOW:
            // NAME then SHOW per item: the phase clock is the item clock.
            if ((uint8_t)(st->preview + 1u) < st->n_preview) {
                st->preview++;
                tut_enter(st, TUT_LANG_NAME, now);
            } else {
                // No reset of `preview` needed: tut_preview_index() answers -1 in every
                // phase but TUT_LANG_SHOW, which is what ends the last item's preview.
                tut_enter_after_preview(st, now);
            }
            return true;
        case TUT_LANG_POINT:  tut_enter(st, TUT_FINALE, now); return true;
        case TUT_FINALE:      tut_enter(st, TUT_DONE, now);   return true;
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

bool tut_hold(tut_state_t *st, tut_hold_kind_t kind, bool pressed, uint8_t slot,
              uint32_t now) {
    // Which three phases this kind of key drives. Anything else it is pressed in — a
    // Shift during the layer chapter, an Fn during the shift chapter — falls straight
    // through to the default below and does nothing, deliberately: silence is the
    // correction here exactly as it is for a wrong letter in chapter 1.
    // ⚠️ TWO wait phases for shift, one for the layer chapter: chapter 2 asks for the
    // left hand and then the right, and TUT_SHIFT_AGAIN is simply the second wait.
    const bool    is_shift = (kind == TUT_HOLD_SHIFT);
    const bool    waiting   = is_shift
                                  ? (st->phase == TUT_SHIFT_WAIT || st->phase == TUT_SHIFT_AGAIN)
                                  : (st->phase == TUT_LAYER_WAIT);
    const uint8_t sweep = is_shift ? TUT_SHIFT_SWEEP : TUT_LAYER_SWEEP;
    const uint8_t held  = is_shift ? TUT_SHIFT_HELD  : TUT_LAYER_HELD;

    if (waiting) {
        if (!pressed) return false;              // a release with nothing held: ignore
        // ⚠️ The OTHER shift does not satisfy this stage. The ring is pointing at one
        // key; accepting its twin would make the pointer a decoration and let both
        // stages be cleared with the same hand, which is the one thing the second stage
        // exists to prevent. A wrong shift does nothing at all — the same silence a
        // wrong letter gets in chapter 1.
        if (is_shift) {
            const uint8_t want = tut_point_slot(st);
            if (want != TUT_SLOT_NONE && slot != want) return false;
        }
        // ⚠️ NO RING ON THE EDGE. The ring's job in this chapter is to POINT at the key
        // you have not found yet; once you are holding it, you have found it, and a
        // ripple bursting out of the key under your own finger is answering a question
        // nobody asked. Chapter 1 is the opposite — there the ring IS the confirmation.
        //
        // Nothing is lost by leaving ripple_seq alone: the repaint that follows a Shift
        // is the board's own (intro mode, the stock renderer, the stock mods sync), and
        // the slave hears about the phase move through the ordinary tut[] push that
        // this `true` return arms.
        st->hold_on = true;
        tut_enter(st, sweep, now);
        return true;
    }
    if (st->phase == sweep || st->phase == held) {
        // A second edge inside the chapter must NOT restart the phase clock — the
        // chapter ends when its dwell does, however many times the key is tapped
        // meanwhile. Tracking hold_on is all there is to do; the legends follow the
        // real modifier through the normal render path.
        if (pressed == st->hold_on) return false;
        st->hold_on = pressed;
        return true;
    }
    return false;
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

uint8_t tut_ripple_travel(uint8_t p) {
    // Ease-OUT: quick off the key, slowing as it goes — a struck-surface ripple, not a
    // constant-speed circle.
    const uint32_t q = 255u - (uint32_t)p;
    return (uint8_t)(((65025u - q * q) * 255u) / 65025u);
}

uint16_t tut_sweep_radius(uint8_t p) {
    return (uint16_t)(((uint32_t)TUT_SWEEP_MAX_R * tut_ripple_travel(p)) / 255u);
}

uint8_t tut_ring_width(uint8_t p) {
    const uint32_t t = tut_ripple_travel(p);
    return (uint8_t)(TUT_RING_W0 - ((TUT_RING_W0 - TUT_RING_W) * t) / 255u);
}

uint16_t tut_ripple_radius(uint8_t p) {
    // Ease-OUT of the travel curve across R0..MAX_R: quick off the key, settling as it
    // goes — a struck surface, not a constant-speed circle.
    return (uint16_t)(TUT_RIPPLE_R0 +
                      ((TUT_RIPPLE_MAX_R - TUT_RIPPLE_R0) * (uint32_t)tut_ripple_travel(p)) / 255u);
}

tut_ring_t tut_ring_bounds(uint16_t outer, uint16_t width) {
    const uint32_t inner = (outer > width) ? (uint32_t)(outer - width) : 0u;
    tut_ring_t b;
    b.outer2 = (uint32_t)outer * outer;
    b.inner2 = inner * inner;
    return b;
}

uint8_t tut_dither(int16_t x, int16_t y) {
    // Two odd primes to decorrelate the axes, then an avalanche so neighbouring
    // positions land nowhere near each other. Integer only and no division — this runs
    // once per lit ring pixel.
    uint32_t h = ((uint32_t)(uint16_t)x * 73856093u) ^ ((uint32_t)(uint16_t)y * 19349663u);
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return (uint8_t)(h & 0xFFu);
}

// The elapsed-ms codec the ripple clock rides on. Saturating rather than wrapping: a
// value past the end of the ripple must read as "the very end", never as "just started".
_Static_assert(TUT_RIPPLE_MS / TUT_SYNC_TICK_MS <= 255u,
               "TUT_RIPPLE_MS no longer fits the one-byte sync clock — widen the field");

uint8_t tut_elapsed_encode(uint32_t ms) {
    const uint32_t units = ms / TUT_SYNC_TICK_MS;
    return (uint8_t)(units > 255u ? 255u : units);
}

uint32_t tut_elapsed_decode(uint8_t units) {
    return (uint32_t)units * TUT_SYNC_TICK_MS;
}

uint8_t tut_ripple_density(uint8_t p) {
    // ⚠️ A function of the RADIUS alone. Three rounds of keying this to the clock kept
    // producing a dissolve that happened where it could not be seen; asking "how far
    // out is the ring" instead makes the curve say exactly what the eye judges, and
    // makes it impossible for the ink to fade on a different schedule from the wave.
    const uint16_t r = tut_ripple_radius(p);
    if (r <= TUT_RIPPLE_SOLID_R) return 255u;
    if (r >= TUT_RIPPLE_MAX_R) return 0u;
    // Quadratic in the REMAINING distance: sheds pixels fastest right after the solid
    // stretch, then trails off — "lose more and more pixels on the way to the max
    // expansion". A linear ramp over the same distance still reads solid for its first
    // third, which is what round 4 shipped.
    const uint32_t span = TUT_RIPPLE_MAX_R - TUT_RIPPLE_SOLID_R;
    const uint32_t left = TUT_RIPPLE_MAX_R - r;
    return (uint8_t)((255u * left * left) / (span * span));
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
