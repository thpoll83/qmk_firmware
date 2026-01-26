#pragma once

#include <stdint.h>
#include <stdbool.h>

// Fills overlay buffer segment with bitmap data and syncs to bridge if needed.
void fill_overlay_buffer(uint8_t segment_0_to_14, uint8_t* buffer_24bytes);

// Decompresses RLE-compressed overlay data and writes to overlay buffer, syncs to bridge if needed.
void decompress_overlay_buffer(uint8_t* compressed, bool first);

// Fills region-of-interest of overlay buffer with data and syncs to bridge when needed.
void fill_roi_overlay_buffer(uint8_t* data, bool first);

// Unpacks 10-bit overlay mapping pairs from buffer and updates overlay_map array.
void set_10bit_overlay_mapping(uint8_t* mapping);

uint16_t adjust_overlay_idx_to_mod(uint16_t idx, uint8_t mods);
