#pragma once

#include <stdint.h>
#include "quantum.h"

typedef struct _poly_layer_t {
    uint32_t      crc32;
    layer_state_t layer;
    layer_state_t def_layer;
    led_t         led_state;
    uint8_t       mods;
} poly_layer_t;

typedef struct _poly_sync_t {
    uint32_t crc32;
    uint8_t  lang;
    uint8_t  contrast;
    uint8_t  flags;
    uint8_t  overlay_flags;
    uint8_t  unicode_mode;
} poly_sync_t;

typedef struct _poly_last_t {
    uint32_t crc32;
    uint16_t latin_kc;
} poly_last_t;

typedef struct _latin_sync_t {
    uint32_t crc32;
    uint8_t  ex[26];
} latin_sync_t;

typedef struct _poly_eeconf_t {
    uint8_t lang;
    uint8_t brightness;
    uint16_t unused;
    uint8_t latin_ex[26];
} poly_eeconf_t;


static_assert(sizeof(poly_eeconf_t) == EECONFIG_USER_DATA_SIZE, "Mismatch in keyboard EECONFIG stored data");

void reset_all_states_and_layers(void);

const poly_layer_t* get_local_layer(void);
poly_layer_t* access_local_layer(void);
void set_local_layer(poly_layer_t value);
void copy_local_layer(const poly_layer_t *value);
const poly_layer_t* get_global_layer(void);
void set_global_layer(poly_layer_t value);
void copy_global_layer(const poly_layer_t *value);

const poly_sync_t* get_local_state(void);
poly_sync_t* access_local_state(void);
void set_local_state(poly_sync_t value);
void copy_local_state(const poly_sync_t* value);
const poly_sync_t* get_global_state(void);
poly_sync_t* access_global_state(void);
void set_global_state(poly_sync_t value);
void copy_global_state(const poly_sync_t* value);

const poly_last_t* get_local_last_latin(void);
poly_last_t* access_local_last_latin(void);
void set_local_last_latin_keycode(uint16_t keycode);
uint16_t get_local_last_latin_keycode(void);
void copy_local_last_latin(const poly_last_t* value);

const poly_last_t* get_global_last_latin(void);
poly_last_t* access_global_last_latin(void);
void set_global_last_latin_keycode(uint16_t keycode);
uint16_t get_global_last_latin_keycode(void);
void copy_global_last_latin(const poly_last_t* value);

const latin_sync_t* get_global_latin_table(void);
latin_sync_t* access_global_latin_table(void);
void set_global_latin_table(latin_sync_t value);
void copy_global_latin_table(const latin_sync_t* value);


// Writes only lang+brightness (4 bytes) to EEPROM. Use when only settings changed, not the latin table.
void save_user_settings(void);

// Writes only the 26-byte latin extension table to EEPROM. Use when only latin mappings changed.
void save_user_latin(void);

// Saves both settings and latin table (convenience wrapper around the two above).
void save_user_eeconf(void);

// Loads user keyboard configuration from EEPROM with brightness validation against maximum.
poly_eeconf_t load_user_eeconf(void);

// Increments brightness by BRIGHT_STEP with clamping to FULL_BRIGHT (deferred EEPROM write).
void inc_brightness(void);

// Decrements brightness by BRIGHT_STEP with clamping to MIN_BRIGHT (deferred EEPROM write).
void dec_brightness(void);

// Marks settings as needing an EEPROM write (restarts the debounce window).
void mark_settings_dirty(void);

// Writes settings to EEPROM if a change is pending and the debounce period has elapsed.
void brightness_save_if_pending(void);

// Queues a default-layer EEPROM write to be executed from housekeeping (not from a sync handler).
void defer_default_layer_save(layer_state_t def_layer);

// Writes the pending default layer to EEPROM if queued. Call from housekeeping_task_user().
void default_layer_save_if_pending(void);

