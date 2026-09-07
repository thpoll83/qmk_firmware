// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "base/overlay.h"

#include <stdint.h>
#include <stdbool.h>

// True while core1 still has a previously-dispatched DECOMPRESS or ROI_UPDATE
// in flight. Callers use this for backpressure: instead of busy-waiting inside
// the dispatcher (which would starve core0's keyboard task), they defer the
// next packet until core1 catches up.
bool core1_is_busy(void);

// Decompress the supplied buffer on core1, will block if previous decompression is still ongoing
// All fragments have to be processed in order and until the end, no parallel processing possible
// `visible` = this overlay's modifier variant is the one currently on screen; core1 only
// requests a display refresh on completion when it is (see fill_overlay.c overlay_variant_visible).
// Global variables: core0_decomp_count, core1_decomp_count, core1_bit_index, core1_max_bitlen, core1_idx, core1_buffer, hid_modifier
void core1_decompress_fragment(uint8_t keycode, uint8_t mod, uint16_t overlay_idx, const uint8_t* compressed, bool visible);

// Queues a region-of-interest overlay update for core1 processing, waits if previous update is ongoing.
// `visible`: see core1_decompress_fragment.
// Global variables: core0_decomp_count, core1_decomp_count, core1_bit_index, core1_roi, core1_idx, core1_buffer, hid_modifier
void core1_update_roi(uint8_t keycode, uint8_t mod, uint16_t overlay_idx, const uint8_t* data, const roi_update_data_t* roi, bool visible);

// Signals core1 to reset bit index for next region-of-interest update.
// Global variables: (none - sends FIFO command only)
void core1_roi_start(void);

#if defined(POLYKYBD_CRASH_TEST) && defined(USE_CORE1)
// TEST BUILDS ONLY (crash_test.h): ask core1 to fault, so the crash record can be
// checked on the core that has no console and used to lock up silently. Returns
// as soon as the command is queued -- core1 faults asynchronously and the fault
// handler reboots the whole chip from there.
void core1_crash_test(void);
#endif
