// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

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
// Returns true if any pair maps a currently-displayed position, so the caller can
// skip the display refresh for an all-off-screen chunk (see fill_overlay.c).
bool set_10bit_overlay_mapping(uint8_t* mapping);

// Runs the four overlay self-actions (RESET_BUFFERS / USAGE_RESET /
// MAPPING_RESET / MAPPING_ALLSET) for whichever bits are set in `flags`.
// Called immediately at HID receive (master) and bridge sync (slave) so the
// post-action state is in place before subsequent commands can race ahead.
void apply_overlay_action_flags(uint8_t flags);

// Sets use_overlay[idx] only when MIRROR_OVERLAYS is *off* (i.e. legacy
// non-MRU mode where uploads use display-addresses and pool_idx == display_idx).
// In MRU mode the upload's HID address is a pool slot, not a display position,
// so setting the bit here would cause the wrong display position (the one
// whose firmware address happens to coincide with the pool slot) to render
// at every press. In MRU mode use_overlay is exclusively set by
// set_10bit_overlay_mapping for from-indices in the host's mapping.
void set_overlay_usage_post_upload(uint16_t idx);

uint16_t adjust_overlay_idx_to_mod(uint16_t idx, uint8_t mods);

// --- Slave overlay-mapping repair ---------------------------------------
// A mapping chunk (cmd 21) is bridged to the slave ONCE and then dropped — it
// has no diff-based retry queue like the periodic state syncs, so a bridge
// give-up used to lose those display positions on the slave silently until the
// host happened to re-send the whole mapping on the next app switch. Symptom:
// one keycap on the link-side half falls back to its plain legend while the
// rest of the overlay set renders (the slave's render gate is the usage bit,
// which set_10bit_overlay_mapping is what sets).
//
// The master holds the authoritative overlay_map[] + use_overlay[] (it applies
// every chunk locally regardless of the bridge), so it can rebuild the slave's
// view from its own tables. note_..._lost() latches the failure;
// arm_...() starts a repair at the end of the app switch (enable_overlays,
// hid_com.c case 11); overlay_map_repair_tick() does the actual sending from
// housekeeping, a bounded number of reports per tick.
//
// ⚠️ The send loop is deliberately NOT run inline in the HID handler: a full
// mapping is up to 34 reports and each bridge can burn 10 retries x the bridge
// timeout, which on the bad link that caused the repair would block the main
// loop for seconds inside one HID command.
void note_overlay_map_sync_lost(void);
void arm_overlay_map_repair(void);
void overlay_map_repair_tick(void);
