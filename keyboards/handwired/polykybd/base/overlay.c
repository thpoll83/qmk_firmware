#include "overlay.h"
#include "config.h" //for all constants

#include "disp_array.h"
#include "rle.h"

#include <string.h>

static volatile uint8_t use_overlay[(NUM_OVERLAYS*NUM_VARIATIONS_WITH_MAP/8)+1];
static /*volatile*/ uint8_t overlays [NUM_OVERLAYS*NUM_VARIATIONS][72*40/8]; // ResX*ResY/PixelPerByte
static uint16_t overlay_map [NUM_OVERLAYS*NUM_VARIATIONS_WITH_MAP];


uint8_t (* get_overlays(void))[72*40/8] {
    return overlays;
}

uint8_t* get_overlay(uint16_t overlay_idx) {
    return overlays[overlay_idx];
}

uint16_t get_overlay_mapping(uint16_t overlay_idx) {
    return overlay_map[overlay_idx];
}

void set_overlay_mapping(uint16_t overlay_idx, uint16_t val) {
    overlay_map[overlay_idx] = val;
}

void set_overlay_usage(uint16_t overlay_idx) {
    use_overlay[overlay_idx/8] |= (1<<(overlay_idx%8));
}

bool is_overlay_used(uint16_t overlay_idx) {
    return (use_overlay[overlay_idx/8] & (1<<(overlay_idx%8))) != 0;
}

void reset_overlay_buffers(void) {
    memset(&overlays, 0, sizeof(overlays));
}

void reset_overlay_usage(void) {
    for(int16_t i = 0; i < sizeof(use_overlay); ++i) {
        use_overlay[i] = 0;
    }
    //memset(&use_overlay, 0, sizeof(use_overlay));
}

void reset_overlay_mapping(void) {
    for(int16_t i = 0; i < NUM_OVERLAYS*NUM_VARIATIONS; ++i) {
        overlay_map[i] = i;
    }
    //the additional map entries for Ctrl+Alt+Shift and GUI Modifiers will point to the no_modifier version (0-NUM_OVERLAYS)
    for(int16_t i = NUM_OVERLAYS*NUM_VARIATIONS; i < NUM_OVERLAYS*NUM_VARIATIONS_WITH_MAP; ++i) {
        overlay_map[i] = i%NUM_OVERLAYS;
    }
}

uint16_t copy_rectangle_to_overlay_xy(uint16_t bit_index, uint8_t* dest, const volatile uint8_t* data, const volatile roi_update_data_t* roi, const uint16_t bitlen) {
    uint16_t bit_cnt = 0;
    uint8_t  start_y = bit_index / SCREEN_WIDTH;
    for (uint8_t dest_y = start_y; dest_y < roi->yy; dest_y++) {
        uint8_t start_x = bit_cnt == 0 ? bit_index % SCREEN_WIDTH : roi->x;
        if (start_x == roi->xx) {
            start_x = roi->x;
            dest_y++;
            if (dest_y >= roi->yy) {
                return bit_index;
            }
        } else if(start_x < roi->x) {
            start_x = roi->x;
        }
        for (uint8_t dest_x = start_x; dest_x < roi->xx; dest_x++) {
            bit_index = dest_y * SCREEN_WIDTH + dest_x;
            uint8_t data_byte   = data[bit_cnt / 8];
            uint8_t bit_in_byte = bit_cnt % 8;
            bool    bit_is_set  = ((0x80 >> bit_in_byte) & data_byte) != 0;

            if (bit_is_set) {
                dest[bit_index / 8] |= 0x80 >> (bit_index % 8);
            } else {
                dest[bit_index / 8] &= ~(0x80 >> (bit_index % 8));
            }

            bit_cnt++;
            if (bit_cnt >= bitlen) {
                bit_index++; //for the next call
                return bit_index;
            }
        }
    }
    return 2880;
}

uint16_t copy_rectangle_to_overlay(uint16_t bit_index, uint8_t* dest, const volatile uint8_t* data, const volatile roi_update_data_t* roi, const uint8_t data_len) {
    if(roi->compressed) {
        for (uint8_t i = 0; i < data_len; i++) {
            uint8_t buffer[16];
            uint16_t num_bits = rle_decompress(buffer, 16, &data[i], 1, 0);
            bit_index = copy_rectangle_to_overlay_xy(bit_index, dest, buffer, roi, num_bits);
            if( bit_index >= 2880) {
                return bit_index;
            }
        }
    } else {
        bit_index = copy_rectangle_to_overlay_xy(bit_index, dest, data, roi, data_len*8);
    }
    return bit_index >= ((roi->yy-1) * SCREEN_WIDTH + roi->xx) ? 2880 : bit_index;
}
