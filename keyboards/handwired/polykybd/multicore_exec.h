#pragma once

#include "base/overlay.h"

#include <stdint.h>

// Decompress the supplied buffer on core1, will block if previous decompression is still ongoing
// All fragments have to be processed in order and until the end, no parallel processing possible
// Global variables: core0_decomp_count, core1_decomp_count, core1_bit_index, core1_max_bitlen, core1_idx, core1_buffer, hid_modifier
void core1_decompress_fragment(uint8_t keycode, uint8_t mod, uint16_t overlay_idx, const uint8_t* compressed);

// Queues a region-of-interest overlay update for core1 processing, waits if previous update is ongoing.
// Global variables: core0_decomp_count, core1_decomp_count, core1_bit_index, core1_roi, core1_idx, core1_buffer, hid_modifier
void core1_update_roi(uint8_t keycode, uint8_t mod, uint16_t overlay_idx, const uint8_t* data, const roi_update_data_t* roi);

// Signals core1 to reset bit index for next region-of-interest update.
// Global variables: (none - sends FIFO command only)
void core1_roi_start(void);
