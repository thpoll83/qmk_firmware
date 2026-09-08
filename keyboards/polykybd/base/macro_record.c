// Copyright 2026 Thomas Pollak
// SPDX-License-Identifier: GPL-2.0-or-later

#include "macro_record.h"

#include <string.h>

// The tap collapse below matches the last three bytes as "PREFIX, OP_DOWN, code". A
// DELAY step is PREFIX, OP_DELAY, then ASCII digits -- so at TWO digits or more the
// third byte from the end is a digit rather than PREFIX and the match cannot land on
// one. At a single digit it could, and rewriting that byte would turn a delay into a
// tap of whichever keycode the digit happens to be (0x30..0x39 are all real keycodes).
// Mutation testing found the OP_DOWN clause unobservable, which is exactly right and
// is a property of this constant, not of the clause -- so pin the constant.
_Static_assert(POLY_MACRO_REC_GAP_MS >= 10,
               "a delay shorter than two digits could be mistaken for a DOWN step");

// ---------------------------------------------------------------------------
// Encoder

void poly_macro_rec_begin(poly_macro_rec_t *r, uint8_t *buf, uint16_t cap) {
    memset(r, 0, sizeof(*r));
    r->buf = buf;
    r->cap = cap;
}

bool poly_macro_rec_full(const poly_macro_rec_t *r) {
    return r->full;
}

// Digits for a DELAY step. Written most-significant first into `out`, which holds at
// most the four of POLY_MACRO_REC_GAP_MAX_MS.
static uint8_t delay_digits(uint16_t ms, uint8_t *out) {
    uint8_t tmp[5];
    uint8_t n = 0;
    do {
        tmp[n++] = (uint8_t)('0' + (ms % 10u));
        ms /= 10u;
    } while (ms > 0u && n < sizeof(tmp));
    for (uint8_t i = 0; i < n; i++) {
        out[i] = tmp[n - 1u - i];
    }
    return n;
}

// The pause to encode before the next step, or 0 for none. Rounded so a 214 ms and a
// 219 ms gap come out identical, and capped -- a long pause was the user thinking.
static uint16_t gap_ms(const poly_macro_rec_t *r, uint32_t now_ms) {
    if (!r->any) return 0;  // nothing to wait after
    uint32_t gap = now_ms - r->last_ms;
    if (gap < POLY_MACRO_REC_GAP_MS) return 0;
    if (gap > POLY_MACRO_REC_GAP_MAX_MS) gap = POLY_MACRO_REC_GAP_MAX_MS;
    gap = ((gap + (POLY_MACRO_REC_GAP_ROUND_MS / 2u)) / POLY_MACRO_REC_GAP_ROUND_MS)
          * POLY_MACRO_REC_GAP_ROUND_MS;
    return (uint16_t)gap;
}

bool poly_macro_rec_key(poly_macro_rec_t *r, uint8_t code, bool pressed, uint32_t now_ms) {
    if (r->full || r->buf == NULL) return false;

    // A press and release with nothing between them is one TAP. Checked BEFORE the gap
    // is encoded, because the hold time of a tap is not something to replay -- and
    // because the collapse rewrites a step that is already in the buffer, so a delay
    // emitted first would sit between the tap and whatever preceded it.
    //
    // "Nothing between them" is asked of the BYTES -- is the last step a DOWN of this
    // key -- rather than of a remembered offset. A first cut carried tap_at/tap_code
    // and additionally checked that len still equalled tap_at + 3; mutation testing
    // showed that clause could never fire, because every event either moves tap_at or
    // clears it, so the offset was always the most recent step. A guard whose
    // precondition nothing can violate is not a guard, and the state it needed is
    // exactly what the buffer already says.
    if (!pressed && r->len >= 3u
        && r->buf[r->len - 3u] == POLY_MACRO_PREFIX
        && r->buf[r->len - 2u] == POLY_MACRO_OP_DOWN
        && r->buf[r->len - 1u] == code
        && (now_ms - r->tap_ms) <= POLY_MACRO_REC_TAP_MS) {
        r->buf[r->len - 2u] = POLY_MACRO_OP_TAP;
        r->last_ms          = now_ms;
        return true;
    }

    // Build the gap and the step together so the space check covers both: a delay
    // written without the step it precedes is a body that waits and then does nothing.
    uint8_t  step[9];
    uint8_t  n   = 0;
    uint16_t pre = gap_ms(r, now_ms);
    if (pre > 0u) {
        step[n++] = POLY_MACRO_PREFIX;
        step[n++] = POLY_MACRO_OP_DELAY;
        n += delay_digits(pre, &step[n]);
    }
    step[n++] = POLY_MACRO_PREFIX;
    step[n++]              = pressed ? POLY_MACRO_OP_DOWN : POLY_MACRO_OP_UP;
    step[n++]              = code;

    // The reserve is what guarantees poly_macro_rec_finish() can always close every
    // open DOWN. Running into it is a clean stop at a step boundary, never a truncated
    // step -- half a DOWN replays as garbage, and a DOWN with no UP holds a modifier
    // down on the host after every replay.
    if ((uint32_t)r->len + n + POLY_MACRO_REC_RESERVE > r->cap) {
        r->full = true;
        return false;
    }

    memcpy(&r->buf[r->len], step, n);
    r->len = (uint16_t)(r->len + n);

    if (pressed) r->tap_ms = now_ms;
    r->any     = true;
    r->last_ms = now_ms;
    return true;
}

// Reader over the staging buffer, so the flush below can walk what it just encoded
// through the real decoder.
static uint8_t rec_read(uint16_t offset, void *ctx) {
    const poly_macro_rec_t *r = (const poly_macro_rec_t *)ctx;
    return r->buf[offset];
}

uint16_t poly_macro_rec_finish(poly_macro_rec_t *r) {
    if (r->buf == NULL || r->len == 0u) return r->len;

    // Which keys are still down, derived by READING BACK the body rather than from a
    // list maintained during capture. The invariant that matters is a property of the
    // bytes ("every DOWN here has an UP"), so the bytes are what should answer it --
    // and it makes the encoder and the decoder check each other for free.
    uint8_t  held[POLY_MACRO_REC_HELD_MAX];
    uint8_t  held_n = 0;
    uint16_t cursor = 0;
    while (cursor < r->len) {
        poly_macro_step_t s = poly_macro_decode(rec_read, r, cursor, r->len);
        if (s.kind == POLY_MACRO_STEP_END || s.next <= cursor) break;
        cursor = s.next;
        if (s.kind == POLY_MACRO_STEP_DOWN) {
            bool seen = false;
            for (uint8_t i = 0; i < held_n; i++) {
                if (held[i] == s.code) {
                    seen = true;
                    break;
                }
            }
            if (!seen && held_n < POLY_MACRO_REC_HELD_MAX) held[held_n++] = s.code;
        } else if (s.kind == POLY_MACRO_STEP_UP) {
            for (uint8_t i = 0; i < held_n; i++) {
                if (held[i] != s.code) continue;
                for (uint8_t j = (uint8_t)(i + 1u); j < held_n; j++) {
                    held[j - 1u] = held[j];
                }
                held_n--;
                break;
            }
        }
    }

    // Reverse press order: a chord is released the way a hand releases it, so the
    // modifier that was pressed first comes up last.
    while (held_n > 0u) {
        held_n--;
        if ((uint32_t)r->len + 3u > r->cap) break;  // the reserve should make this dead
        r->buf[r->len++] = POLY_MACRO_PREFIX;
        r->buf[r->len++] = POLY_MACRO_OP_UP;
        r->buf[r->len++] = held[held_n];
    }

    return r->len;
}

// ---------------------------------------------------------------------------
// Report diff

#define POLY_MACRO_REC_MOD_FIRST 0xE0u

// Is `code` one of the six slots? A 6KRO key array is a SET, not a positional record:
// QMK compacts it when a key is released, so a key that never moved can change slots.
// Comparing slot i to slot i therefore reports a release and a press for every key
// after the one that went up -- which replays as those keys being re-typed.
//
// ⚠️ No `code == 0` guard here, deliberately: both call sites screen the sentinel with
// their own `code != 0` before asking, so a guard in here is unreachable. It WAS here,
// and a mutation deleting it changed nothing at all -- which is the tell that it was
// decoration rather than a check. The live guard is at the call sites; keep it there.
static bool kro_has(const uint8_t *keys, uint8_t code) {
    for (uint8_t i = 0; i < POLY_MACRO_REC_KRO_KEYS; i++) {
        if (keys[i] == code) return true;
    }
    return false;
}

// 0xE0..0xE7 fall inside the 240-bit map AND are carried in the `mods` byte, so a
// scan that did not skip them could report one modifier twice -- a DOWN Ctrl followed
// by a second DOWN Ctrl, which replays as a stuck modifier when only one UP follows.
// QMK routes modifiers through `mods` today; skipping is a no-op in that case and the
// guard in the other, so it costs nothing to be right either way.
static bool nkro_skip(uint16_t code) {
    return code >= POLY_MACRO_REC_MOD_FIRST && code <= (POLY_MACRO_REC_MOD_FIRST + 7u);
}

static bool nkro_has(const uint8_t *bits, uint16_t code) {
    return (bits[code >> 3] & (uint8_t)(1u << (code & 7u))) != 0u;
}

// The four passes every diff makes, in the one order that replays as what was typed.
// Modifiers are 0xE0..0xE7 and are carried in the `mods` BYTE rather than the key
// array, so they are diffed bit by bit against that byte.
static void emit_mod_pass(uint8_t prev_mods, uint8_t next_mods, bool pressed,
                          poly_macro_rec_event_fn emit, void *ctx) {
    // A press is a bit that is set now and was not; a release is the complement.
    const uint8_t changed = pressed ? (uint8_t)(next_mods & (uint8_t)~prev_mods)
                                    : (uint8_t)(prev_mods & (uint8_t)~next_mods);
    for (uint8_t b = 0; b < 8u; b++) {
        if (changed & (uint8_t)(1u << b)) {
            emit((uint8_t)(POLY_MACRO_REC_MOD_FIRST + b), pressed, ctx);
        }
    }
}

void poly_macro_rec_diff_6kro(uint8_t prev_mods, const uint8_t *prev_keys,
                              uint8_t next_mods, const uint8_t *next_keys,
                              poly_macro_rec_event_fn emit, void *ctx) {
    if (emit == NULL || prev_keys == NULL || next_keys == NULL) return;

    for (uint8_t i = 0; i < POLY_MACRO_REC_KRO_KEYS; i++) {
        const uint8_t code = prev_keys[i];
        if (code != 0 && !kro_has(next_keys, code)) emit(code, false, ctx);
    }
    emit_mod_pass(prev_mods, next_mods, false, emit, ctx);
    emit_mod_pass(prev_mods, next_mods, true, emit, ctx);
    for (uint8_t i = 0; i < POLY_MACRO_REC_KRO_KEYS; i++) {
        const uint8_t code = next_keys[i];
        if (code != 0 && !kro_has(prev_keys, code)) emit(code, true, ctx);
    }
}

void poly_macro_rec_diff_nkro(uint8_t prev_mods, const uint8_t *prev_bits,
                              uint8_t next_mods, const uint8_t *next_bits,
                              poly_macro_rec_event_fn emit, void *ctx) {
    if (emit == NULL || prev_bits == NULL || next_bits == NULL) return;

    const uint16_t codes = (uint16_t)POLY_MACRO_REC_NKRO_BYTES * 8u;
    for (uint16_t c = 0; c < codes; c++) {
        if (nkro_skip(c)) continue;
        if (nkro_has(prev_bits, c) && !nkro_has(next_bits, c)) emit((uint8_t)c, false, ctx);
    }
    emit_mod_pass(prev_mods, next_mods, false, emit, ctx);
    emit_mod_pass(prev_mods, next_mods, true, emit, ctx);
    for (uint16_t c = 0; c < codes; c++) {
        if (nkro_skip(c)) continue;
        if (!nkro_has(prev_bits, c) && nkro_has(next_bits, c)) emit((uint8_t)c, true, ctx);
    }
}

// ---------------------------------------------------------------------------
// Splice

poly_macro_splice_result_t poly_macro_commit_begin(poly_macro_commit_t *c,
                                                   poly_macro_read_fn rd,
                                                   poly_macro_write_fn wr, void *ctx,
                                                   uint8_t id, const uint8_t *body,
                                                   uint16_t len, uint16_t end) {
    memset(c, 0, sizeof(*c));
    c->rd   = rd;
    c->wr   = wr;
    c->ctx  = ctx;
    c->body = body;
    c->len  = len;
    c->end  = end;

    // id+1 terminators have to fit before anything else does.
    if (end == 0u || id >= end) return POLY_MACRO_SPLICE_BAD_ID;

    const uint16_t start = poly_macro_find(rd, ctx, id, end);
    if (start >= end) return POLY_MACRO_SPLICE_BAD_ID;

    uint16_t term = start;
    while (term < end && rd(term, ctx) != 0u) {
        term++;
    }
    // No terminator at all means the region is not a macro buffer (or is mid-write).
    // Splicing into it would guess where the next macro starts.
    if (term >= end) return POLY_MACRO_SPLICE_BAD_ID;

    const uint16_t old_span = (uint16_t)(term + 1u - start);
    const uint16_t new_span = (uint16_t)(len + 1u);

    // Everything after this macro that is worth moving. poly_macro_used() stops at the
    // last NON-EMPTY macro, so trailing empty slots collapse into the zero fill and
    // re-materialise from it -- which is exactly what makes them free.
    const uint16_t tail_from = (uint16_t)(term + 1u);
    const uint16_t used      = poly_macro_used(rd, ctx, end);
    const uint16_t tail_len  = (used > tail_from) ? (uint16_t)(used - tail_from) : 0u;

    // Where the data ends once this splice lands, i.e. the first free byte.
    const int32_t new_end = (int32_t)start + (int32_t)new_span + (int32_t)tail_len;
    // Byte end-1 must stay NUL: it is the marker poly_macro_buffer_intact() reads, so
    // a body that reached it would leave the whole buffer unplayable.
    if (new_end >= (int32_t)end) return POLY_MACRO_SPLICE_NO_ROOM;

    c->start     = start;
    c->tail_from = tail_from;
    c->tail_len  = tail_len;
    c->delta     = (int32_t)new_span - (int32_t)old_span;
    if (c->delta < 0 && (int32_t)(tail_from + tail_len) > new_end) {
        // What the move vacated at the top. Left as-is it would sit past the last
        // terminator and read as further macros.
        c->zero_from = (uint16_t)new_end;
        c->zero_len  = (uint16_t)((int32_t)(tail_from + tail_len) - new_end);
    }
    c->phase  = POLY_MACRO_COMMIT_MARK;
    c->cursor = 0;
    return POLY_MACRO_SPLICE_OK;
}

bool poly_macro_commit_done(const poly_macro_commit_t *c) {
    return c->phase == POLY_MACRO_COMMIT_DONE;
}

bool poly_macro_commit_step(poly_macro_commit_t *c, uint16_t budget) {
    while (budget > 0u && c->phase != POLY_MACRO_COMMIT_DONE) {
        switch (c->phase) {
            case POLY_MACRO_COMMIT_MARK:
                c->wr((uint16_t)(c->end - 1u), POLY_MACRO_INCOMPLETE, c->ctx);
                budget--;
                c->phase  = POLY_MACRO_COMMIT_MOVE;
                c->cursor = 0;
                break;

            case POLY_MACRO_COMMIT_MOVE: {
                if (c->delta == 0 || c->tail_len == 0u || c->cursor >= c->tail_len) {
                    c->phase  = POLY_MACRO_COMMIT_ZERO;
                    c->cursor = 0;
                    break;
                }
                // Direction is the whole trick: growing copies from the TOP down and
                // shrinking from the BOTTOM up, so the read is always ahead of the
                // write and no scratch buffer is needed for up to 2 KB of tail.
                const uint16_t i = (c->delta > 0)
                                       ? (uint16_t)(c->tail_len - 1u - c->cursor)
                                       : c->cursor;
                const uint16_t src = (uint16_t)(c->tail_from + i);
                const uint16_t dst = (uint16_t)((int32_t)src + c->delta);
                c->wr(dst, c->rd(src, c->ctx), c->ctx);
                c->cursor++;
                budget--;
                break;
            }

            case POLY_MACRO_COMMIT_ZERO:
                if (c->cursor >= c->zero_len) {
                    c->phase  = POLY_MACRO_COMMIT_BODY;
                    c->cursor = 0;
                    break;
                }
                c->wr((uint16_t)(c->zero_from + c->cursor), 0u, c->ctx);
                c->cursor++;
                budget--;
                break;

            case POLY_MACRO_COMMIT_BODY:
                if (c->cursor > c->len) {
                    c->phase = POLY_MACRO_COMMIT_UNMARK;
                    break;
                }
                // One past the body writes the terminator.
                c->wr((uint16_t)(c->start + c->cursor),
                      (c->cursor < c->len) ? c->body[c->cursor] : 0u, c->ctx);
                c->cursor++;
                budget--;
                break;

            case POLY_MACRO_COMMIT_UNMARK:
                c->wr((uint16_t)(c->end - 1u), 0u, c->ctx);
                budget--;
                c->phase = POLY_MACRO_COMMIT_DONE;
                break;

            default:
                c->phase = POLY_MACRO_COMMIT_DONE;
                break;
        }
    }
    return c->phase != POLY_MACRO_COMMIT_DONE;
}
