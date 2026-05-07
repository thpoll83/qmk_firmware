#include "split_sync.h"

#include "quantum.h"

#include "multicore_exec.h"
#include "hid_com.h"

#include "base/overlay.h"
#include "eeconfig.h"
#include "dynamic_keymap.h"
#include "base/disp_array.h"
#include "base/update.h"
#include "base/crc32.h"
#include "fill_overlay.h"
#include "state.h"
#include "side.h"
#include "matrix_helper.h"

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
            copy_local_state(incoming);
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
                set_overlay_usage(ov->adj_idx);
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
                set_overlay_usage(ov->adj_idx);
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
                    set_overlay_usage(roi_ov->adj_idx);
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
            ((poly_sync_reply_t*)out_data)->ack = SYNC_ACK;
        } else {
            ((poly_sync_reply_t*)out_data)->ack = SYNC_CRC32_ERR;
        }
    }
}

