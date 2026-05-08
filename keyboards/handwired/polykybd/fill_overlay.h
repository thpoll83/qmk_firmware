#pragma once

#include <stdint.h>
#include <stdbool.h>

// Fills overlay buffer segment with bitmap data and syncs to bridge if needed.
void fill_overlay_buffer(uint8_t segment_index, uint8_t* buffer);

// Decompresses RLE-compressed overlay data and writes to overlay buffer, syncs to bridge if needed.
void decompress_overlay_buffer(uint8_t* compressed, bool first);

// Fills region-of-interest of overlay buffer with data and syncs to bridge when needed.
void fill_roi_overlay_buffer(uint8_t* data, bool first);

// Unpacks 10-bit overlay mapping pairs from buffer and updates overlay_map array.
void set_10bit_overlay_mapping(uint8_t* mapping);

// Runs the four overlay self-actions (RESET_BUFFERS / USAGE_RESET /
// MAPPING_RESET / MAPPING_ALLSET) for whichever bits are set in `flags`.
// Called immediately at HID receive (master) and bridge sync (slave) so the
// post-action state is in place before subsequent commands can race ahead.
void apply_overlay_action_flags(uint8_t flags);

uint16_t adjust_overlay_idx_to_mod(uint16_t idx, uint8_t mods);
