#pragma once

#include "config.h"

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

#ifdef VIA_ENABLE
typedef struct _via_sync_t {
    uint32_t crc32;
    uint8_t  via_commands[32];
} via_sync_t;
#endif

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

#ifdef VIA_ENABLE

void dynamic_keymap_set_buffer_poly(uint16_t offset, uint16_t size, const uint8_t *data);

void user_sync_via_data_handler(uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data);

#endif

