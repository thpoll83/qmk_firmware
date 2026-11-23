// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Helpers poly_keymap.c shares with the files split out of it (latin_picker.c,
// render_key.c, tutorial_board.c, housekeeping.c, custom_keys.c). Not a public API: only keys_com/
// includes this.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "quantum.h"
#include "lib/system/state.h"   // poly_layer_t
#include "base/legend_plan.h"  // legend_plan_env_t
#include "fonts/gfxfont.h"     // GFXfont

// Publish the Intl latin table to the slave half (display_sync.c owns the retry).
void latin_publish(void);

// Shared between poly_keymap.c and render_key.c.
void render_macro_picker_key(uint8_t id, bool armed);
bool render_menu_key(uint16_t keycode, led_t state, uint8_t current_lang);
void draw_mru_top_bar(uint16_t keycode);
uint32_t glyph_script_codepoint(uint8_t script, uint16_t keycode);
uint16_t kcl_for_lang(uint8_t lang);
int16_t  kcl_lang_index(uint16_t keycode);   // the LANG index of a KCL_ keycode, or -1
void render_lang_flag_key(uint8_t idx, const uint32_t* label, uint8_t current_lang);
extern const GFXfont* const lang_label_fonts[];
extern const legend_plan_env_t legend_plan_env;
extern const GFXfont* const mid_fonts[];
void plan_main_legend(const uint32_t* text, int8_t small_x, int8_t small_y,
                      uint32_t* scratch, uint8_t scratch_cap, main_legend_t* out);

// The Intl picker opens while this modifier is held on _ADDLANG1.
#define LATIN_PICKER_MOD MOD_MASK_CTRL

// The first-run tutorial's keyboard side, in tutorial_board.c. poly_keymap.c calls these
// from process_record_user() and housekeeping_task_user().
bool poly_tutorial_tour_passes(keyrecord_t* record);
bool poly_tutorial_record(keyrecord_t* record);
void poly_tutorial_forget_passes(void);
void poly_tutorial_skip_reset(void);
void poly_tutorial_skip_if_held(void);
void poly_tutorial_publish_active(void);
void poly_tutorial_push_sync(void);
void poly_tutorial_finish_if_done(void);
void poly_apply_draw_script(void);
// Arms the tutorial to start when Eden ends (display_sync.c). KC_EDEN calls it too.
void arm_tutorial_after_intro(void);

// The settings and utility keycodes, in custom_keys.c. process_record_user() calls it;
// true means the keycode was ours and is swallowed.
bool poly_custom_key_action(uint16_t keycode, keyrecord_t* record);

// Defined in poly_keymap.c and used by tutorial_board.c too.
uint16_t poly_keycode_at(uint8_t layer, uint8_t row, uint8_t col);
uint16_t display_keycode_at(const poly_layer_t* lyr, uint8_t row, uint8_t col);
void     draw_legend_cx_cy(const uint32_t* text, int8_t y, int8_t cy_radius);
void     render_fw_confirm_key(bool accept);
bool     overlay_present(uint16_t keycode, uint8_t mods);

// Variant data, defined in each variant's keymaps/default/keymap.c.
extern const uint16_t keymaps[][MATRIX_ROWS][MATRIX_COLS];

// The firmware-update confirm prompt's key: the home-row index finger on both halves.
#if defined(KEYBOARD_polykybd_split42)
#    define FW_CONFIRM_ROW 1   // middle letter row (a s d f g)
#else
#    define FW_CONFIRM_ROW 2   // split72 home row (Fn a s d f g ')
#endif
#define FW_CONFIRM_COL 3

// The base-layer switch: drop every layer, then turn `layer` on. layer_clear() +
// layer_on(index) is deliberate, NOT default_layer_set(), which takes a BITMASK and
// so set the wrong base when handed an index (see KC_L0 ... KC_L4).
static inline void layer_reset_to(uint8_t layer) {
    layer_clear();
    layer_on(layer);
}

// Phases of housekeeping_task_user(), in housekeeping.c. Called in this order.
void macro_rec_sync_tick(void);
void fw_confirm_sync_tick(void);
void ltr559_housekeeping_tick(void);
void idle_fade_tick(void);
void master_state_sync_tick(void);

// The display sync, in display_sync.c. housekeeping_task_user() calls eden_idle_tick()
// and then housekeeping_display_tick(); keyboard_post_init_user() re-arms the boot
// layer resync. status_oled_level() is in poly_keymap.c.
#define FORCE_LAYER_RESYNC_TRIES 12
extern bool    g_force_layer_resync;
extern uint8_t g_force_resync_tries;
void    eden_idle_tick(void);
void    housekeeping_display_tick(void);
uint8_t status_oled_level(bool idle);
