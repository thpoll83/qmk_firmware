#include "multicore_exec.h"
#include "polykybd.h"

#include "print.h"
#include "config.h"
#include "base/helpers.h"
#include "base/update.h"
#include "base/rle.h"
#include "base/disp_array.h"
#include "base/multicore/core1.h"

#ifdef USE_CORE1
static volatile uint16_t core1_bit_index = 0;
static volatile uint32_t core1_decomp_count = 0;
static uint32_t core0_decomp_count = 0;

static volatile uint8_t core1_buffer[HID_DATA_MAX];
static volatile int16_t core1_max_bitlen;
static volatile uint16_t core1_idx;
static volatile roi_update_data_t core1_roi;

typedef enum {
    CORE1_CMD_DECOMPRESS     = 0xcafe0001,
    CORE1_CMD_ROI_UPDATE     = 0xcafe0002,
    CORE1_CMD_RESET_BIT_IDX  = 0xcafe0003,
} fifo_command_t;

// Overlay buffers for core1 processing
extern uint8_t overlays [NUM_OVERLAYS*NUM_VARIATIONS][72*40/8]; // ResX*ResY/PixelPerByte

// Main function for core1, do not use any prtintf or similar, stack is limited!
// Processes decompression and ROI update commands from core0 via FIFO, handles overlay buffer updates.
// Global variables: core1_bit_index, core1_decomp_count, core1_idx, core1_max_bitlen, core1_buffer, core1_roi
void core1_entry(void) {
    multicore_fifo_drain();
    while (true) {
        uint32_t cmd = multicore_fifo_pop_blocking();  // blocks if empty
        switch (cmd) {
            case CORE1_CMD_DECOMPRESS:{
                    uint16_t data_len = core1_bit_index==0?COMPRESSED_START:COMPRESSED_MAX;
                    core1_bit_index += rle_decompress(get_overlay(core1_idx)+core1_bit_index/8, PK_MAX(0,core1_max_bitlen), core1_buffer, data_len, core1_bit_index);

                    if (core1_bit_index >= 360*8 -1) {
                        set_overlay_usage(core1_idx);
                        update_performed();
                        request_disp_refresh();
                        core1_bit_index = 0;
                    }
                    core1_decomp_count++;
                    if(core1_decomp_count==0) { //handle overflow
                        core1_decomp_count=1;
                    }
                    dmb();
                } break;
            case CORE1_CMD_ROI_UPDATE:{
                    uint8_t data_len = ROI_MAX;
                    if(core1_bit_index==0) {
                        core1_bit_index = core1_roi.y * SCREEN_WIDTH + core1_roi.x;
                        data_len = ROI_START;
                    }
                    core1_bit_index = copy_rectangle_to_overlay(core1_bit_index, get_overlay(core1_idx), core1_buffer, &core1_roi, data_len);
                    if(core1_bit_index >= 2880) {
                        set_overlay_usage(core1_idx);
                        update_performed();
                        request_disp_refresh();
                        core1_bit_index = 0;
                    }
                    core1_decomp_count++;
                    if(core1_decomp_count==0) { //handle overflow
                        core1_decomp_count=1;
                    }
                    dmb();
                } break;
            case CORE1_CMD_RESET_BIT_IDX:
                core1_bit_index = 0;
                dmb();
                break;
            default: break;
        }
    }
}

void core1_decompress_fragment(uint8_t keycode, uint8_t mod, uint16_t overlay_idx, const uint8_t* compressed) {
    //wait in case decompression is still ongoing
    dmb();
    while(core0_decomp_count!=core1_decomp_count) {
        //no wait function needed as the uprintf inside the loop will already take some time
        uprintf("CORE1: Waiting for decompression...\n");
        dmb();
    }
    //copy data to dedicated buffers
    uint8_t data_len = core1_bit_index==0?COMPRESSED_START:COMPRESSED_MAX;
    core1_max_bitlen = 360 - core1_bit_index/8;
    core1_idx = overlay_idx;
    for(uint8_t i=0;i<data_len;++i) {
        core1_buffer[i] = compressed[i]; //memcopy not avialable for volatile memory
    }

    uprintf("CORE1: Key 0x%x (mod 0x%x) fragment decompression: (added %d bytes, bit index: %d).\n", keycode, mod, core1_bit_index==0?COMPRESSED_START:COMPRESSED_MAX, core1_bit_index);
    core0_decomp_count++;
    if(core0_decomp_count==0) { //handle overflow
        core0_decomp_count=1;
    }
    dmb();
    //allow core1 to start decompressing
    multicore_fifo_push_blocking(CORE1_CMD_DECOMPRESS);
}

void core1_roi_start(void) {
    multicore_fifo_push_blocking(CORE1_CMD_RESET_BIT_IDX);
}

void core1_update_roi(uint8_t keycode, uint8_t mod, uint16_t overlay_idx, const uint8_t* data, const roi_update_data_t* roi) {
    //wait in case decompression is still ongoing
    dmb();
    while(core0_decomp_count!=core1_decomp_count) {
        //no wait function needed as the uprintf inside the loop will already take some time
        uprintf("CORE1: Waiting for roi update...\n");
        dmb();
    }
    //copy data to dedicated buffers
    //core1_max_bitlen = 360 - core1_bit_index/8;
    core1_roi = *roi;
    core1_idx = overlay_idx;
    const uint8_t data_len = core1_bit_index==0?ROI_START:ROI_MAX;
    for(uint8_t i=0;i<data_len;++i) {
        core1_buffer[i] =  data[i]; //memcopy not available for volatile memory
    }

    uprintf("CORE1: Key 0x%x (mod 0x%x) roi update: (added %d bytes, bit index: %d).\n", keycode, mod, data_len, core1_bit_index);
    core0_decomp_count++;
    if(core0_decomp_count==0) { //handle overflow
        core0_decomp_count=1;
    }
    dmb();
    //allow core1 to start decompressing
    multicore_fifo_push_blocking(CORE1_CMD_ROI_UPDATE);
}
#endif
