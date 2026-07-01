// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#include "fill_overlay.h"

#include QMK_KEYBOARD_H
#include "quantum.h"
#include "print.h"

#include "state.h"
#include "side.h"
#include "split_sync.h"
#include "matrix_helper.h"
#include "multicore_exec.h"
#include "bridge_helper.h"
#include "base/com.h"
#include "base/overlay.h"
#include "lang/lang_lut.h"

#include <print.h>
#include <transactions.h>

layer_state_t get_function_layer(layer_state_t def_layer);

// Resolve which half(s) should receive an upload. With MIRROR_OVERLAYS set we
// force POS_ON_BOTH so MRU mappings that cross the split find the bitmap on
// the rendering side. Otherwise fall back to the keycode's matrix side, with
// POS_ON_BOTH as a fail-safe when the keycode isn't on any active layer.
static enum key_split_pos resolve_upload_side(uint8_t keycode) {
    if (test_flag(get_local_state()->overlay_flags, MIRROR_OVERLAYS)) {
        return POS_ON_BOTH;
    }
    layer_state_t def_layer = get_local_layer()->def_layer;
    enum key_split_pos pos = get_split_matrix_side(keycode, def_layer);
    if (pos == POS_NOT_FOUND) {
        layer_state_t layer = get_function_layer(def_layer);
        if (layer != 0) {
            pos = get_split_matrix_side(keycode, layer);
        } else {
            pos = POS_ON_BOTH;
            uprintf("Warning: Could not locate side for keycode 0x%x, using both as fail-safe option.\n", keycode);
        }
    }
    return pos;
}


// Maps overlay index to modifier combination offset
// NO_MOD(0), CTRL(1), SHIFT(2), CTRL_SHIFT(3), ALT(4), CTRL_ALT(5),
// ALT_SHIFT(6), CTRL_ALT_SHIFT(7) and GUI_KEY(8)
uint16_t adjust_overlay_idx_to_mod(uint16_t idx, uint8_t mods) {
    // GUI_KEY key cannot be combined with other modifiers in case of overlays
    // for the moment, so GUI_KEY takes priority over all modifiers
    if((mods&0x88) != 0) {
        return idx + NUM_OVERLAYS * 8;
    }
    //no difference between r&l mods:
    mods |= mods>>4;
    mods &= 0x0f;
    return idx + NUM_OVERLAYS * mods;
}

// Computes the 0..89 overlay slot index for a (translate_a_to_z'd) overlay keycode.
// Returns false and logs when the index is out of range.
static bool overlay_slot_index(uint8_t keycode, uint16_t* out_idx) {
    uint16_t idx = (keycode > KC_APP) ? (keycode - KC_LEFT_CTRL + 82) : (keycode > KC_NUM_LOCK ? keycode - KC_NUBS + 80 : keycode - KC_A);
    if (idx >= 90) {
        uprint("Warning: Calculated index for overlay out of bounds. Dropping overlay.\n");
        return false;
    }
    *out_idx = idx;
    return true;
}

// Fills overlay buffer segment with bitmap data and syncs to bridge if needed.
void fill_overlay_buffer(uint8_t segment_index, uint8_t* buffer) {
    uint8_t keycode = get_fragment_context()->keycode;
    if (keycode > KC_RGUI) {
        uprint("Warning: Supplied overlay keycode not supported.\n");
        return;
    }

    keycode = translate_a_to_z(keycode);

    uint16_t idx;
    if (!overlay_slot_index(keycode, &idx)) {
        return;
    }

    uint8_t mods = get_fragment_context()->modifier;
    idx = adjust_overlay_idx_to_mod(idx, mods);
    idx = get_overlay_mapping(idx);

    enum key_split_pos pos = resolve_upload_side(keycode);
    if (is_on_current_side(pos)) {
        memcpy(get_overlay(idx) + segment_index * BYTES_PER_SEGMENT, buffer, BYTES_PER_SEGMENT);
    }
    if (is_on_other_side(pos)) {
        overlay_sync_t transfer;
        transfer.segment = segment_index;
        transfer.adj_idx = idx;
        memcpy(&transfer.overlay, buffer, BYTES_PER_SEGMENT);
        send_to_bridge(USER_SYNC_OVERLAY_DATA, (void*)&transfer, sizeof(transfer), 10);
    }

    if (segment_index == NUM_SEGMENTS_PER_OVERLAY - 1) {
        set_overlay_usage_post_upload(idx);
        uprintf("Received overlay for keycode 0x%x (modifiers: 0x%x): %d bytes, index %d, side: %s.\n",
                keycode, mods, (segment_index+1)*BYTES_PER_SEGMENT, idx, pos_to_str(pos));
    }
}

// Decompresses RLE-compressed overlay data and writes to overlay buffer, syncs to bridge if needed.
void decompress_overlay_buffer(uint8_t* compressed, bool first) {
    uint8_t keycode = get_fragment_context()->keycode;
    if (keycode > KC_RGUI) {
        uprint("Warning: Supplied overlay keycode not supported.\n");
        return;
    }

    keycode = translate_a_to_z(keycode);
    uint16_t idx;
    if (!overlay_slot_index(keycode, &idx)) {
        return;
    }
    uint8_t ctx_mod = get_fragment_context()->modifier;
    idx = adjust_overlay_idx_to_mod(idx, ctx_mod);
    idx = get_overlay_mapping(idx);

    enum key_split_pos pos = resolve_upload_side(keycode);
    uint16_t bit_index = get_fragment_context()->bit_index;
    uint8_t compressed_len = first?COMPRESSED_START:COMPRESSED_MAX;

    if (is_on_current_side(pos)) {
#ifdef USE_CORE1
        core1_decompress_fragment(keycode, ctx_mod, idx, compressed);
#else
        int16_t maxlen = 360 - bit_index/8;
        bit_index += rle_decompress(get_overlay(idx)+bit_index/8, PK_MAX(0,maxlen), compressed, compressed_len, bit_index);

        if (bit_index >= 360*8 -1) {
            set_overlay_usage_post_upload(idx);
            uprintf("--> Finished keycode 0x%x (mod 0x%x): side %s, total bytes %d.\n",
                keycode, ctx_mod, pos_to_str(pos), bit_index/8);
            update_performed();
            request_disp_refresh();
        }
#endif
    }

    if (is_on_other_side(pos)) {
        compressed_overlay_sync_t transfer;
        transfer.len = compressed_len;
        transfer.adj_idx = idx;
        memcpy(&transfer.compressed, compressed, compressed_len);
        send_to_bridge(USER_SYNC_COMPRESSED_DATA, (void*)&transfer, sizeof(transfer), 10);
    }

    set_fragment_context_bit_index(bit_index);
}

// Fills region-of-interest of overlay buffer with data and syncs to bridge when needed.
void fill_roi_overlay_buffer(uint8_t* data, bool first) {
    uint8_t keycode = get_fragment_context()->keycode;
    if (keycode > KC_RGUI) {
        uprint("Warning: Supplied overlay keycode not supported.\n");
        return;
    }

    keycode = translate_a_to_z(keycode);
    uint16_t idx;
    if (!overlay_slot_index(keycode, &idx)) {
        return;
    }
    uint8_t ctx_mod = get_fragment_context()->modifier;
    idx = adjust_overlay_idx_to_mod(idx, ctx_mod);
    idx = get_overlay_mapping(idx);

    enum key_split_pos pos = resolve_upload_side(keycode);

    if (is_on_current_side(pos)) {
        roi_update_data_t ctx_roi = get_fragment_context()->roi;
        #ifdef USE_CORE1
            if(first) {
                core1_roi_start();
            }
            core1_update_roi(keycode, ctx_mod, idx, first?(&(data[5])):data, &ctx_roi);
        #else
            uint16_t data_len = first?ROI_START:ROI_MAX;
            uint16_t bit_index = get_fragment_context()->bit_index;
            if(first) {
                bit_index = ctx_roi.y * SCREEN_WIDTH + ctx_roi.x;
            }
            bit_index = copy_rectangle_to_overlay(bit_index, get_overlay(idx), first?(&(data[5])):data, &ctx_roi, data_len);
            if(bit_index >= 2880) {
                set_overlay_usage_post_upload(idx);
                update_performed();
                request_disp_refresh();
            }
            set_fragment_context_bit_index(bit_index);
        #endif
    }

    if (is_on_other_side(pos)) {
        roi_overlay_sync_t transfer;
        transfer.adj_idx = idx;
        transfer.msg_idx = get_fragment_context()->msg_count;
        set_fragment_context_msg_count(transfer.msg_idx+1);
        memcpy(&transfer.data, data, ROI_MAX);

        if (send_to_bridge(USER_SYNC_ROI_DATA, (void*)&transfer, sizeof(transfer), 10)==SYNC_ACK_SIG) {
            uprintf("--> Finished ROI update for keycode 0x%x (mod 0x%x), sent to bridge: side %s in %d messages.\n",
                keycode, ctx_mod, pos_to_str(pos), get_fragment_context()->msg_count);
        }
    }
}

// Unpacks 10-bit overlay mapping pairs from buffer and updates overlay_map array.
// `from` indexes overlay_map[] (0..OVERLAY_MAP_IDX_CNT-1, currently 810).
// `to` indexes overlays[] (0..NUM_OVERLAYS*NUM_VARIATIONS-1, currently 630) — the
// physical pool. A `to` >= NUM_OVERLAYS*NUM_VARIATIONS is an out-of-pool value
// and would cause an OOB read in copy_overlay_to_buffer / fill_overlay_buffer.
void set_10bit_overlay_mapping(uint8_t* mapping) {
    uint16_t from = UNSET_OVERLAY_MAPPING;
    for(uint8_t idx=0;idx<OVERLAY_MAP_IDX_CNT_PER_REPORT;++idx) {
        // start_bit must be wide enough to hold idx*10 up to 480 — uint8_t wraps at idx=26.
        uint16_t start_bit = (uint16_t)idx*OVERLAY_MAP_IDX_BITS;
        uint8_t start_byte = start_bit/8;
        uint8_t start_bit_in_byte = start_bit%8;
        uint8_t num_bits_in_byte2 = OVERLAY_MAP_IDX_BITS-(8-start_bit_in_byte);
        uint16_t to =   ((uint16_t)(mapping[start_byte]>>start_bit_in_byte)) |
                        ((uint16_t)(0xff>>(8-num_bits_in_byte2))&mapping[start_byte+1])<<(8-start_bit_in_byte);
        if(from==UNSET_OVERLAY_MAPPING) {
            from = to;
        } else {
            if(from < OVERLAY_MAP_IDX_CNT) {
                if(to < NUM_OVERLAYS*NUM_VARIATIONS) {
                    set_overlay_mapping(from, to);
                    // use_overlay[] is from-indexed: a display position is
                    // "in use" iff it has an overlay assigned. Establishing
                    // the mapping is exactly that act, so set the bit here.
                    set_overlay_usage(from);
                    uprintf("Setting overlay mapping from %u to %u\n", from, to);
                } else {
                    uprintf("REJECTED overlay mapping from %u to %u (to out of pool, max %u)\n",
                            from, to, NUM_OVERLAYS*NUM_VARIATIONS - 1);
                }
            }
            // from >= OVERLAY_MAP_IDX_CNT is the host's deliberate noop padding (e.g. 810/810); silent.
            from = UNSET_OVERLAY_MAPPING;
        }
    }
}

void apply_overlay_action_flags(uint8_t flags) {
    if(test_flag(flags, RESET_BUFFERS))  reset_overlay_buffers();
    if(test_flag(flags, USAGE_RESET))    reset_overlay_usage();
    if(test_flag(flags, MAPPING_RESET))  reset_overlay_mapping();
    if(test_flag(flags, MAPPING_ALLSET)) set_all_overlay_mapping();
}

void set_overlay_usage_post_upload(uint16_t idx) {
    if (!test_flag(get_local_state()->overlay_flags, MIRROR_OVERLAYS)) {
        set_overlay_usage(idx);
    }
}
