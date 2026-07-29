// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#include "split_sync.h"

#include "quantum.h"

#include "multicore_exec.h"
#include "hid_com.h"
#include "emoji/emoji_layer.h"
#include "lang_layer.h"
#include "mru.h"

#include "base/overlay.h"
#include "eeconfig.h"
#include "eeprom.h"
#include "nvm_eeprom_eeconfig_internal.h"
#include "dynamic_keymap.h"
#include "base/com.h"
#include "base/disp_array.h"
#include "base/update.h"
#include "polymod_crc32.h"
#include "fill_overlay.h"
#include "state.h"
#include "anim/startup_anim.h"
#include "side.h"
#ifdef POLYKYBD_DOOM
#include "doom/doom_arena.h"
#include "doom/doom_mirror.h"
#include "doom/doom_mode.h"   // doom_shim_mirror_engaged (mirror ack status)
#endif
#include "matrix_helper.h"
#include "poly_util.h"

void rgb_matrix_update_pwm_buffers(void);

void invert_display(uint8_t r, uint8_t c, bool state);

#include <stddef.h>
#include <string.h>


// Validates an incoming split-sync transaction: checks the payload/reply sizes and
// the per-transaction CRC32, NACKing with SYNC_CRC32_ERR on a CRC mismatch. On a
// size mismatch it bails out leaving the reply untouched (a no-op, as before). The
// handler body runs only for a verified frame and sets the success ack itself.
#define SYNC_VALIDATE_OR_RETURN(TYPE)                                            \
    if (!(in_len == sizeof(TYPE) && in_data != NULL &&                           \
          out_len == sizeof(poly_sync_reply_t) && out_data != NULL)) {           \
        return;                                                                  \
    }                                                                            \
    if (crc32_1byte(&((uint8_t *)in_data)[4], in_len - 4, 0) !=                  \
        ((const TYPE *)in_data)->crc32) {                                        \
        ((poly_sync_reply_t *)out_data)->ack = SYNC_CRC32_ERR;                   \
        return;                                                                  \
    }


// Handles incoming poly_sync data for the bridge with CRC32 validation.
void user_sync_poly_data_handler(uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data) {
    SYNC_VALIDATE_OR_RETURN(poly_sync_t);
    const poly_sync_t* incoming = (const poly_sync_t *)in_data;
    const poly_sync_t* current  = get_local_state();
    if (incoming->lang != current->lang) {
        mark_settings_dirty();
    }
    // Track the master's awake contrast in RAM so the slave's idle/wake
    // restore uses the right level — but do NOT persist it. In host-auto
    // mode the master's contrast IS the auto value, and persisting that as
    // the slave's manual brightness made the slave boot at a stale auto
    // value (e.g. a night-time 2). The master is authoritative and syncs
    // brightness on every boot, so the slave never needs its own EEPROM
    // copy. (Transient suspend/idle/fade levels are still excluded.)
    if (incoming->contrast != current->contrast && incoming->contrast > DISP_OFF &&
        (incoming->flags & ((uint8_t)DISP_IDLE | (uint8_t)IDLE_TRANSITION)) == 0) {
        note_user_brightness(incoming->contrast);
    }
    // Detect action flags newly set in this sync and run them immediately,
    // mirroring the master's case-11 handling. Without this, a mapping
    // bridge transaction arriving before housekeeping runs would have its
    // use_overlay bits wiped by a later deferred reset_overlay_usage().
    uint8_t newly_set     = (incoming->overlay_flags & ~current->overlay_flags);
#ifdef RGB_MATRIX_ENABLE
    uint8_t newly_cleared = (current->overlay_flags  & ~incoming->overlay_flags);
#endif
    // Doom control-pad mode flipped on the master (strip legends down to the
    // game controls / restore the full set), or the weapon-pad state changed
    // (picked up / switched weapon) -> re-render this half's legends.
    bool doom_ctl_changed = incoming->doom_ctl != current->doom_ctl ||
                            incoming->doom_wpn_owned != current->doom_wpn_owned ||
                            incoming->doom_wpn_ready != current->doom_wpn_ready;
    // Master bumped the startup-animation replay nonce -> replay on this half too.
    // Guard on !active: on a fresh boot the slave is already animating from its own
    // boot-intro marker when the master's post_init nonce arrives, so this must NOT
    // restart it (that flashed a visible frame-0 stutter). It only starts the
    // animation when the slave is idle — a genuine HID replay (cmd 31) or a
    // KC_EDEN-rearmed boot where the slave's own marker was already consumed.
    bool anim_replay = (incoming->anim_nonce != current->anim_nonce) && !startup_anim_active();
    copy_local_state(incoming);
    if (doom_ctl_changed) {
        request_disp_refresh();
    }
    if (anim_replay) {
        startup_anim_start();
    }
    emj_apply_sync(incoming->emj_category, incoming->emj_page);
    lang_apply_sync(incoming->lang_page);
    if(newly_set & OVERLAY_ACTION_FLAGS) {
        apply_overlay_action_flags(newly_set);
        // Clear the bits locally so housekeeping has nothing to do.
        access_local_state()->overlay_flags &= ~OVERLAY_ACTION_FLAGS;
        // Housekeeping's state_diff branch (which used to call this at the end)
        // won't fire now that local matches global — trigger the refresh ourselves.
        request_disp_refresh();
    }
    if(newly_set & BOOTLOADER_DISPLAY) {
        uprint("Slave: BOOTLOADER_DISPLAY received\n");
        display_bootloader_message();
    }
    if(newly_set & SAVE_EEPROM) {
        // Master pressed the store key — flush our own EEPROM too, but
        // defer the write to housekeeping (never inside this handler).
        request_eeprom_save();
        // Edge-consumed: clear locally so it doesn't linger as a diff.
        access_local_state()->overlay_flags &= ~SAVE_EEPROM;
    }
#ifdef RGB_MATRIX_ENABLE
    // Disable RGB so the normal split transport restores master's config cleanly.
    if(newly_cleared & BOOTLOADER_DISPLAY) {
        rgb_matrix_disable_noeeprom();
    }
#endif
    ((poly_sync_reply_t*)out_data)->ack = SYNC_ACK;
}

// Handles incoming latin_sync data with CRC32 validation, saves to EEPROM and refreshes display.
// Global variables: g_latin
void user_sync_latin_ex_data_handler(uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data) {
    SYNC_VALIDATE_OR_RETURN(latin_sync_t);
    copy_global_latin_table((const latin_sync_t *)in_data);
    ((poly_sync_reply_t*)out_data)->ack = SYNC_ACK;
    // Defer the flash write out of this UART transaction callback — the
    // next flush (suspend / store key, save_all_dirty) persists it.
    mark_latin_dirty();
    request_disp_refresh();
}

// Handles incoming last key data on bridge with CRC32 validation, updates both local and global state.
void user_sync_lastkey_data_handler(uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data) {
    SYNC_VALIDATE_OR_RETURN(poly_last_t);
    copy_local_last_latin((const poly_last_t *)in_data);
    copy_global_last_latin((const poly_last_t *)in_data);
    ((poly_sync_reply_t*)out_data)->ack = SYNC_ACK;
    request_disp_refresh();
}

// Handles incoming layer data on bridge with CRC32 validation.
// Global variables: l_layer
void user_sync_layer_data_handler(uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data) {
    SYNC_VALIDATE_OR_RETURN(poly_layer_t);
    const poly_layer_t* incoming = (const poly_layer_t *)in_data;
    if (incoming->def_layer != get_local_layer()->def_layer) {
        defer_default_layer_save(incoming->def_layer);
    }
    copy_local_layer(incoming);
    ((poly_sync_reply_t*)out_data)->ack = SYNC_ACK;
    //request_disp_refresh();
}

// Handles incoming overlay segment data on bridge with CRC32 validation, marks as used when complete.
void user_sync_overlay_data_handler(uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data) {
    SYNC_VALIDATE_OR_RETURN(overlay_sync_t);
    note_overlay_activity();   // coalesce the slave's per-chunk renders (see update.h)
    const overlay_sync_t* ov = ((const overlay_sync_t *)in_data);
    // NOTE (FW-6, risk accepted): `ov->segment` is not bounded here, so a value
    // outside 0..NUM_SEGMENTS_PER_OVERLAY-1 would offset the memcpy past the
    // 360-byte overlay row. This is deliberately NOT guarded: the only way to
    // inject such a value is a compromised/forged peer half on the UART bridge,
    // which requires physical access to the internal bridge connector — an
    // attacker with that access can already flash arbitrary firmware over the
    // bootloader, so the split link is inside the trust boundary. The master's
    // HID path (hid_com.c case 10) bounds `segment` on the way in, so normal
    // traffic never carries an out-of-range value. (adj_idx is still bounded by
    // get_overlay()/FW-4.)
    memcpy(get_overlay(ov->adj_idx) + ov->segment*BYTES_PER_SEGMENT, ov->overlay, BYTES_PER_SEGMENT);
    if(ov->segment==NUM_SEGMENTS_PER_OVERLAY-1) {
        set_overlay_usage_post_upload(ov->adj_idx);
        request_disp_refresh();
    }
    ((poly_sync_reply_t*)out_data)->ack = SYNC_ACK;
}

// Handles compressed overlay data on bridge with CRC32 validation, decompresses using core1 or local decompression.
// Global variables: hid_bit_index
void user_sync_compressed_overlay_data_handler(uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data) {
    SYNC_VALIDATE_OR_RETURN(compressed_overlay_sync_t);
    note_overlay_activity();   // coalesce the slave's per-chunk renders (see update.h)
    const compressed_overlay_sync_t* ov = ((const compressed_overlay_sync_t *)in_data);
#ifdef USE_CORE1
    //keycode info is lost, so KC_NO used (only used for diagnostics)
    core1_decompress_fragment(KC_NO, 0, ov->adj_idx, ov->compressed);
#else
    if(ov->len == COMPRESSED_START) {
        hid_bit_index = 0;
    }
    int16_t maxlen = 360 - hid_bit_index/8;
    hid_bit_index += rle_decompress(get_overlay(ov->adj_idx)+hid_bit_index/8, PK_MAX(0,maxlen), ov->compressed, ov->len, hid_bit_index);
    if (hid_bit_index >= 360*8) {
        set_overlay_usage_post_upload(ov->adj_idx);
        request_disp_refresh();
        hid_bit_index = 0;
    }
#endif
    ((poly_sync_reply_t*)out_data)->ack = SYNC_ACK;
}

void user_sync_roi_data_handler(uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data) {
    SYNC_VALIDATE_OR_RETURN(roi_overlay_sync_t);
    note_overlay_activity();   // coalesce the slave's per-chunk renders (see update.h)
    const roi_overlay_sync_t* roi_ov = ((const roi_overlay_sync_t *)in_data);
    bool first = roi_ov->msg_idx==0;
    const uint8_t* start = roi_ov->data;
    if(first) {
        reset_fragment_context();
        set_fragment_context_key(roi_ov->data[0], roi_ov->data[1]&0x0f);
        uint8_t x = roi_ov->data[3];
        uint8_t y = (roi_ov->data[2]&0x03) | ((roi_ov->data[1]>>2)&0x3c);
        set_fragment_context_roi(x, y, roi_ov->data[4]&0x7f, roi_ov->data[2] >> 2, ((roi_ov->data[4]&0x80)!=0));
        set_fragment_context_bit_index(y * SCREEN_WIDTH + x);
        start = &((roi_ov->data)[5]);
    }
    const overlay_fragment_context_t* ctx = get_fragment_context();

    #ifdef USE_CORE1
        if(first) {
            core1_roi_start();
        }
        core1_update_roi(ctx->keycode, ctx->modifier, roi_ov->adj_idx, start, &ctx->roi);
        ((poly_sync_reply_t*)out_data)->ack = SYNC_ACK; // we cannot send SIG, we do not know if we are finished
    #else
        uint16_t new_index = copy_rectangle_to_overlay(ctx->bit_index, get_overlay(roi_ov->adj_idx), start, &ctx->roi, first?ROI_START:ROI_MAX);
        if(new_index >= 2880) {
            //finished roi update
            ((poly_sync_reply_t*)out_data)->ack = SYNC_ACK_SIG;
            set_overlay_usage_post_upload(roi_ov->adj_idx);
            request_disp_refresh();
        } else {
            ((poly_sync_reply_t*)out_data)->ack = SYNC_ACK;
        }
        set_fragment_context_bit_index(new_index);
    #endif
}

_Static_assert(DYNAMIC_KEYMAP_UPDATE_MAX_LAYER_COUNT <= DYNAMIC_KEYMAP_LAYER_COUNT, "Maximum cannot exceed DYNAMIC_KEYMAP_LAYER_COUNT");

// Writes data to EEPROM at specified offset within the dynamic keymap region with bounds checking.
void dynamic_keymap_set_buffer_poly(uint16_t offset, uint16_t size, const uint8_t *data) {
    uint16_t max = DYNAMIC_KEYMAP_UPDATE_MAX_LAYER_COUNT * MATRIX_ROWS * MATRIX_COLS * 2;
    if (offset >= max) return;
    uint16_t clamped = (offset + size > max) ? max - offset : size;
    eeprom_update_block(data, (void *)(POLY_EEPROM_CONFIG_END + offset), clamped);
}

// Same layer cap as dynamic_keymap_set_buffer_poly, but for single-keycode writes:
// the host may only remap layers below DYNAMIC_KEYMAP_UPDATE_MAX_LAYER_COUNT, so the
// language/emoji/etc. function layers above it can't be clobbered from the host.
void dynamic_keymap_set_keycode_poly(uint8_t layer, uint8_t row, uint8_t column, uint16_t keycode) {
    if (layer >= DYNAMIC_KEYMAP_UPDATE_MAX_LAYER_COUNT) return;
    dynamic_keymap_set_keycode(layer, row, column, keycode);
}

// Handles dynamic keymap commands on the bridge with CRC32 validation, including keymap resets and key press events.
void user_sync_dynamic_keymap_data_handler(uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data) {
    if (in_len >= (sizeof(uint32_t)+1) && in_data != NULL && out_len == sizeof(poly_sync_reply_t) && out_data!= NULL) {
        uint32_t crc32 = crc32_1byte(&((uint8_t *)in_data)[4], in_len-4, 0);
        const dynamic_keymap_sync_t* data = (const dynamic_keymap_sync_t *)in_data;
        if(crc32 == data->crc32) {
            const uint8_t* command_data = &data->commands[1];
            switch(data->commands[0]) {
                case id_dynamic_keymap_reset:
                    dynamic_keymap_reset();
                    request_disp_refresh();
                    break;
                case id_dynamic_keymap_set_keycode:
                    dynamic_keymap_set_keycode_poly(command_data[0], command_data[1], command_data[2], (command_data[3] << 8) | command_data[4]);
                    request_disp_refresh();
                    break;
                case id_dynamic_keymap_set_buffer: {
                    uint16_t offset = (command_data[0] << 8) | command_data[1];
                    uint16_t size   = command_data[2]; // size <= 28
                    dynamic_keymap_set_buffer_poly(offset, size, &command_data[3]);
                    request_disp_refresh();
                    break;
                }
                case id_custom_save: //handle the same way
                case 'P':
                    if(command_data[0]==14) {
                        uint16_t keycode = ((uint16_t)command_data[2])<<8 | command_data[3];
                        uint8_t r = 0, c = 0;
                        enum key_split_pos pos = get_split_matrix_pos(keycode, get_highest_layer(get_local_layer()->layer), &r, &c, is_left_side());
                        if(pos==POS_NOT_FOUND) {
                            //actually it should be the previous layer instead of default, but it worked so far
                            pos = get_split_matrix_pos(keycode, get_local_layer()->def_layer, &r, &c, is_left_side());
                        }
                        if (is_on_current_side(pos)) {
                            invert_display(r, c, command_data[4] == 0);
                        }
                    }
                    break;
                default: break;
            }
            ((poly_sync_reply_t*)out_data)->ack = SYNC_ACK;
        } else {
            ((poly_sync_reply_t*)out_data)->ack = SYNC_CRC32_ERR;
        }
    }
}

// The MRU recents ride on the overlay-map transaction id (multiplexed by payload
// size) so we stay within QMK's 32-transaction limit. The two payloads must keep
// distinct sizes for the size-based dispatch in the handler below to work.
_Static_assert(MRU_SYNC_BYTES != sizeof(overlay_map_sync_t),
               "MRU and overlay-map payloads must differ in size (shared transaction id)");
#ifdef POLYKYBD_DOOM
// The doom mirror messages are the third tenant of this id (also size-keyed;
// no cross-talk — the host's overlay-map commands are frozen in game mode).
_Static_assert(sizeof(doom_mirror_msg_t) != sizeof(overlay_map_sync_t) &&
               sizeof(doom_mirror_msg_t) != MRU_SYNC_BYTES,
               "doom mirror payload must differ in size (shared transaction id)");
#endif

// Handles incoming overlay mapping data on bridge with CRC32 validation.
// Also dispatches MRU snapshots (and, in POLYKYBD_DOOM builds, the doom
// mirror messages), which share this transaction id by distinct size.
void user_sync_overlay_map_data_handler(uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data) {
    if (in_len == MRU_SYNC_BYTES) {
        user_sync_mru_data_handler(in_len, in_data, out_len, out_data);
        return;
    }
#ifdef POLYKYBD_DOOM
    if (in_len == sizeof(doom_mirror_msg_t)) {
        user_sync_doom_mirror_handler(in_len, in_data, out_len, out_data);
        return;
    }
#endif
    SYNC_VALIDATE_OR_RETURN(overlay_map_sync_t);
    note_overlay_activity();   // coalesce the slave's per-chunk renders (see update.h)
    const overlay_map_sync_t* data = (const overlay_map_sync_t *)in_data;
    set_10bit_overlay_mapping((uint8_t *)data->mapping);
    request_disp_refresh();
    ((poly_sync_reply_t*)out_data)->ack = SYNC_ACK;
}

// Handles incoming MRU recents (emoji + language) on the bridge with CRC32 validation.
void user_sync_mru_data_handler(uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data) {
    if (in_len == MRU_SYNC_BYTES && in_data != NULL && out_len == sizeof(poly_sync_reply_t) && out_data != NULL) {
        uint32_t crc32 = crc32_1byte(&((uint8_t *)in_data)[4], in_len-4, 0);
        const mru_sync_t* data = (const mru_sync_t *)in_data;
        if (crc32 == data->crc32) {
            mru_apply_sync(data->emoji, data->lang);
            request_disp_refresh();   // redraw the recents row on the slave
            ((poly_sync_reply_t*)out_data)->ack = SYNC_ACK;
        } else {
            ((poly_sync_reply_t*)out_data)->ack = SYNC_CRC32_ERR;
        }
    }
}


#ifdef POLYKYBD_DOOM
// Doom slave lockstep mirror (doom/doom_mirror.h): feed received ticcmds into
// the arena's rolling window / hand control messages to the game core. While
// game mode is not (yet) active on this half the arena does not exist — the
// message is ACKed and dropped (pre-session attract cmds are disposable; the
// window realigns on the first message that lands after the session is up).
void user_sync_doom_mirror_handler(uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data) {
    SYNC_VALIDATE_OR_RETURN(doom_mirror_msg_t);
    const doom_mirror_msg_t* msg = (const doom_mirror_msg_t *)in_data;
    doom_mirror_t* m = (doom_mirror_t *)doom_arena_at(DOOM_ARENA_MIRROR_OFF);
    if (m != NULL) {
        switch (msg->kind) {
            case DOOM_MIRROR_MSG_TIC: {
                uint32_t t = msg->tic;
                uint8_t  n = msg->count > DOOM_MIRROR_MSG_MAX_CMDS ? DOOM_MIRROR_MSG_MAX_CMDS
                                                                   : msg->count;
                if (t > m->rx_w) {
                    // Gap: cmds streamed while this half had no session (or the
                    // master's ring reset). Realign the window here — a consumer
                    // engaged across the gap detects it via rx_r and freezes.
                    m->rx_r = m->rx_w = t;
                }
                for (uint8_t i = 0; i < n; ++i, ++t) {
                    if (t < m->rx_w) {
                        continue; // duplicate from a bridge retry
                    }
                    memcpy(m->rx_cmds[t % DOOM_MIRROR_RX_LEN], msg->cmds[i], DOOM_MIRROR_CMD_BYTES);
                    __asm volatile("dmb" ::: "memory");
                    m->rx_w = t + 1;
                    if (m->rx_w - m->rx_r > DOOM_MIRROR_RX_LEN) {
                        m->rx_r = m->rx_w - DOOM_MIRROR_RX_LEN; // roll the window
                    }
                }
                break;
            }
            case DOOM_MIRROR_MSG_START:
                m->start_in_tic   = msg->tic;
                m->start_in_skill = msg->skill;
                m->start_in_epi   = msg->epi;
                m->start_in_map   = msg->map;
                __asm volatile("dmb" ::: "memory");
                m->start_in_seq++;
                break;
            case DOOM_MIRROR_MSG_BREAK:
                m->break_in = 1;
                break;
            case DOOM_MIRROR_MSG_MENU: {
                // Readable menu mirror snapshot (count 0 = menu closed);
                // item vpatch handles travel as raw u16s in the cmds bytes.
                uint8_t n = msg->count > DOOM_MIRROR_MENU_MAX_ITEMS
                                ? DOOM_MIRROR_MENU_MAX_ITEMS : msg->count;
                memcpy(m->menu_in_items, msg->cmds, (size_t)n * sizeof(uint16_t));
                m->menu_in_item_on = msg->skill;
                __asm volatile("dmb" ::: "memory");
                m->menu_in_count = n;
                m->menu_in_seq++;
                break;
            }
            default:
                break;
        }
    }
    // The ack byte doubles as the slave's mirror status: SYNC_ACK_SIG = the
    // drone is engaged, plain SYNC_ACK = not (yet). Both count as success
    // (sync_succeeded); the master pump reads it to re-offer a missed START
    // and to log engagement transitions (round 24).
    ((poly_sync_reply_t*)out_data)->ack = doom_shim_mirror_engaged() ? SYNC_ACK_SIG : SYNC_ACK;
}
#endif // POLYKYBD_DOOM
