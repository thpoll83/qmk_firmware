// Copyright 2026 Thomas Pollak
// SPDX-License-Identifier: GPL-2.0-or-later

#include "poly_macro.h"

#include "quantum.h"
#include "eeprom.h"
#include "dynamic_keymap.h"
#include "nvm_eeprom_eeconfig_internal.h"   // EECONFIG_BASE_SIZE, via the keymap base
#include "send_string.h"

#include "base/macro_decode.h"

// The label array must not overlap the bodies, and both must stay inside the region
// QMK sized. If a later edit to POLY_MACRO_LABEL_LEN or the count breaks either, the
// build stops here rather than the labels quietly eating the last macro.
_Static_assert(POLY_MACRO_LABEL_ADDR == DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR + DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE,
               "label array must start exactly where the body buffer ends");
_Static_assert(POLY_MACRO_LABEL_ADDR + POLY_MACRO_LABEL_BYTES - 1 <= DYNAMIC_KEYMAP_EEPROM_MAX_ADDR,
               "label array runs past the end of EEPROM");
_Static_assert(DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE > 256,
               "body buffer has shrunk to the point where the feature is not worth shipping");
_Static_assert(POLY_MACRO_COUNT <= POLY_MACRO_NONE,
               "POLY_MACRO_NONE must not collide with a real macro id");

// ---------------------------------------------------------------------------
// EEPROM plumbing

static uint8_t body_read(uint16_t offset, void *ctx) {
    (void)ctx;
    return eeprom_read_byte((const uint8_t *)(uintptr_t)(DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR + offset));
}

uint16_t poly_macro_capacity(void) {
    return (uint16_t)DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE;
}

uint16_t poly_macro_bytes_used(void) {
    return poly_macro_used(body_read, NULL, poly_macro_capacity());
}

void poly_macro_read(uint16_t offset, uint16_t size, uint8_t *out) {
    const uint16_t cap = poly_macro_capacity();
    for (uint16_t i = 0; i < size; i++) {
        out[i] = (offset + i < cap) ? body_read(offset + i, NULL) : 0;
    }
}

void poly_macro_write(uint16_t offset, uint16_t size, const uint8_t *data) {
    const uint16_t cap = poly_macro_capacity();
    for (uint16_t i = 0; i < size; i++) {
        if (offset + i >= cap) return;
        eeprom_update_byte((uint8_t *)(uintptr_t)(DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR + offset + i), data[i]);
    }
}

// ---------------------------------------------------------------------------
// Labels

void poly_macro_label_get(uint8_t id, char *out) {
    if (id >= POLY_MACRO_COUNT) {
        out[0] = '\0';
        return;
    }
    const uint16_t base = POLY_MACRO_LABEL_ADDR + (uint16_t)id * POLY_MACRO_LABEL_LEN;
    uint8_t        n    = 0;
    for (; n < POLY_MACRO_LABEL_LEN; n++) {
        uint8_t c = eeprom_read_byte((const uint8_t *)(uintptr_t)(base + n));
        if (c == 0) break;
        out[n] = (char)c;
    }
    out[n] = '\0';
}

void poly_macro_label_set(uint8_t id, const char *text) {
    if (id >= POLY_MACRO_COUNT) return;
    const uint16_t base = POLY_MACRO_LABEL_ADDR + (uint16_t)id * POLY_MACRO_LABEL_LEN;
    uint8_t        n    = 0;
    if (text != NULL) {
        for (const char *p = text; *p && n < POLY_MACRO_LABEL_LEN; ++p) {
            // The _Nano_ face is 0x20..0x7E. Anything else draws nothing, which on a
            // keycap is indistinguishable from a bug -- so it never gets stored.
            if ((uint8_t)*p < 0x20 || (uint8_t)*p > 0x7E) continue;
            eeprom_update_byte((uint8_t *)(uintptr_t)(base + n), (uint8_t)*p);
            n++;
        }
    }
    for (; n < POLY_MACRO_LABEL_LEN; n++) {
        eeprom_update_byte((uint8_t *)(uintptr_t)(base + n), 0);
    }
}

void poly_macro_reset_all(void) {
    poly_macro_abort();
    for (uint16_t i = 0; i < (uint16_t)DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE; i++) {
        eeprom_update_byte((uint8_t *)(uintptr_t)(DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR + i), 0);
    }
    for (uint16_t i = 0; i < (uint16_t)POLY_MACRO_LABEL_BYTES; i++) {
        eeprom_update_byte((uint8_t *)(uintptr_t)(POLY_MACRO_LABEL_ADDR + i), 0);
    }
}

// ---------------------------------------------------------------------------
// Playback

static bool     s_active;
static uint16_t s_cursor;
static uint32_t s_resume_ref;
static uint16_t s_resume_ms;

bool poly_macro_active(void) {
    return s_active;
}

void poly_macro_abort(void) {
    if (!s_active) return;
    s_active = false;
    // Same rule as the firmware-confirm prompt and doom_begin(): anything that stops
    // producing key events while a key may be registered has to clear the report, or
    // the host keeps the keycode down and auto-repeats it until USB drops.
    clear_keyboard();
}

bool poly_macro_start(uint8_t id) {
    poly_macro_abort();
    if (id >= POLY_MACRO_COUNT) return false;

    const uint16_t cap = poly_macro_capacity();
    // A buffer whose last byte is not NUL was cut short mid-write. Playing it would
    // type an arbitrary prefix of whatever was being uploaded.
    if (!poly_macro_buffer_intact(body_read, NULL, cap)) return false;

    uint16_t start = poly_macro_find(body_read, NULL, id, cap);
    if (start >= cap) return false;
    if (body_read(start, NULL) == 0) return false; // empty macro: nothing to play

    s_active     = true;
    s_cursor     = start;
    s_resume_ref = timer_read32();
    s_resume_ms  = 0;
    return true;
}

void poly_macro_tick(void) {
    if (!s_active) return;
    if (timer_elapsed32(s_resume_ref) < s_resume_ms) return;

    poly_macro_step_t step = poly_macro_decode(body_read, NULL, s_cursor, poly_macro_capacity());

    switch (step.kind) {
        case POLY_MACRO_STEP_CHAR: {
            // send_char() is the same translation table dynamic_keymap_macro_send()
            // would use, so a body typed here and a body played by QMK agree.
            char s[2] = {(char)step.code, '\0'};
            send_string(s);
            break;
        }
        case POLY_MACRO_STEP_TAP:
            tap_code(step.code);
            break;
        case POLY_MACRO_STEP_DOWN:
            register_code(step.code);
            break;
        case POLY_MACRO_STEP_UP:
            unregister_code(step.code);
            break;
        case POLY_MACRO_STEP_DELAY:
            // The whole reason this is a state machine: the wait is a deadline, not a
            // busy loop, so the main loop keeps scanning while it runs.
            s_cursor     = step.next;
            s_resume_ref = timer_read32();
            s_resume_ms  = step.ms;
            return;
        case POLY_MACRO_STEP_END:
        default:
            // Reached the terminator, or the body is malformed from here on. Either
            // way stop, and release anything a DOWN step left registered.
            poly_macro_abort();
            return;
    }

    s_cursor     = step.next;
    s_resume_ref = timer_read32();
    s_resume_ms  = POLY_MACRO_STEP_MS;
}
