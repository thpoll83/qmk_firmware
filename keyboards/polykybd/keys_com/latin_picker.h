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
