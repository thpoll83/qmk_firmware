#pragma once

#include "config.h"
#include "state.h"

#include <stdint.h>

#define SYNC_ACK_SIG    0b01001101
#define SYNC_ACK        0b11001010
#define SYNC_CRC32_ERR  0b00110101

typedef struct _poly_sync_reply_t {
    uint8_t ack;
} poly_sync_reply_t;


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

// Handles incoming RGB config on bridge: updates rgb_matrix_config and saves to EEPROM.
void user_sync_rgb_save_handler(uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data);

// Sends current RGB matrix config to slave so it saves to its own EEPROM.
void sync_rgb_config_to_bridge(void);

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

void user_sync_dynamic_keymap_data_handler(uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data);

// Keyboard level code can change where VIA stores the magic.
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

// The end of the EEPROM memory used by VIA
// By default, dynamic keymaps will start at this if there is no
// custom config
#define POLY_EEPROM_CUSTOM_CONFIG_ADDR (POLY_EEPROM_LAYOUT_OPTIONS_ADDR + POLY_EEPROM_LAYOUT_OPTIONS_SIZE)

#ifndef POLY_EEPROM_CUSTOM_CONFIG_SIZE
#    define POLY_EEPROM_CUSTOM_CONFIG_SIZE 0
#endif

#define POLY_EEPROM_CONFIG_END (POLY_EEPROM_CUSTOM_CONFIG_ADDR + POLY_EEPROM_CUSTOM_CONFIG_SIZE)
