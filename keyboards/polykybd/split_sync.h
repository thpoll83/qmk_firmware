// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "config.h"
#include "state.h"
#include "mru.h"     // MRU_EMOJI_PACKED / MRU_CAP used by mru_sync_t below
// The ack vocabulary (SYNC_ACK / SYNC_ACK_SIG / SYNC_CRC32_ERR /
// SYNC_NACK_REFUSED, poly_sync_reply_t, sync_succeeded) lives in its own
// dependency-free header so the contract can be unit-tested without the keyboard
// config this file needs for the structs below. Re-exported here, so every
// existing `#include "split_sync.h"` consumer is unaffected.
#include "base/sync_ack.h"

#include <stdint.h>
#include <stdbool.h>


typedef struct _overlay_sync_t {
    uint32_t crc32;
    uint16_t adj_idx;
    uint8_t segment;
    uint8_t overlay[BYTES_PER_SEGMENT];
} overlay_sync_t;

typedef struct _compressed_overlay_sync_t {
    uint32_t crc32;
    uint16_t adj_idx;
    uint8_t len;
    uint8_t compressed[COMPRESSED_MAX];
} compressed_overlay_sync_t;

typedef struct _roi_overlay_sync_t {
    uint32_t crc32;
    uint16_t adj_idx;
    uint8_t data[ROI_MAX];
    uint8_t msg_idx;
} roi_overlay_sync_t;

typedef struct _dynamic_keymap_sync_t {
    uint32_t crc32;
    uint8_t  commands[RAW_EPSIZE];
} dynamic_keymap_sync_t;

// A bridged overlay-mapping report. `width`/`bytes` describe the packed stream so
// the slave decodes it exactly as the master did — they differ per source: cmd 21
// is always (10, HID_DATA_MAX) while cmd 33 carries the host's chosen width over
// OVERLAY_MAP_W_BYTES. There is no count field (the sender fills every value by
// repeating the last pair), so getting these two wrong decodes trailing junk as
// real mappings.
typedef struct _overlay_map_sync_t {
    uint32_t crc32;
    uint8_t  width;
    uint8_t  bytes;
    uint8_t  mapping[HID_DATA_MAX];
} overlay_map_sync_t;

// Emoji + language MRU recents, pushed master->slave so both halves render the
// top row identically. Sent only when the lists change (mru_sync_pending()).
typedef struct _mru_sync_t {
    uint32_t crc32;
    uint8_t  emoji[MRU_EMOJI_PACKED];
    uint8_t  lang[MRU_CAP];
} mru_sync_t;
// crc + packed emoji + lang are contiguous (no padding before `lang`); only these
// bytes are transmitted, so the uint32 crc's tail padding never goes on the wire.
#define MRU_SYNC_BYTES (4u + MRU_EMOJI_PACKED + MRU_CAP)

// Handles incoming poly_sync data for the bridge with CRC32 validation.
void user_sync_poly_data_handler(uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data);

// Handles incoming latin_sync data with CRC32 validation, saves to EEPROM and refreshes display.
void user_sync_latin_ex_data_handler(uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data);

// Handles incoming last key data on bridge with CRC32 validation, updates both local and global state.
void user_sync_lastkey_data_handler(uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data);

// Handles incoming layer data on bridge with CRC32 validation.
void user_sync_layer_data_handler(uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data);

// Handles incoming overlay segment data on bridge with CRC32 validation, marks as used when complete.
void user_sync_overlay_data_handler(uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data);

// Handles compressed overlay data on bridge with CRC32 validation, decompresses using core1 or local decompression.
void user_sync_compressed_overlay_data_handler(uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data);

void user_sync_roi_data_handler(uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data);

void dynamic_keymap_set_buffer_poly(uint16_t offset, uint16_t size, const uint8_t *data);

// Single-keycode write capped to the host-writable layers (same bound as
// dynamic_keymap_set_buffer_poly) so the function layers (language, emoji, …) at
// indices >= DYNAMIC_KEYMAP_UPDATE_MAX_LAYER_COUNT stay read-only.
void dynamic_keymap_set_keycode_poly(uint8_t layer, uint8_t row, uint8_t column, uint16_t keycode);
void dynamic_keymap_reset_poly(void);

void user_sync_dynamic_keymap_data_handler(uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data);

// Handles incoming overlay mapping data on bridge with CRC32 validation.
void user_sync_overlay_map_data_handler(uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data);

// Handles incoming MRU (emoji + language recents) data on bridge with CRC32 validation.
void user_sync_mru_data_handler(uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data);

#ifdef POLYKYBD_DOOM
// Doom slave lockstep mirror: ticcmd stream + START/BREAK control messages
// into the game arena's mailbox (doom/doom_mirror.h).
void user_sync_doom_mirror_handler(uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data);
#endif

// Keyboard level code can change where the dynamic keymap stores the magic.
// The magic is the build date YYMMDD encoded as BCD in 3 bytes,
// thus installing firmware built on a different date to the one
// already installed can be detected and the EEPROM data is reset.
// The only reason this is important is in case EEPROM usage changes
// and the EEPROM was not explicitly reset by bootmagic lite.
#ifndef POLY_EEPROM_MAGIC_ADDR
#    define POLY_EEPROM_MAGIC_ADDR (EECONFIG_SIZE)
#endif

#define POLY_EEPROM_LAYOUT_OPTIONS_ADDR (POLY_EEPROM_MAGIC_ADDR + 3)

// Changing the layout options size after release will invalidate EEPROM,
// but this is something that should be set correctly on initial implementation.
// 1 byte is enough for most uses (i.e. 8 binary states, or 6 binary + 1 ternary/quaternary )
#ifndef POLY_EEPROM_LAYOUT_OPTIONS_SIZE
#    define POLY_EEPROM_LAYOUT_OPTIONS_SIZE 1
#endif

// The end of the EEPROM memory used by the dynamic keymap
// By default, dynamic keymaps will start at this if there is no
// custom config
#define POLY_EEPROM_CUSTOM_CONFIG_ADDR (POLY_EEPROM_LAYOUT_OPTIONS_ADDR + POLY_EEPROM_LAYOUT_OPTIONS_SIZE)

#ifndef POLY_EEPROM_CUSTOM_CONFIG_SIZE
#    define POLY_EEPROM_CUSTOM_CONFIG_SIZE 0
#endif

#define POLY_EEPROM_CONFIG_END (POLY_EEPROM_CUSTOM_CONFIG_ADDR + POLY_EEPROM_CUSTOM_CONFIG_SIZE)
