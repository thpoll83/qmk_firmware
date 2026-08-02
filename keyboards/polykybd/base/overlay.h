#pragma once

// Overlay vocabulary — three distinct things that all used to be called "overlay
// something", which was the main source of confusion when reading this code:
//
//   overlay_pool[NUM_OVERLAY_SLOTS][360]  the physical bank of keycap IMAGES.
//                                         Addressed by a "pool slot" (0..599).
//   display_to_pool[OVERLAY_MAP_IDX_CNT]  "display position" -> pool slot. A
//                                         display position is a flat index
//                                         (keycode slot 0..89) + 90*(modifier
//                                         variant 0..15), i.e. WHICH KEY under
//                                         WHICH modifier state.
//   display_has_overlay_bits[]            1 bit per display position: does it
//                                         have an image assigned at all? This is
//                                         the render gate.
//
// The indirection is what lets several modifier variants that share the same
// artwork share ONE pool slot — which is why the pool (600) is much smaller than
// the position space (1440).
//
// The host uses the same two words: PolyKybdHost's OverlayMRUCache has
// display_flat_idx() and pool_slot_to_firmware_address(). Keep them aligned.
//
// LEGACY NAMES (renamed 2026-08, may still appear in older commits/comments/docs):
//   overlays              -> overlay_pool
//   overlay_map           -> display_to_pool
//   get/set_overlay_mapping -> get/set_display_pool_slot
//   reset_overlay_mapping -> reset_display_to_pool
//   use_overlay           -> display_has_overlay_bits
//   is_overlay_used       -> display_has_overlay
//   set_overlay_usage     -> mark_display_has_overlay
//   reset_overlay_usage   -> clear_display_has_overlay
//   set_all_overlay_mapping -> set_all_display_has_overlay
//   reset_overlay_buffers -> reset_overlay_pool
// get_overlay(pool_slot) kept its name — it already reads as a pool accessor.

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

// Outcome of decoding an ROI overlay header (see set_fragment_context_from_buffer).
// Named so the call site reads unambiguously instead of testing a bare bool.
typedef enum {
    ROI_BOUNDS_OK = 0,       // host bounds were within the display
    ROI_BOUNDS_CLAMPED = 1,  // out-of-range bounds had to be clamped (log it)
} roi_bounds_t;

const overlay_fragment_context_t* get_fragment_context(void);
overlay_fragment_context_t* access_fragment_context(void);
void reset_fragment_context(void);
// Decode an ROI overlay header from a HID payload into the fragment context,
// clamping out-of-range ROI coords to the display. Returns ROI_BOUNDS_CLAMPED if
// any value had to be clamped (the host sent out-of-bounds bounds), so the caller
// can log it, else ROI_BOUNDS_OK.
roi_bounds_t set_fragment_context_from_buffer(const uint8_t* buffer);
void set_fragment_context_key(uint8_t keycode, uint8_t modifier);
void set_fragment_context_bit_index(uint16_t bit_index);
void set_fragment_context_byte_len(uint8_t byte_len);
void set_fragment_context_msg_count(uint8_t msg_count);
void set_fragment_context_roi(uint8_t x, uint8_t y, uint8_t xx, uint8_t yy, bool compressed);

uint8_t (*get_overlay_pool(void))[72*40/8];

uint8_t* get_overlay(uint16_t overlay_idx);

uint16_t get_display_pool_slot(uint16_t overlay_idx);

void set_display_pool_slot(uint16_t overlay_idx, uint16_t val);

void mark_display_has_overlay(uint16_t overlay_idx);

// Checks if the specified overlay is marked as being used.
// Global variables: display_has_overlay_bits
bool display_has_overlay(uint16_t overlay_idx);

// Clears all overlay buffer data by setting it to zero.
// Global variables: overlays
void reset_overlay_pool(void);

// Resets all overlay usage flags by clearing the entire usage array.
// Global variables: display_has_overlay_bits
void clear_display_has_overlay(void);

// Marks every overlay slot as used (inverse of clear_display_has_overlay).
// Global variables: display_has_overlay_bits
void set_all_display_has_overlay(void);

// Initializes the overlay mapping indices: standard entries map 1:1, followed by modifier combinations.
// Global variables: display_to_pool
void reset_display_to_pool(void);

// Copies rectangle region of overlay data handling both compressed and uncompressed formats.
// Global variables: (none - uses passed parameters only)
uint16_t copy_rectangle_to_overlay(uint16_t bit_index, uint8_t* dest, const volatile uint8_t* data, const volatile roi_update_data_t* roi, const uint8_t data_len);
