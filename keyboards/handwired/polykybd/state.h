// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdint.h>
#include "quantum.h"
#include "mru.h"

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
    // Emoji layer state — synced so both halves show the same category/page.
    uint8_t  emj_category;
    uint8_t  emj_page;
    // Language layer page — synced so both halves show the same page of languages.
    uint8_t  lang_page;
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
    // MRU recents for the emoji / language selection layers. Persisted only on a
    // power-suspension event (and the host save command), and only when dirty.
    // Emoji are bit-packed 14-bit category|offset codes; languages are LANG_* bytes.
    uint8_t  mru_emoji[MRU_EMOJI_PACKED];
    uint8_t  mru_lang[MRU_CAP];
} poly_eeconf_t;


static_assert(sizeof(poly_eeconf_t) == EECONFIG_USER_DATA_SIZE, "Mismatch in keyboard EECONFIG stored data");
// The user datablock must fit inside the budget reserved ahead of the pinned
// dynamic-keymap base (see DYNAMIC_KEYMAP_EEPROM_ADDR in config.h); otherwise it
// would overlap the stored keymap. Raise POLY_EECONFIG_USER_RESERVED (a one-time
// keymap relocation/reset) if this trips.
static_assert(EECONFIG_USER_DATA_SIZE <= POLY_EECONFIG_USER_RESERVED, "poly_eeconf_t exceeds POLY_EECONFIG_USER_RESERVED — bump the reservation (one-time reset)");

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

// Writes the emoji/language MRU lists to EEPROM, but only if they changed since
// the last load/save (mru_dirty()). Called on both halves from suspend (and on
// the master from the host save command); each half persists its own copy.
void save_user_mru_if_dirty(void);

// Loads the persisted MRU lists from EEPROM into the RAM lists (mru_load()).
void load_user_mru(void);

// Saves both settings and latin table (convenience wrapper around the two above).
void save_user_eeconf(void);

// Loads user keyboard configuration from EEPROM with brightness validation against maximum.
poly_eeconf_t load_user_eeconf(void);

// Increments brightness by BRIGHT_STEP with clamping to FULL_BRIGHT (deferred EEPROM write).
void inc_brightness(void);

// Decrements brightness by BRIGHT_STEP with clamping to MIN_BRIGHT (deferred EEPROM write).
void dec_brightness(void);

// Sets contrast to a deliberate user-chosen level and records it as the brightness
// to persist (deferred EEPROM write). Transient contrast changes (suspend, idle
// fade) must NOT use this — they write l_state.contrast directly.
void set_user_brightness(uint8_t value);

// Records the intended user brightness without marking settings dirty (boot-time
// load, slave adopting an awake master's synced contrast).
void note_user_brightness(uint8_t value);

// The brightness to restore after idle/suspend (tracks unflushed changes).
uint8_t get_user_brightness(void);

// Marks settings (lang + brightness) as needing an EEPROM write at the next flush.
void mark_settings_dirty(void);

// Queues a default-layer EEPROM write — safe from a sync handler. Written at the next flush.
void defer_default_layer_save(layer_state_t def_layer);

// Marks the latin extension table as needing an EEPROM write (deferred to the next flush).
void mark_latin_dirty(void);

// Flushes every dirty user-state block (settings, latin, default layer, MRU) to
// EEPROM at once. Call only at flush points (suspend / host shutdown / store key).
void save_all_dirty(void);

// Requests a deferred flush-all — safe from a sync handler (sets a flag only).
void request_eeprom_save(void);

// Performs the flush-all if one was requested. Call from housekeeping_task_user().
void save_all_if_requested(void);

