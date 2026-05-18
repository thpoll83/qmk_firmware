// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#include "split_sync.h"

#include "quantum.h"

#include "multicore_exec.h"
#include "hid_com.h"

#include "base/overlay.h"
#include "eeconfig.h"
#include "eeprom.h"
#include "nvm_eeprom_eeconfig_internal.h"
#include "dynamic_keymap.h"
#include "base/com.h"
#include "base/disp_array.h"
#include "base/update.h"
#include "base/crc32.h"
#include "base/ota_flash.h"
#include "fill_overlay.h"
#include "state.h"
#include "side.h"
#include "matrix_helper.h"
#include "poly_util.h"

void rgb_matrix_update_pwm_buffers(void);

void invert_display(uint8_t r, uint8_t c, bool state);

#include <stddef.h>
#include <string.h>


// Handles incoming poly_sync data for the bridge with CRC32 validation.
void user_sync_poly_data_handler(uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data) {
    if (in_len == sizeof(poly_sync_t) && in_data != NULL && out_len == sizeof(poly_sync_reply_t) && out_data!= NULL) {
        uint32_t crc32 = crc32_1byte(&((uint8_t *)in_data)[4], in_len-4, 0);
        if(crc32 == ((const poly_sync_t *)in_data)->crc32) {
            const poly_sync_t* incoming = (const poly_sync_t *)in_data;
            const poly_sync_t* current  = get_local_state();
            if (incoming->contrast != current->contrast || incoming->lang != current->lang) {
                mark_settings_dirty();
            }
            // Detect action flags newly set in this sync and run them immediately,
            // mirroring the master's case-11 handling. Without this, a mapping
            // bridge transaction arriving before housekeeping runs would have its
            // use_overlay bits wiped by a later deferred reset_overlay_usage().
            uint8_t newly_set     = (incoming->overlay_flags & ~current->overlay_flags);
#ifdef RGB_MATRIX_ENABLE
            uint8_t newly_cleared = (current->overlay_flags  & ~incoming->overlay_flags);
#endif
            copy_local_state(incoming);
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
#ifdef RGB_MATRIX_ENABLE
            // Disable RGB so the normal split transport restores master's config cleanly.
            if(newly_cleared & BOOTLOADER_DISPLAY) {
                rgb_matrix_disable_noeeprom();
            }
#endif
            ((poly_sync_reply_t*)out_data)->ack = SYNC_ACK;
        } else {
            ((poly_sync_reply_t*)out_data)->ack = SYNC_CRC32_ERR;
        }
    }
}

// Handles incoming latin_sync data with CRC32 validation, saves to EEPROM and refreshes display.
// Global variables: g_latin
void user_sync_latin_ex_data_handler(uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data) {
    if (in_len == sizeof(latin_sync_t) && in_data != NULL && out_len == sizeof(poly_sync_reply_t) && out_data!= NULL) {
        uint32_t crc32 = crc32_1byte(&((uint8_t *)in_data)[4], in_len-4, 0);
        if(crc32 == ((const latin_sync_t *)in_data)->crc32) {
            copy_global_latin_table((const latin_sync_t *)in_data);
            ((poly_sync_reply_t*)out_data)->ack = SYNC_ACK;
            save_user_latin();
            request_disp_refresh();
        } else {
            ((poly_sync_reply_t*)out_data)->ack = SYNC_CRC32_ERR;
        }
    }
}

// Handles incoming last key data on bridge with CRC32 validation, updates both local and global state.
void user_sync_lastkey_data_handler(uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data) {
    if (in_len == sizeof(poly_last_t) && in_data != NULL && out_len == sizeof(poly_sync_reply_t) && out_data!= NULL) {
        uint32_t crc32 = crc32_1byte(&((uint8_t *)in_data)[4], in_len-4, 0);
        if(crc32 == ((const poly_last_t *)in_data)->crc32) {
            copy_local_last_latin((const poly_last_t *)in_data);
            copy_global_last_latin((const poly_last_t *)in_data);
            ((poly_sync_reply_t*)out_data)->ack = SYNC_ACK;
            request_disp_refresh();
        } else {
            ((poly_sync_reply_t*)out_data)->ack = SYNC_CRC32_ERR;
        }
    }
}

// Handles incoming layer data on bridge with CRC32 validation.
// Global variables: l_layer
void user_sync_layer_data_handler(uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data) {
    if (in_len == sizeof(poly_layer_t) && in_data != NULL && out_len == sizeof(poly_sync_reply_t) && out_data!= NULL) {
        uint32_t crc32 = crc32_1byte(&((uint8_t *)in_data)[4], in_len-4, 0);
        if(crc32 == ((const poly_layer_t *)in_data)->crc32) {
            const poly_layer_t* incoming = (const poly_layer_t *)in_data;
            if (incoming->def_layer != get_local_layer()->def_layer) {
                defer_default_layer_save(incoming->def_layer);
            }
            copy_local_layer(incoming);
            ((poly_sync_reply_t*)out_data)->ack = SYNC_ACK;
            //request_disp_refresh();
        } else {
            ((poly_sync_reply_t*)out_data)->ack = SYNC_CRC32_ERR;
        }
    }
}

// Handles incoming overlay segment data on bridge with CRC32 validation, marks as used when complete.
void user_sync_overlay_data_handler(uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data) {
    if (in_len == sizeof(overlay_sync_t) && in_data != NULL && out_len == sizeof(poly_sync_reply_t) && out_data!= NULL) {
        uint32_t crc32 = crc32_1byte(&((uint8_t *)in_data)[4], in_len-4, 0);
        const overlay_sync_t* ov = ((const overlay_sync_t *)in_data);
        if(crc32 == ov->crc32) {
            memcpy(get_overlay(ov->adj_idx) + ov->segment*BYTES_PER_SEGMENT, ov->overlay, BYTES_PER_SEGMENT);
            if(ov->segment==NUM_SEGMENTS_PER_OVERLAY-1) {
                set_overlay_usage_post_upload(ov->adj_idx);
                request_disp_refresh();
            }
            ((poly_sync_reply_t*)out_data)->ack = SYNC_ACK;
        } else {
            ((poly_sync_reply_t*)out_data)->ack = SYNC_CRC32_ERR;
        }
    }
}

// Handles compressed overlay data on bridge with CRC32 validation, decompresses using core1 or local decompression.
// Global variables: hid_bit_index
void user_sync_compressed_overlay_data_handler(uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data) {
    if (in_len == sizeof(compressed_overlay_sync_t) && in_data != NULL && out_len == sizeof(poly_sync_reply_t) && out_data!= NULL) {
        uint32_t crc32 = crc32_1byte(&((uint8_t *)in_data)[4], in_len-4, 0);
        const compressed_overlay_sync_t* ov = ((const compressed_overlay_sync_t *)in_data);
        if(crc32 == ov->crc32) {
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
        } else {
            ((poly_sync_reply_t*)out_data)->ack = SYNC_CRC32_ERR;
        }
    }
}

void user_sync_roi_data_handler(uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data) {
    if (in_len == sizeof(roi_overlay_sync_t) && in_data != NULL && out_len == sizeof(poly_sync_reply_t) && out_data!= NULL) {
        uint32_t crc32 = crc32_1byte(&((uint8_t *)in_data)[4], in_len-4, 0);
        const roi_overlay_sync_t* roi_ov = ((const roi_overlay_sync_t *)in_data);
        if(crc32 == roi_ov->crc32) {
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
        } else {
            ((poly_sync_reply_t*)out_data)->ack = SYNC_CRC32_ERR;
        }
    }
}

_Static_assert(DYNAMIC_KEYMAP_UPDATE_MAX_LAYER_COUNT <= DYNAMIC_KEYMAP_LAYER_COUNT, "Maximum cannot exceed DYNAMIC_KEYMAP_LAYER_COUNT");

// Writes data to EEPROM at specified offset within the dynamic keymap region with bounds checking.
void dynamic_keymap_set_buffer_poly(uint16_t offset, uint16_t size, const uint8_t *data) {
    uint16_t max = DYNAMIC_KEYMAP_UPDATE_MAX_LAYER_COUNT * MATRIX_ROWS * MATRIX_COLS * 2;
    if (offset >= max) return;
    uint16_t clamped = (offset + size > max) ? max - offset : size;
    eeprom_update_block(data, (void *)(POLY_EEPROM_CONFIG_END + offset), clamped);
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
                    dynamic_keymap_set_keycode(command_data[0], command_data[1], command_data[2], (command_data[3] << 8) | command_data[4]);
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

// Handles incoming overlay mapping data on bridge with CRC32 validation.
void user_sync_overlay_map_data_handler(uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data) {
    if (in_len == sizeof(overlay_map_sync_t) && in_data != NULL && out_len == sizeof(poly_sync_reply_t) && out_data != NULL) {
        uint32_t crc32 = crc32_1byte(&((uint8_t *)in_data)[4], in_len-4, 0);
        const overlay_map_sync_t* data = (const overlay_map_sync_t *)in_data;
        if (crc32 == data->crc32) {
            set_10bit_overlay_mapping((uint8_t *)data->mapping);
            request_disp_refresh();
            ((poly_sync_reply_t*)out_data)->ack = SYNC_ACK;
        } else {
            ((poly_sync_reply_t*)out_data)->ack = SYNC_CRC32_ERR;
        }
    }
}

// ---------------------------------------------------------------------------
// OTA handlers (slave side)
// ---------------------------------------------------------------------------

// Boot-time version query.  Slave fills out_data with its own version+size.
void user_sync_ota_query_handler(uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data) {
    if (in_len != sizeof(ota_query_sync_t) || !in_data || out_len != sizeof(ota_query_sync_t) || !out_data) return;
    ota_query_sync_t *reply = (ota_query_sync_t *)out_data;
    memset(reply, 0, sizeof(*reply));
    strncpy(reply->version, FW_VERSION, OTA_VERSION_LEN - 1);
    reply->fw_size = ota_get_own_fw_size();
    reply->crc32   = crc32_1byte(reply->version, sizeof(*reply) - 4, 0);
}

// Slave schedules staging erase and reports readiness back to master.
//
// Protocol:
//   New image   : start deferred erase, return SYNC_ACK_SIG ("erase started, not ready").
//   Re-poll     : SYNC_CRC32_ERR while erasing, SYNC_ACK once complete.
//                 s_begun_size/crc are NOT reset on "done" — additional polls of the
//                 same image return SYNC_ACK without re-triggering the erase, so a
//                 UART-missed ACK does not cause an unnecessary full re-erase cycle.
//   Dirty retry : if chunks were written since the last erase (partial previous attempt),
//                 re-erase before allowing more writes (NOR flash needs erase before 0→1).
//
// The deferred erase is rate-limited to one sector per 70 ms in housekeeping so
// the split UART stays responsive between 50 ms erase blackouts.
void user_sync_ota_begin_handler(uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data) {
    if (in_len != sizeof(ota_begin_sync_t) || !in_data || out_len != sizeof(poly_sync_reply_t) || !out_data) return;
    const ota_begin_sync_t *msg = (const ota_begin_sync_t *)in_data;
    uint32_t crc32 = crc32_1byte(&((const uint8_t *)in_data)[4], in_len - 4, 0);
    if (crc32 != msg->crc32) {
        ((poly_sync_reply_t *)out_data)->ack = SYNC_CRC32_ERR;
        return;
    }

    static uint32_t s_begun_size = 0;
    static uint32_t s_begun_crc  = 0;
    bool same_image = (msg->image_size == s_begun_size && msg->image_crc == s_begun_crc);

    if (!same_image) {
        s_begun_size = msg->image_size;
        s_begun_crc  = msg->image_crc;
        ota_begin_deferred(msg->image_size, msg->image_crc);
        uprintf("slave OTA_BEGIN: size=%lu crc=0x%08lx started erase\n", msg->image_size, msg->image_crc);
        ((poly_sync_reply_t *)out_data)->ack = SYNC_ACK_SIG;  // "erase started, keep polling"
        return;
    }

    // Re-poll: same image.
    if (ota_erase_pending()) {
        ((poly_sync_reply_t *)out_data)->ack = SYNC_CRC32_ERR;
        return;
    }

    // Erase done.  If any chunks were written since the last erase (retry after a
    // partial OTA failure), re-erase before handing off — NOR flash bits cannot go
    // 0→1 without erasing first.
    if (ota_staging_written()) {
        ota_begin_deferred(msg->image_size, msg->image_crc);
        uprintf("slave OTA_BEGIN: re-erasing dirty staging\n");
        ((poly_sync_reply_t *)out_data)->ack = SYNC_ACK_SIG;
        return;
    }

    // Staging is clean and erase is complete.  Keep s_begun_size/crc set so
    // subsequent polls of the same image also return SYNC_ACK without restarting.
    uprintf("slave OTA_BEGIN: erase complete, ready\n");
    ((poly_sync_reply_t *)out_data)->ack = SYNC_ACK;
}

// Slave writes one firmware chunk to its staging area.
void user_sync_ota_chunk_handler(uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data) {
    if (in_len != sizeof(ota_chunk_sync_t) || !in_data || out_len != sizeof(poly_sync_reply_t) || !out_data) return;
    const ota_chunk_sync_t *msg = (const ota_chunk_sync_t *)in_data;
    uint32_t crc32 = crc32_1byte(&((const uint8_t *)in_data)[4], in_len - 4, 0);
    if (crc32 != msg->crc32) {
        uprintf("slave OTA_CHUNK: CRC32 err offset=%lu\n", msg->offset);
        ((poly_sync_reply_t *)out_data)->ack = SYNC_CRC32_ERR;
        return;
    }
    bool ok = ota_write_chunk(msg->offset, msg->data, OTA_CHUNK_SIZE);
    uprintf("slave OTA_CHUNK: offset=%lu ok=%d\n", msg->offset, ok);
    ((poly_sync_reply_t *)out_data)->ack = ok ? SYNC_ACK : SYNC_CRC32_ERR;
}

// Slave verifies staged CRC and arms the commit flag.
// The actual flash apply is deferred to housekeeping_task_user() so the ACK
// can be returned before the split link goes dark during the reboot.
void user_sync_ota_commit_handler(uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data) {
    if (out_len != sizeof(poly_sync_reply_t) || !out_data) return;
    bool ok = ota_finalize();
    ((poly_sync_reply_t *)out_data)->ack = ok ? SYNC_ACK : SYNC_CRC32_ERR;
}
