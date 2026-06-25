#pragma once

#include <stdint.h>
#include <stdbool.h>


typedef struct _roi_update_data_t{
    uint8_t x;
    uint8_t y;
    uint8_t xx;
    uint8_t yy;
    bool compressed;
} roi_update_data_t;

typedef struct _overlay_fragment_context_t {
    uint8_t           keycode;
    uint8_t           modifier;
    uint8_t           byte_len;
    roi_update_data_t roi;
    uint16_t          bit_index;
    uint8_t           msg_count; //only used for the bridge
} overlay_fragment_context_t;

const overlay_fragment_context_t* get_fragment_context(void);
overlay_fragment_context_t* access_fragment_context(void);
void reset_fragment_context(void);
// Decode an ROI overlay header from a HID payload into the fragment context,
// clamping out-of-range ROI coords to the display. Returns true if any value had
// to be clamped (i.e. the host sent out-of-bounds bounds), so the caller can log it.
bool set_fragment_context_from_buffer(const uint8_t* buffer);
void set_fragment_context_key(uint8_t keycode, uint8_t modifier);
void set_fragment_context_bit_index(uint16_t bit_index);
void set_fragment_context_byte_len(uint8_t byte_len);
void set_fragment_context_msg_count(uint8_t msg_count);
void set_fragment_context_roi(uint8_t x, uint8_t y, uint8_t xx, uint8_t yy, bool compressed);

uint8_t (*get_overlays(void))[72*40/8];

uint8_t* get_overlay(uint16_t overlay_idx);

uint16_t get_overlay_mapping(uint16_t overlay_idx);

void set_overlay_mapping(uint16_t overlay_idx, uint16_t val);

void set_overlay_usage(uint16_t overlay_idx);

// Checks if the specified overlay is marked as being used.
// Global variables: use_overlay
bool is_overlay_used(uint16_t overlay_idx);

// Clears all overlay buffer data by setting it to zero.
// Global variables: overlays
void reset_overlay_buffers(void);

// Resets all overlay usage flags by clearing the entire usage array.
// Global variables: use_overlay
void reset_overlay_usage(void);

// Marks every overlay slot as used (inverse of reset_overlay_usage).
// Global variables: use_overlay
void set_all_overlay_mapping(void);

// Initializes the overlay mapping indices: standard entries map 1:1, followed by modifier combinations.
// Global variables: overlay_map
void reset_overlay_mapping(void);

// Copies rectangle region of overlay data handling both compressed and uncompressed formats.
// Global variables: (none - uses passed parameters only)
uint16_t copy_rectangle_to_overlay(uint16_t bit_index, uint8_t* dest, const volatile uint8_t* data, const volatile roi_update_data_t* roi, const uint8_t data_len);
