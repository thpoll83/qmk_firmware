// Copyright 2026 Thomas Pollak
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Runtime combos and tap dance (HID cmd 45, protocol v31): the record format, its
// validation and the tap-dance decision. Header-only and pure (no QMK headers), so
// it is unit-tested on the host (make test:polykybd_combo_store). The flash sector,
// the RAM tables and the QMK hooks are in combo_store.c.
//
// Storage: one 4 KB flash sector of its own at FW_COMBO_STORE_OFFSET (top of the
// staging region, below the handedness stamp), NOT the emulated EEPROM. The EEPROM
// is full (the macro body buffer takes all the space it leaves), every EEPROM byte
// is also a RAM byte (the wear-levelling cache), and a wear-levelling recovery
// clears the whole store; a sector of our own has none of those costs.
//
// A record is five little-endian uint16 values (10 bytes on the wire and in flash):
//   combo      keys[0..3], result        keys packed from the front, 0 = unused
//   tap dance  on_tap, on_hold, on_double_tap, on_tap_hold, term_ms
// An all-zero record is an empty slot.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
#    define POLY_COMBO_STATIC_ASSERT static_assert
#else
#    define POLY_COMBO_STATIC_ASSERT _Static_assert
#endif

#define POLY_COMBO_SLOTS      16u
#define POLY_TAP_DANCE_SLOTS  16u
#define POLY_COMBO_MAX_KEYS   4u
#define POLY_COMBO_REC_WORDS  5u
#define POLY_COMBO_REC_BYTES  (POLY_COMBO_REC_WORDS * 2u)

#define POLY_COMBO_KIND_COMBO     0u
#define POLY_COMBO_KIND_TAP_DANCE 1u

typedef struct {
    uint16_t keys[POLY_COMBO_MAX_KEYS];
    uint16_t result;
} poly_combo_rec_t;

typedef struct {
    uint16_t on_tap;
    uint16_t on_hold;
    uint16_t on_double_tap;
    uint16_t on_tap_hold;
    uint16_t term_ms;   // 0 = the global tapping term
} poly_tap_dance_rec_t;

POLY_COMBO_STATIC_ASSERT(sizeof(poly_combo_rec_t) == POLY_COMBO_REC_BYTES, "combo record is 10 bytes");
POLY_COMBO_STATIC_ASSERT(sizeof(poly_tap_dance_rec_t) == POLY_COMBO_REC_BYTES, "tap dance record is 10 bytes");

// The flash image of the sector's first bytes. A header that does not match (an
// erased sector reads 0xFF, a format change, a torn write) means "no entries".
#define POLY_COMBO_STORE_MAGIC   0x424D4350u   // "PCMB"
#define POLY_COMBO_STORE_VERSION 1u

typedef struct {
    uint32_t             magic;
    uint8_t              version;
    uint8_t              combo_slots;
    uint8_t              tap_dance_slots;
    uint8_t              reserved;
    uint32_t             crc;        // over combos[] and tap_dances[]
    uint32_t             reserved2;
    poly_combo_rec_t     combos[POLY_COMBO_SLOTS];
    poly_tap_dance_rec_t tap_dances[POLY_TAP_DANCE_SLOTS];
} poly_combo_store_t;

POLY_COMBO_STATIC_ASSERT(sizeof(poly_combo_store_t) == 16u + (POLY_COMBO_SLOTS + POLY_TAP_DANCE_SLOTS) * POLY_COMBO_REC_BYTES,
               "the store image has no padding");

// ---- Records <-> wire bytes (little-endian) ----------------------------------

static inline void poly_combo_words_put(uint8_t out[POLY_COMBO_REC_BYTES], const uint16_t w[POLY_COMBO_REC_WORDS]) {
    for (uint8_t i = 0; i < POLY_COMBO_REC_WORDS; i++) {
        out[2u * i]      = (uint8_t)(w[i] & 0xFFu);
        out[2u * i + 1u] = (uint8_t)(w[i] >> 8);
    }
}

static inline void poly_combo_words_get(const uint8_t in[POLY_COMBO_REC_BYTES], uint16_t w[POLY_COMBO_REC_WORDS]) {
    for (uint8_t i = 0; i < POLY_COMBO_REC_WORDS; i++) {
        w[i] = (uint16_t)(in[2u * i] | (in[2u * i + 1u] << 8));
    }
}

static inline bool poly_combo_words_zero(const uint16_t w[POLY_COMBO_REC_WORDS]) {
    for (uint8_t i = 0; i < POLY_COMBO_REC_WORDS; i++) {
        if (w[i] != 0u) return false;
    }
    return true;
}

// ---- Validation (both CLOSED: cmd 45 NACKs a record that fails) --------------

// Number of trigger keys, or 0 when the record is empty or malformed.
static inline uint8_t poly_combo_key_count(const poly_combo_rec_t *r) {
    uint8_t n = 0;
    while (n < POLY_COMBO_MAX_KEYS && r->keys[n] != 0u) n++;
    for (uint8_t i = n; i < POLY_COMBO_MAX_KEYS; i++) {
        if (r->keys[i] != 0u) return 0;   // a gap: keys must be packed from the front
    }
    return n;
}

// An empty record, or 2..4 packed trigger keys with a result.
static inline bool poly_combo_rec_valid(const poly_combo_rec_t *r) {
    if (poly_combo_words_zero((const uint16_t *)r)) return true;
    return poly_combo_key_count(r) >= 2u && r->result != 0u;
}

static inline bool poly_combo_rec_active(const poly_combo_rec_t *r) {
    return poly_combo_key_count(r) >= 2u && r->result != 0u;
}

// The keycodes a tap-dance action can send: what the generic handler can press and
// release without a key position of its own (combo_store.c td_key_down/up).
//   0x0000..0x1FFF  basic keycodes, with or without modifiers (QK_MODS range)
//   0x5200..0x521F  TO(layer)          0x5220..0x523F  MO(layer)
//   0x5260..0x527F  TG(layer)
// Others (custom PolyKybd keycodes, macros, mod-tap, ...) are refused rather than
// stored as an action that silently does nothing.
#define POLY_TD_KC_BASIC_MAX 0x1FFFu
#define POLY_TD_KC_TO_MIN    0x5200u
#define POLY_TD_KC_TO_MAX    0x521Fu
#define POLY_TD_KC_MO_MIN    0x5220u
#define POLY_TD_KC_MO_MAX    0x523Fu
#define POLY_TD_KC_TG_MIN    0x5260u
#define POLY_TD_KC_TG_MAX    0x527Fu

static inline bool poly_td_keycode_supported(uint16_t kc) {
    return kc <= POLY_TD_KC_BASIC_MAX
        || (kc >= POLY_TD_KC_TO_MIN && kc <= POLY_TD_KC_TO_MAX)
        || (kc >= POLY_TD_KC_MO_MIN && kc <= POLY_TD_KC_MO_MAX)
        || (kc >= POLY_TD_KC_TG_MIN && kc <= POLY_TD_KC_TG_MAX);
}

// The per-dance term, like the global tapping term: 0 (use the global one) or
// 100..500 ms in 10 ms steps (base/tap_terms.h).
#define POLY_TD_TERM_MIN_MS  100u
#define POLY_TD_TERM_MAX_MS  500u
#define POLY_TD_TERM_STEP_MS 10u

static inline bool poly_td_term_valid(uint16_t ms) {
    return ms == 0u || (ms >= POLY_TD_TERM_MIN_MS && ms <= POLY_TD_TERM_MAX_MS && ms % POLY_TD_TERM_STEP_MS == 0u);
}

static inline bool poly_td_rec_valid(const poly_tap_dance_rec_t *r) {
    return poly_td_keycode_supported(r->on_tap) && poly_td_keycode_supported(r->on_hold)
        && poly_td_keycode_supported(r->on_double_tap) && poly_td_keycode_supported(r->on_tap_hold)
        && poly_td_term_valid(r->term_ms);
}

// ---- The tap-dance decision ---------------------------------------------------
//
// What a finished dance sends, from the number of taps and whether the key is still
// held. An unset action falls back to on_tap, so a dance with only on_tap set acts
// like a plain key:
//   1 tap,  released   tap on_tap
//   1 tap,  held       hold on_hold (or on_tap)
//   2 taps, released   tap on_double_tap (or on_tap twice)
//   2 taps, held       hold on_tap_hold (or tap on_tap, then hold on_tap)
//   n >= 3             tap on_tap n times, or n-1 times and hold it when held
typedef struct {
    uint16_t tap_kc;     // tapped `taps` times, 0 = nothing
    uint8_t  taps;
    uint16_t hold_kc;    // pressed now and released when the dance resets, 0 = nothing
} poly_td_plan_t;

static inline poly_td_plan_t poly_td_plan(const poly_tap_dance_rec_t *r, uint8_t count, bool held) {
    poly_td_plan_t p = {0, 0, 0};
    if (count == 0u) return p;
    if (count == 1u) {
        if (held) {
            p.hold_kc = r->on_hold != 0u ? r->on_hold : r->on_tap;
        } else {
            p.tap_kc = r->on_tap;
            p.taps   = 1;
        }
    } else if (count == 2u && held && r->on_tap_hold != 0u) {
        p.hold_kc = r->on_tap_hold;
    } else if (count == 2u && !held && r->on_double_tap != 0u) {
        p.tap_kc = r->on_double_tap;
        p.taps   = 1;
    } else {
        p.tap_kc = r->on_tap;
        p.taps   = held ? (uint8_t)(count - 1u) : count;
        if (held) p.hold_kc = r->on_tap;
    }
    if (p.tap_kc == 0u) p.taps = 0;
    return p;
}
