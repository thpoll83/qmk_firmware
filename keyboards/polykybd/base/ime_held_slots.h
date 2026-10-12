// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// The usage each HELD KC_IME stroke registered on its press, per KEY POSITION, so a
// release unregisters exactly what that key's press registered even if the language
// or OS changed while it was down (the same latch reasoning as s_apple_swap_latch).
// One latch shared by every KC_IME key was not enough: a second KC_IME key pressed
// after a language switch overwrote the first key's usage, and the Right Alt it had
// registered was never released (Greptile on #355).
//
// Pure: everything QMK-side goes through ime_slot_io_t, so the table links without
// QMK and is unit-tested (make test:polykybd_ime_held_slots). The binding is in
// poly_keymap.c.

#include <stdbool.h>
#include <stdint.h>

#define IME_HELD_SLOTS 4

typedef struct {
    uint8_t row, col, usage;   // usage 0 = slot free
} ime_slot_t;

typedef struct {
    ime_slot_t slot[IME_HELD_SLOTS];
} ime_slots_t;

typedef struct {
    bool (*key_down)(uint8_t row, uint8_t col);  // matrix_is_on
    bool (*in_report)(uint8_t usage);            // the host report still carries it
    void (*press)(uint8_t usage);                // register_code
    void (*release)(uint8_t usage);              // unregister_code
    void (*tap)(uint8_t usage);                  // tap_code
} ime_slot_io_t;

// Forget every slot whose usage the report no longer carries: clear_keyboard() ran
// (the macro picker, a confirm prompt, demo mode, ...) and took the hold with it, so
// there is nothing left to release. Call it at the TOP of process_record_user, before
// any gate can swallow the event and before a newer ordinary Right Alt can land in the
// report and make a stale slot look like a live hold (Greptile on #355).
void ime_slots_forget_cleared(ime_slots_t* t, const ime_slot_io_t* io);

// A KC_IME key at (row, col) is pressed with a HELD stroke of `usage`.
void ime_slots_press(ime_slots_t* t, uint8_t row, uint8_t col, uint8_t usage, const ime_slot_io_t* io);

// The KC_IME key at (row, col) is released.
void ime_slots_release(ime_slots_t* t, uint8_t row, uint8_t col, const ime_slot_io_t* io);
