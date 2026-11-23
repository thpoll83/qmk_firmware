// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The Intl latin-variation picker: which variation a target key shows and emits on
// the _ADDLANG1 layer, the picker row's slots and pages, and the remap mode that
// points a key at another letter's variations. Split out of poly_keymap.c, which
// calls it from the key handling, the renderer, the tutorial and post-init.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "quantum.h"   // keyrecord_t

int8_t latin_picker_slot(uint16_t keycode);
uint8_t latin_variation_count(uint8_t row);
uint8_t latin_page_count(uint8_t row);
int8_t latin_target_slot(uint16_t keycode);
bool latin_has_row(uint16_t keycode);
uint8_t latin_picker_row(uint16_t last_latin_keycode, bool upper_case);
int8_t latin_picker_index(uint8_t row, uint8_t page, int8_t slot);
void latin_remap_cancel(void);
void latin_remap_apply(uint8_t slot, uint8_t letter);
void latin_remap_reset_all(void);
void latin_picker_reset_page(void);
const uint32_t* latin_variation(uint16_t keycode, bool upper_case);

// The Intl keys in process_record_user(). Each returns true when it decided the event,
// with process_record_user()'s return value in *ret.
bool latin_picker_record_early(uint16_t keycode, keyrecord_t *record, bool addlang, bool *ret);
bool latin_picker_record_pressed(uint16_t keycode, bool addlang, bool *ret);
// The picker's Ctrl latch: true only while the Ctrl it rides on was registered by the
// latch. latin_picker_latch_release() drops it, if the latch holds it.
bool latin_picker_latched(void);
void latin_picker_latch_release(void);
