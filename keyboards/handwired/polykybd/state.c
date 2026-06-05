// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#include "state.h"

#include <string.h>
#include "eeconfig.h"

static poly_layer_t l_layer;
static poly_layer_t g_layer;

static poly_sync_t l_state;
static poly_sync_t g_state;

static bool     g_brightness_dirty       = false;
static uint32_t g_brightness_dirty_timer = 0;
#define BRIGHTNESS_EEPROM_DEBOUNCE_MS 5000

static bool          g_def_layer_dirty = false;
static layer_state_t g_def_layer_pending = 0;

static poly_last_t l_last;
static poly_last_t g_last;

static latin_sync_t g_latin;

void reset_all_states_and_layers(void) {
    memset(&l_layer, 0, sizeof(l_layer));
    memset(&g_layer, 0, sizeof(g_layer));
    memset(&l_state, 0, sizeof(l_state));
    memset(&g_state, 0, sizeof(g_state));
}

// Getters and setters for l_layer
const poly_layer_t* get_local_layer(void) {
    return &l_layer;
}

poly_layer_t* access_local_layer(void) {
    return &l_layer;
}

void set_local_layer(poly_layer_t value) {
    l_layer = value;
}

void copy_local_layer(const poly_layer_t *value) {
    memcpy(&l_layer, value, sizeof(poly_layer_t));
}

// Getters and setters for g_layer
const poly_layer_t* get_global_layer(void) {
    return &g_layer;
}

void set_global_layer(poly_layer_t value) {
    g_layer = value;
}

void copy_global_layer(const poly_layer_t *value) {
    memcpy(&g_layer, value, sizeof(poly_layer_t));
}


// Getters and setters for l_state
const poly_sync_t* get_local_state(void) {
    return &l_state;
}

poly_sync_t* access_local_state(void) {
    return &l_state;
}

void set_local_state(poly_sync_t value) {
    l_state = value;
}

void copy_local_state(const poly_sync_t* value) {
    memcpy(&l_state, value, sizeof(poly_sync_t));
}

// Getters and setters for g_state
const poly_sync_t* get_global_state(void) {
    return &g_state;
}

poly_sync_t* access_global_state(void) {
    return &g_state;
}

void set_global_state(poly_sync_t value) {
    g_state = value;
}

void copy_global_state(const poly_sync_t* value) {
    memcpy(&g_state, value, sizeof(poly_sync_t));
}


// Getters and setters for l_last
const poly_last_t* get_local_last_latin(void) {
    return &l_last;
}

poly_last_t* access_local_last_latin(void) {
    return &l_last;
}

uint16_t get_local_last_latin_keycode(void) {
    return l_last.latin_kc;
}

void set_local_last_latin_keycode(uint16_t keycode) {
    l_last.latin_kc = keycode;
}

void copy_local_last_latin(const poly_last_t* value) {
    memcpy(&l_last, value, sizeof(poly_last_t));
}

const poly_last_t* get_global_last_latin(void) {
    return &g_last;
}

poly_last_t* access_global_last_latin_keycode(void) {
    return &g_last;
}

void set_global_last_latin_keycode(uint16_t keycode) {
    g_last.latin_kc = keycode;
}

void copy_global_last_latin(const poly_last_t* value) {
    memcpy(&g_last, value, sizeof(poly_last_t));
}

// Getters and setters for g_latin
const latin_sync_t* get_global_latin_table(void) {
    return &g_latin;
}

latin_sync_t* access_global_latin_table(void) {
    return &g_latin;
}

void set_global_latin_table(latin_sync_t value) {
    g_latin = value;
}

void copy_global_latin_table(const latin_sync_t* value) {
    memcpy(&g_latin, value, sizeof(latin_sync_t));
}


// Writes only lang+brightness+unused (4 bytes) to EEPROM.
void save_user_settings(void) {
    const poly_eeconf_t ee = { .lang = l_state.lang, .brightness = (uint8_t)(~l_state.contrast), .unused = 0 };
    eeconfig_update_user_datablock(&ee, 0, offsetof(poly_eeconf_t, latin_ex));
}

// Writes only the 26-byte latin extension table to EEPROM.
void save_user_latin(void) {
    eeconfig_update_user_datablock(g_latin.ex, offsetof(poly_eeconf_t, latin_ex), sizeof(g_latin.ex));
}

// Saves both settings and latin table. Use save_user_settings() or save_user_latin() when only one part changed.
void save_user_eeconf(void) {
    save_user_settings();
    save_user_latin();
}

// Loads user keyboard configuration from EEPROM with brightness validation against maximum.
// Global variables: (none - returns result)
poly_eeconf_t load_user_eeconf(void) {
    poly_eeconf_t ee;
    eeconfig_read_user_datablock(&ee, 0, sizeof(ee));
    ee.brightness = ~ee.brightness;
    if(ee.brightness>FULL_BRIGHT) {
        ee.brightness = FULL_BRIGHT;
    }
    return ee;
}

// Increments brightness by BRIGHT_STEP with clamping to FULL_BRIGHT.
// EEPROM write is deferred; call brightness_save_if_pending() from housekeeping.
// Global variables: l_state
void inc_brightness(void) {
    if (l_state.contrast < FULL_BRIGHT) {
        l_state.contrast += BRIGHT_STEP;
    }
    if (l_state.contrast > FULL_BRIGHT) {
        l_state.contrast = FULL_BRIGHT;
    }
    g_brightness_dirty       = true;
    g_brightness_dirty_timer = timer_read32();
}

// Decrements brightness by BRIGHT_STEP with clamping to MIN_BRIGHT.
// EEPROM write is deferred; call brightness_save_if_pending() from housekeeping.
// Global variables: l_state
void dec_brightness(void) {
    if (l_state.contrast > (MIN_BRIGHT + BRIGHT_STEP)) {
        l_state.contrast -= BRIGHT_STEP;
    } else {
        l_state.contrast = MIN_BRIGHT;
    }
    g_brightness_dirty       = true;
    g_brightness_dirty_timer = timer_read32();
}

// Marks settings as needing an EEPROM write (restarts the debounce window).
// Call on the bridge from on_local_state_synced() when lang or contrast changes.
void mark_settings_dirty(void) {
    g_brightness_dirty       = true;
    g_brightness_dirty_timer = timer_read32();
}

// Writes settings to EEPROM once the debounce period has elapsed since the last change.
// Call from housekeeping_task_user() on both sides.
void brightness_save_if_pending(void) {
    if (g_brightness_dirty && timer_elapsed32(g_brightness_dirty_timer) >= BRIGHTNESS_EEPROM_DEBOUNCE_MS) {
        save_user_settings();
        g_brightness_dirty = false;
    }
}

// Defers a default-layer EEPROM write to housekeeping — safe to call from split sync handlers.
void defer_default_layer_save(layer_state_t def_layer) {
    g_def_layer_pending = def_layer;
    g_def_layer_dirty   = true;
}

// Writes the pending default layer to EEPROM if one is queued.
// Call from housekeeping_task_user() on both sides.
void default_layer_save_if_pending(void) {
    if (g_def_layer_dirty) {
        eeconfig_update_default_layer(g_def_layer_pending);
        g_def_layer_dirty = false;
    }
}
