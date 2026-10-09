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
#include "base/map_codec.h"
#include "base/overlay.h"
#include "base/update.h"
#include "base/prc_record.h"
#include "prc_table_v1.h"
#include "lang/lang_lut.h"

#include <print.h>
#include <transactions.h>
#include "poly_keymap.h"

// The overlay decode runs on core1. The old core0 fallback was removed: it had not
// been built since USE_CORE1 became unconditional in config.h.
#ifndef USE_CORE1
#    error "fill_overlay.c requires USE_CORE1"
#endif


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


// Modifier-variant (0..NUM_VARIATIONS_WITH_MAP-1) of a QMK modifier byte: fold the
// right-hand mods onto the left-hand ones and keep the low nibble, so the variant IS
// the modifier bitmask (bit0 Ctrl, bit1 Shift, bit2 Alt, bit3 GUI).
//
// ⚠️ Protocol v12 removed a GUI special case here: GUI used to short-circuit to
// variant 8, i.e. Cmd+Shift / Cmd+Alt / Cmd+Shift+Alt all rendered the *bare Cmd*
// overlay. Mac apps lean on those chords (Sublime Text's Cmd+Shift+P command
// palette was the report), so all 16 combinations are addressable now. The upload
// paths pass an already-4-bit value from the host, which this leaves untouched.
static uint8_t overlay_mod_variant(uint8_t mods) {
    mods |= mods >> 4;
    return mods & 0x0f;
}

// Maps overlay index to modifier combination offset (see overlay_mod_variant).
uint16_t adjust_overlay_idx_to_mod(uint16_t idx, uint8_t mods) {
    return idx + NUM_OVERLAYS * overlay_mod_variant(mods);
}

// True if an overlay staged for keycode-slot `base_slot` (0..89) and modifier-variant
// `ctx_mod` is what would be on screen right now — i.e. its key is currently displayed
// AND the held modifier variant matches. Two independent invisibility axes:
//   - LAYER: `overlay_slot_displayed()` is false when no displayed key resolves to this
//     keycode (e.g. an F-key overlay while the Fn layer is inactive).
//   - MODIFIER: update_displays() indexes the pool by the *held* variant directly
//     (adjust_overlay_idx_to_mod does no fall-down), so a Shift image while Shift is up
//     is staged into memory but not shown.
// Either way the overlay lands in overlay memory and is picked up by the eventual
// layer/modifier-change refresh (both ungated), so completing it need not re-render now.
// At rest a program switch pushes every modifier variant of every key incl. off-layer keys, and
// only the displayed variant-0 slots are visible — skipping the rest is the coalescing
// win. Uses the same local_layer->mods + displayed-slot set the display resolves against,
// so it never suppresses a render that is actually on screen.
static bool overlay_visible(uint16_t base_slot, uint8_t ctx_mod) {
    return overlay_slot_displayed(base_slot) &&
           overlay_mod_variant(ctx_mod) == overlay_mod_variant(get_local_layer()->mods);
}

// Same visibility test for an overlay-map "from" index, which already encodes the
// variant (from = base_slot + NUM_OVERLAYS*variant). Used to gate the mapping-chunk
// render — an all-off-screen chunk (e.g. only non-held modifier variants, or F-key
// slots while Fn is inactive) need not re-render; the visible state is guaranteed by
// the enable-overlays refresh (case 11 / the synced DISPLAY_OVERLAYS state change).
// Works on both halves (the slave also has local_layer->mods + the displayed set).
static bool overlay_from_index_visible(uint16_t from) {
    if (from >= OVERLAY_MAP_IDX_CNT) {
        return false;   // host padding / out of range
    }
    uint16_t base_slot = from % NUM_OVERLAYS;
    uint8_t  variant   = (uint8_t)(from / NUM_OVERLAYS);
    return overlay_slot_displayed(base_slot) &&
           variant == overlay_mod_variant(get_local_layer()->mods);
}

// The keycode an upload is stored under. Outside MRU mode the host addresses an
// image by the LETTER it belongs to, and translate_a_to_z() moves it to the key
// that types that letter in the active language. Under MIRROR_OVERLAYS (the host's
// MRU mode) the keycode + modifier pair is a POOL SLOT address instead, and the
// display reads the pool through the host's mapping by physical keycode
// (copy_overlay_to_buffer() does not translate). Translating there swapped the
// slots whose addresses are letters the layout moves: under de-DE the images in
// the Y- and Z-address slots traded places, so whichever keys the mapping pointed
// at them showed each other's image.
static uint8_t upload_keycode(uint8_t keycode) {
    if (test_flag(get_local_state()->overlay_flags, MIRROR_OVERLAYS)) {
        return keycode;
    }
    return translate_a_to_z(keycode);
}

// Computes the 0..89 overlay slot index for an upload_keycode()'d overlay keycode.
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

    keycode = upload_keycode(keycode);

    uint16_t idx;
    if (!overlay_slot_index(keycode, &idx)) {
        return;
    }

    uint8_t mods = get_fragment_context()->modifier;
    idx = adjust_overlay_idx_to_mod(idx, mods);
    idx = get_display_pool_slot(idx);

    enum key_split_pos pos = resolve_upload_side(keycode);
    if (is_on_current_side(pos)) {
        memcpy(get_overlay(idx) + segment_index * BYTES_PER_SEGMENT, buffer, BYTES_PER_SEGMENT);
    }
    if (is_on_other_side(pos)) {
        overlay_sync_t transfer;
        transfer.segment = segment_index;
        transfer.adj_idx = idx;
        memcpy(&transfer.overlay, buffer, BYTES_PER_SEGMENT);
        // ⚠️ NOT recoverable by the master: for a key on the other half
        // is_on_current_side() is false above, so these bytes are never stored
        // here — only the slave gets them. The host's MRU cache meanwhile records
        // the image as resident, so it will NOT re-send it on the next app switch
        // (a cache hit). A loss therefore sticks until the cache is reset. All we
        // can do is say so; classify the ack, never bool-test it (split_sync.h).
        if (!sync_succeeded(send_to_bridge(USER_SYNC_OVERLAY_DATA, (void*)&transfer, sizeof(transfer), 10))) {
            uprintf("Warning: overlay segment %u for keycode 0x%x (idx %u) did not reach the slave.\n",
                    segment_index, keycode, idx);
        }
    }

    if (segment_index == NUM_SEGMENTS_PER_OVERLAY - 1) {
        mark_display_has_overlay_post_upload(idx);
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

    keycode = upload_keycode(keycode);
    uint16_t idx;
    if (!overlay_slot_index(keycode, &idx)) {
        return;
    }
    uint8_t ctx_mod = get_fragment_context()->modifier;
    uint16_t base_slot = idx;   // 0..89, before the modifier/mapping resolve — for the visibility gate
    bool visible = overlay_visible(base_slot, ctx_mod);
    idx = adjust_overlay_idx_to_mod(idx, ctx_mod);
    idx = get_display_pool_slot(idx);

    enum key_split_pos pos = resolve_upload_side(keycode);
    uint8_t compressed_len = first?COMPRESSED_START:COMPRESSED_MAX;

    if (is_on_current_side(pos)) {
        core1_decompress_fragment(keycode, ctx_mod, idx, compressed, visible, first);
    }

    if (is_on_other_side(pos)) {
        compressed_overlay_sync_t transfer;
        transfer.len = compressed_len;
        transfer.adj_idx = idx;
        memcpy(&transfer.compressed, compressed, compressed_len);
        // Same one-way street as the plain path above — the master keeps no copy
        // of an other-side image, so a give-up here is unrecoverable until the
        // host's MRU cache drops the entry. Report it rather than lose it silently.
        if (!sync_succeeded(send_to_bridge(USER_SYNC_COMPRESSED_DATA, (void*)&transfer, sizeof(transfer), 10))) {
            uprintf("Warning: compressed overlay fragment for keycode 0x%x (idx %u) did not reach the slave.\n",
                    keycode, idx);
        }
    }
}

// Fills region-of-interest of overlay buffer with data and syncs to bridge when needed.
void fill_roi_overlay_buffer(uint8_t* data, bool first) {
    uint8_t keycode = get_fragment_context()->keycode;
    if (keycode > KC_RGUI) {
        uprint("Warning: Supplied overlay keycode not supported.\n");
        return;
    }

    keycode = upload_keycode(keycode);
    uint16_t idx;
    if (!overlay_slot_index(keycode, &idx)) {
        return;
    }
    uint8_t ctx_mod = get_fragment_context()->modifier;
    uint16_t base_slot = idx;   // 0..89, before the modifier/mapping resolve — for the visibility gate
    bool visible = overlay_visible(base_slot, ctx_mod);
    idx = adjust_overlay_idx_to_mod(idx, ctx_mod);
    idx = get_display_pool_slot(idx);

    enum key_split_pos pos = resolve_upload_side(keycode);

    if (is_on_current_side(pos)) {
        roi_update_data_t ctx_roi = get_fragment_context()->roi;
        if(first) {
            core1_roi_start();
        }
        core1_update_roi(keycode, ctx_mod, idx, first?(&(data[5])):data, &ctx_roi, visible);
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

// A bridged PRC record (see receive_prc_overlay_report) is the 4 box bytes
// plus the payload; it has to fit the compressed transaction it rides in.
_Static_assert(4 + HID_DATA_MAX - PRC_RECORD_HDR <= COMPRESSED_MAX,
               "a cmd 41 record does not fit compressed_overlay_sync_t");

// Decodes one PRC-coded image into pool slot `slot`: the step both halves run.
// False, with the slot untouched, if the box does not fit the frame.
bool prc_overlay_apply(uint16_t slot, uint8_t top, uint8_t left, uint8_t height, uint8_t width,
                       const uint8_t* payload, uint8_t len) {
    return prc_decode_roi(get_overlay(slot), top, left, height, width, payload, len, prc_table_v1);
}

// PRC-coded images (cmd 41, protocol v19). One report carries one or more
// records back to back (base/prc_record.h); each is a whole image, so there is no
// fragment context to keep between reports. Addressing, side resolution and the
// visibility gate are the same as the RLE path above.
//
// ⚠️ Decoded on core0, in the HID handler. On USE_CORE1 builds the receive is
// gated on core1 being idle (raw_hid_pre_receive_kb), so no RLE fragment can be
// writing into the pool at the same time. Estimated, not yet measured on the
// rig: ~150 cycles per ROI pixel, so ~0.7 ms for a 30x30 icon at 200 MHz and
// ~2 ms for a full 72x40 frame. The perf harness's overlay burst would show it.
uint8_t receive_prc_overlay_report(const uint8_t* data, uint8_t avail) {
    uint8_t     pos     = 0;
    uint8_t     images  = 0;
    prc_record_t r;
    uint8_t     n;
    while ((n = prc_parse_record(data + pos, avail - pos, &r)) != 0) {
        pos += n;
        if (r.keycode < KC_A || r.keycode > KC_RGUI) {
            uprintf("Warning: PRC overlay for unsupported keycode 0x%x dropped.\n", r.keycode);
            continue;
        }
        uint8_t  keycode = upload_keycode(r.keycode);
        uint16_t idx;
        if (!overlay_slot_index(keycode, &idx)) {
            continue;
        }
        bool visible = overlay_visible(idx, r.modifier);
        idx = adjust_overlay_idx_to_mod(idx, r.modifier);
        idx = get_display_pool_slot(idx);

        enum key_split_pos side = resolve_upload_side(keycode);
        if (is_on_current_side(side)) {
            (void)prc_overlay_apply(idx, r.top, r.left, r.height, r.width, r.payload, r.len);   // box checked by the parser
            mark_display_has_overlay_post_upload(idx);
            // No update_performed() — see base/update.h.
            if (visible) {
                request_disp_refresh();
            }
        }
        if (is_on_other_side(side)) {
            // Rides the compressed transaction, flagged in `len` (split_sync.h):
            // a new transaction id would cost the split42 transaction budget.
            // Zeroed: a record fills only 4 + len of the 62 bytes, and the whole
            // struct is CRC'd and sent, so the rest must not carry stack contents.
            compressed_overlay_sync_t transfer;
            memset(&transfer, 0, sizeof(transfer));
            transfer.adj_idx       = idx;
            transfer.len           = PRC_BRIDGE_FLAG | (uint8_t)(4 + r.len);
            transfer.compressed[0] = r.top;
            transfer.compressed[1] = r.left;
            transfer.compressed[2] = r.height;
            transfer.compressed[3] = r.width;
            memcpy(&transfer.compressed[4], r.payload, r.len);
            // Same one-way street as the RLE path: the master keeps no copy of an
            // other-side image. Classify the ack, never bool-test it.
            if (!sync_succeeded(send_to_bridge(USER_SYNC_COMPRESSED_DATA, (void*)&transfer, sizeof(transfer), 10))) {
                uprintf("Warning: PRC overlay for keycode 0x%x (idx %u) did not reach the slave.\n",
                        keycode, idx);
            }
        }
        images++;
    }
    if (pos < avail && data[pos] != 0) {
        uprintf("Warning: malformed PRC record at byte %u; rest of the report dropped.\n", pos);
    }
    return images;
}

// Unpacks `width`-bit overlay mapping pairs from `bytes` bytes of buffer and
// updates the display_to_pool array. Both values of a pair share the width; the
// caller supplies it (cmd 21 is always OVERLAY_MAP_IDX_BITS, cmd 33 carries it).
// `from` indexes display_to_pool[] (0..OVERLAY_MAP_IDX_CNT-1, currently 1440).
// `to` indexes overlay_pool[] (0..NUM_OVERLAY_SLOTS-1, currently 600) — the
// physical pool. A `to` >= NUM_OVERLAY_SLOTS is an out-of-pool value
// and would cause an OOB read in copy_overlay_to_buffer / fill_overlay_buffer.
// Returns true if any mapping in this chunk lands on a currently-displayed position
// (see overlay_from_index_visible) — the caller renders only then; an all-off-screen
// chunk is staged silently and shown by the eventual layer/modifier/enable refresh.
bool set_packed_overlay_mapping(const uint8_t* mapping, uint8_t bytes, uint8_t width) {
    bool any_visible = false;
    uint16_t from = UNSET_OVERLAY_MAPPING;
    if (width < OVERLAY_MAP_WIDTH_MIN || width > OVERLAY_MAP_WIDTH_MAX) {
        uprintf("REJECTED overlay mapping report: bad width %u\n", (unsigned)width);
        return false;
    }
    // Accumulate this report's mappings into ONE log line instead of one uprintf
    // per pair (20-30 pairs/report flooded the console). "from>to" pairs, space-sep.
    char    map_log[220];
    int     map_log_n  = 0;
    uint8_t map_count  = 0;   // pairs accepted (established) this report
    uint8_t map_shown  = 0;   // pairs actually rendered into map_log (may be fewer if it filled)
    map_log[0] = '\0';
    const uint16_t values = OVERLAY_MAP_VALUES(bytes, width);
    for(uint16_t idx=0;idx<values;++idx) {
        // The bit arithmetic lives in base/map_codec.h (shared with the repair
        // packer below and unit-tested there) — the conditional byte reads are
        // what keep a value's load inside the last data byte it occupies.
        uint16_t to = map_codec_read(mapping, idx, width);
        if(from==UNSET_OVERLAY_MAPPING) {
            from = to;
        } else {
            if(from < OVERLAY_MAP_IDX_CNT) {
                if(to < NUM_OVERLAY_SLOTS) {
                    set_display_pool_slot(from, to);
                    // display_has_overlay_bits[] is from-indexed: a display position is
                    // "in use" iff it has an overlay assigned. Establishing
                    // the mapping is exactly that act, so set the bit here.
                    mark_display_has_overlay(from);
                    if (overlay_from_index_visible(from)) {
                        any_visible = true;
                    }
                    // Build the log line only when console debugging is on — the
                    // accumulation is purely for the uprintf below (itself gated on
                    // debug_enable), so skip the snprintf work entirely otherwise.
                    if (debug_enable) {
                        ++map_count;
                        // Append " from>to" only if it fits whole (snprintf returns the
                        // length it WOULD write; a value >= rem means truncation → skip it
                        // and let the "(N, M shown)" line below flag the omission).
                        int rem = (int)sizeof(map_log) - map_log_n;
                        int w   = snprintf(map_log + map_log_n, rem, " %u>%u", from, to);
                        if (w > 0 && w < rem) {
                            map_log_n += w;
                            ++map_shown;
                        }
                    }
                } else {
                    uprintf("REJECTED overlay mapping from %u to %u (to out of pool, max %u)\n",
                            from, to, NUM_OVERLAY_SLOTS - 1);
                }
            }
            // from >= OVERLAY_MAP_IDX_CNT is the host's deliberate noop padding (e.g. 1440/1440); silent.
            from = UNSET_OVERLAY_MAPPING;
        }
    }
    if (map_count) {
        if (map_shown == map_count) {
            uprintf("Overlay mapping (%u):%s\n", map_count, map_log);
        } else {
            // Buffer filled before all pairs fit — say how many are shown so the
            // count never overstates the listed pairs.
            uprintf("Overlay mapping (%u, %u shown):%s\n", map_count, map_shown, map_log);
        }
    }
    return any_visible;
}

// --- Slave overlay-mapping repair ---------------------------------------
// See the contract in fill_overlay.h. Latched by the cmd-21 bridge check in
// hid_com.c, drained at enable_overlays.
static bool s_map_sync_lost = false;

void note_overlay_map_sync_lost(void) {
    s_map_sync_lost = true;
}

// Inverse of the unpacking in set_packed_overlay_mapping — now literally the
// same pair, base/map_codec.h, where the round-trip is unit-tested instead of
// verified by eye.
static void pack_map_value(uint8_t *buf, uint16_t idx, uint16_t v, uint8_t width) {
    map_codec_write(buf, idx, v, width);
}

// Repair cursor. The walk is SPLIT ACROSS HOUSEKEEPING TICKS rather than run in
// one go: a full mapping is up to 34 reports, and each send_to_bridge can burn
// 10 retries x the bridge timeout before giving up — precisely on the bad link
// that triggers a repair in the first place. Done inline in raw_hid_receive that
// is seconds of dead main loop inside one HID command; two reports per tick keeps
// each pass in the same cost class as the periodic syncs while still finishing
// the whole mapping within a few milliseconds of wall clock.
#define MAP_REPAIR_REPORTS_PER_TICK 2
static bool     s_repair_active = false;
static bool     s_repair_reset_pending = false;
static uint16_t s_repair_from   = 0;
static uint16_t s_repair_pairs  = 0;
static uint8_t  s_repair_reports = 0;

void arm_overlay_map_repair(void) {
    if (!s_map_sync_lost) {
        return;
    }
    s_map_sync_lost  = false;
    s_repair_active  = true;
    s_repair_reset_pending = true;
    s_repair_from    = 0;
    s_repair_pairs   = 0;
    s_repair_reports = 0;
}

void overlay_map_repair_tick(void) {
    if (!s_repair_active) {
        return;
    }
    overlay_map_sync_t msg;
    uint8_t sent_this_tick = 0;

    // The repair packs at the WIDEST width so any display position fits — it walks
    // the whole index space, unlike the host which partitions by required width.
    const uint8_t  width  = OVERLAY_MAP_REPAIR_WIDTH;
    const uint16_t values = OVERLAY_MAP_VALUES(sizeof(msg.mapping), width);

    // Reset the slave's mapping before replaying ours. The replay below sends only
    // the positions the master uses, so without this a slave that missed the app
    // switch's reset keeps the PREVIOUS app's entries wherever the new app has
    // none, and shows old icons there. The slave applies action bits from a state
    // sync whose bits it has not seen set (split_sync.c, newly_set), on the same
    // thread as the mapping handler, so this lands before the first replayed pair.
    // A copy, because the master's own state must not carry the action bits.
    if (s_repair_reset_pending) {
        poly_sync_t reset = *access_local_state();
        reset.overlay_flags |= USAGE_RESET | MAPPING_RESET;
        if (!sync_succeeded(send_to_bridge(USER_SYNC_POLY_DATA, (void *)&reset,
                                           sizeof(poly_sync_t), 10))) {
            s_repair_active = false;
            note_overlay_map_sync_lost();
            uprint("Overlay mapping repair: slave reset did not land, aborting this pass.\n");
            return;
        }
        s_repair_reset_pending = false;
        ++sent_this_tick;
    }

    while (sent_this_tick < MAP_REPAIR_REPORTS_PER_TICK) {
        uint16_t slot = 0;      // value slot in this report (from,to,from,to,...)
        uint16_t last_from = 0, last_to = 0;
        memset(msg.mapping, 0, sizeof(msg.mapping));
        msg.width = width;
        msg.bytes = (uint8_t)sizeof(msg.mapping);
        // Collect up to one report's worth of used entries from the cursor.
        while (s_repair_from < OVERLAY_MAP_IDX_CNT && slot + 1 < values) {
            if (display_has_overlay(s_repair_from)) {
                last_from = s_repair_from;
                last_to   = get_display_pool_slot(s_repair_from);
                pack_map_value(msg.mapping, slot++, last_from, width);
                pack_map_value(msg.mapping, slot++, last_to,   width);
                ++s_repair_pairs;
            }
            ++s_repair_from;
        }
        if (slot == 0) {        // cursor ran out with nothing pending -> done
            s_repair_active = false;
            uprintf("Overlay mapping repair: re-pushed %u pairs in %u report(s) to the slave.\n",
                    (unsigned)s_repair_pairs, (unsigned)s_repair_reports);
            return;
        }
        // Pad by REPEATING THE LAST PAIR. Re-applying a mapping is idempotent
        // (set_display_pool_slot + mark_display_has_overlay), so a duplicate is a
        // semantic no-op and needs no reserved sentinel — which matters because a
        // sentinel would have to exceed OVERLAY_MAP_IDX_CNT (1440) and so could not
        // be expressed at the narrower widths at all. Every value must be written:
        // there is no count field, so anything left zero would decode as the real
        // pair 0>0 and wrongly light up display position 0.
        while (slot + 1 < values) {
            pack_map_value(msg.mapping, slot++, last_from, width);
            pack_map_value(msg.mapping, slot++, last_to,   width);
        }
        if (!sync_succeeded(send_to_bridge(USER_SYNC_OVERLAY_MAP_DATA, (void *)&msg,
                                           sizeof(overlay_map_sync_t), 10))) {
            // Give up on this pass rather than grinding through the rest of the
            // pool against a dead link; re-latch so the next enable re-arms.
            s_repair_active = false;
            note_overlay_map_sync_lost();
            uprint("Overlay mapping repair: bridge still failing, aborting this pass.\n");
            return;
        }
        ++s_repair_reports;
        ++sent_this_tick;
    }
}

void apply_overlay_action_flags(uint8_t flags) {
    if(test_flag(flags, RESET_BUFFERS))  reset_overlay_pool();
    if(test_flag(flags, USAGE_RESET))    clear_display_has_overlay();
    if(test_flag(flags, MAPPING_RESET))  reset_display_to_pool();
    if(test_flag(flags, MAPPING_ALLSET)) set_all_display_has_overlay();
}

void mark_filled_icon_slots(const uint8_t *pairs, uint8_t bytes, uint8_t width, uint8_t applied) {
    if (width < OVERLAY_MAP_WIDTH_MIN || width > OVERLAY_MAP_WIDTH_MAX) {
        return;   // the fill refused it too; and a width of 0 would divide by zero below
    }
    const uint16_t n = (uint16_t)(((uint16_t)bytes * 8u / width) / 2u);
    for (uint16_t p = 0; p < n && p < applied; ++p) {
        mark_display_has_overlay_post_upload(map_codec_read(pairs, (uint16_t)(2u * p), width));
    }
}

void mark_display_has_overlay_post_upload(uint16_t idx) {
    if (!test_flag(get_local_state()->overlay_flags, MIRROR_OVERLAYS)) {
        mark_display_has_overlay(idx);
    }
}
