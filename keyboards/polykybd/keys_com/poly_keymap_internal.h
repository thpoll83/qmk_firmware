// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Helpers poly_keymap.c shares with the files split out of it (latin_picker.c,
// render_key.c). Not a public API: only keys_com/ includes this.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "quantum.h"
#include "lib/system/state.h"   // poly_layer_t
#include "base/legend_plan.h"  // legend_plan_env_t
#include "fonts/gfxfont.h"     // GFXfont

// Publish the Intl latin table to the slave half (poly_keymap.c owns the retry).
void latin_publish(void);

// Shared between poly_keymap.c and render_key.c.
void render_macro_picker_key(uint8_t id, bool armed);
bool render_menu_key(uint16_t keycode, led_t state, uint8_t current_lang);
void draw_mru_top_bar(uint16_t keycode);
uint32_t glyph_script_codepoint(uint8_t script, uint16_t keycode);
uint16_t kcl_for_lang(uint8_t lang);
void render_lang_flag_key(uint8_t idx, const uint32_t* label, uint8_t current_lang);
extern const GFXfont* const lang_label_fonts[];
extern const legend_plan_env_t legend_plan_env;
extern const GFXfont* const mid_fonts[];
void plan_main_legend(const uint32_t* text, int8_t small_x, int8_t small_y,
                      uint32_t* scratch, uint8_t scratch_cap, main_legend_t* out);

// The Intl picker opens while this modifier is held on _ADDLANG1.
#define LATIN_PICKER_MOD MOD_MASK_CTRL
